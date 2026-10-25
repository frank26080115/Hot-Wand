"""Shared-test outputs stay disabled while the production control logic runs."""
import ctypes
import os
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]


class OutputInhibitTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build = ROOT / ".cache/faults-native"
        build.mkdir(parents=True, exist_ok=True)
        cls.libraries = []
        for name in ("power_bridge", "rf_inhibit_bridge"):
            dll = build / f"inhibited-{name}-{os.getpid()}.dll"
            subprocess.run(["clang", "-shared", "-nostdlib", "-fuse-ld=lld", "-O1",
                            "-ffunction-sections", "-fdata-sections", "-DPWRLVL_PWM_JUMP_LEVEL=16", "-DHOT_WAND_TEST_HARNESS=1",
                            "-DNATIVE_OUTPUT_MODEL=1", "-DHSE_VALUE=27120000",
                            "-Itest/faults/support", "-Iinc", "-Ilib/u8g2/csrc",
                            f"test/faults/{name}.c", "-o", str(dll), "-Wl,/noentry", "-Wl,/opt:ref"],
                           cwd=ROOT, check=True)
            cls.libraries.append(ctypes.CDLL(str(dll)))
        cls.power, cls.rf = cls.libraries
        cls.power.bridge_fault.restype = ctypes.c_char_p

    def assert_buck_minimum(self):
        self.assertEqual(self.power.bridge_value(5), 1)  # GPIO output.
        self.assertEqual(self.power.bridge_value(6), 1)  # Driven high.
        self.assertEqual(self.power.bridge_value(7), 0)  # No alternate-function handoff.
        self.assertEqual(self.power.bridge_value(8), 0)  # TIM3 not started.

    def test_init_regulation_and_release_never_connect_physical_pwm(self):
        p = self.power
        p.bridge_reset(100)
        self.assert_buck_minimum()
        p.bridge_input(9, 1)
        self.assertEqual(p.bridge_value(0), 32)
        p.bridge_input(9, 0)
        self.assertEqual(p.bridge_value(0), 31)
        for tick in range(150, 1800, 50):
            p.bridge_step(tick, 0)
            self.assert_buck_minimum()
        self.assertEqual(p.bridge_value(0), 0)
        p.bridge_input(0, 5200)
        p.bridge_step(1802, 0)
        self.assertEqual(p.bridge_value(0), 16)
        self.assertEqual(p.bridge_value(1), 1)
        self.assert_buck_minimum()

    def test_short_protection_still_qualifies_with_physical_outputs_inhibited(self):
        p = self.power
        p.bridge_reset(100)
        p.bridge_input(0, 5801)
        p.bridge_step(100, 1)
        p.bridge_step(5100, 1)
        self.assertEqual(p.bridge_fault(), b"SHORT\nCIRKT\nFAULT")
        self.assert_buck_minimum()

    def test_normal_and_tip_bypass_rf_starts_both_hold_gpio_low(self):
        for bypass in (0, 1):
            for _ in range(5):
                self.rf.rf_check(bypass)
                self.assertEqual(self.rf.rf_value(0), 0)
                self.assertEqual(self.rf.rf_value(1), 1)
                self.assertEqual(self.rf.rf_value(2), 0)
                self.assertEqual(self.rf.rf_value(3), 0)
                self.assertEqual(self.rf.rf_value(4), 0)


if __name__ == "__main__":
    unittest.main()
