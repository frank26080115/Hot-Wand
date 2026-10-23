"""Waveform time/hold reconstruction and exported plot artifacts, without SWD."""
import argparse
import copy
import csv
import json
from pathlib import Path
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/auto_testing"))
from adc_waveform import build_timeline, save_adc_waveform
from faults_cases import Case
from faults_test import FaultRunner


BASE = [698, 561, 0, 869, 869, 362, 373]


def frame(dc, repeats):
    actual = list(BASE)
    actual[0] = dc
    return {"actual": actual, "requested": [value + 1 for value in actual], "repeat_rounds": repeats}


def example():
    return {"id": "F05.trend", "boot": {"boot_number": 1},
            "cadence": {"target_ms": 1000, "rounds": 12800},
            "stimulus": [{"before": {"rounds_completed": 100, "decoded": list(BASE),
                                      "queued": 0, "rounds_remaining": 0,
                                      "producer_count": 0, "consumer_count": 0},
                          "frames": [frame(693, 128), frame(687, 256)], "adc_host_time": 10}],
            "observations": [{"label": "held_fault", "state": {"boot_number": 1}, "adc_host_time": 11,
                              "adc_progress": {"rounds_completed": 12900}}]}


class AdcWaveformTests(unittest.TestCase):
    def test_time_uses_repeats_and_measured_cadence_and_extends_final_hold(self):
        timeline = build_timeline(example())
        self.assertEqual([p["round"] for p in timeline["points"]], [0, 1, 129, 385, 12800])
        self.assertAlmostEqual(timeline["points"][2]["time_seconds"], 129 / 12800)
        self.assertAlmostEqual(timeline["points"][-1]["time_seconds"], 1)
        self.assertEqual(timeline["points"][-1]["actual"][0], 687)
        self.assertEqual(timeline["batches"][0]["end_round"], 385)
        # Draw the LUT-decoded input, never the requested but unavailable count.
        self.assertEqual(timeline["points"][1]["actual"][0], 693)

    def test_refill_preserves_current_repeats_and_unread_fifo_slots(self):
        result = example()
        result["stimulus"][0]["frames"] = [frame(693, 8), frame(687, 4), frame(682, 6)]
        result["stimulus"].append({"before": {"rounds_completed": 105, "decoded": [693, *BASE[1:]],
                                               "queued": 2, "rounds_remaining": 4,
                                               "producer_count": 3, "consumer_count": 1},
                                   "frames": [frame(676, 2)]})
        timeline = build_timeline(result)
        self.assertEqual(timeline["batches"][0]["end_round"], 19)
        self.assertEqual(timeline["batches"][1]["start_round"], 19)
        self.assertEqual(timeline["batches"][1]["end_round"], 21)

    def test_counter_wrap_and_reset_observations(self):
        result = example()
        result["stimulus"][0]["before"]["rounds_completed"] = 0xFFFFFFFA
        result["observations"][0]["adc_progress"]["rounds_completed"] = 10
        result["observations"].append({"state": {"boot_number": 2},
                                        "adc_progress": {"rounds_completed": 1000000}})
        timeline = build_timeline(result)
        self.assertEqual(timeline["observed_end_round"], 16)
        self.assertEqual(timeline["end_round"], 385)

    def test_marked_trend_uses_firmware_activation_round(self):
        result = example()
        result["stimulus"][0]["mark"] = True
        result["observations"][0]["state"].update(event_raw=3 << 16, qualify_ms=103)
        timeline = build_timeline(result)
        self.assertEqual(timeline["batches"][0]["start_round"], 3)
        self.assertIn("FT_MARK", timeline["batches"][0]["activation"])

    def test_progress_fallback_excludes_overlapping_halts(self):
        result = example()
        result.pop("cadence")
        result["observations"][0]["adc_progress"]["rounds_completed"] = 6500
        spans = [{"host_start": 10.1, "seconds": 0.3}, {"host_start": 10.3, "seconds": 0.3}]
        timeline = build_timeline(result, spans)
        # The overlapping pauses cover 0.5 s total, leaving 6400 rounds/0.5 s.
        self.assertAlmostEqual(timeline["seconds_per_round"], 0.5 / 6400)
        self.assertIn("excluding debugger halts", timeline["time_basis"])

    def test_no_cadence_does_not_invent_a_conversion_rate(self):
        result = example()
        result.pop("cadence")
        result["observations"] = []
        with self.assertRaisesRegex(ValueError, "no cadence"):
            build_timeline(result)

    def test_large_repeats_do_not_allocate_per_round_samples(self):
        result = example()
        result["stimulus"][0]["frames"] = [frame(693, 0xFFFFFFFF)]
        timeline = build_timeline(result)
        self.assertEqual(len(timeline["points"]), 3)
        self.assertEqual(timeline["batches"][0]["end_round"], 0x100000000)

    def test_real_png_csv_json_and_report_links(self):
        from PIL import Image
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder) / "run"
            args = argparse.Namespace(native_only=False, output_dir=directory, fixture="offline", camera="off")
            case = Case("F05.trend", "trend")
            runner = FaultRunner(args, [case])
            try:
                result = runner.results[case.id]
                result.update(copy.deepcopy(example()), status="PASS")
                graph = save_adc_waveform(result, directory / case.id)
                result["adc_waveform"] = graph
                runner.save_results()
                with Image.open(directory / graph["image"]) as image:
                    self.assertGreaterEqual(image.width, 1200)
                    self.assertGreaterEqual(image.height, 1200)
                self.assertTrue((directory / graph["sequences"][0]["image"]).is_file())
                data = json.loads((directory / graph["data"]).read_text())
                self.assertEqual(len(data["channels"]), 7)
                with (directory / graph["csv"]).open(newline="") as stream:
                    rows = list(csv.DictReader(stream))
                self.assertEqual(int(rows[1]["dc"]), 693)
                self.assertAlmostEqual(float(rows[-1]["time_seconds_estimated"]), 1)
                index = (directory / "index.html").read_text()
                self.assertIn(graph["image"], index)
                self.assertIn(graph["csv"], index)
                self.assertIn(graph["sequences"][0]["image"], index)
            finally:
                runner.report.close()

    def test_plotting_runs_after_case_cleanup(self):
        with tempfile.TemporaryDirectory() as folder:
            args = argparse.Namespace(native_only=False, output_dir=Path(folder) / "run", fixture="offline", camera="off")
            case = Case("F05.trend", "trend")
            runner = FaultRunner(args, [case])
            events = []
            runner.original_nvm = bytes(1024)
            runner.session = SimpleNamespace(reset=lambda: events.append("reset"), halt_spans=[],
                                             read_memory=lambda *args: runner.original_nvm)
            runner.exercise = lambda c: events.append("stimulus")
            try:
                with patch("faults_test.save_adc_waveform", side_effect=lambda *args: events.append("plot")):
                    runner.run_case(case)
                self.assertEqual(events, ["stimulus", "reset", "plot"])
            finally:
                runner.report.close()

    def test_plot_error_is_reported_without_masking_an_existing_case_failure(self):
        for failing in (False, True):
            with tempfile.TemporaryDirectory() as folder:
                args = argparse.Namespace(native_only=False, output_dir=Path(folder) / "run", fixture="offline", camera="off")
                case = Case("F05.trend", "trend")
                runner = FaultRunner(args, [case])
                runner.original_nvm = bytes(1024)
                runner.session = SimpleNamespace(reset=lambda: None, halt_spans=[],
                                                 read_memory=lambda *args: runner.original_nvm)
                def exercise(c):
                    if failing:
                        raise AssertionError("original fault assertion")
                runner.exercise = exercise
                try:
                    with patch("faults_test.save_adc_waveform", side_effect=ValueError("no cadence")):
                        runner.run_case(case)
                    result = runner.results[case.id]
                    self.assertEqual(result["status"], "FAIL" if failing else "ERROR")
                    self.assertEqual(result["adc_waveform_error"], "no cadence")
                    if failing:
                        self.assertEqual(result["error"], "original fault assertion")
                finally:
                    runner.report.close()


if __name__ == "__main__":
    unittest.main()
