"""RF phases and current-only buck control using actual production C."""
import ctypes
import unittest
from test_native import library, covers


class RFDutyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rf = library("rf", support="functional", flags=("-DNATIVE_OUTPUT_MODEL=1", "-DPWRLVL_POWER_LIMIT_ENABLED=0"))
        cls.legacy = library("rf", support="functional", flags=("-DNATIVE_OUTPUT_MODEL=1", "-DPWRLVL_POWER_LIMIT_ENABLED=1"))
        cls.power = library("power", flags=("-DPWRLVL_POWER_LIMIT_ENABLED=0", "-DPWRLVL_RAMP_UP_STEP=1"))
        cls.power.bridge_fault.restype = ctypes.c_char_p
        cls.integrated = library("rf_power", support="functional", flags=("-DNATIVE_OUTPUT_MODEL=1",))
        cls.integrated.bridge_fault.restype = ctypes.c_char_p

    def setUp(self):
        self.rf.rf_reset(1, 0)
        self.legacy.rf_reset(1, 0)
        self.power.normal_reset(100)

    @covers("outputs.production")
    def test_all_mode_boundaries_and_next_cycle(self):
        for mode, on_ms in ((0, 1000), (1, 750), (2, 500)):
            self.rf.rf_reset(1, 0)
            for now in (0, 499, 500, 749, 750, 999, 1000, 1499, 1500, 1749, 1750, 1999):
                self.assertEqual(self.rf.rf_step(now, mode), int(now % 1000 < on_ms), (mode, now))

    def test_repeated_calls_do_not_reconfigure_active_timer_or_stopped_pin(self):
        self.assertEqual(self.rf.rf_step(100, 2), 1)
        on_handoffs = self.rf.rf_handoffs()
        for now in (100, 101, 300, 499):
            self.assertEqual(self.rf.rf_step(now, 2), 1)
        self.assertEqual(self.rf.rf_handoffs(), on_handoffs)
        self.assertEqual(self.rf.rf_step(500, 2), 0)
        off_handoffs = self.rf.rf_handoffs()
        for now in (500, 800, 999):
            self.assertEqual(self.rf.rf_step(now, 2), 0)
        self.assertEqual(self.rf.rf_handoffs(), off_handoffs)
        self.assertEqual(self.rf.rf_enabled(), 0)

    def test_delayed_calls_join_current_phase_without_catchup(self):
        for now in (100, 2700, 5100, 5700, 15700, 22000):
            self.assertEqual(self.rf.rf_step(now, 2), int(now % 1000 < 500))

    def test_mode_changes_join_phase_immediately(self):
        for now, mode, expected in ((600, 0, 1), (600, 2, 0), (600, 1, 1),
                                    (800, 1, 0), (800, 0, 1), (800, 2, 0), (1100, 2, 1)):
            self.assertEqual(self.rf.rf_step(now, mode), expected)

    @covers("time.wrap")
    def test_counter_wrap_uses_unsigned_counter_modulo(self):
        for mode, on_ms in ((0, 1000), (1, 750), (2, 500)):
            for now in (0xFFFFFF00, 0xFFFFFFFE, 0xFFFFFFFF, 0, 1, 499, 500):
                self.assertEqual(self.rf.rf_step(ctypes.c_uint32(now), mode), int(now % 1000 < on_ms))

    def test_latched_faults_block_start_and_stop_running_rf(self):
        for flag in (4, 8, 16, 32):
            for running in (False, True):
                self.rf.rf_reset(1, 0)
                if running:
                    self.assertEqual(self.rf.rf_step(100, 0), 1)
                self.rf.rf_interlocks(flag)
                self.assertEqual(self.rf.rf_step(200, 0), 0)
                self.assertEqual(self.rf.rf_enabled(), 0)

    @covers("outputs.production")
    def test_unconfirmed_tip_edges_do_not_stop_or_restart_running_rf(self):
        # Raw input and qualification inhibit a START, not RF already running.
        # The timer ISR decides whether a transition is a confirmed removal.
        for lib in (self.rf, self.legacy):
            for mode in range(3):
                for flag in (1, 2, 3):  # Raw low, qualifying, or both.
                    lib.rf_reset(1, 0)
                    self.assertEqual(lib.rf_step(100, mode), 1)
                    handoffs = lib.rf_handoffs()
                    lib.rf_interlocks(flag)
                    self.assertEqual(lib.rf_step(150, mode), 1)
                    lib.rf_interlocks(0)
                    self.assertEqual(lib.rf_step(200, mode), 1)
                    self.assertEqual(lib.rf_handoffs(), handoffs)

    def test_every_start_inhibit_still_blocks_inactive_rf(self):
        for lib in (self.rf, self.legacy):
            for flag in (1, 2, 3, 4, 8, 16, 32):
                lib.rf_reset(1, 0)
                lib.rf_interlocks(flag)
                self.assertEqual(lib.rf_step(100, 0), 0)
                self.assertEqual(lib.rf_enabled(), 0)

    @covers("outputs.production")
    def test_cycle_off_stops_during_qualification_and_restart_waits(self):
        for mode, off_start in ((1, 750), (2, 500)):
            self.rf.rf_reset(1, 0)
            self.assertEqual(self.rf.rf_step(100, mode), 1)
            self.rf.rf_interlocks(3)
            self.assertEqual(self.rf.rf_step(off_start, mode), 0)
            self.assertEqual(self.rf.rf_enabled(), 0)
            self.rf.rf_interlocks(2)  # Raw input recovered; debounce still pending.
            self.assertEqual(self.rf.rf_step(1000, mode), 0)
            self.rf.rf_interlocks(0)
            self.assertEqual(self.rf.rf_step(1001, mode), 1)

    def test_rearm_never_starts_and_next_task_respects_phase(self):
        self.rf.rf_interlocks(32)
        self.assertEqual(self.rf.rf_step(100, 2), 0)
        self.assertEqual(self.rf.rf_rearm_case(8), 1)
        self.assertEqual(self.rf.rf_latches(), 0)
        self.assertEqual(self.rf.rf_step(750, 2), 0)
        self.assertEqual(self.rf.rf_step(1000, 2), 1)

    def test_legacy_regulation_uses_continuous_rf_and_same_interlocks(self):
        for mode in range(3):
            for now in (0, 500, 750, 999, 1000):
                self.assertEqual(self.legacy.rf_step(now, mode), 1)
        self.legacy.rf_interlocks(16)
        self.assertEqual(self.legacy.rf_step(1000, 0), 0)

    def test_direct_bringup_start_does_not_depend_on_cycle_phase(self):
        self.assertEqual(self.rf.rf_step(999, 2), 0)
        self.assertEqual(self.rf.rf_run(0), 1)

    @covers("power.request")
    def test_wattage_alone_does_not_attenuate_in_any_mode(self):
        for mode in range(3):
            self.power.normal_reset(100)
            self.power.normal_mode(mode)
            self.power.bridge_input(4, 150000)
            self.power.bridge_input(0, 5000)
            for now in (102, 104, 500, 1000, 5000):
                self.power.bridge_step(now, 1)
                self.assertEqual(self.power.bridge_value(0), 0)
                self.assertEqual(self.power.bridge_value(1), 0)

    @covers("power.request")
    def test_current_limit_keeps_jump_ramp_previous_sample_and_slow_release(self):
        p = self.power
        p.bridge_input(4, 150000)  # Wattage must not prevent release below current limit.
        p.bridge_input(0, 5199)
        p.bridge_step(100, 0)
        self.assertEqual(p.bridge_value(0), 0)
        p.bridge_input(0, 5200)
        for now, expected in ((101, 0), (102, 16), (103, 16), (104, 17)):
            p.bridge_step(now, 0)
            self.assertEqual(p.bridge_value(0), expected)
        p.bridge_input(0, 0)
        p.bridge_step(106, 0)
        self.assertEqual(p.bridge_value(0), 18)
        p.bridge_step(155, 0)
        self.assertEqual(p.bridge_value(0), 18)
        p.bridge_step(156, 0)
        self.assertEqual(p.bridge_value(0), 17)

    def test_current_only_control_still_has_five_second_overcurrent_fault(self):
        p = self.power
        p.bridge_input(0, 5801)
        p.bridge_step(100, 0)
        p.bridge_step(5099, 0)
        self.assertIsNone(p.bridge_fault())
        p.bridge_step(5100, 0)
        self.assertEqual(p.bridge_fault(), b"SHORT\nCIRKT\nFAULT")

    @covers("power.preference")
    def test_applied_derating_controls_rf_without_wattage_attenuation(self):
        p = self.integrated
        for selector, value, reason in ((1, 85, 1), (2, 18000, 2)):
            p.integrated_reset(100, 0)
            p.bridge_input(selector, value)
            p.bridge_input(4, 90000)
            self.assertEqual(p.integrated_step(600), 0)
            self.assertEqual(p.bridge_value(3), 2)
            self.assertTrue(p.bridge_value(2) & reason)
            self.assertEqual(p.bridge_value(0), 0)
            self.assertEqual(p.integrated_step(1100), 1)

    def test_input_recovery_retains_buck_release_and_joins_rf_phase(self):
        p = self.integrated
        for selector in (7, 2):  # Power loss and absolute undervoltage.
            p.integrated_reset(100, 2)
            p.bridge_input(selector, 1 if selector == 7 else 13000)
            self.assertEqual(p.integrated_step(600), 0)  # Recovery advances time by a full second.
            self.assertEqual(p.bridge_value(0), 31)
            self.assertEqual(p.integrated_step(2100), 1)

    def test_terminal_supervision_never_reaches_rf_task(self):
        p = self.integrated
        for selector, value in ((6, 1), (8, 0), (3, 2000)):
            p.integrated_reset(100, 0)
            p.bridge_input(selector, value)
            self.assertEqual(p.integrated_step(100), 0)
            self.assertIsNotNone(p.bridge_fault())


if __name__ == "__main__":
    unittest.main()
