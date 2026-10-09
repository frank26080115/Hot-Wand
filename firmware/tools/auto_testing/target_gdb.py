"""Persistent OpenOCD/GDB-MI control of the SWD menu test target."""

from __future__ import annotations

import queue
import re
import socket
import subprocess
import threading
import time
from collections import deque

try:
    from .build_metadata import GDB_EXE, OPENOCD_EXE, OPENOCD_SCRIPTS
except ImportError:  # Direct execution from this directory.
    from build_metadata import GDB_EXE, OPENOCD_EXE, OPENOCD_SCRIPTS


NO_COMMAND = 0xFFFFFFFF


class GdbSession:
    """Start OpenOCD, connect GDB, and read/write target memory via MI2.

    Each memory inspection briefly halts the MCU and resumes it immediately.
    The target's own millisecond timer, rather than host sleep, times presses.
    """

    def __init__(self, elf, symbols, *, port: int = 3333):
        self.elf = elf
        self.symbols = symbols
        self.port = port
        self.server = None
        self.gdb = None
        self.messages = queue.Queue()
        self.server_lines = deque(maxlen=100)
        self.stopped = threading.Event()
        self.sequence = 0
        self.running = False

    def __enter__(self):
        if not GDB_EXE.is_file() or not OPENOCD_EXE.is_file() or not OPENOCD_SCRIPTS.is_dir():
            raise RuntimeError("GDB or OpenOCD installation path at top of build_metadata.py is invalid")
        command = [str(OPENOCD_EXE), "-s", str(OPENOCD_SCRIPTS),
                   "-f", "interface/stlink.cfg", "-f", "target/stm32f0x.cfg",
                   "-c", "adapter speed 100",
                   "-c", "reset_config srst_only srst_nogate connect_assert_srst",
                   "-c", f"gdb_port {self.port}", "-c", "init", "-c", "reset halt"]
        self.server = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        threading.Thread(target=self._server_reader, daemon=True).start()
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            if self.server.poll() is not None:
                self.__exit__(None, None, None)
                raise RuntimeError("OpenOCD exited before the GDB server started: " + " | ".join(self.server_lines))
            try:
                with socket.create_connection(("127.0.0.1", self.port), timeout=0.2):
                    break
            except OSError:
                time.sleep(0.2)
        else:
            self.__exit__(None, None, None)
            raise RuntimeError("OpenOCD did not open its GDB port")

        self.gdb = subprocess.Popen([str(GDB_EXE), "--nx", "--quiet", "--interpreter=mi2"],
                                    stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, text=True, bufsize=1)
        threading.Thread(target=self._reader, daemon=True).start()
        try:
            self.command(f'-file-exec-and-symbols "{str(self.elf).replace(chr(92), "/")}"')
            self.command("-gdb-set pagination off")
            # This GDB defaults target-async to off. MI must return ^running
            # promptly so Python can keep issuing reads and button commands.
            self.command("-gdb-set target-async on")
            self.command(f"-target-select remote 127.0.0.1:{self.port}", timeout=20)
        except Exception:
            self.__exit__(None, None, None)
            raise
        return self

    def __exit__(self, *_):
        if self.gdb is not None:
            self.gdb.terminate()
            try:
                self.gdb.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.gdb.kill()
        if self.server is not None:
            self.server.terminate()
            try:
                self.server.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.server.kill()

    def _reader(self):
        for line in self.gdb.stdout:
            if line.startswith("*stopped"):
                self.stopped.set()
            elif line.startswith("*running"):
                self.stopped.clear()
            self.messages.put(line.rstrip("\r\n"))

    def _server_reader(self):
        for line in self.server.stdout:
            self.server_lines.append(line.rstrip("\r\n"))

    def command(self, instruction: str, *, timeout: float = 12) -> str:
        self.sequence += 1
        token = self.sequence
        self.gdb.stdin.write(f"{token}{instruction}\n")
        self.gdb.stdin.flush()
        deadline = time.monotonic() + timeout
        output = []
        while time.monotonic() < deadline:
            try:
                line = self.messages.get(timeout=min(0.25, max(0.01, deadline - time.monotonic())))
            except queue.Empty:
                if self.gdb.poll() is not None:
                    raise RuntimeError("GDB exited unexpectedly")
                continue
            output.append(line)
            if line.startswith(f"{token}^done") or line.startswith(f"{token}^connected") or line.startswith(f"{token}^running"):
                return "\n".join(output)
            if line.startswith(f"{token}^error"):
                raise RuntimeError(f"GDB command failed ({instruction}): {line}")
        raise RuntimeError(f"GDB timed out: {instruction}; last output: {output[-4:]}")

    def halt(self):
        if self.running:
            self.command("-exec-interrupt --all")
            if not self.stopped.wait(timeout=3):
                raise RuntimeError("GDB did not report the target stopped after interrupt")
            self.running = False

    def resume(self):
        if not self.running:
            self.stopped.clear()
            self.command("-exec-continue")
            self.running = True

    def inspect(self, callback):
        was_running = self.running
        self.halt()
        try:
            return callback()
        finally:
            if was_running:
                self.resume()

    def read_memory(self, address: int, length: int) -> bytes:
        if self.running:
            return self.inspect(lambda: self.read_memory(address, length))
        # A pending *stopped notification may precede this reply and include
        # unrelated frame arguments. Parse only this command's ^done record.
        reply = self.command(f"-data-read-memory-bytes 0x{address:08x} {length}").splitlines()[-1]
        match = re.search(r'contents="([0-9a-fA-F]+)"', reply)
        if not match or len(match[1]) != length * 2:
            raise RuntimeError(f"GDB returned an incomplete {length}-byte memory read")
        return bytes.fromhex(match[1])

    def write_u32(self, address: int, value: int):
        if self.running:
            return self.inspect(lambda: self.write_u32(address, value))
        self.command(f"-data-write-memory-bytes 0x{address:08x} {value.to_bytes(4, 'little').hex()}")

    def read_u32(self, address: int) -> int:
        return int.from_bytes(self.read_memory(address, 4), "little")

    def read_c_string(self, address: int, maximum: int = 256) -> str:
        # Read in short blocks so strings near a flash boundary remain usable.
        data = bytearray()
        for offset in range(0, maximum, 16):
            block = self.read_memory(address + offset, 16)
            if 0 in block:
                return (data + block[:block.index(0)]).decode("ascii")
            data.extend(block)
        raise RuntimeError(f"Unterminated string at 0x{address:08x}")

    def expression_address(self, expression: str) -> int:
        reply = self.command(f'-data-evaluate-expression "{expression}"').splitlines()[-1]
        match = re.search(r'value="(?:\\")?(0x[0-9a-fA-F]+)', reply)
        if not match:
            raise RuntimeError(f"Could not resolve GDB expression {expression}: {reply}")
        return int(match[1], 16)

    def framebuffer(self) -> bytes:
        def read_once():
            generation_address = self.symbols["vision_test_oled_sent_generation"].address
            framebuffer_address = self.symbols["vision_test_oled_sent_framebuffer"].address
            before = self.read_u32(generation_address)
            frame = self.read_memory(framebuffer_address, 512)
            after = self.read_u32(generation_address)
            return before, frame, after

        # The firmware increments the generation before and after copying a
        # successfully sent frame. An odd value means a debugger halt landed
        # in the copy; resume briefly and read the completed snapshot.
        for attempt in range(8):
            before, frame, after = self.inspect(read_once)
            if before == after and before != 0 and before % 2 == 0:
                return frame
            time.sleep(0.02)
        raise RuntimeError(f"OLED sent-frame mirror did not become coherent; generation {before} -> {after}")

    def reset(self):
        self.halt()
        self.command('-interpreter-exec console "monitor reset halt"', timeout=20)
        self.running = False
        self.resume()

    def flash(self):
        self.halt()
        self.command("-target-download", timeout=90)
        comparison = self.command('-interpreter-exec console "compare-sections"', timeout=30)
        if "mis-match" in comparison.lower() or "does not match" in comparison.lower():
            raise RuntimeError(f"Flashed ELF verification failed: {comparison}")
        self.reset()

    def button_state(self) -> tuple[int, int, int]:
        def read():
            names = ("btn_swd_test_command_ms", "btn_swd_test_state", "btn_swd_test_completed")
            return tuple(self.read_u32(self.symbols[name].address) for name in names)
        return self.inspect(read)

    def wait_state(self, wanted: int, timeout: float = 8) -> tuple[int, int, int]:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            state = self.button_state()
            if state[1] == wanted:
                return state
            time.sleep(0.08)
        raise RuntimeError(f"Button state did not reach {wanted}; last state {state}")

    def press(self, duration_ms: int) -> tuple[tuple[int, int, int], tuple[int, int, int]]:
        if not 1 <= duration_ms <= 5000:
            raise ValueError("Synthetic press duration must be 1..5000 ms")
        before = self.wait_state(1)
        if before[0] != NO_COMMAND:
            raise RuntimeError("Button mailbox has an unconsumed command")
        self.write_u32(self.symbols["btn_swd_test_command_ms"].address, duration_ms)
        # Let the target's timer run without repeated debug halts during the
        # requested hold and 50 ms release debounce. This is particularly
        # important for long presses near the menu inactivity deadline.
        time.sleep(duration_ms / 1000 + 0.15)
        deadline = time.monotonic() + duration_ms / 1000 + 5
        while time.monotonic() < deadline:
            state = self.button_state()
            if state[1] == 0:
                return before, state  # Save/discard reset before release polling.
            # A 200 ms press can finish between slow SWD polls. The mailbox
            # returning to sentinel plus one completed release is sufficient
            # evidence of acceptance even if states 2/3 were not sampled.
            if state[1] == 1 and state[0] == NO_COMMAND and state[2] == before[2] + 1:
                return before, state
            time.sleep(0.12)
        raise RuntimeError(f"Button press {duration_ms} ms did not complete; last state {state}")

    def dismiss_fiducial(self):
        self.write_u32(self.symbols["vision_test_fiducial_press"].address, 1)

    def release_boot_hold(self):
        before = self.wait_state(0, timeout=10)
        self.write_u32(self.symbols["btn_swd_test_command_ms"].address, 0)
        after = self.wait_state(1, timeout=5)
        if after[2] != before[2] + 1:
            raise RuntimeError("Boot hold release did not complete exactly once")
        return before, after
