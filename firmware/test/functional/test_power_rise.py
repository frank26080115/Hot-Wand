"""Actual-C SPORT transient response and its graph/current/fault integration."""
import ctypes
import subprocess
import unittest
from test_native import library, covers, ROOT


class PowerRiseTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = library("power_rise", flags=("-DPWRLVL_POWER_RISE_ENABLED=1",))
        cls.off = library("power_rise", flags=("-DPWRLVL_POWER_RISE_ENABLED=0",))
        cls.tuned = library("power_rise", flags=("-DPWRLVL_POWER_RISE_ENABLED=1",
                            "-DPWRLVL_POWER_RISE_FULL_SCALE_MW=10000", "-DPWRLVL_POWER_RISE_DECAY_PERIOD_MS=10"))
        cls.legacy = library("power_rise", flags=("-DPWRLVL_POWER_RISE_ENABLED=1", "-DPWRLVL_POWER_LIMIT_ENABLED=1"))
        cls.lib.bridge_fault.restype = ctypes.c_char_p
        cls.inhibited = library("power_rise", flags=("-DPWRLVL_POWER_RISE_ENABLED=1",
                                "-DHOT_WAND_TEST_HARNESS=1", "-DNATIVE_OUTPUT_MODEL=1"))

    def setUp(self):
        self.lib.normal_reset(0)

    def observe(self, now, power, reference=20000, new=False, valid=True):
        self.lib.rise_observe(ctypes.c_uint32(now), ctypes.c_uint32(power), reference, new, valid)
        return self.lib.rise_value(2)

    def history(self, now, power):
        self.lib.bridge_input(4, power)
        self.lib.bridge_step(ctypes.c_uint32(now), 1)
        return self.lib.rise_value(2)

    @covers("power.request")
    def test_full_mapping_boundaries_and_saturation_without_a_minimum_jump(self):
        for delta in (0, 1, 624, 625, 626, 4999, 5000, 9999, 10000, 14999, 15000, 19999, 20000, 100000):
            with self.subTest(delta=delta):
                self.lib.normal_reset(0)
                self.assertEqual(self.observe(1, 20000 + delta, new=True), min(32, delta * 32 // 20000))
        self.lib.normal_reset(0)
        self.assertEqual(self.observe(1, 0xFFFFFFFF, new=True), 32)

    @covers("power.request")
    def test_repeated_and_falling_readings_cannot_cancel_free_running_decay(self):
        for now, power, expected in ((1, 30000, 16), (49, 30000, 16), (50, 30000, 15),
                                     (60, 35000, 24), (100, 35000, 23), (150, 25000, 22),
                                     (200, 20000, 21), (250, 0, 20)):
            self.assertEqual(self.observe(now, power), expected)
        self.assertEqual(self.lib.rise_value(3), 35000)

    def test_simultaneous_decay_and_new_peak_applies_the_peak_last(self):
        self.assertEqual(self.observe(1, 30000), 16)
        self.assertEqual(self.observe(50, 35000), 24)
        self.assertEqual(self.lib.rise_value(7), 50)
        self.assertEqual(self.observe(99, 35000), 24)
        self.assertEqual(self.observe(100, 35000), 23)

    def test_new_period_changes_reference_and_resets_its_running_peak(self):
        self.assertEqual(self.observe(1, 40000), 32)
        self.assertEqual(self.observe(50, 30000, 35000, True), 31)
        self.assertEqual(self.lib.rise_value(3), 30000)
        self.assertEqual(self.observe(100, 36000, 35000), 30)  # Mapped CCR1 cannot lower a stronger request.

    def test_delayed_calls_do_not_catch_up_and_decay_stops_at_zero(self):
        self.observe(1, 40000)
        self.assertEqual(self.observe(5000, 40000), 31)
        self.assertEqual(self.observe(5000, 40000), 31)
        for step in range(1, 33):
            self.observe(5000 + step * 50, 40000)
        self.assertEqual(self.lib.rise_value(2), 0)

    @covers("time.wrap")
    def test_decay_and_history_rollover_across_counter_wrap(self):
        start = 0xFFFFFFF0
        self.lib.normal_reset(ctypes.c_uint32(start))
        self.assertEqual(self.observe(start + 1, 40000), 32)
        self.assertEqual(self.observe(start + 49, 40000), 32)
        self.assertEqual(self.observe(start + 50, 40000), 31)
        self.lib.normal_reset(ctypes.c_uint32(start))
        self.history(start, 10000)
        self.assertEqual(self.history(start + 250, 20000), 16)

    def test_first_call_is_not_a_saved_history_segment_and_real_zero_is_valid(self):
        self.lib.normal_reset(1000)
        self.assertEqual(self.history(1000, 20000), 0)
        self.assertEqual(self.history(1001, 30000), 0)
        self.assertEqual(self.lib.rise_value(5), 0)
        self.assertEqual(self.history(1250, 40000), 16)
        self.assertEqual(self.lib.rise_value(6), 30000)
        self.lib.normal_reset(0)
        self.history(0, 0)
        self.assertEqual(self.history(250, 20000), 32)
        self.assertEqual(self.lib.rise_value(5), 1)
        self.assertEqual(self.lib.activity_value(0), 0)  # RF-OFF zeros are still valid power references.

    def test_first_task_long_after_reset_does_not_qualify_an_unwritten_slot(self):
        self.assertEqual(self.history(2000, 100000), 0)
        self.assertEqual(self.lib.rise_value(5), 0)
        self.assertEqual(self.history(2250, 110000), 16)

    def test_current_segment_participates_immediately_and_last_peak_is_fixed(self):
        self.history(0, 20000)
        self.assertEqual(self.history(250, 30000), 16)
        self.assertEqual(self.history(251, 35000), 24)
        self.assertEqual(self.lib.rise_value(6), 20000)
        self.assertEqual(self.lib.normal_peak(0), 35000)
        self.assertEqual(self.history(500, 55000), 32)
        self.assertEqual(self.lib.rise_value(6), 35000)

    def test_history_ring_recycles_without_expiring_the_wrong_reference(self):
        for index in range(40):
            power = 10000 + index * 2000
            self.history(index * 250, power)
            if index:
                self.assertEqual(self.lib.rise_value(6), power - 2000)
                self.assertEqual(self.lib.rise_value(2), 3)

    @covers("power.preference")
    def test_only_sport_attacks_and_exiting_clears_immediately(self):
        for mode in (1, 2):
            self.lib.normal_reset(0)
            self.observe(1, 40000)
            self.lib.rise_mode(mode)
            self.assertEqual(self.lib.rise_value(2), 0)
            self.assertEqual(self.observe(2, 100000, new=True), 0)
            self.lib.rise_mode(0)
            self.assertEqual(self.observe(3, 30000), 16)  # Fresh peak on entry, even within a segment.

    @covers("power.preference")
    def test_thermal_and_low_voltage_derating_clear_the_spike_contribution(self):
        for selector, threshold in ((1, 10), (2, 11)):
            self.lib.normal_reset(0)
            self.history(0, 10000)
            self.assertEqual(self.history(250, 30000), 32)
            self.lib.bridge_input(selector, self.lib.rise_value(threshold))
            self.assertEqual(self.history(251, 50000), 0)
            self.assertEqual(self.lib.bridge_value(3), 2)

    def test_current_regulator_is_independent_and_cannot_be_postponed_by_spikes(self):
        self.lib.bridge_input(0, 5200)
        self.assertEqual(self.observe(1, 21000), 1)
        self.observe(2, 22000)
        self.assertEqual(self.lib.rise_value(1), self.lib.rise_value(8))
        self.observe(3, 23000)
        self.observe(4, 24000)
        self.assertEqual(self.lib.rise_value(1), min(31, self.lib.rise_value(8) + self.lib.rise_value(9)))
        self.lib.rise_mode(1)
        self.assertEqual(self.lib.rise_value(2), self.lib.rise_value(1))

    def test_output_takes_maximum_not_sum_and_regulator_still_saturates_at_31(self):
        self.observe(1, 40000)
        self.lib.bridge_input(0, 5200)
        for tick in range(2, 110):
            self.observe(tick, 40000)
            self.assertEqual(self.lib.rise_value(2), max(self.lib.rise_value(0), self.lib.rise_value(1)))
            self.assertLessEqual(self.lib.rise_value(1), 31)

    @covers("outputs.production", "power.recovery")
    def test_forced_shutdown_and_recovery_discard_pending_and_active_spikes(self):
        self.observe(1, 30000)
        self.lib.rise_pending(40000, 20000)
        self.assertEqual(self.lib.rise_value(4), 32)
        self.lib.bridge_input(9, 1)
        self.assertEqual(self.observe(2, 100000), 32)
        self.assertEqual(self.lib.rise_value(0), 0)
        self.assertEqual(self.lib.rise_value(4), 0)
        self.lib.bridge_input(9, 0)
        self.assertEqual(self.lib.rise_value(2), 31)
        self.lib.bridge_step(52, 0)
        self.assertEqual(self.lib.rise_value(2), 30)
        self.assertEqual(self.lib.rise_value(0), 0)

    def test_returning_input_fault_preserves_gradual_release_not_stale_spike(self):
        self.history(0, 10000)
        self.history(250, 30000)
        self.lib.bridge_input(7, 1)
        self.history(251, 30000)
        self.assertEqual(self.lib.rise_value(2), 31)
        self.assertEqual(self.lib.rise_value(0), 0)
        self.assertEqual(self.lib.rise_value(4), 0)

    def test_sustained_overcurrent_still_trips_with_spike_controller_enabled(self):
        self.lib.bridge_input(0, 5801)
        self.observe(1, 40000)
        self.observe(5000, 40000)
        self.assertIsNone(self.lib.bridge_fault())
        self.observe(5001, 40000)
        self.assertEqual(self.lib.bridge_fault(), b"SHORT\nCIRKT\nFAULT")

    def test_switch_off_and_independent_gain_decay_tuning(self):
        self.off.normal_reset(0)
        self.off.bridge_input(4, 10000)
        self.off.bridge_step(0, 1)
        self.off.bridge_input(4, 150000)
        self.off.bridge_step(250, 1)
        self.assertEqual(self.off.rise_value(2), 0)
        self.tuned.normal_reset(0)
        self.tuned.rise_observe(1, 25000, 20000, 1, 1)
        self.assertEqual(self.tuned.rise_value(2), 16)
        self.tuned.rise_observe(10, 25000, 20000, 0, 1)
        self.assertEqual(self.tuned.rise_value(2), 15)

    @covers("outputs.production")
    def test_regulator_updates_the_timer_in_both_compile_variants(self):
        for lib in (self.lib, self.off):
            lib.normal_reset(0)
            lib.bridge_input(0, 5200)
            lib.bridge_step(2, 0)
            self.assertEqual(lib.rise_value(12), lib.rise_value(8))
            self.assertEqual(lib.rise_value(12), lib.rise_value(2))
            lib.bridge_step(4, 0)
            self.assertEqual(lib.rise_value(12), min(31, lib.rise_value(8) + lib.rise_value(9)))

    @covers("outputs.production")
    def test_shared_harness_keeps_physical_pwm_off_during_full_spike_request(self):
        p = self.inhibited
        p.bridge_reset(0)
        p.rise_observe(1, 40000, 20000, 1, 1)
        self.assertEqual(p.rise_value(2), 32)
        self.assertEqual(p.bridge_value(5), 1)  # GPIO output, not alternate function.
        self.assertEqual(p.bridge_value(6), 1)  # Attenuation held high.
        self.assertEqual(p.bridge_value(7), 0)  # No alternate-function handoff.
        self.assertEqual(p.bridge_value(8), 0)  # TIM3 never started.

    def test_optional_legacy_wattage_regulation_can_coexist_with_rise_response(self):
        p = self.legacy
        p.normal_reset(0)
        p.bridge_input(4, 150000)
        p.rise_observe(1, 40000, 20000, 1, 1)
        self.assertEqual(p.rise_value(2), 32)
        p.rise_observe(2, 40000, 20000, 0, 1)
        self.assertEqual(p.rise_value(1), 31)
        p.rise_mode(1)
        self.assertEqual(p.rise_value(0), 0)
        self.assertEqual(p.rise_value(2), 31)

    def test_bad_configuration_is_rejected(self):
        for setting, value in (("ENABLED", 2), ("FULL_SCALE_MW", 0), ("FULL_SCALE_MW", 134217728),
                               ("DECAY_PERIOD_MS", 0), ("DECAY_PERIOD_MS", 1001)):
            result = subprocess.run(["clang", "-E", "-x", "c", "-Iinc", f"-DPWRLVL_POWER_RISE_{setting}={value}", "-"],
                                    input='#include "conf.h"\n', cwd=ROOT, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0, setting)


if __name__ == "__main__":
    unittest.main()
