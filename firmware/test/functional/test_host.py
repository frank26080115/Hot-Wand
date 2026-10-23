"""Functional selection, safety, static-frame and reporting regression checks."""
import argparse
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/auto_testing"))
sys.path.insert(0, str(ROOT / "test/faults"))
from test_faults_host import MemorySession
from test_native import covers
from functional_cases import catalog, calibrate, guess_cells, MODE_SEQUENCES
from functional_test import FunctionalRunner, SafetyViolation, main
from fault_target import FaultTarget, REGISTERS
from functional_native import EvidenceResult


def arguments(folder):
    return argparse.Namespace(output_dir=Path(folder) / "run", camera="off", fixture="offline memory model",
                              require_camera=False, native_only=False, native=False, long=False)


def safe_state():
    return {"stage": 5, "boot_number": 1, "generation": 68, "flags": 64,
            "registers": {**dict.fromkeys(REGISTERS, 0), "gpiob_mode": 4,
                          "gpioa_mode": 4096 | 2 << 26 | 2 << 28, "gpioa_odr": 64}}


class FunctionalHostTests(unittest.TestCase):
    def timed_runner(self, ticks, *, read_seconds=8, stage=5):
        runner = FunctionalRunner.__new__(FunctionalRunner)
        clock = [0.0]
        samples = iter(ticks)
        last = [0]
        def status():
            clock[0] += read_seconds
            last[0] = next(samples, last[0])
            return {"tick": last[0], "stage": stage, "flags": 64, "message": ""}
        runner.status = status
        def sleep(seconds):
            clock[0] += seconds
        return runner, clock, sleep

    def test_runtime_wait_allows_debugger_delay_beyond_old_wall_timeout(self):
        runner, clock, sleep = self.timed_runner(range(0, 35000, 5000))
        with patch("functional_test.time.monotonic", side_effect=lambda: clock[0]), \
             patch("functional_test.time.sleep", side_effect=sleep):
            state = runner.wait(lambda s: s["tick"] >= 30000, seconds=40, target_time=True)
        self.assertEqual(state["tick"], 30000)
        self.assertGreater(clock[0], 40)

    def test_runtime_hold_tolerates_slow_debugger_reads(self):
        runner, clock, sleep = self.timed_runner(range(0, 35000, 5000), read_seconds=10)
        with patch("functional_test.time.monotonic", side_effect=lambda: clock[0]), \
             patch("functional_test.time.sleep", side_effect=sleep):
            self.assertEqual(runner.hold_runtime(30000)["tick"], 30000)
        self.assertGreater(clock[0], 57)  # Old 1.4x + 15s host cap.

    def test_runtime_timeout_counts_mcu_time_across_wrap(self):
        runner, clock, sleep = self.timed_runner((0xFFFFFF00, 0x2E8), read_seconds=1)
        with patch("functional_test.time.monotonic", side_effect=lambda: clock[0]), \
             patch("functional_test.time.sleep", side_effect=sleep):
            with self.assertRaisesRegex(TimeoutError, "1 MCU seconds"):
                runner.wait(lambda s: False, seconds=1, target_time=True)

    def test_runtime_wait_still_detects_stopped_tick_and_extreme_host_delay(self):
        for ticks, read_seconds, expected in (([123], 1, "MCU tick stopped"),
                                               (range(100), 10, "host safety deadline")):
            runner, clock, sleep = self.timed_runner(ticks, read_seconds=read_seconds)
            with patch("functional_test.time.monotonic", side_effect=lambda: clock[0]), \
                 patch("functional_test.time.sleep", side_effect=sleep):
                with self.assertRaisesRegex(TimeoutError, expected):
                    runner.wait(lambda s: False, seconds=1, target_time=True)

    def test_sleep_transition_is_accepted_before_frozen_tick_check(self):
        runner, clock, sleep = self.timed_runner([123], stage=6)
        with patch("functional_test.time.monotonic", side_effect=lambda: clock[0]), \
             patch("functional_test.time.sleep", side_effect=sleep):
            self.assertEqual(runner.wait(lambda s: s["stage"] == 6, target_time=True)["stage"], 6)

    def test_catalog_unique_and_long_cases_are_separate(self):
        cases = catalog()
        self.assertEqual(len(cases), len({c.id for c in cases}))
        self.assertTrue(next(c for c in cases if c.id == "FUN.sleep.auto.3").long)
        self.assertFalse(next(c for c in cases if c.id == "FUN.sleep.auto.1").long)
        self.assertEqual(MODE_SEQUENCES[0], (1,2,1,0,1))

    def test_selection_default_long_explicit_and_native_only(self):
        with patch.object(FunctionalRunner, "run", return_value=0) as run, patch.object(FunctionalRunner, "__init__", return_value=None) as init:
            main([])
            selected = init.call_args.args[1]
            self.assertTrue(all(c.level == "target" and not c.long for c in selected))
            main(["--long"])
            self.assertTrue(any(c.id == "FUN.sleep.auto.3" for c in init.call_args.args[1]))
            main(["--case", "FUN.sleep.auto.3"])
            self.assertEqual([c.id for c in init.call_args.args[1]], ["FUN.sleep.auto.3"])
            main(["--native-only"])
            self.assertTrue(all(c.level == "native" for c in init.call_args.args[1]))

    def test_camera_off_import_has_no_camera_dependency(self):
        code = "import sys; sys.path.insert(0, 'tools/auto_testing'); import functional_test; assert not any(x in sys.modules for x in ('cv2','gphoto2','eyes_rx0'))"
        self.assertEqual(subprocess.run([sys.executable, "-c", code], cwd=ROOT).returncode, 0)

    @covers("display.static")
    def test_static_frame_and_transition_current_boot_rules(self):
        session = MemorySession()
        metadata = SimpleNamespace(symbols=session.symbols, address=lambda name: session.symbols[name].address)
        target = FaultTarget(session, metadata)
        address = session.symbols["vision_test_oled_sent_generation"].address
        session.put(address, struct.pack("<I", 68))
        self.assertEqual(target.frame()[0], 68)
        session.after_inspect = lambda: session.put(address, struct.pack("<I", 70))
        with patch("fault_target.time.sleep"):
            self.assertEqual(target.frame(68)[0], 70)
        for invalid in (0, 71):
            session.put(address, struct.pack("<I", invalid))
            session.after_inspect = lambda: session.put(address, struct.pack("<I", 72))
            with patch("fault_target.time.sleep"):
                self.assertEqual(target.frame()[0], 72)

    def test_saved_settings_boot_can_leave_override_invalid(self):
        session = MemorySession()
        metadata = SimpleNamespace(symbols=session.symbols, address=lambda name: session.symbols[name].address)
        target = FaultTarget(session, metadata)
        sys.path.insert(0, str(ROOT / "tools/auto_testing"))
        from adc_sim import Frame
        with patch("fault_target.time.sleep"):
            target.boot(Frame((500,) * 7), settings=None)
        self.assertEqual(session.read_u32(target.address + 52), 0)

    def test_output_violation_is_fatal_even_with_other_safe_registers(self):
        with tempfile.TemporaryDirectory() as folder:
            runner = FunctionalRunner(arguments(folder), [])
            try:
                runner.assert_outputs_inhibited(safe_state())
                bad = safe_state()
                bad["registers"]["gpiob_odr"] = 2
                with self.assertRaises(SafetyViolation):
                    runner.assert_outputs_inhibited(bad)
                self.assertIsNotNone(runner.safety_error)
                with self.assertRaises(SafetyViolation):
                    runner.require(False, "NVM remains byte-for-byte unchanged")
            finally:
                runner.report.close()

    def test_functional_report_keeps_real_pngs_and_correct_title(self):
        with tempfile.TemporaryDirectory() as folder:
            case = next(c for c in catalog() if c.id == "FUN.boot.healthy.0")
            runner = FunctionalRunner(arguments(folder), [case])
            runner.current = case
            runner.target = SimpleNamespace(status=lambda **kwargs: safe_state(), frame=lambda after: (70, bytes(512)))
            try:
                observation = runner.capture("healthy")
                runner.results[case.id]["status"] = "PASS"
                runner.save_results()
                self.assertTrue((runner.output / observation["native_image"]).is_file())
                self.assertIn("Functional test evidence", (runner.output / "index.html").read_text(encoding="utf-8"))
                manifest = json.loads((runner.output / "results.json").read_text(encoding="utf-8"))
                self.assertEqual(manifest["selected"], [case.id])
                self.assertFalse(manifest["complete_electrical_coverage"])
            finally:
                runner.report.close()

    def test_calibration_and_healthy_pack_oracles(self):
        self.assertGreater(calibrate(24000, 1), 24000)
        self.assertLess(calibrate(24000, 6), 24000)
        self.assertEqual(guess_cells(24000, 1), 6)
        self.assertEqual(guess_cells(24000, 5), 7)
        with self.assertRaises(ValueError):
            guess_cells(10000, 5)

    def test_native_evidence_keeps_subtest_failures_and_skips(self):
        class Examples(unittest.TestCase):
            @covers("example")
            def test_failure(self):
                with self.subTest(boundary=1):
                    self.assertEqual(1, 2)

            @covers("example")
            @unittest.skip("fixture unavailable")
            def test_skip(self):
                pass

        result = unittest.TextTestRunner(stream=io.StringIO(), resultclass=EvidenceResult).run(
            unittest.defaultTestLoader.loadTestsFromTestCase(Examples))
        self.assertEqual([check["status"] for check in result.evidence["example"]], ["FAIL", "BLOCKED"])

    def test_waveforms_are_partitioned_by_boot_before_plotting(self):
        with tempfile.TemporaryDirectory() as folder:
            runner = FunctionalRunner(arguments(folder), [])
            runner.session = SimpleNamespace(halt_spans=[])
            result = {"id": "FUN.battery.reboot", "stimulus": [
                {"boot_number": 1, "cadence": {"rounds": 100}},
                {"boot_number": 2, "cadence": {"rounds": 200}}]}
            calls = []
            def export(segment, directory, halts):
                calls.append(segment)
                return {"image": "boot_1/adc_waveform.png", "csv": "boot_1/adc_waveform.csv",
                        "data": "boot_1/adc_waveform.json", "sequences": []}
            try:
                with patch("functional_test.save_adc_waveform", side_effect=export):
                    first = runner.save_waveforms(result)
                self.assertEqual([call["boot"]["boot_number"] for call in calls], [1,2])
                self.assertTrue(all(len(call["stimulus"]) == 1 for call in calls))
                self.assertEqual(first["image"], "FUN.battery.reboot/boot_1/adc_waveform.png")
                self.assertEqual(result["extra_adc_waveforms"][0]["image"], "FUN.battery.reboot/boot_2/adc_waveform.png")
            finally:
                runner.report.close()

    def test_case_safety_violation_resets_then_aborts_instead_of_continuing(self):
        with tempfile.TemporaryDirectory() as folder:
            case = next(c for c in catalog() if c.id == "FUN.boot.healthy.0")
            runner = FunctionalRunner(arguments(folder), [case])
            runner.original_nvm = bytes(1024)
            runner.session = SimpleNamespace(reset=lambda: None, read_memory=lambda *args: bytes(1024), halt_spans=[])
            runner.target = SimpleNamespace(status=lambda **kwargs: safe_state())
            bad = safe_state()
            bad["registers"]["gpioa_odr"] = 0
            try:
                with patch.object(runner, "exercise", side_effect=lambda case: runner.assert_outputs_inhibited(bad)):
                    with self.assertRaises(SafetyViolation):
                        runner.run_case(case)
                self.assertEqual(runner.results[case.id]["status"], "ERROR")
            finally:
                runner.report.close()


if __name__ == "__main__":
    unittest.main()
