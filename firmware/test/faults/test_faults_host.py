"""Offline fault-runner protocol, scenario, reporting and failure-path tests."""
import argparse
from dataclasses import asdict
import json
import math
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
from faults_cases import Case, catalog, Stimulus, modeled_history, trend_matches
from fault_target import FaultTarget, BootSettings, HEAD, REGISTERS, AbiNotReady
from faults_test import FaultRunner
from adc_sim import AdcSimProducer

TABLE = tuple(round(341 * (4 ** (i / 255) - 1)) for i in range(256))
CALIBRATION = {"mcu_cal": 1450, "vref_cal": 1500, "device_uid": "test"}


class MemorySession:
    def __init__(self):
        self.symbols = {name: SimpleNamespace(address=address) for name, address in (
            ("fault_test", 0x20000000), ("adc_sim", 0x20000200),
            ("adc_sim_decode_table", 0x08001000),
            ("vision_test_oled_sent_generation", 0x20001000),
            ("vision_test_oled_sent_framebuffer", 0x20001100),
            ("btn_swd_test_boot_hold", 0x20001300),
            ("vision_test_fiducial_enabled", 0x20001304),
            ("tip_swd_test_command", 0x20001308))}
        self.memory = {}
        self.writes = []
        self.running = False
        self.halt_spans = []
        self.put(0x20000000, struct.pack("<4I", 2, 0, 0, 1))
        self.put(0x20000200, struct.pack("<4I", 1, 128, 12, 7))
        self.put(0x08001000, struct.pack("<256H", *TABLE))
        self.put(0x1FFFF7B8, struct.pack("<2H", 1450, 1500))
        self.after_inspect = None

    def put(self, address, data):
        self.memory.update((address + i, b) for i, b in enumerate(data))

    def read_memory(self, address, length):
        return bytes(self.memory.get(address + i, 0) for i in range(length))

    def read_u32(self, address):
        return int.from_bytes(self.read_memory(address, 4), "little")

    def read_c_string(self, address, maximum=80):
        return self.read_memory(address, maximum).split(b"\0", 1)[0].decode("ascii")

    def write_memory(self, address, data):
        if self.running:
            raise AssertionError("SWD mutation outside a halt")
        self.writes.append((address, bytes(data)))
        self.put(address, data)

    def write_u32(self, address, value):
        self.write_memory(address, struct.pack("<I", value))

    def inspect(self, callback):
        was_running = self.running
        self.running = False
        try:
            return callback()
        finally:
            self.running = was_running
            if self.after_inspect:
                self.after_inspect()

    def resume(self):
        self.running = True

    def reset(self):
        self.resume()


