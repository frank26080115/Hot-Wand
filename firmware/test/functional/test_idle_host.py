"""Replay the host's idle profiles with real C history/timers, without hardware."""
import ctypes
import sys
import unittest
from unittest.mock import Mock, patch
from test_native import ROOT, library, covers

sys.path.insert(0, str(ROOT / "tools/auto_testing"))
from functional_test import FunctionalRunner
from faults_cases import Stimulus

MASK = 0xFFFFFFFF


class IdleHostReplayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = library("host_idle", flags=("-DPWRLVL_POWER_RISE_ENABLED=1", "-DPWRLVL_POWER_LIMIT_ENABLED=0"))

    def reset(self, profile=1, start=0):
        self.now = start
        self.elapsed = 0
        self.power = 0
        self.lib.normal_reset(ctypes.c_uint32(start))
        self.lib.normal_threshold(profile)
        self.lib.bridge_input(13, 1)  # Continuous SPORT phase; normal eligibility guards still apply.
        self.lib.bridge_step(ctypes.c_uint32(self.now), 1)
        self.advance(3000)  # Match the report's boot baseline of roughly three seconds at 0W.

    def advance(self, milliseconds):
        stop = self.elapsed + milliseconds
        while self.elapsed < stop:
            step = min(25, stop - self.elapsed)
            self.elapsed += step
            self.now = (self.now + step) & MASK
            self.lib.bridge_step(ctypes.c_uint32(self.now), 1)

    def status(self):
        return {"tick": self.now, "inactive_ms": self.lib.normal_value(1), "stage": 5, "flags": 64}

    def set_power(self, power):
        self.power = power
        self.lib.bridge_input(4, power)
        self.advance(1)

    def replay(self, profile, rule, start=0):
        self.reset(profile, start)
        runner = object.__new__(FunctionalRunner)
        table = tuple(round(341 * (4 ** (i / 255) - 1)) for i in range(256))
        runner.model = Stimulus(table, {"mcu_cal": 1450, "vref_cal": 1500})
        runner.log = Mock()
        runner.dimmed = Mock(side_effect=AssertionError("Dimming must not be accessed"))
        runner.status = self.status
        baseline_started = self.elapsed
        matches = []
        original_wait = runner.wait

        def wait(predicate, **kwargs):
            result = original_wait(predicate, **kwargs)
            matches.append((kwargs.get("description"), self.elapsed, dict(result)))
            return result

        def settle(frame):
            predicted = runner.model.predict(frame.channels)
            self.lib.bridge_input(0, predicted["current_ma"])
            self.set_power(predicted["power_mw"])
            return self.status()

        runner.wait = wait
        runner.settle = settle
        runner.capture = Mock()
        with patch("functional_test.time.monotonic", side_effect=lambda: self.elapsed / 1000), \
             patch("functional_test.time.sleep", side_effect=lambda seconds: self.advance(round(seconds * 1000))):
            runner.exercise_idle_profile(profile, rule)
        baseline = next(s for d, t, s in matches if d.startswith("Low-power baseline"))
        baseline_time = next(t for d, t, s in matches if d.startswith("Low-power baseline"))
        self.assertGreaterEqual(baseline["inactive_ms"], 5000)
        self.assertGreaterEqual(baseline_time - baseline_started, 8000)
        self.assertLess(baseline_time - baseline_started, 12000)
        self.assertGreaterEqual(self.status()["inactive_ms"], 1000)
        runner.dimmed.assert_not_called()
        self.assertEqual([call.args[0] for call in runner.capture.call_args_list],
                         ["profile_activity", "profile_inactive"])

    @covers("idle.threshold")
    def test_all_profiles_and_both_host_rules_survive_the_baseline_history_transition(self):
        for profile in range(8):
            for rule in ("absolute", "delta"):
                with self.subTest(profile=profile, rule=rule):
                    self.replay(profile, rule)

    @covers("idle.threshold")
    def test_original_six_second_expectation_fails_while_firmware_behaves_correctly(self):
        for profile in (0, 7):
            self.reset(profile)
            self.set_power(5067)  # Exact settled 5W stimulus from the user's report.
            self.advance(6000)
            self.assertGreaterEqual(self.status()["inactive_ms"], 2000)
            self.assertLess(self.status()["inactive_ms"], 3000)
            self.advance(3000)
            self.assertGreaterEqual(self.status()["inactive_ms"], 5000)

    @covers("idle.threshold", "time.wrap")
    def test_host_plateau_waits_cross_tick_wrap(self):
        self.replay(7, "delta", 0xFFFFF000)


if __name__ == "__main__":
    unittest.main()
