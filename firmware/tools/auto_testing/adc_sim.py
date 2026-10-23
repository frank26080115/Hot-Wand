"""Producer for the adc_sim.h SWD ABI, using the existing persistent GDB transport.

Build stm32f042_test, the shared automated-test image. A sequence JSON is a list of objects:
  [{"channels": [676, 560, 100, 870, 870, 360, 372], "repeat_rounds": 12800}]
Values are requested raw 10-bit counts in DC/buck/current/NTC1/NTC2/MCU/VREF
order, not volts/degrees. Codes and actual representable counts are reported.
Durations include boot/setup/terminal-screen time: ADC interrupts keep running.

Example (uses an already flashed matching ELF; does not reset or flash):
  python tools/auto_testing/adc_sim.py sequence.json --start
Import AdcSimProducer for streaming/refilling and read status() for progress.
One producer must own the queue. Reset the MCU before an independent sequence.
The shared image also enables virtual button/tip inputs and framebuffer capture.
Boot defaults to a released button, a present virtual tip, and no camera gate.
The shared test image keeps physical RF off and buck attenuation GPIO-high
(minimum voltage). Normal fault supervision and logical limiter calculations
remain active; reported PWM is the requested logical value, not physical PWM.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
from pathlib import Path
import struct
import time

try:
    from .build_metadata import BuildMetadata, TEST_ELF
    from .target_gdb import GdbSession
except ImportError:
    from build_metadata import BuildMetadata, TEST_ELF
    from target_gdb import GdbSession


UINT32_MASK = 0xFFFFFFFF
FIFO_OFFSET = 68
FRAME_SIZE = 12
CHANNELS = ("dc", "buck", "current", "ntc1", "ntc2", "mcu", "vrefint")


@dataclass(frozen=True)
class Frame:
    channels: tuple[int, ...]
    repeat_rounds: int = 1

    def __post_init__(self):
        if len(self.channels) != 7 or any(type(v) is not int or not 0 <= v <= 1023 for v in self.channels):
            raise ValueError("channels must contain seven integer raw ADC counts in 0..1023")
        if type(self.repeat_rounds) is not int or not 1 <= self.repeat_rounds <= UINT32_MASK:
            raise ValueError("repeat_rounds must be an integer in 1..4294967295")


class AdcSimProducer:
    """The firmware consumes slots only after the final producer-count commit.

    Every batch is one inspect() transaction, including status, payload and
    commit. A halt inside an ISR copy is safe: consumer_count is conservative
    until the copy finishes. All counter arithmetic is modulo 2**32.
    """

    def __init__(self, target, symbols):
        self.target = target
        self.address = symbols["adc_sim"].address
        table_address = symbols["adc_sim_decode_table"].address

        def initialize():
            header = struct.unpack("<4I", target.read_memory(self.address, 16))
            version, self.capacity, frame_size, count = header
            if version != 1 or frame_size != FRAME_SIZE or count != 7:
                raise RuntimeError("ADC simulation ABI is unavailable or incompatible; wait for the boot gate")
            if not 2 <= self.capacity <= 65536 or self.capacity & (self.capacity - 1):
                raise RuntimeError("Invalid ADC simulation FIFO capacity")
            self.table = struct.unpack("<256H", target.read_memory(table_address, 512))
            if self.table[0] != 0 or self.table[-1] != 1023 or any(a >= b for a, b in zip(self.table, self.table[1:])):
                raise RuntimeError("Invalid ADC simulation decode table")

        target.inspect(initialize)

    def encode(self, frame: Frame) -> tuple[bytes, dict]:
        codes = tuple(min(range(256), key=lambda c: (abs(self.table[c] - value), c)) for value in frame.channels)
        actual = tuple(self.table[c] for c in codes)
        payload = struct.pack("<7BxI", *codes, frame.repeat_rounds)
        return payload, {"requested": frame.channels, "codes": codes, "actual": actual,
                         "repeat_rounds": frame.repeat_rounds}

    def status(self) -> dict:
        def read():
            words = struct.unpack("<13I", self.target.read_memory(self.address, 52))
            keys = ("abi_version", "capacity", "frame_size", "channel_count", "producer_count", "consumer_count",
                    "start_request", "state", "rounds_remaining", "rounds_completed", "frames_dequeued",
                    "underruns", "protocol_errors")
            result = dict(zip(keys, words))
            result["queued"] = (result["producer_count"] - result["consumer_count"]) & UINT32_MASK
            if result["queued"] > self.capacity:
                raise RuntimeError("ADC simulation queue counters are inconsistent")
            result["decoded"] = struct.unpack("<7H", self.target.read_memory(self.address + 52, 14))
            return result
        return self.target.inspect(read)

    def enqueue(self, frames) -> list[dict]:
        """Commit a whole batch or raise BufferError without writing any slots."""
        encoded = [self.encode(frame) for frame in frames]
        if not encoded:
            return []

        def commit():
            producer, consumer = struct.unpack("<2I", self.target.read_memory(self.address + 16, 8))
            queued = (producer - consumer) & UINT32_MASK
            if queued > self.capacity:
                raise RuntimeError("ADC simulation queue counters are inconsistent")
            if len(encoded) > self.capacity - queued:
                raise BufferError(f"FIFO has {self.capacity - queued} free frames, need {len(encoded)}")
            slot = producer & (self.capacity - 1)
            split = min(len(encoded), self.capacity - slot)
            payload = b"".join(item[0] for item in encoded)
            self.target.write_memory(self.address + FIFO_OFFSET + slot * FRAME_SIZE, payload[:split * FRAME_SIZE])
            if split < len(encoded):
                self.target.write_memory(self.address + FIFO_OFFSET, payload[split * FRAME_SIZE:])
            # No counter publication if either payload write raises.
            self.target.write_u32(self.address + 16, (producer + len(encoded)) & UINT32_MASK)

        self.target.inspect(commit)
        return [report for _, report in encoded]

    def start(self):
        def request():
            state = self.status()
            if state["state"] in (1, 2):
                return
            if state["state"] != 0 or state["queued"] == 0:
                raise RuntimeError("Queue at least one frame before starting simulation")
            self.target.write_u32(self.address + 24, 1)
        self.target.inspect(request)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("sequence", nargs="?", type=Path, help="JSON frame list; omit for status only")
    parser.add_argument("--elf", type=Path, default=TEST_ELF)
    parser.add_argument("--start", action="store_true", help="Request playback after committing the sequence")
    parser.add_argument("--gdb-port", type=int, default=3333)
    args = parser.parse_args()
    frames = []
    if args.sequence:
        frames = [Frame(tuple(item["channels"]), item.get("repeat_rounds", 1))
                  for item in json.loads(args.sequence.read_text())]
    metadata = BuildMetadata(args.elf, required_symbols=("adc_sim", "adc_sim_decode_table", "adc_sim_start"))
    with GdbSession(metadata.elf, metadata.symbols, port=args.gdb_port, reset_on_connect=False) as target:
        # GdbSession connects halted but does not reset the target here. Let
        # firmware initialize its BSS metadata while the watchdog is serviced.
        target.resume()
        deadline = time.monotonic() + 5
        while target.read_u32(metadata.address("adc_sim")) == 0:
            if time.monotonic() >= deadline:
                raise RuntimeError("ADC simulation boot gate did not become ready")
            time.sleep(0.02)
        producer = AdcSimProducer(target, metadata.symbols)
        print(json.dumps({"encoding": producer.enqueue(frames)}))
        if args.start:
            producer.start()
        print(json.dumps(producer.status()))


if __name__ == "__main__":
    main()
