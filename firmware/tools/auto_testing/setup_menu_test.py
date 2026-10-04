"""Run the STM32F042 Setup Menu SWD and RX0 II hardware test.

Build ``stm32f042_setup_swd`` in PlatformIO first. The powered board, ST-Link
V3MINIE, and RX0 II in PC Remote still-photo mode must already be connected.
This script flashes the existing ELF, preserves the NVM page during flashing,
drives synthetic presses, and saves a report and every observation image.
"""

from __future__ import annotations

import argparse
from datetime import datetime
import json
from pathlib import Path
import sys
import time
import traceback

try:
    from .build_metadata import (BuildMetadata, GPHOTO2_EXE, NVM_START,
                                 latest_nvm_record, read_menu_items, settings_indices)
    from .eyes_rx0 import Rx0Camera
    from .frmbuf2png import save_framebuffer_png
    from .image_processing import compare_framebuffer_to_capture, stable_frame_signature
    from .target_gdb import GdbSession
except ImportError:  # Direct execution from this directory.
    from build_metadata import (BuildMetadata, GPHOTO2_EXE, NVM_START,
                                latest_nvm_record, read_menu_items, settings_indices)
    from eyes_rx0 import Rx0Camera
    from frmbuf2png import save_framebuffer_png
    from image_processing import compare_framebuffer_to_capture, stable_frame_signature
    from target_gdb import GdbSession


FIRMWARE = Path(__file__).resolve().parents[2]
ELF = FIRMWARE / ".pio" / "build" / "stm32f042_setup_swd" / "firmware.elf"
SHORT_MS = 200
LONG_MS = 1250
VISION_MISMATCH_LIMIT = 0.05


