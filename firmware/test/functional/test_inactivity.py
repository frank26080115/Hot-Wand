"""Combined activity profiles against real peak-history and controller code."""
import ctypes
import unittest
from test_native import library, covers

PROFILES = ((2, 20), (2, 30), (2, 40), (3, 20), (3, 30), (3, 40), (4, 25), (4, 40))


class ActivityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = library("activity", flags=("-DPWRLVL_POWER_LIMIT_ENABLED=0",))

    def setUp(self):
        self.reset()

    def reset(self, profile=1, start=0):
        self.start = start
        self.lib.normal_reset(ctypes.c_uint32(start))
        self.lib.normal_threshold(profile)

    def step(self, offset, power, rf=None):
        if rf is not None:
            self.lib.bridge_input(13, rf)
        self.lib.bridge_input(4, power)
        self.lib.bridge_step(ctypes.c_uint32(self.start + offset), 1)

    def warm(self, power=5000):
        for tick in (0, 250, 500, 750, 1000):
            self.step(tick, power, True)

    @covers("idle.threshold")
    def test_all_eight_profile_mappings_and_invalid_index(self):
        for index, (delta, absolute) in enumerate(PROFILES):
            self.lib.normal_threshold(index)
            self.assertEqual(self.lib.activity_value(4), delta * 1000)
            self.assertEqual(self.lib.activity_value(5), absolute * 1000)
        self.lib.normal_threshold(8)
        self.assertEqual(self.lib.activity_value(4), 4000)
        self.assertEqual(self.lib.activity_value(5), 40000)
        self.lib.normal_reset(0)
        self.assertEqual(self.lib.activity_value(4), 2000)
        self.assertEqual(self.lib.activity_value(5), 30000)

    @covers("idle.threshold")
    def test_delta_below_equal_and_above_for_every_profile(self):
        for profile, (delta, _) in enumerate(PROFILES):
            with self.subTest(profile=profile):
                self.reset(profile)
                self.warm()
                self.step(1250, 5000 + delta * 1000 - 1)
                self.assertEqual(self.lib.activity_value(2), 0)
                self.assertEqual(self.lib.normal_value(1), 1250)
                self.step(1251, 5000 + delta * 1000)
                self.assertEqual(self.lib.activity_value(2), 1)
                self.assertEqual(self.lib.activity_value(3), 0)
                self.assertEqual(self.lib.normal_value(1), 0)
                self.step(1252, 5001 + delta * 1000)
                self.assertEqual(self.lib.normal_value(1), 0)

    @covers("idle.threshold")
    def test_absolute_boundaries_still_apply_while_rf_off_for_every_profile(self):
        for profile, (_, absolute) in enumerate(PROFILES):
            with self.subTest(profile=profile):
                self.reset(profile)
                self.step(100, absolute * 1000 - 1, False)
                self.assertEqual(self.lib.normal_value(1), 100)
                self.assertEqual(self.lib.activity_value(0), 0)
                self.step(200, absolute * 1000)
                self.assertEqual(self.lib.normal_value(1), 0)
                self.step(300, absolute * 1000 + 1)
                self.assertEqual(self.lib.normal_value(1), 0)

    def test_no_eligible_history_or_one_entry_cannot_make_delta_activity(self):
        self.assertEqual(self.lib.activity_value(0), 0)
        self.assertEqual(self.lib.activity_value(2), 0)
        for tick, guard in ((0, 3), (250, 2), (500, 1), (750, 0)):
            self.step(tick, 10000, True)
            self.assertEqual(self.lib.activity_value(1), guard)
            self.assertEqual(self.lib.activity_value(2), 0)
        self.assertEqual(self.lib.activity_value(0), 1 << 3)
        self.assertEqual(self.lib.normal_value(1), 750)

    def test_real_zero_readings_are_eligible_and_not_confused_with_unfilled_slots(self):
        self.warm(0)
        self.assertEqual(self.lib.activity_value(0), (1 << 3) | (1 << 4))
        self.step(1250, 2000)
        self.assertEqual(self.lib.activity_value(2), 1)
        self.assertEqual(self.lib.normal_value(1), 0)

    def test_current_segment_peak_participates_without_waiting_for_rollover(self):
        self.warm()
        self.step(1001, 7000)
        self.assertEqual(self.lib.normal_peak(0), 7000)
        self.assertEqual(self.lib.activity_value(2), 1)
        self.assertEqual(self.lib.normal_value(1), 0)

    def test_off_and_mixed_slots_remain_visible_but_do_not_contribute_to_delta(self):
        self.warm()
        self.step(1250, 6000)
        self.assertTrue(self.lib.activity_value(0) & (1 << 5))
        self.step(1251, 11000, False)
        self.assertFalse(self.lib.activity_value(0) & (1 << 5))
        self.assertEqual(self.lib.normal_peak(0), 11000)
        self.assertEqual(self.lib.activity_value(2), 0)
        self.step(1500, 0)
        self.assertEqual(self.lib.activity_value(2), 0)
        self.step(1650, 14000, True)
        for tick, guard in ((1750, 2), (2000, 1), (2250, 0)):
            self.step(tick, 5000)
            self.assertEqual(self.lib.activity_value(1), guard)
            self.assertEqual(self.lib.activity_value(2), 0)
        self.assertEqual(self.lib.activity_value(0), (1 << 3) | (1 << 4) | (1 << 9))

    def test_repeated_on_transition_restarts_full_guard(self):
        self.step(0, 5000, True)
        self.step(250, 5000)
        self.step(300, 0, False)
        self.step(350, 5000, True)
        self.assertEqual(self.lib.activity_value(1), 3)
        for tick, guard in ((500, 2), (750, 1), (1000, 0)):
            self.step(tick, 5000)
            self.assertEqual(self.lib.activity_value(1), guard)
        self.assertEqual(self.lib.activity_value(0), 1 << 4)

    def test_absolute_and_button_activity_work_during_guard(self):
        self.step(0, 30000, True)
        self.assertEqual(self.lib.activity_value(0), 0)
        self.assertEqual(self.lib.activity_value(3), 0)
        self.step(100, 0)
        self.assertEqual(self.lib.normal_value(1), 100)
        self.lib.normal_press()
        self.assertEqual(self.lib.normal_value(1), 0)
        self.step(101, 0)
        self.assertEqual(self.lib.normal_value(1), 1)

    @covers("idle.threshold")
    def test_fifty_percent_cycle_can_have_no_eligible_history(self):
        for tick in range(0, 5501, 50):
            on = tick % 1000 < 500
            self.step(tick, 5000 if on else 0, on)
            self.assertEqual(self.lib.activity_value(0), 0)
            self.assertEqual(self.lib.activity_value(2), 0)
        self.assertEqual(self.lib.normal_value(1), 5500)
        self.lib.normal_press()
        self.assertEqual(self.lib.normal_value(1), 0)
        self.step(5501, 30000, False)
        self.assertEqual(self.lib.normal_value(1), 0)

    def test_extrema_expire_and_excluded_slots_become_eligible_after_recycle(self):
        for extremum in (3000, 7000):
            self.reset()
            self.warm()
            self.step(1250, extremum)
            self.assertEqual(self.lib.normal_value(1), 0)
            for tick in range(1500, 5001, 250):
                self.step(tick, 5000)
                self.assertEqual(self.lib.normal_value(1), 0)
            self.step(5250, 5000)
            self.assertEqual(self.lib.activity_value(0), 65535)
            self.assertEqual(self.lib.activity_value(2), 0)
            self.assertEqual(self.lib.normal_value(1), 250)
            self.assertEqual(self.lib.normal_value(2), 0)
            self.step(5499, 5000)
            self.assertEqual(self.lib.normal_value(2), 249)

    @covers("time.wrap")
    def test_clock_wrap_keeps_guard_history_and_inactivity_arithmetic(self):
        self.reset(start=0xFFFFFF00)
        self.warm()
        self.step(1250, 7000)
        self.assertEqual(self.lib.activity_value(2), 1)
        self.assertEqual(self.lib.normal_value(1), 0)
        self.reset(start=0xFFFFFFF0)
        self.step(5, 30000, False)
        self.step(32, 0)
        self.assertEqual(self.lib.normal_value(1), 27)


class WattageHistoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = library("activity")  # Exercise the production default.

    def setUp(self):
        self.lib.normal_reset(0)

    def step(self, now, power, rf):
        self.lib.bridge_input(13, rf)
        self.lib.bridge_input(4, power)
        self.lib.bridge_step(ctypes.c_uint32(now), 1)

    @covers("idle.threshold")
    def test_empty_slots_are_excluded_and_recorded_zero_counts_even_with_rf_off(self):
        self.assertEqual(self.lib.activity_value(0), 0)
        self.step(0, 0, False)
        self.assertEqual(self.lib.activity_value(0), 1)
        self.assertEqual(self.lib.activity_value(2), 0)
        self.step(250, 2000, False)
        self.assertEqual(self.lib.activity_value(0), 3)
        self.assertEqual(self.lib.activity_value(2), 1)
        self.assertEqual(self.lib.normal_value(1), 0)

    def test_rf_edges_and_mixed_segments_never_exclude_recorded_history(self):
        for now, rf in ((0, False), (1, True), (2, False), (250, True), (251, False)):
            self.step(now, 5000 if now < 250 else 7000, rf)
            self.assertEqual(self.lib.activity_value(1), 0)
            self.assertEqual(self.lib.activity_value(0), 1 if now < 250 else 3)
        self.assertEqual(self.lib.activity_value(2), 1)

    def test_history_ring_eviction_removes_old_activity_without_rf_guards(self):
        self.step(0, 0, False)
        for index in range(1, 16):
            self.step(index * 250, 5000, bool(index % 2))
            self.assertEqual(self.lib.activity_value(2), 1)
        self.step(4000, 5000, False)
        self.assertEqual(self.lib.activity_value(0), 65535)
        self.assertEqual(self.lib.activity_value(2), 0)
        self.assertEqual(self.lib.normal_value(1), 250)

    @covers("time.wrap")
    def test_recorded_history_and_delta_survive_clock_wrap(self):
        self.lib.normal_reset(ctypes.c_uint32(0xFFFFFFF0))
        self.step(0xFFFFFFF0, 0, False)
        self.step(0xFFFFFFF0 + 250, 2000, True)
        self.assertEqual(self.lib.activity_value(0), 3)
        self.assertEqual(self.lib.activity_value(2), 1)


class ActivityRecoveryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = library("rf_power", support="functional",
                          flags=("-DNATIVE_OUTPUT_MODEL=1", "-DNATIVE_ACTIVITY_INTEGRATION=1", "-DPWRLVL_POWER_LIMIT_ENABLED=0"))

    @covers("idle.threshold", "power.recovery")
    def test_returning_input_fault_restarts_the_on_guard_after_rf_restarts(self):
        for fault_input in (2, 7):
            with self.subTest(fault_input=fault_input):
                p = self.lib
                p.integrated_reset(0, 0)
                p.bridge_input(4, 5000)
                for now in (0, 250, 500, 750, 1000, 1250):
                    self.assertEqual(p.integrated_step(now), 1)
                self.assertEqual(p.integrated_activity_guard(), 0)
                p.bridge_input(fault_input, 13000 if fault_input == 2 else 1)
                # The returning screen advances the clock 1 s with RF stopped.
                self.assertEqual(p.integrated_step(1300), 1)
                self.assertEqual(p.bridge_value(10), 1)
                self.assertEqual(p.integrated_step(2350), 1)
                self.assertEqual(p.integrated_activity_guard(), 3)
                for now, guard in ((2550, 2), (2800, 1), (3050, 0)):
                    self.assertEqual(p.integrated_step(now), 1)
                    self.assertEqual(p.integrated_activity_guard(), guard)


class ActivityStorageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = library("storage", flags=("-DNATIVE_ACTIVITY_STORAGE_TEST=1",), memory=False)

    @covers("settings.defaults", "settings.journal")
    def test_default_and_all_eight_profiles_roundtrip_without_layout_change(self):
        self.lib.storage_reset()
        self.assertEqual(self.lib.default_value(4), 1)
        for index in range(8):
            self.assertEqual(self.lib.storage_idle_save(index), 1)
            self.lib.storage_reboot()
            self.assertEqual(self.lib.storage_idle_read(), index)
            self.assertEqual(self.lib.storage_read(), 0)


if __name__ == "__main__":
    unittest.main()
