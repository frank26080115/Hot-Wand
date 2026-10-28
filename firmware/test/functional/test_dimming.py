"""Both compile-time dimming variants, NVM incompatibility, and host projection."""
import argparse
from contextlib import redirect_stdout
import io
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock

from test_native import library, covers, ROOT
sys.path.insert(0, str(ROOT / "tools/auto_testing"))
from build_metadata import latest_nvm_record, settings_values, menu_settings_indices, MenuItem
from functional_test import FunctionalRunner
from functional_cases import catalog


def record(magic, first=0, second=1, third=0x50):
    data = bytes((magic, first, second, third))
    a = b = 0
    for value in data:
        a = (a + value) % 255
        b = (b + a) % 255
    return data + (b << 8 | a).to_bytes(2, "little")


def page(*records):
    data = b"".join(records)
    return data + b"\xff" * (1024 - len(data))


class DimmingVariants(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.storage = [library("storage", flags=(f"-DOLED_DIMMING_ENABLED={flag}",
                       "-DNATIVE_DIMMING_MATRIX=1"), memory=False) for flag in (0, 1)]
        cls.main = library("main", flags=("-DOLED_DIMMING_ENABLED=0", "-DNATIVE_DIMMING_MATRIX=1"))
        cls.recovery = library("recovery", flags=("-DOLED_DIMMING_ENABLED=0", "-DNATIVE_DIMMING_MATRIX=1"))

    @covers("settings.defaults", "settings.journal")
    def test_default_record_and_repacked_flags_in_both_variants(self):
        for enabled, lib in enumerate(self.storage):
            lib.storage_reset()
            self.assertEqual(lib.storage_save(0), 1)
            self.assertEqual(lib.storage_byte(0), 0xA8 if enabled else 0xA7)
            self.assertEqual(lib.storage_byte(1), 0x40 if enabled else 0x10)
            self.assertEqual(lib.storage_flags_save(), 1)
            self.assertEqual(lib.storage_byte(7), 0xFC if enabled else 0x3C)
            lib.storage_reboot()
            self.assertEqual(lib.storage_read(), 0)

    @covers("settings.journal")
    def test_legacy_and_opposite_layout_reset_and_force_new_journal(self):
        for lib in self.storage:
            magic = lib.storage_layout()
            for foreign in (0xA6, 0xA7 if magic == 0xA8 else 0xA8):
                lib.storage_reset()
                lib.storage_record(0, foreign, 2, 7)
                lib.storage_reboot()
                self.assertEqual(lib.storage_read(), 255)
                self.assertEqual(lib.storage_pending_erase(), 1)
                self.assertEqual(lib.storage_save(1), 1)
                self.assertEqual(lib.storage_byte(0), magic)
                self.assertEqual(lib.storage_byte(6), 255)
                lib.storage_reboot()
                self.assertEqual(lib.storage_read(), 1)

    @covers("settings.journal")
    def test_mixed_layouts_never_resurrect_an_older_matching_record(self):
        for lib in self.storage:
            lib.storage_reset()
            magic = lib.storage_layout()
            lib.storage_record(0, magic, 2, 1)
            lib.storage_record(1, 0xA6, 0, 1)
            lib.storage_record(2, magic, 1, 1)
            lib.storage_reboot()
            self.assertEqual(lib.storage_read(), 255)
            self.assertEqual(lib.storage_save(0), 1)
            self.assertEqual(lib.storage_byte(6), 255)
            lib.storage_reboot()
            self.assertEqual(lib.storage_read(), 0)

    @covers("settings.journal")
    def test_foreign_format_after_an_erased_gap_also_invalidates_the_journal(self):
        for lib in self.storage:
            lib.storage_reset()
            lib.storage_record(0, lib.storage_layout(), 2, 1)
            lib.storage_record(2, 0xA6, 0, 1)
            lib.storage_reboot()
            self.assertEqual(lib.storage_read(), 255)
            self.assertEqual(lib.storage_pending_erase(), 1)

    @covers("settings.journal")
    def test_reserved_bits_are_canonical_and_corrupt_records_are_rejected(self):
        for lib in self.storage:
            lib.storage_reset()
            self.assertEqual(lib.storage_reserved_save(), 1)
            self.assertEqual(lib.storage_byte(2) & 0xC0, 0)
            if lib.storage_layout() == 0xA7:
                self.assertEqual(lib.storage_byte(1) & 0xC0, 0)
            lib.storage_corrupt(4, lib.storage_byte(4) ^ 1)
            lib.storage_reboot()
            self.assertEqual(lib.storage_read(), 255)
            lib.storage_reset()
            lib.storage_record(0, lib.storage_layout(), 0, 0xC1)
            lib.storage_reboot()
            self.assertEqual(lib.storage_read(), 255)
        lib = self.storage[0]
        lib.storage_reset()
        lib.storage_record(0, 0xA7, 0xC0, 1)
        lib.storage_reboot()
        self.assertEqual(lib.storage_read(), 255)

    @covers("sleep.auto", "display.debug")
    def test_disabled_main_never_dims_but_sleep_and_debug_inhibition_remain(self):
        never = 0xFFFFFFFF
        for timeout in range(4):
            self.assertEqual(self.main.main_run(61000, timeout, 0, 0, never, never, 0), 0)
            self.assertEqual(self.main.main_loop_value(0), 0)
            self.assertEqual(self.main.main_loop_value(1), 0)
        self.assertEqual(self.main.main_run(300001, 3, 1, 0, never, never, 0), 1)
        self.assertEqual(self.main.main_debug_idle_run(300001, 1, 3), 0)

    @covers("power.recovery")
    def test_disabled_recovery_screen_still_services_watchdog_and_returns(self):
        self.assertEqual(self.recovery.recovery_run(0, 14001, 0, 0xFFFFFFFF, 0, 2000, 0xFFFFFFFF, 0, 0), 1)
        self.assertEqual(self.recovery.recovery_value(1), 1)
        self.assertGreater(self.recovery.recovery_value(2), 0)
        self.assertEqual(self.recovery.recovery_value(11), 0)

    def test_invalid_feature_flag_fails_compilation(self):
        result = subprocess.run(["clang", "-E", "-x", "c", "-Iinc", "-DOLED_DIMMING_ENABLED=2", "-"],
                                input='#include "conf.h"\n', cwd=ROOT, capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("OLED_DIMMING_ENABLED must be 0 or 1", result.stderr)


class DimmingHostTests(unittest.TestCase):
    def test_foreign_journals_and_named_menu_projection(self):
        for enabled in (False, True):
            magic = 0xA8 if enabled else 0xA7
            data = record(magic, 0xFE if enabled else 0x3E, 7 | 6 << 3, 0xFA)
            self.assertEqual(latest_nvm_record(page(data), dimming_enabled=enabled), data)
            for foreign in (0xA6, 0xA7 if enabled else 0xA8):
                self.assertIsNone(latest_nvm_record(page(data, record(foreign)), dimming_enabled=enabled))
            values = settings_values(data, dimming_enabled=enabled)
            self.assertEqual(values["START\nPOWER\nLEVEL"], 2)
            self.assertEqual(values["AUTO\nSLEEP"], 3)
            self.assertEqual(values["SHOW\nSPLASH"], 1)
            self.assertEqual(values["FAN\nSIGNL\nPOLAR"], 1)
            self.assertEqual("AUTO\nDIM" in values, enabled)
            # Reorder pages to verify mapping uses titles, including optional ones.
            items = [MenuItem(title, tuple(str(i) for i in range(16))) for title in reversed(values)]
            items += [MenuItem("SAVE\nAND\nEXIT", ()), MenuItem("EXIT\nNO\nSAVE", ())]
            self.assertEqual(menu_settings_indices(page(data), items), list(reversed(values.values())))

    def test_activity_profile_cases_use_timers_without_any_dimming_access(self):
        from faults_cases import Stimulus
        table = tuple(round(341 * (4 ** (i / 255) - 1)) for i in range(256))
        for profile in range(8):
            for rule in ("absolute", "delta"):
                with self.subTest(profile=profile, rule=rule):
                    runner = object.__new__(FunctionalRunner)
                    runner.model = Stimulus(table, {"mcu_cal": 1450, "vref_cal": 1500})
                    runner.log = Mock()
                    runner.dimmed = Mock(side_effect=AssertionError("Dimming is unavailable"))
                    runner.settle = runner.hold_runtime = runner.capture = Mock()
                    runner.status = Mock(side_effect=({"inactive_ms": 6000}, {"inactive_ms": 0},
                                                      {"inactive_ms": 1500}))
                    runner.wait = Mock(side_effect=lambda predicate, **kw: self.assertTrue(predicate({"inactive_ms": 0})))
                    runner.exercise_idle_profile(profile, rule)
                    runner.dimmed.assert_not_called()

    def test_disabled_dim_case_is_skipped_and_does_not_fail_reporting(self):
        with tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()):
            args = argparse.Namespace(output_dir=Path(folder) / "run", camera="off", fixture="offline",
                                      native_only=True, native=False, long=False)
            case = next(c for c in catalog() if c.id == "FUN.dim.delay.1")
            runner = FunctionalRunner(args, [case])
            runner.native = Mock(return_value=True)
            runner.run_case(case)
            self.assertEqual(runner.results[case.id]["status"], "SKIPPED")
            self.assertEqual(runner.run(), 0)
            self.assertIsNone(runner.target)


if __name__ == "__main__":
    unittest.main()