class TestRunner:
    def __init__(self, args):
        self.args = args
        self.output = args.output_dir.resolve() if args.output_dir else (
            Path.cwd() / f"setup-menu-{datetime.now():%Y-%m-%d_%H-%M-%S-%f}").resolve()
        self.output.mkdir(parents=True, exist_ok=False)
        self.report_file = (self.output / "report.txt").open("w", encoding="utf-8", buffering=1)
        self.events = []
        self.errors = 0
        self.camera_ok = True
        self.target = None
        self.camera = None
        self.items = []
        self.values = []
        self.baseline = []
        self.page = 0
        self.serial = 0

    def log(self, message: str, *, status: str = "INFO"):
        line = f"{datetime.now().astimezone().isoformat()} [{status}] {message}"
        print(line, flush=True)
        print(line, file=self.report_file, flush=True)
        self.events.append(line)
        if status == "FAIL":
            self.errors += 1

    def check(self, condition: bool, message: str, *, critical: bool = False):
        self.log(message, status="PASS" if condition else "FAIL")
        if critical and not condition:
            raise RuntimeError(message)

    def capture(self, label: str, *, compare: bool = True) -> bytes:
        """Read the OLED, save synthetic pixels and a corrected RX0 still."""
        self.serial += 1
        stem = f"{self.serial:04d}_{label}"
        prefix = self.output / stem
        time.sleep(0.3)
        framebuffer = self.target.framebuffer()
        (self.output / f"{stem}_framebuffer.bin").write_bytes(framebuffer)
        save_framebuffer_png(framebuffer, prefix.with_name(stem + "_framebuffer.png"))
        if self.camera_ok:
            try:
                corrected = self.camera.capture_rectified(prefix.with_name(stem + "_camera.png"),
                                                         raw_output=prefix.with_name(stem + "_full.jpg"))
                if compare:
                    # The voltage footer is recomputed every 100 ms, while a
                    # PTP still takes several seconds. Compare its stable
                    # title/value region, and report whole-frame error too.
                    volatile_rows = 18 if self.page == 7 else 0
                    score = compare_framebuffer_to_capture(framebuffer, corrected, prefix,
                                                           volatile_bottom_rows=volatile_rows)
                    self.log(f"{label}: camera/framebuffer mismatch {score.mismatch_count}/{score.pixel_count} "
                             f"({score.mismatch_fraction:.2%}); checked region "
                             f"{score.checked_mismatch_count}/{score.checked_pixel_count} "
                             f"({score.checked_fraction:.2%}), center threshold {score.threshold:.1f}, "
                             f"white {score.white_level:.1f}, black {score.black_level:.1f}",
                             status="PASS" if score.checked_fraction <= VISION_MISMATCH_LIMIT else "FAIL")
            except Exception as exc:
                self.camera_ok = False
                self.log(f"Camera evidence failed at {label}: {exc}; continuing framebuffer-only", status="FAIL")
        return framebuffer

    def page_name(self, index: int | None = None) -> str:
        index = self.page if index is None else index
        return self.items[index].title.replace("\n", "/")

    def state_text(self) -> str:
        if self.page >= len(self.values):
            return self.page_name()
        option = self.items[self.page].options[self.values[self.page]]
        return f"{self.page_name()} = {option.replace(chr(10), '/') }"

    def press(self, duration: int, label: str):
        before, after = self.target.press(duration)
        self.log(f"{label}: command {duration} ms, button {before} -> {after}")
        if after[1] != 0:
            self.check(after[2] == before[2] + 1, f"{label}: exactly one completed release")
        return after

    def short(self, label: str, *, observe: bool = True) -> bytes | None:
        self.press(SHORT_MS, label)
        self.page = (self.page + 1) % len(self.items)
        self.log(f"Expected page {self.page}: {self.state_text()}")
        return self.capture(label) if observe else None

    def long(self, label: str, *, duration: int = LONG_MS, observe: bool = True) -> bytes | None:
        if self.page >= len(self.values):
            raise RuntimeError(f"Cannot cycle an action page: {self.page_name()}")
        self.press(duration, label)
        self.values[self.page] = (self.values[self.page] + 1) % len(self.items[self.page].options)
        self.log(f"Expected value: {self.state_text()}")
        return self.capture(label) if observe else None

    def go_to(self, index: int, label: str, *, observe: bool = False):
        for step in range((index - self.page) % len(self.items)):
            self.short(f"{label}_navigate_{step + 1}", observe=observe)

    def set_value(self, index: int, value: int, label: str):
        self.go_to(index, label)
        count = len(self.items[index].options)
        for step in range((value - self.values[index]) % count):
            self.long(f"{label}_cycle_{step + 1}", observe=False)
        self.capture(label)

    def dismiss_and_enter(self, label: str, *, reset: bool = True):
        if reset:
            self.target.reset()
            self.log(f"{label}: GDB reset; fiducial gate should be visible")
        self.target.wait_state(0, timeout=12)
        self.target.dismiss_fiducial()
        # Boot setup hold is 3000 target milliseconds after the gate exits.
        time.sleep(3.5)
        self.target.release_boot_hold()
        self.page = 0
        self.values = settings_indices(latest_nvm_record(self.target.read_memory(NVM_START, 1024)))
        self.capture(label)

    def exit_menu(self, save: bool, label: str):
        self.go_to(len(self.items) - (2 if save else 1), label)
        after = self.press(LONG_MS, label)
        self.check(after[1] == 0, f"{label}: menu exit reset to boot-held state")
        self.page = 0

    def read_saved_values(self) -> list[int]:
        page = self.target.read_memory(NVM_START, 1024)
        return settings_indices(latest_nvm_record(page))

    def save_nvm_at_stop(self):
        """Capture NVM while GDB is still connected, even after a test error."""
        try:
            current = self.target.read_memory(NVM_START, 1024)
            (self.output / "nvm_at_stop.bin").write_bytes(current)
            self.log(f"NVM setting indices at stop: {settings_indices(latest_nvm_record(current))}")
        except Exception as exc:
            self.log(f"Could not read NVM at stop: {exc}", status="ERROR")

    def phase_a(self):
        self.log("A: entry and button classification")
        self.target.reset()
        self.target.dismiss_fiducial()
        time.sleep(1.5)
        self.target.release_boot_hold()
        self.capture("A1_early_release", compare=True)
        self.dismiss_and_enter("A2_enter")
        self.check(self.target.button_state()[1] == 1, "A2: mailbox idle after entry")
        self.short("A3_fan")
        self.short("A3_polarity")
        start = stable_frame_signature(self.target.framebuffer())
        self.long("A4_long")
        changed = stable_frame_signature(self.target.framebuffer())
        self.check(changed != start, "A4: long press visibly changed the value")
        self.long("A4_long_3000", duration=3000)
        self.press(850, "A5_short_850")
        self.page = (self.page + 1) % len(self.items)
        self.capture("A5_short_850")
        self.long("A5_long_1150", duration=1150)
        self.dismiss_and_enter("A_reset_for_B")

    def phase_b(self):
        self.log("B: page order, option cycles, and unsaved memory")
        first = stable_frame_signature(self.capture("B1_first_page"))
        signatures = [first]
        for index in range(1, len(self.items) + 1):
            frame = self.short(f"B1_page_{index:02d}")
            signatures.append(stable_frame_signature(frame))
        self.check(len(set(signatures[:-1])) == len(self.items), "B1: all page images are distinct")
        self.check(signatures[-1] == signatures[0], "B1: page walk wraps to original image")

        for index, item in enumerate(self.items[:len(self.values)]):
            self.go_to(index, f"B2_go_{index}")
            initial = stable_frame_signature(self.capture(f"B2_{index}_initial"), voltage_page=index == 7)
            images = [initial]
            for option in range(len(item.options)):
                image = self.long(f"B2_{index}_option_{option + 1:02d}")
                images.append(stable_frame_signature(image, voltage_page=index == 7))
            self.check(images[-1] == initial, f"B2 {self.page_name()}: options wrap to initial image")
            self.check(len(set(images[:-1])) == len(item.options),
                       f"B2 {self.page_name()}: each option has a distinct image")

        # Two RAM edits must survive page navigation, then disappear on discard.
        self.set_value(0, (self.values[0] + 1) % 3, "B3_edit_power")
        edited_power = stable_frame_signature(self.target.framebuffer())
        self.set_value(3, (self.values[3] + 1) % 4, "B3_edit_sleep")
        edited_sleep = stable_frame_signature(self.target.framebuffer())
        self.go_to(0, "B3_return_power")
        self.check(stable_frame_signature(self.capture("B3_unsaved_power")) == edited_power,
                   "B3: unsaved power selection survived navigation")
        self.go_to(3, "B3_return_sleep")
        self.check(stable_frame_signature(self.capture("B3_unsaved_sleep")) == edited_sleep,
                   "B3: unsaved sleep selection survived navigation")
        self.exit_menu(False, "B3_discard")
        self.check(self.read_saved_values() == self.baseline, "B3: discard left NVM baseline unchanged")
        self.dismiss_and_enter("B3_reenter", reset=False)
        self.check(self.values == self.baseline, "B3: discarded RAM edits absent after reentry")

        self.set_value(7, 0, "B4_voltage_zero")
        self.capture("B4_voltage_first")
        time.sleep(2)
        self.capture("B4_voltage_second")
        self.long("B4_voltage_positive")
        for step in range(5):
            self.long(f"B4_voltage_to_negative_{step + 1}", observe=False)
        self.capture("B4_voltage_negative")
        self.dismiss_and_enter("B_reset_for_C")

    def phase_c(self):
        self.log("C: save, discard, GDB reset persistence, restore")
        changed_power = (self.baseline[0] + 1) % 3
        self.set_value(0, changed_power, "C1_power")
        self.exit_menu(True, "C1_save")
        saved = self.read_saved_values()
        self.check(saved[0] == changed_power, "C1: power level saved in NVM")
        self.dismiss_and_enter("C1_reenter", reset=False)
        self.check(self.values[0] == changed_power, "C1: saved power level visible after reset")

        edits = {0: (self.values[0] + 1) % 3, 1: (self.values[1] + 1) % 16,
                 3: (self.values[3] + 1) % 4, 6: (self.values[6] + 1) % 7,
                 7: (self.values[7] + 1) % 11}
        for index, value in edits.items():
            self.set_value(index, value, f"C2_edit_{index}")
        self.exit_menu(True, "C2_save")
        saved = self.read_saved_values()
        for index, value in edits.items():
            self.check(saved[index] == value, f"C2: setting {index} saved as {value}")
        self.dismiss_and_enter("C2_reenter", reset=False)
        self.check(self.values == saved, "C2: all saved settings reloaded")

        self.set_value(0, (self.values[0] + 1) % 3, "C3_unsaved_power")
        self.set_value(3, (self.values[3] + 1) % 4, "C3_unsaved_sleep")
        self.exit_menu(False, "C3_discard")
        self.check(self.read_saved_values() == saved, "C3: discard preserved last saved NVM record")
        self.dismiss_and_enter("C3_reenter", reset=False)
        self.check(self.values == saved, "C3: discarded edits absent after reentry")

        # A debugger reset emulates reboot, but does not remove physical power.
        self.dismiss_and_enter("C4_debugger_reset", reset=True)
        self.check(self.values == saved, "C4: settings persisted through GDB reset")

        for index, value in enumerate(self.baseline):
            if self.values[index] != value:
                self.set_value(index, value, f"C5_restore_{index}")
        self.exit_menu(True, "C5_save_restore")
        self.check(self.read_saved_values() == self.baseline, "C5: baseline values restored in NVM")
        self.dismiss_and_enter("C5_reenter", reset=False)
        self.check(self.values == self.baseline, "C5: baseline values reloaded")

    def phase_d(self):
        self.log("D: inactivity and safe state")
        self.set_value(0, (self.baseline[0] + 1) % 3, "D1_unsaved_edit")
        before_timeout = stable_frame_signature(self.capture("D1_before_timeout"))
        self.log("D1: waiting 305 seconds with no button commands")
        time.sleep(305)
        after_timeout = stable_frame_signature(self.capture("D1_after_timeout"))
        self.check(after_timeout != before_timeout, "D1: timeout replaced the menu screen")
        self.check(self.read_saved_values() == self.baseline, "D1: timeout did not save edit")
        self.dismiss_and_enter("D1_reenter")

        self.short("D2_start_clock", observe=False)
        self.log("D2: waiting 270 seconds after a button action")
        time.sleep(270)
        active_frame = stable_frame_signature(self.short("D2_activity"))
        time.sleep(45)
        self.check(stable_frame_signature(self.capture("D2_still_in_menu")) == active_frame,
                   "D2: menu still visible after activity")
        self.log("D2: waiting for timeout after the last action")
        time.sleep(260)
        self.check(stable_frame_signature(self.capture("D2_after_timeout")) != active_frame,
                   "D2: menu eventually timed out after activity")
        self.dismiss_and_enter("D2_reenter")

        self.log("D3: scheduling long press near the 300-second deadline")
        # Start the clock on a button action, with no camera transfer between
        # that action and the scheduled near-deadline command.
        self.short("D3_start_clock", observe=False)
        deadline = time.monotonic() + 299.3
        while time.monotonic() < deadline:
            time.sleep(min(1, max(0.01, deadline - time.monotonic())))
        started = time.monotonic()
        try:
            self.long("D3_near_timeout")
            self.log(f"D3: command started {started - (deadline - 299.3):.3f} seconds after entry")
        except RuntimeError as exc:
            self.log(f"D3: late press or firmware timeout: {exc}", status="FAIL")
        self.capture("D3_after_press")
        self.log("D4: electrical PB1 and PA6 output checks require separate probe evidence", status="BLOCKED")
        self.dismiss_and_enter("D_final_reenter")
        self.check(self.read_saved_values() == self.baseline, "D: NVM baseline still present")

    def run(self):
        try:
            self.log(f"Output directory: {self.output}")
            metadata = BuildMetadata(ELF)
            self.log(f"Build environment: stm32f042_setup_swd; ELF: {ELF}")
            self.log(f"ELF SHA-256: {metadata.sha256}; load ranges: {metadata.load_ranges}")
            self.log("Key ELF symbols: " + json.dumps({name: f"0x{metadata.address(name):08x}"
                                                   for name in ("setup_menu_items", "vision_test_oled_sent_generation",
                                                                "vision_test_oled_sent_framebuffer",
                                                                "btn_swd_test_command_ms", "btn_swd_test_state",
                                                                "btn_swd_test_completed", "vision_test_fiducial_press")}))
            self.log("C4 uses GDB reset and does not claim removal of board power")
            with GdbSession(metadata.elf, metadata.symbols, port=self.args.gdb_port) as target:
                self.target = target
                original_page = target.read_memory(NVM_START, 1024)
                (self.output / "nvm_before.bin").write_bytes(original_page)
                self.baseline = settings_indices(latest_nvm_record(original_page))
                self.log(f"Starting setting indices: {self.baseline}")
                target.flash()
                flashed_page = target.read_memory(NVM_START, 1024)
                self.check(flashed_page == original_page, "Flash preserved the reserved NVM page", critical=True)
                self.items = target.inspect(lambda: read_menu_items(target, metadata.address("setup_menu_items")))
                self.log("Compiled menu: " + json.dumps([{"title": x.title, "options": x.options} for x in self.items]))

                try:
                    self.camera = Rx0Camera(camera_index=self.args.camera_index, gphoto2=self.args.gphoto2)
                    self.log(f"Camera: {self.camera.model}")
                    if self.args.calibration:
                        self.camera.use_calibration(self.args.calibration)
                        self.log(f"Reusing calibration: {self.args.calibration}")
                    else:
                        image = self.camera.calibrate(self.output / "fiducial_calibration.png",
                                                      crop_to=self.args.crop_to,
                                                      show_progress=self.args.show_progress)
                        self.log(f"Camera calibration: {image.with_suffix('.json')}")
                except Exception as exc:
                    self.camera_ok = False
                    self.log(f"Camera setup failed: {exc}; proceeding with framebuffer evidence", status="FAIL")
                try:
                    self.capture("fiducial_gate")
                    self.dismiss_and_enter("initial_menu", reset=False)
                    self.phase_a()
                    self.phase_b()
                    self.phase_c()
                    self.phase_d()
                finally:
                    self.save_nvm_at_stop()
                self.log(f"Final result: {self.errors} failures", status="PASS" if not self.errors else "FAIL")
                self.log(f"Artifacts: {self.output}")
        except Exception as exc:
            self.log(f"Test interrupted: {exc}", status="ERROR")
            print(traceback.format_exc(), file=self.report_file)
            return 2
        finally:
            self.report_file.close()
        return 1 if self.errors else 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, help="New artifact directory; default is timestamped under cwd")
    parser.add_argument("--calibration", type=Path, help="Reuse a prior fiducial JSON instead of searching focus/EV")
    parser.add_argument("--crop-to", type=int, default=800, help="Central fiducial search square (default 800)")
    parser.add_argument("--camera-index", type=int, help="RX0 camera index; default first available")
    parser.add_argument("--gphoto2", type=Path, default=GPHOTO2_EXE, help="Full path to gphoto2.exe")
    parser.add_argument("--gdb-port", type=int, default=3333, help="Local OpenOCD GDB TCP port")
    parser.add_argument("--show-progress", action="store_true", help="Show calibration OpenCV preview")
    args = parser.parse_args(argv)
    if args.crop_to < 1:
        parser.error("--crop-to must be positive")
    return TestRunner(args).run()


if __name__ == "__main__":
    sys.exit(main())