class FaultHostTests(unittest.TestCase):
    def setUp(self):
        self.model = Stimulus(TABLE, CALIBRATION)
        self.session = MemorySession()
        metadata = SimpleNamespace(symbols=self.session.symbols,
                                   address=lambda name: self.session.symbols[name].address)
        self.target = FaultTarget(self.session, metadata)

    def test_catalog_unique_complete_families_and_no_target_placeholders(self):
        cases = catalog()
        self.assertEqual(len(cases), len({c.id for c in cases}))
        for family in [f"F{i:02d}" for i in range(1, 12)] + ["W01", "W02", "L01", "L02", "L03", "R01", "R02", "R03", "R04"]:
            self.assertTrue(any(c.id.startswith(family + ".") for c in cases), family)
        for case in cases:
            if case.level == "target":
                self.assertNotIn(case.kind, ("native", "electrical"))
        self.assertTrue(all(c.level != "target" for c in cases if c.kind == "electrical"))

    def test_boot_settings_reject_out_of_range_or_disabled_battery_count(self):
        for settings in (BootSettings(mode=7), BootSettings(cells=3), BootSettings(power=3),
                         BootSettings(calibration=11), BootSettings(auto_sleep=4)):
            with self.assertRaises(ValueError):
                settings.pack()
        self.assertEqual(BootSettings(mode=1, cells=5).pack() & 0x7FF, 0x105)

    def test_baseline_isolated_and_representable(self):
        predicted = self.model.predict(self.model.base)
        self.assertTrue(23000 < predicted["dc_mv"] < 25000)
        self.assertTrue(18000 < predicted["buck_mv"] < 22000)
        self.assertTrue(3000 < predicted["reference_mv"] < 3600)
        self.assertEqual(predicted["current_ma"], 0)
        self.assertTrue(all(0 < t < 60 for t in predicted["temperatures"]))
        self.assertTrue(all(value in TABLE for value in self.model.base))

    def test_reference_scenarios_do_not_require_raw_buck_spike(self):
        for reference in (2300, 4400):
            frame = self.model.frame(reference=reference)
            self.assertLess(frame.channels[1], 705)
            final = self.model.predict(frame.channels)
            self.assertTrue(final["reference_mv"] < 2475 or final["reference_mv"] > 4125)
            self.assertGreater(final["dc_mv"], 20000)
            for intermediate in range(min(3300, reference), max(3300, reference) + 1, 100):
                self.assertLessEqual(self.model.mcu(frame.channels[5], intermediate), 110)

    def test_ramp_never_qualifies_declining_window(self):
        for destination in (self.model.voltage_count(14000, side="below"),
                            self.model.voltage_count(32000, side="above")):
            frames = self.model.ramp(self.model.base[0], destination)
            self.assertLessEqual(len(frames), 128)
            self.assertEqual(frames[-1].channels[0], destination)
            for phase in (0, 1, 63, 127):
                self.assertFalse(any(trend_matches(h) for h in modeled_history(frames, phase, self.model.base[0])))

    def test_trend_is_distinct_from_single_step(self):
        frames = [self.model.frame(dc=698, repeats=256)] + [
            self.model.frame(dc=value, repeats=128) for value in (698, 693, 687, 682, 676, 670, 665)]
        for phase in (0, 1, 63, 127):
            self.assertTrue(any(trend_matches(h) for h in modeled_history(frames, phase, 698)))
            step = [self.model.frame(dc=650, repeats=1280)]
            self.assertFalse(any(trend_matches(h) for h in modeled_history(step, phase, 676)))

    def test_power_and_current_stimuli_avoid_competing_temperature_fault(self):
        frame = self.model.frame(buck=self.model.voltage_count(1500),
                                 current=self.model.current_count(5800, above=True))
        predicted = self.model.predict(frame.channels)
        self.assertGreater(predicted["current_ma"], 5800)
        self.assertLess(predicted["power_mw"], 15000)
        self.assertTrue(all(t < 60 for t in predicted["temperatures"]))

    def test_boot_commits_options_and_payload_before_start(self):
        self.target.boot(self.model.frame(), settings=BootSettings(mode=1, cells=5),
                         injection="adc_init", reset=False)
        addresses = [address for address, _ in self.session.writes]
        self.assertLess(addresses.index(0x20000034), addresses.index(0x20000210))
        self.assertLess(addresses.index(0x20000004), addresses.index(0x20000218))
        self.assertEqual(addresses[-1], 0x20000218)
        self.assertEqual(self.session.read_u32(0x20000004), COMMAND_ADC_INIT)

    def test_command_rejects_pending_selector(self):
        self.session.put(0x20000004, struct.pack("<I", 20))
        with self.assertRaisesRegex(RuntimeError, "Unconsumed"):
            self.target.command("clock")
        self.assertEqual(self.session.writes, [])

    def test_status_decodes_layout_and_message(self):
        self.session.put(0x20000000, struct.pack("<14I", 2, 0, 0, 6, 0, 0x08002000, *range(6, 14)))
        self.session.put(0x20000038, struct.pack("<6H7H7HI4H8B3I",
            *range(6), *range(7), *range(7), 123456, 24000, 5801, 111, 3300,
            5, 1, 2, 4, 32, 5, 0, 0, 12, 13, 0x10002))
        self.session.put(0x08002000, b"SHORT\nCIRKT\nFAULT\0")
        state = self.target.status(registers=True)
        self.assertEqual(state["message"], "SHORT\nCIRKT\nFAULT")
        self.assertEqual(state["dc_mv"], 24000)
        self.assertEqual(state["current_ma"], 5801)
        self.assertEqual(state["pwm"], 32)
        self.assertEqual(state["event_raw"], 0x10002)
        self.assertEqual(len(state["registers"]), len(REGISTERS))

    def test_busy_snapshot_resumes_before_retry(self):
        self.session.put(0x20000071, b"\x80")
        self.session.after_inspect = lambda: self.session.put(0x20000071, b"\0")
        with patch("fault_target.time.sleep"):
            self.assertEqual(self.target.status()["flags"], 0)

    def test_unknown_abi_is_rejected(self):
        for version in (1, 3):
            self.session.put(0x20000000, struct.pack("<I", version))
            with self.assertRaisesRegex(RuntimeError, "incompatible"):
                self.target.status()

    def test_physical_inhibit_check_is_independent_of_logical_pwm(self):
        state = {"flags": 64, "pwm": 0,
                 "registers": {**dict.fromkeys(REGISTERS, 0), "gpiob_mode": 4,
                               "gpioa_mode": 4096, "gpioa_odr": 64}}
        FaultRunner.assert_outputs_inhibited(state)
        for register, value in (("gpiob_odr", 2), ("gpiob_mode", 8), ("tim1_cr1", 1),
                                ("tim1_bdtr", 32768), ("tim1_ccer", 1024),
                                ("gpioa_odr", 0), ("gpioa_mode", 8192)):
            bad = {**state, "registers": {**state["registers"], register: value}}
            with self.assertRaises(AssertionError):
                FaultRunner.assert_outputs_inhibited(bad)
        for flags in (0, 66):
            with self.assertRaises(AssertionError):
                FaultRunner.assert_outputs_inhibited({**state, "flags": flags})

    def test_reset_uninitialized_abi_is_distinguishable(self):
        self.session.put(0x20000000, bytes(4))
        with self.assertRaises(AbiNotReady):
            self.target.status()

    def test_boot_gate_tolerates_stale_ram_then_waits_for_both_interfaces(self):
        # This is the old ADC-table data seen at the new ABI address while
        # Reset_Handler was still clearing BSS after flashing a new layout.
        self.session.put(self.target.address, struct.pack("<4I", 0x0153014F, 0, 0, 0))
        adc_address = self.session.symbols["adc_sim"].address
        self.session.put(adc_address, bytes(4))
        steps = [0]
        def startup():
            steps[0] += 1
            if steps[0] == 1:
                self.session.put(self.target.address, bytes(16))
            elif steps[0] == 2:
                self.session.put(self.target.address, struct.pack("<4I", 2, 0, 0, 1))
            elif steps[0] == 3:
                self.session.put(adc_address, struct.pack("<I", 1))
        self.session.after_inspect = startup
        with patch("fault_target.time.sleep"), patch.object(self.target, "status", side_effect=AssertionError("premature snapshot")):
            self.target.wait_boot_gate()
        self.assertEqual(steps[0], 4)

    def test_boot_gate_reports_persistent_mismatch_without_weakening_status(self):
        self.session.put(self.target.address, struct.pack("<I", 1))
        with self.assertRaisesRegex(RuntimeError, "incompatible"):
            self.target.status()
        with patch("fault_target.time.sleep"), patch("fault_target.time.monotonic", side_effect=[0, 0, 2]):
            with self.assertRaisesRegex(TimeoutError, r"observed \(1, 1, 1\)"):
                self.target.wait_boot_gate(timeout=1)

    def test_frame_rejects_odd_and_old_generations(self):
        address = self.session.symbols["vision_test_oled_sent_generation"].address
        self.session.put(address, struct.pack("<I", 1))
        self.session.after_inspect = lambda: self.session.put(address, struct.pack("<I", 4))
        with patch("fault_target.time.sleep"):
            generation, frame = self.target.frame(after_generation=2)
        self.assertEqual(generation, 4)
        self.assertEqual(len(frame), 512)

    def test_static_frame_accepts_existing_coherent_generation(self):
        address = self.session.symbols["vision_test_oled_sent_generation"].address
        pixels = bytes([0x55, 0xAA]) * 256
        self.session.put(address, struct.pack("<I", 68))
        self.session.put(self.session.symbols["vision_test_oled_sent_framebuffer"].address, pixels)
        self.assertEqual(self.target.frame(), (68, pixels))

    def test_static_frame_rejects_zero_and_in_progress_generations(self):
        address = self.session.symbols["vision_test_oled_sent_generation"].address
        for invalid in (0, 67):
            self.session.put(address, struct.pack("<I", invalid))
            self.session.after_inspect = lambda: self.session.put(address, struct.pack("<I", 68))
            with patch("fault_target.time.sleep"):
                self.assertEqual(self.target.frame()[0], 68)

    def test_reset_requires_new_gate_and_assigns_distinct_boot_context(self):
        self.target.boot_number = 3
        self.target.producer = SimpleNamespace(status=lambda: {"abi_version": 1, "state": 0, "queued": 0})
        self.session.put(0x20000010, struct.pack("<I", 1 << 29))
        state = self.target.expect_reset(watchdog=True)
        self.assertEqual(state["boot_number"], 4)
        self.session.put(0x20000010, bytes(4))
        with self.assertRaisesRegex(AssertionError, "IWDG"):
            self.target.expect_reset(watchdog=True)

    def test_camera_off_cli_imports_no_camera_or_opencv(self):
        script = """
import importlib.abc, sys
class Deny(importlib.abc.MetaPathFinder):
    def find_spec(self, fullname, path=None, target=None):
        if fullname.split('.')[0] in ('cv2', 'eyes_rx0', 'gphoto2', 'fiducial'):
            raise AssertionError('camera dependency: ' + fullname)
sys.meta_path.insert(0, Deny())
from tools.auto_testing.faults_test import main
assert main(['--list', '--camera', 'off']) == 0
"""
        result = subprocess.run([sys.executable, "-c", script], cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_waveform_counters_and_publication_share_one_halt(self):
        with tempfile.TemporaryDirectory() as folder:
            args = argparse.Namespace(native_only=False, output_dir=Path(folder) / "run", fixture="offline", camera="off")
            case = Case("F05.fast", "fast_loss")
            runner = FaultRunner(args, [case])
            self.target.producer = AdcSimProducer(self.session, self.session.symbols)
            runner.current, runner.session, runner.target, runner.model = case, self.session, self.target, self.model
            inspect = self.session.inspect
            outer_halts = []
            def counted(callback):
                if self.session.running:
                    outer_halts.append(True)
                return inspect(callback)
            self.session.inspect = counted
            self.session.running = True
            try:
                entry = runner.queue([self.model.frame(repeats=128)])
                self.assertEqual(len(outer_halts), 1)
                self.assertEqual(entry["before"]["producer_count"], 0)
                self.assertEqual(entry["before"]["queued"], 0)
                self.assertGreater(entry["adc_host_time"], 0)
                self.assertEqual(self.target.producer.status()["queued"], 1)
                self.assertEqual(entry["frames"][0]["repeat_rounds"], 128)
            finally:
                runner.report.close()

    def test_report_contains_real_frame_artifacts_and_separate_visual_status(self):
        with tempfile.TemporaryDirectory() as folder:
            args = argparse.Namespace(native_only=False, output_dir=Path(folder) / "run", fixture="offline memory model",
                                      camera="off", require_camera=False)
            case = Case("control.healthy", "healthy")
            runner = FaultRunner(args, [case])
            runner.current = case
            runner.target = SimpleNamespace(status=lambda **kwargs: {"stage": 5, "boot_number": 1, "generation": 0, "flags": 64,
                        "registers": {**dict.fromkeys(REGISTERS, 0), "gpiob_mode": 4, "gpioa_mode": 4096, "gpioa_odr": 64}},
                                            frame=lambda old: (2, bytes([0x55, 0xAA]) * 256))
            observation = runner.capture("baseline")
            runner.save_results()
            runner.report.close()
            self.assertEqual(observation["generation"], 2)
            self.assertEqual((runner.output / case.id / "01_baseline_framebuffer.bin").stat().st_size, 512)
            from PIL import Image
            with Image.open(runner.output / observation["native_image"]) as image:
                self.assertEqual(image.size, (32, 128))
            with Image.open(runner.output / observation["image"]) as image:
                self.assertEqual(image.size, (256, 1024))
            manifest = json.loads((runner.output / "results.json").read_text())
            self.assertFalse(manifest["complete_electrical_coverage"])
            self.assertIn("Visual approval is pending", (runner.output / "index.html").read_text())

    def test_setup_capture_accepts_static_frame_but_default_requires_new_send(self):
        with tempfile.TemporaryDirectory() as folder:
            args = argparse.Namespace(native_only=False, output_dir=Path(folder) / "run",
                                      fixture="offline", camera="off")
            case = Case("F11.save", "save")
            runner = FaultRunner(args, [case])
            runner.current = case
            requested = []
            def frame(after):
                requested.append(after)
                return (68 if after == 0 else 70, bytes(512))
            runner.target = SimpleNamespace(status=lambda **kwargs: {
                "stage": 4, "boot_number": 1, "generation": 68, "flags": 64,
                "registers": {**dict.fromkeys(REGISTERS, 0), "gpiob_mode": 4,
                              "gpioa_mode": 4096, "gpioa_odr": 64}}, frame=frame)
            try:
                observation = runner.capture("setup", require_new_frame=False)
                self.assertEqual(observation["generation"], 68)
                runner.capture("transition")
                self.assertEqual(requested, [0, 68])
            finally:
                runner.report.close()

    def test_missing_frame_records_error_instead_of_reusing_pixels(self):
        with tempfile.TemporaryDirectory() as folder:
            args = argparse.Namespace(native_only=False, output_dir=Path(folder) / "run", fixture="offline", camera="off")
            case = Case("control.healthy", "healthy")
            runner = FaultRunner(args, [case])
            runner.current = case
            def no_frame(old):
                raise TimeoutError("NO_FRAME")
            runner.target = SimpleNamespace(status=lambda **kwargs: {"stage": 2, "generation": 0, "flags": 64,
                        "registers": {**dict.fromkeys(REGISTERS, 0), "gpiob_mode": 4, "gpioa_mode": 4096, "gpioa_odr": 64}}, frame=no_frame)
            with self.assertRaises(TimeoutError):
                runner.capture("failed")
            runner.save_results()
            runner.report.close()
            self.assertEqual(runner.results[case.id]["observations"][0]["frame_status"], "NO_FRAME")
            self.assertFalse(list((runner.output / case.id).glob("*.png")))


COMMAND_ADC_INIT = 20

if __name__ == "__main__":
    unittest.main()
