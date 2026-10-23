"""Actual journal failure returns and watchdog initialization, using native RAM."""
import ctypes
import os
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]


class StorageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build = ROOT / ".cache/faults-native"
        build.mkdir(parents=True, exist_ok=True)
        dll = build / f"storage-{os.getpid()}.dll"
        subprocess.run(["clang", "-shared", "-nostdlib", "-fuse-ld=lld", "-O1", "-Wno-pointer-to-int-cast",
                        "-ffunction-sections", "-fdata-sections", "-DHOT_WAND_TARGET_STM32F042",
                        "-Itest/faults/support", "-Iinc", "-Ilib/u8g2/csrc", "test/faults/storage_bridge.c",
                        "-o", str(dll), "-Wl,/noentry", "-Wl,/opt:ref"], cwd=ROOT, check=True)
        cls.lib = ctypes.CDLL(str(dll))

    def setUp(self):
        self.lib.storage_reset()

    def test_successful_journal_and_unchanged_save(self):
        self.assertEqual(self.lib.storage_save(0), 1)
        calls = self.lib.storage_value(0)
        self.assertEqual(self.lib.storage_save(0), 1)
        self.assertEqual(self.lib.storage_value(0), calls)
        self.lib.storage_reboot()
        self.assertEqual(self.lib.storage_read(), 0)

    def test_unlock_and_program_failures_return_false_preserve_previous(self):
        for failure in (1, 5, 6, 7):
            self.setUp()
            self.assertEqual(self.lib.storage_save(0), 1)
            self.lib.storage_fail(failure)
            self.assertEqual(self.lib.storage_save(1), 0)
            self.lib.storage_reboot()
            self.assertEqual(self.lib.storage_read(), 0)
            if failure != 1:
                self.assertGreaterEqual(self.lib.storage_value(1), 2)
            self.lib.storage_fail(0)
            self.assertEqual(self.lib.storage_save(1), 1)
            self.lib.storage_reboot()
            self.assertEqual(self.lib.storage_read(), 1)

    def test_torn_record_consumed_and_never_adopted(self):
        self.assertEqual(self.lib.storage_save(0), 1)
        self.lib.storage_fail(7)
        self.assertEqual(self.lib.storage_save(1), 0)
        self.lib.storage_reboot()
        self.assertEqual(self.lib.storage_read(), 0)
        self.assertEqual(self.lib.storage_value(2), 2)

    def test_erase_status_error_and_readback_failures(self):
        for failure in (1, 2, 3, 4):
            self.setUp()
            self.lib.storage_save(0)
            self.lib.storage_fail(failure)
            self.assertEqual(self.lib.storage_erase(), 0)
        self.lib.storage_fail(0)
        self.assertEqual(self.lib.storage_erase(), 1)
        self.lib.storage_reboot()
        self.assertEqual(self.lib.storage_read(), 255)

    def test_watchdog_start_failure_is_returned_without_claiming_reset(self):
        self.lib.storage_fail(8)
        self.assertEqual(self.lib.storage_watchdog(), 0)
        self.assertEqual(self.lib.storage_value(3), 1)
        self.assertEqual(self.lib.storage_value(4), 0)
        self.lib.storage_fail(0)
        self.assertEqual(self.lib.storage_watchdog(), 1)
        self.lib.storage_feed()
        self.assertEqual(self.lib.storage_value(4), 1)


if __name__ == "__main__":
    unittest.main()
