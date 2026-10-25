"""python -m unittest discover -s test/tipdetect_swd -v

Native LLVM tests of real tipdetect.c with register/timer mocks. No board is
flashed; hardware timing and the interrupt vector wiring need hardware checks.
"""

import ctypes
import os
from pathlib import Path
import shutil
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
NO_COMMAND = 0xFFFFFFFF


class TipDetectorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        clang = shutil.which("clang")
        if sys.platform != "win32" or not clang or not shutil.which("lld-link"):
            raise RuntimeError("Native tip tests require Windows LLVM clang and lld-link")
        build = ROOT / ".cache/tipdetect_swd"
        build.mkdir(parents=True, exist_ok=True)
        cls.libraries = []
        for harness in (0, 1):
            dll = build / f"tip-{harness}-{os.getpid()}.dll"
            command = [clang, "-shared", "-nostdlib", "-fuse-ld=lld", "-O1", "-Wno-unknown-attributes",
                       f"-DTIP_SWD_TEST_HARNESS={harness}", "-DHSE_VALUE=27120000",
                       "-DTIPDETECT_EDGE_FAULT_COUNT=10",  # Positive cases are independent of bench tuning.
                       "-Itest/tipdetect_swd/support", "-Iinc", "-Ilib/u8g2/csrc",
                       "test/tipdetect_swd/bridge.c", "-o", str(dll), "-Wl,/noentry"]
            if harness:
                command += [f"-Wl,/export:{name}" for name in
                            ("tip_swd_test_command", "tip_swd_test_state", "tip_swd_test_completed")]
            subprocess.run(command, cwd=ROOT, check=True)
            lib = ctypes.CDLL(str(dll))
            lib.bridge_read.argtypes = [ctypes.c_uint32]
            lib.bridge_read.restype = ctypes.c_uint32
            cls.libraries.append(lib)

    def setUp(self):
        self.real, self.tip = self.libraries
        for lib in self.libraries:
            lib.bridge_reset()
        self.command = ctypes.c_uint32.in_dll(self.tip, "tip_swd_test_command")
        self.state = ctypes.c_uint32.in_dll(self.tip, "tip_swd_test_state")
        self.completed = ctypes.c_uint32.in_dll(self.tip, "tip_swd_test_completed")

    def request(self, level):
        self.command.value = level
        self.tip.bridge_task()

    def test_virtual_presence_at_boot_ignores_physical_absence(self):
        self.assertEqual(self.state.value, 3)
        self.tip.bridge_physical(0)
        self.tip.bridge_init()
        self.assertEqual(self.state.value, 1)
        self.assertEqual(self.command.value, NO_COMMAND)
        self.assertEqual(self.tip.bridge_read(0), 1)
        self.assertEqual(self.tip.bridge_read(1), 0)
        self.assertEqual(self.tip.bridge_read(3), 0)

    def test_removal_uses_exti_and_timer_before_latching(self):
        self.tip.bridge_init()
        self.request(0)
        self.assertEqual(self.command.value, NO_COMMAND)
        self.assertEqual(self.state.value, 2)
        self.assertEqual(self.tip.bridge_read(0), 0)
        self.assertEqual(self.tip.bridge_read(6), 1)
        self.assertEqual(self.tip.bridge_read(1), 0)
        self.tip.bridge_irq()
        self.assertEqual(self.tip.bridge_read(2), 1)
        self.assertEqual(self.tip.bridge_read(4), 1)
        self.assertEqual(self.tip.bridge_read(5), 0)
        self.assertEqual((self.tip.bridge_read(9), self.tip.bridge_read(10)), (8135, 0))
        self.assertEqual(self.tip.bridge_read(3), 0)
        self.tip.bridge_expire()
        self.assertEqual(self.tip.bridge_read(1), 1)
        self.assertEqual(self.tip.bridge_read(3), 1)
        self.assertEqual(self.tip.bridge_read(5), 1)
        self.assertEqual(self.state.value, 0)
        self.assertEqual(self.completed.value, 1)
        # A stale timer interrupt cannot count/stop a second time.
        self.tip.bridge_expire()
        self.assertEqual(self.completed.value, 1)
        self.assertEqual(self.tip.bridge_read(3), 1)

    def test_busy_qualification_defers_next_command_and_preserves_latch(self):
        self.tip.bridge_init()
        self.request(0)
        self.tip.bridge_irq()
        self.request(1)
        self.assertEqual(self.command.value, 1)
        self.assertEqual(self.tip.bridge_read(0), 0)
        self.tip.bridge_expire()
        self.tip.bridge_reset_latch()
        self.assertEqual(self.tip.bridge_read(1), 1)
        self.tip.bridge_task()
        self.assertEqual(self.command.value, NO_COMMAND)
        self.assertEqual(self.tip.bridge_read(0), 1)
        self.tip.bridge_irq()
        self.tip.bridge_reset_latch()
        self.assertEqual(self.tip.bridge_read(1), 1)
        self.tip.bridge_expire()
        self.assertEqual(self.state.value, 1)
        self.assertEqual(self.completed.value, 2)
        self.assertEqual(self.tip.bridge_read(1), 1)  # Insertion does not auto-clear.
        self.tip.bridge_reset_latch()
        self.assertEqual(self.tip.bridge_read(1), 0)

    def test_same_level_and_invalid_commands_do_not_create_edges(self):
        self.tip.bridge_init()
        self.request(1)
        self.assertEqual(self.completed.value, 1)
        self.assertEqual(self.tip.bridge_read(6), 0)
        self.assertEqual(self.state.value, 1)
        for invalid in (2, 100, NO_COMMAND):
            self.request(invalid)
            self.assertEqual(self.command.value, NO_COMMAND)
            self.assertEqual(self.state.value, 1)
            self.assertEqual(self.completed.value, 1)
        self.assertEqual(self.tip.bridge_read(4), 0)

    def test_physical_edges_isolated_shared_button_irq_still_dispatched(self):
        self.tip.bridge_init()
        for level in (0, 1, 0, 1):
            self.tip.bridge_physical(level)
            self.tip.bridge_irq()
        self.assertEqual(self.tip.bridge_read(4), 0)
        self.assertEqual(self.tip.bridge_read(0), 1)
        self.tip.bridge_button_irq()
        self.assertEqual(self.tip.bridge_read(7), 1)
        self.assertEqual(self.state.value, 1)

    def test_timer_init_failures_stay_not_ready_and_fail_closed(self):
        for phase in (1, 2):
            self.tip.bridge_reset()
            self.tip.bridge_timer_failure(phase)
            self.tip.bridge_init()
            self.assertEqual(self.state.value, 3)
            self.assertEqual(self.tip.bridge_read(1), 1)
            self.assertEqual(self.tip.bridge_read(3), 1)
            self.request(0)
            self.assertEqual(self.command.value, 0)
            self.assertEqual(self.tip.bridge_read(6), 0)

    def test_task_restores_existing_interrupt_mask(self):
        self.tip.bridge_init()
        for mask in (0, 1):
            self.tip.bridge_mask(mask)
            self.request(1)
            self.assertEqual(self.tip.bridge_read(8), mask)

    def test_production_still_latches_missing_tip_at_boot(self):
        with self.assertRaises(ValueError):
            ctypes.c_uint32.in_dll(self.real, "tip_swd_test_command")
        self.real.bridge_physical(0)
        self.real.bridge_init()
        self.assertEqual(self.real.bridge_read(0), 0)
        self.assertEqual(self.real.bridge_read(1), 1)
        self.assertEqual(self.real.bridge_read(3), 1)

    def test_production_glitch_restored_before_timer_does_not_latch(self):
        self.real.bridge_init()
        self.real.bridge_physical(0)
        self.real.bridge_irq()
        self.real.bridge_physical(1)
        self.real.bridge_expire()
        self.assertEqual(self.real.bridge_read(0), 1)
        self.assertEqual(self.real.bridge_read(1), 0)
        self.assertEqual(self.real.bridge_read(3), 0)

    def test_activity_count_boundary_and_decay(self):
        self.real.bridge_init()
        for count in range(1, 12):
            # Shared EXTI handler counts even if presence is restored before
            # the qualification timer samples the line.
            self.real.bridge_physical(0)
            self.real.bridge_irq()
            self.real.bridge_physical(1)
            self.real.bridge_expire()
            self.real.bridge_task()
            self.assertEqual(self.real.bridge_read(4), count)
            self.assertEqual(self.real.bridge_read(11), int(count >= 11))
        self.real.bridge_time(100)
        self.real.bridge_task()
        self.assertEqual(self.real.bridge_read(4), 10)

    def test_production_still_debounces_physical_removal(self):
        self.real.bridge_init()
        self.real.bridge_physical(0)
        self.real.bridge_irq()
        self.assertEqual(self.real.bridge_read(2), 1)
        self.assertEqual(self.real.bridge_read(1), 0)
        self.real.bridge_expire()
        self.assertEqual(self.real.bridge_read(1), 1)
        self.assertEqual(self.real.bridge_read(3), 1)


class TipStormConfigurationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build = ROOT / ".cache/tipdetect_swd"
        build.mkdir(parents=True, exist_ok=True)
        cls.variants = {}
        for threshold, harness in ((0, 0), (2, 0), (0, 1)):
            dll = build / f"tip-threshold-{threshold}-{harness}-{os.getpid()}.dll"
            command = ["clang", "-shared", "-nostdlib", "-fuse-ld=lld", "-O1", "-Wno-unknown-attributes",
                       f"-DTIPDETECT_EDGE_FAULT_COUNT={threshold}", f"-DTIP_SWD_TEST_HARNESS={harness}",
                       "-DHSE_VALUE=27120000", "-Itest/tipdetect_swd/support", "-Iinc", "-Ilib/u8g2/csrc",
                       "test/tipdetect_swd/bridge.c", "-o", str(dll), "-Wl,/noentry"]
            if harness:
                command += [f"-Wl,/export:{name}" for name in
                            ("tip_swd_test_command", "tip_swd_test_state", "tip_swd_test_completed")]
            subprocess.run(command, cwd=ROOT, check=True)
            cls.variants[threshold, harness] = ctypes.CDLL(str(dll))

    def glitch(self, lib):
        lib.bridge_physical(0)
        lib.bridge_irq()
        lib.bridge_physical(1)
        lib.bridge_expire()
        lib.bridge_task()

    def test_custom_threshold_is_strictly_greater(self):
        lib = self.variants[2, 0]
        lib.bridge_reset()
        lib.bridge_init()
        for count in range(1, 4):
            self.glitch(lib)
            self.assertEqual(lib.bridge_read(4), count)
            self.assertEqual(lib.bridge_read(11), int(count == 3))

    def test_zero_disables_storm_tracking_but_still_latches_removal(self):
        lib = self.variants[0, 0]
        lib.bridge_reset()
        lib.bridge_init()
        for count in range(100):
            self.glitch(lib)
            lib.bridge_time(count * 100)
        self.assertEqual(lib.bridge_read(4), 0)
        self.assertEqual(lib.bridge_read(11), 0)
        self.assertEqual(lib.bridge_read(1), 0)
        lib.bridge_physical(0)
        lib.bridge_irq()
        self.assertEqual(lib.bridge_read(2), 1)
        lib.bridge_expire()
        self.assertEqual(lib.bridge_read(1), 1)
        self.assertEqual(lib.bridge_read(3), 1)

    def test_zero_still_detects_absent_tip_at_boot_and_timer_failure(self):
        lib = self.variants[0, 0]
        lib.bridge_reset()
        lib.bridge_physical(0)
        lib.bridge_init()
        self.assertEqual(lib.bridge_read(1), 1)
        self.assertEqual(lib.bridge_read(3), 1)
        lib.bridge_reset()
        lib.bridge_timer_failure(1)
        lib.bridge_init()
        self.assertEqual(lib.bridge_read(1), 1)
        self.assertEqual(lib.bridge_read(3), 1)

    def test_zero_retains_virtual_tip_commands(self):
        lib = self.variants[0, 1]
        lib.bridge_reset()
        lib.bridge_init()
        command = ctypes.c_uint32.in_dll(lib, "tip_swd_test_command")
        command.value = 0
        lib.bridge_task()
        self.assertEqual(command.value, NO_COMMAND)
        lib.bridge_irq()
        lib.bridge_expire()
        self.assertEqual(lib.bridge_read(1), 1)
        self.assertEqual(lib.bridge_read(3), 1)
        self.assertEqual(lib.bridge_read(11), 0)


if __name__ == "__main__":
    unittest.main()
