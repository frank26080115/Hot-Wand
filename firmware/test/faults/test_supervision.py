"""Exact fault boundaries in actual pwrmgt.c/pwrlvl.c, with no board attached."""
import ctypes
import os
from pathlib import Path
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]


class SupervisionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which("clang") or not shutil.which("lld-link"):
            raise RuntimeError("Native supervision requires Windows LLVM clang/lld-link")
        build = ROOT / ".cache/faults-native"
        build.mkdir(parents=True, exist_ok=True)
        dll = build / f"power-{os.getpid()}.dll"
        subprocess.run(["clang", "-shared", "-nostdlib", "-fuse-ld=lld", "-O1",
                        "-ffunction-sections", "-fdata-sections", "-DPWRLVL_PWM_JUMP_LEVEL=16", "-DPWRLVL_RAMP_UP_STEP=1", "-Itest/faults/support",
                        "-DLEAKY_BUCKET_ENABLED=1", "-DPWRMGT_POWER_LOSS_SUSTAINED_ENABLED=1",
                        "-DPWRLVL_POWER_LIMIT_ENABLED=1",
                        "-Iinc", "-Ilib/u8g2/csrc", "test/faults/power_bridge.c",
                        "-o", str(dll), "-Wl,/noentry", "-Wl,/opt:ref"], cwd=ROOT, check=True)
        cls.lib = ctypes.CDLL(str(dll))
        cls.lib.bridge_fault.restype = ctypes.c_char_p
        cls.lib.bridge_value.restype = ctypes.c_uint32

    def setUp(self):
        self.lib.bridge_reset(100)

    def input(self, selector, value):
        self.lib.bridge_input(selector, value)

    def step(self, now, full=True):
        self.lib.bridge_step(ctypes.c_uint32(now), full)
        return self.lib.bridge_fault()

    def test_short_strict_threshold_and_exact_duration(self):
        self.input(0, 5800)
        self.assertIsNone(self.step(100))
        self.assertIsNone(self.step(10100))
        self.input(0, 5801)
        self.assertIsNone(self.step(10101))
        self.assertIsNone(self.step(15100))
        self.assertEqual(self.step(15101), b"SHORT\nCIRKT\nFAULT")
        self.assertEqual(self.lib.bridge_value(4), 0)

    def test_short_interrupt_cancels_entire_interval(self):
        self.input(0, 5801)
        self.step(100)
        self.step(5099)
        self.input(0, 5800)
        self.assertIsNone(self.step(5100))
        self.input(0, 5801)
        self.assertIsNone(self.step(5101))
        self.assertIsNone(self.step(10100))
        self.assertEqual(self.step(10101), b"SHORT\nCIRKT\nFAULT")

    def test_short_still_checked_at_forced_minimum(self):
        self.input(9, 1)
        self.input(0, 5801)
        self.step(100)
        self.assertEqual(self.lib.bridge_value(0), 32)
        self.assertEqual(self.step(5100), b"SHORT\nCIRKT\nFAULT")

    def test_short_tick_wrap(self):
        self.input(0, 5801)
        self.step(0xFFFFFE00)
        self.assertIsNone(self.step(0xFFFFFE00 + 4999))
        self.assertEqual(self.step(0xFFFFFE00 + 5000), b"SHORT\nCIRKT\nFAULT")

    def test_thermal_strict_threshold_and_exact_duration(self):
        self.input(1, 110)
        self.assertIsNone(self.step(100))
        self.assertIsNone(self.step(10100))
        self.input(1, 111)
        self.assertIsNone(self.step(10101))
        self.assertIsNone(self.step(11100))
        self.assertEqual(self.step(11101), b"CIRKT\nTOO\nHOT\nFAULT")
        self.assertEqual(self.lib.bridge_value(4), 1)

    def test_thermal_cancel_and_bucket_handoff_share_continuous_interval(self):
        self.input(1, 111)
        self.step(100)
        self.input(1, 110)
        self.assertIsNone(self.step(1099))
        self.input(10, 111)
        self.step(1100)
        self.input(10, 110)
        self.input(1, 111)
        self.assertIsNone(self.step(2099))
        self.assertEqual(self.step(2100), b"CIRKT\nTOO\nHOT\nFAULT")

    def test_thermal_tick_wrap(self):
        self.input(1, 111)
        self.step(0xFFFFFE00)
        self.assertIsNone(self.step(0xFFFFFE00 + 999))
        self.assertEqual(self.step(0xFFFFFE00 + 1000), b"CIRKT\nTOO\nHOT\nFAULT")

    def test_thermal_eco_hysteresis(self):
        for degrees, expected in ((80, 0), (81, 1), (76, 1), (75, 0)):
            self.input(1, degrees)
            self.assertIsNone(self.step(100))
            self.assertEqual(self.lib.bridge_value(2) & 1, expected)
            self.assertEqual(self.lib.bridge_value(3), 2 if expected else 0)

    def test_input_eco_hysteresis(self):
        for voltage, expected in ((19000, 0), (18999, 2), (19999, 2), (20000, 0)):
            self.input(2, voltage)
            self.assertIsNone(self.step(100))
            self.assertEqual(self.lib.bridge_value(2) & 2, expected)

    def test_reference_inclusive_boundaries(self):
        for voltage, expected in ((2475, None), (4125, None),
                                  (2474, b"VOLT\nREF\nFAULT"), (4126, b"VOLT\nREF\nFAULT")):
            self.setUp()
            self.input(3, voltage)
            self.assertEqual(self.step(100), expected)

    def test_power_loss_returns_through_supervision_and_gradual_release(self):
        self.input(7, 1)
        self.assertEqual(self.step(100), b"POWER\nLOSS\nFAULT")
        self.assertEqual(self.lib.bridge_value(9), 0)
        self.assertEqual(self.lib.bridge_value(0), 31)
        self.assertEqual(self.lib.bridge_value(10), 1)
        self.assertEqual(self.lib.bridge_value(11), 1)
        self.input(7, 1)
        self.step(1200)
        self.assertEqual(self.lib.bridge_value(10), 2)
        self.assertEqual(self.lib.bridge_value(0), 31)

    def test_still_qualified_trend_repeats_hold_before_any_output_release(self):
        self.input(11, 3)
        self.assertEqual(self.step(100), b"POWER\nLOSS\nFAULT")
        self.assertEqual(self.lib.bridge_value(10), 3)
        self.assertEqual(self.lib.bridge_value(11), 1)
        self.assertEqual(self.lib.bridge_value(0), 31)
        self.assertEqual(self.lib.bridge_value(9), 0)

    def test_recovered_loss_cannot_bypass_battery_or_reference_faults(self):
        for selector, value, expected in ((8, 0, b"BATTERY"), (3, 2000, b"VOLT\nREF\nFAULT")):
            self.setUp()
            self.input(selector, value)
            if selector == 1:
                self.step(100)  # Start the existing thermal qualification.
            self.lib.bridge_abort_terminal()
            self.input(7, 1)
            self.assertEqual(self.step(1100), expected)
            self.assertEqual(self.lib.bridge_value(9), 1)
            self.assertEqual(self.lib.bridge_value(11), 0)

    def test_recovered_loss_cannot_release_outputs_before_qualified_thermal_shutdown(self):
        self.input(1, 111)
        self.step(100)
        self.input(7, 1)
        self.lib.bridge_guarded_step(1100)
        self.assertEqual(self.lib.bridge_fault(), b"CIRKT\nTOO\nHOT\nFAULT")
        self.assertEqual(self.lib.bridge_value(9), 1)
        self.assertEqual(self.lib.bridge_value(11), 0)

    def test_undervoltage_recovery_is_supervised_gradual_and_repeatable(self):
        self.input(2, 13999)
        self.input(12, 14001)  # Above restart threshold, still below high-power threshold.
        self.assertEqual(self.step(100), b"LOW\nVOLT\nFAULT")
        self.assertEqual(self.lib.bridge_value(0), 31)
        self.assertEqual(self.lib.bridge_value(3), 2)  # Recovered operation uses the normal 50 W Eco cap.
        self.assertEqual(self.lib.bridge_value(9), 0)
        self.assertEqual(self.lib.bridge_value(11), 1)
        self.input(2, 13999)  # Another USB-source dropout.
        self.step(2200)
        self.assertEqual(self.lib.bridge_value(12), 2)
        self.assertEqual(self.lib.bridge_value(0), 31)
        self.assertEqual(self.lib.bridge_value(11), 2)

    def test_loss_recovery_rechecks_undervoltage_before_releasing_outputs(self):
        self.input(7, 1)
        self.input(2, 10000)
        self.assertEqual(self.step(100), b"POWER\nLOSS\nFAULT")
        self.assertEqual(self.lib.bridge_value(10), 2)  # Power-loss hold, then undervoltage hold.
        self.assertEqual(self.lib.bridge_value(12), 1)
        self.assertEqual(self.lib.bridge_value(11), 1)  # Single release after both checks pass.
        self.assertEqual(self.lib.bridge_value(0), 31)

    def test_absolute_input_boundary(self):
        self.input(2, 14000)
        self.assertIsNone(self.step(100))
        self.input(2, 13999)
        self.assertEqual(self.step(100), b"LOW\nVOLT\nFAULT")

    def test_same_pass_first_diagnosis_precedence(self):
        for flags, expected in (
            ({6: 1, 7: 1, 8: 0, 2: 10000, 3: 2000}, b"VOLT\nSPIKE\nFAULT"),
            ({7: 1, 8: 0, 2: 10000, 3: 2000}, b"POWER\nLOSS\nFAULT"),
            ({8: 0, 2: 10000, 3: 2000}, b"BATTERY"),
            ({2: 10000, 3: 2000}, b"LOW\nVOLT\nFAULT"),
        ):
            self.setUp()
            for selector, value in flags.items():
                self.input(selector, value)
            self.assertEqual(self.step(100), expected)

    def test_qualified_thermal_precedes_reference_otherwise_reference_wins(self):
        for delay, expected in ((999, b"VOLT\nREF\nFAULT"), (1000, b"CIRKT\nTOO\nHOT\nFAULT")):
            self.setUp()
            self.input(1, 111)
            self.step(100)
            self.input(3, 2000)
            self.assertEqual(self.step(100 + delay), expected)

    def test_current_inclusive_and_previous_sample_retained(self):
        self.input(0, 5199)
        self.step(100, False)
        self.assertEqual(self.lib.bridge_value(1), 0)
        self.input(0, 5200)
        self.step(102, False)
        self.assertEqual(self.lib.bridge_value(1), 1)
        self.assertEqual(self.lib.bridge_value(0), 16)
        self.input(0, 0)
        self.step(104, False)
        self.assertEqual(self.lib.bridge_value(1), 1)
        self.assertEqual(self.lib.bridge_value(0), 17)
        self.step(105, False)
        self.assertEqual(self.lib.bridge_value(1), 0)
        self.assertEqual(self.lib.bridge_value(0), 17)
        self.step(154, False)
        self.assertEqual(self.lib.bridge_value(0), 16)

    def test_three_power_ceilings_strict_and_two_ms_ramp(self):
        for mode, limit in enumerate((100000, 75000, 50000)):
            self.setUp()
            self.input(5, mode)
            self.input(4, limit)
            self.step(102, False)
            self.assertEqual(self.lib.bridge_value(0), 0)
            self.input(4, limit + 1)
            self.step(103, False)
            self.assertEqual(self.lib.bridge_value(0), 16)
            self.step(104, False)
            self.assertEqual(self.lib.bridge_value(0), 16)
            self.step(105, False)
            self.assertEqual(self.lib.bridge_value(0), 17)


if __name__ == "__main__":
    unittest.main()
