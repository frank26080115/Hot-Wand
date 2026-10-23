"""Shared boot protocol tests; no board, debugger, or camera is accessed.

Exercise the real Python producer/boot helper against an SWD memory model.
Native ADC and tip tests separately execute the firmware consumers.
"""

import math
from pathlib import Path
import struct
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/auto_testing"))
from build_metadata import read_menu_items
from harness import start_test_boot, MCU_CALIBRATION, VREF_CALIBRATION


class MemoryTarget:
    def __init__(self):
        self.memory = {}
        self.running = False
        self.transaction = 0
        self.writes = []
        self.symbols = {name: SimpleNamespace(address=address) for name, address in (
            ("adc_sim", 0x20000000), ("adc_sim_decode_table", 0x08001000),
            ("btn_swd_test_boot_hold", 0x20001000), ("vision_test_fiducial_enabled", 0x20001004),
            ("vision_test_oled_sent_generation", 0x20001008), ("setup_menu_item_count", 0x08001200))}
        self.put(0x20000000, struct.pack("<4I", 1, 128, 12, 7))
        table = [round(341 * (4 ** (code / 255) - 1)) for code in range(256)]
        self.put(0x08001000, struct.pack("<256H", *table))
        self.put(MCU_CALIBRATION, struct.pack("<H", 1450))
        self.put(VREF_CALIBRATION, struct.pack("<H", 1500))

    def put(self, address, data):
        self.memory.update((address + i, byte) for i, byte in enumerate(data))

    def read_memory(self, address, length):
        return bytes(self.memory.get(address + i, 0) for i in range(length))

    def read_u32(self, address):
        return int.from_bytes(self.read_memory(address, 4), "little")

    def read_c_string(self, address):
        data = bytearray()
        while self.memory.get(address, 0):
            data.append(self.memory[address])
            address += 1
        return data.decode("ascii")

    def write_memory(self, address, data):
        if self.running:
            raise AssertionError("Boot writes must happen under a debugger halt")
        self.writes.append((self.transaction, address, bytes(data)))
        self.put(address, data)
        if address == self.symbols["adc_sim"].address + 24:
            # Stand in for the firmware starting acquisition and sending an OLED
            # frame after the halt ends. Payload/option ordering is checked below.
            self.put(self.symbols["vision_test_oled_sent_generation"].address, struct.pack("<I", 2))

    def write_u32(self, address, value):
        self.write_memory(address, struct.pack("<I", value))

    def inspect(self, callback):
        running = self.running
        if running:
            self.transaction += 1
        self.running = False
        try:
            return callback()
        finally:
            self.running = running

    def resume(self):
        self.running = True


class SharedBootTests(unittest.TestCase):
    def setUp(self):
        self.target = MemoryTarget()

    def boot(self, **options):
        return start_test_boot(self.target, self.target.symbols, **options)

    def test_runtime_boot_defaults_and_publish_order(self):
        report = self.boot()
        self.assertTrue(self.target.running)
        self.assertEqual(self.target.read_u32(0x20001000), 0)
        self.assertEqual(self.target.read_u32(0x20001004), 0)
        # No floating point or call into target code is needed over SWD. All
        # options, a full FIFO slot, head commit and start share one short halt.
        writes = self.target.writes
        self.assertEqual(len({item[0] for item in writes}), 1)
        self.assertEqual([item[1] for item in writes],
                         [0x20001000, 0x20001004, 0x20000044, 0x20000010, 0x20000018])
        self.assertEqual(struct.unpack("<I", writes[-2][2])[0], 1)
        self.assertEqual(struct.unpack("<I", writes[-1][2])[0], 1)
        codes = struct.unpack("<7BxI", writes[2][2])
        self.assertEqual(codes[-1], 1)  # One frame, then held last by firmware.
        self.assertEqual(list(codes[:7]), list(report[0]["codes"]))
        self.assertEqual(report[0]["requested"], (676, 676, 0, 870, 870, 363, 375))
        self.assertTrue(all(abs(a - b) <= 4 for a, b in
                            zip(report[0]["requested"], report[0]["actual"])))

    def test_setup_and_camera_opt_in_use_same_boot_path(self):
        self.boot(setup=True, fiducial=True)
        self.assertEqual(self.target.read_u32(0x20001000), 1)
        self.assertEqual(self.target.read_u32(0x20001004), 1)

    def test_setup_can_boot_without_camera_gate(self):
        self.boot(setup=True)
        self.assertEqual(self.target.read_u32(0x20001000), 1)
        self.assertEqual(self.target.read_u32(0x20001004), 0)

    def test_active_sequence_is_never_overwritten(self):
        self.target.put(0x2000001c, struct.pack("<I", 2))
        with self.assertRaisesRegex(RuntimeError, "fresh, empty"):
            self.boot()
        self.assertEqual(self.target.writes, [])

    def test_preloaded_sequence_is_never_overwritten(self):
        self.target.put(0x20000010, struct.pack("<I", 1))
        with self.assertRaisesRegex(RuntimeError, "fresh, empty"):
            self.boot()
        self.assertEqual(self.target.writes, [])

    def test_invalid_factory_calibration_does_not_start(self):
        self.target.put(VREF_CALIBRATION, b"\xff\xff")
        with self.assertRaisesRegex(RuntimeError, "calibration"):
            self.boot()
        self.assertEqual(self.target.writes, [])

    def test_failure_before_producer_commit_never_starts(self):
        original = self.target.write_memory
        def fail_payload(address, data):
            if address == 0x20000044:
                raise IOError("transport failure")
            original(address, data)
        with patch.object(self.target, "write_memory", side_effect=fail_payload):
            with self.assertRaisesRegex(IOError, "transport failure"):
                self.boot(setup=True)
        self.assertEqual(self.target.read_u32(0x20000010), 0)
        self.assertEqual(self.target.read_u32(0x20000018), 0)
        self.assertTrue(self.target.running)

    def test_menu_length_is_read_from_firmware(self):
        for count in (10, 11):
            with self.subTest(count=count):
                target = MemoryTarget()
                target.put(target.symbols["setup_menu_item_count"].address, struct.pack("<I", count))
                table_address = 0x08002000
                for index in range(count):
                    title = f"VALUE{index}" if index < count - 2 else ("SAVE\nAND\nEXIT" if index == count - 2 else "EXIT\nNO\nSAVE")
                    title_address = 0x08003000 + index * 128
                    option_address = title_address + 64
                    target.put(title_address, title.encode("ascii") + b"\0")
                    target.put(option_address, b"A|B\0")
                    target.put(table_address + index * 12,
                               struct.pack("<IIB3x", title_address, option_address, 2 if index < count - 2 else 0))
                items = read_menu_items(target, table_address)
                self.assertEqual(len(items), count)
                self.assertEqual(items[-2].title, "SAVE\nAND\nEXIT")


if __name__ == "__main__":
    unittest.main()
