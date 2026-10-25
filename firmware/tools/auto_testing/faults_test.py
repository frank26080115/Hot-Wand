"""Fault runner for the shared stm32f042_test image.

Build first: pio run -e stm32f042_test
Run: py tools/auto_testing/faults_test.py --camera off
List coverage: py tools/auto_testing/faults_test.py --list
Offline companions: py tools/auto_testing/faults_test.py --native-only

Expected fixture: unloaded board, working OLED, ST-Link and NRST. Simulated
readings exercise the control logic while physical RF/buck outputs stay inhibited. Physical qualification is reported
separately; it cannot be established from a framebuffer or register read.
"""
from __future__ import annotations

import argparse
from dataclasses import asdict
from collections import Counter
from datetime import datetime
import fnmatch
import html
import json
from pathlib import Path
import subprocess
import sys
import time
import traceback

try:
    from .adc_waveform import save_adc_waveform, check_plotting_available
    from .adc_sim import AdcSimProducer, Frame
    from .build_metadata import BuildMetadata, TEST_ELF, NVM_START, read_menu_items
    from .fault_target import FaultTarget, BootSettings, COMMANDS, BOOT, SETUP, RUNTIME, FAULT, BATTERY
    from .faults_cases import catalog, Stimulus, BATTERY_MINIMUM, trend_matches, modeled_history
    from .frmbuf2png import save_framebuffer_png
    from .target_gdb import GdbSession
except ImportError:
    from adc_waveform import save_adc_waveform, check_plotting_available
    from adc_sim import AdcSimProducer, Frame
    from build_metadata import BuildMetadata, TEST_ELF, NVM_START, read_menu_items
    from fault_target import FaultTarget, BootSettings, COMMANDS, BOOT, SETUP, RUNTIME, FAULT, BATTERY
    from faults_cases import catalog, Stimulus, BATTERY_MINIMUM, trend_matches, modeled_history
    from frmbuf2png import save_framebuffer_png
    from target_gdb import GdbSession

ROOT = Path(__file__).resolve().parents[2]
OVERRIDE_MESSAGE = "LOW\nBATT\n!!!!!\nHOLD\nTO\nOVER-\nRIDE"


class FaultRunner:
    # Shared execution/evidence machinery is also used by the functional suite.
    # Subclasses supply contracts and exercises, retaining the same SWD/NVM
    # lifecycle, optional camera, coherent captures and ADC waveform artifacts.
    case_catalog = staticmethod(catalog)
    report_prefix = "faults"
    report_title = "Fault test evidence"
    scope = "faults, related warnings/limits, fatal recovery"
    show_unselected = False

    def __init__(self, args, cases):
        self.args, self.cases = args, cases
        self.output = (args.output_dir or Path.cwd() / f"{self.report_prefix}-{datetime.now():%Y-%m-%d_%H-%M-%S-%f}").resolve()
        self.output.mkdir(parents=True, exist_ok=False)
        self.report = (self.output / "report.txt").open("w", encoding="utf-8", buffering=1)
        self.results = {c.id: {**asdict(c), "status": "NOT_RUN", "observations": [], "visual_review": "PENDING" if c.level == "target" else "NOT_APPLICABLE"}
                        for c in self.case_catalog()}
        self.target = self.session = self.model = self.camera = self.current = None
        self.camera_available = False
        self.serial = self.last_generation = 0
        self.manifest = {"contract_version": 2, "physical_outputs": "RF off; buck PA6 GPIO-high/minimum", "pwm_semantics": "logical request; physical GPIO remains clamped", "native_only": args.native_only, "fixture": args.fixture, "camera": args.camera,
                         "selected": [c.id for c in cases], "electrical_qualification": False,
                         "scope": self.scope}
        self.original_nvm = None
        self.case_frames = []

    def run(self):
        exit_code = 0
        try:
            revision = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True, text=True)
            dirty = subprocess.run(["git", "status", "--porcelain", "--", "."], cwd=ROOT, capture_output=True, text=True)
            self.manifest.update(source_revision=revision.stdout.strip(), working_tree=dirty.stdout.splitlines())
            for case in self.case_catalog():
                if case.level in ("electrical", "deferred", "requirement"):
                    self.results[case.id].update(status="BLOCKED", error=case.note)
            if self.args.native or self.args.native_only or any(c.level == "native" for c in self.cases):
                if not self.native():
                    exit_code = 1
            if not self.args.native_only and any(c.level == "target" for c in self.cases):
                check_plotting_available()
                metadata = BuildMetadata(self.args.elf)
                metadata.address("fault_test")
                self.manifest.update(elf=str(metadata.elf), elf_sha256=metadata.sha256, load_ranges=metadata.load_ranges)
                with GdbSession(metadata.elf, metadata.symbols, port=self.args.gdb_port, log_dir=self.output) as session:
                    session.factory_defaults_on_exit = True
                    self.manifest["nvm_exit_policy"] = "Factory defaults; cleanup failure fails the run"
                    self.session = session
                    self.original_nvm = session.read_memory(NVM_START, 1024)
                    (self.output / "nvm_before.bin").write_bytes(self.original_nvm)
                    try:
                        if not self.args.no_flash:
                            session.flash()
                        else:
                            for name in (".text", ".rodata", ".isr_vector"):
                                reply = session.command(f'-interpreter-exec console "compare-sections {name}"', timeout=30)
                                if "mis-match" in reply.lower() or "does not match" in reply.lower():
                                    raise RuntimeError("Attached firmware does not match selected ELF")
                            session.reset()
                        self.require(session.read_memory(NVM_START, 1024) == self.original_nvm, "Flash preserved NVM")
                        self.target = FaultTarget(session, metadata)
                        self.target.wait_boot_gate()
                        self.target.producer = AdcSimProducer(session, metadata.symbols)
                        calibration = self.target.calibration()
                        self.model = Stimulus(self.target.producer.table, calibration)
                        self.manifest.update(calibration=calibration, adc_decode_table=self.target.producer.table,
                                             baseline=self.model.predict(self.model.base))
                        self.setup_camera()
                        for case in self.cases:
                            if case.level != "target":
                                if self.results[case.id]["status"] == "NOT_RUN":
                                    self.results[case.id].update(status="BLOCKED", error=case.note)
                                continue
                            self.run_case(case)
                        if any(self.results[c.id]["status"] != "PASS" for c in self.cases):
                            exit_code = 1
                    finally:
                        try:
                            session.reset()  # Watchdog-fed RF-off/minimum-output gate.
                            page = session.read_memory(NVM_START, 1024)
                            (self.output / "nvm_at_stop.bin").write_bytes(page)
                            self.manifest["nvm_preservation"] = "PASS" if page == self.original_nvm else "FAIL"
                            if page != self.original_nvm:
                                exit_code = 1
                        except Exception as exc:
                            self.manifest["cleanup_error"] = str(exc)
                            exit_code = 2
                        self.manifest["debugger_halts"] = session.halt_spans
            if any(self.results[c.id]["status"] != "PASS" for c in self.cases):
                exit_code = max(exit_code, 1)
        except KeyboardInterrupt:
            self.log("Run interrupted; partial evidence retained and cleanup attempted", "ERROR")
            self.manifest["error"] = "Interrupted by user"
            exit_code = 130
        except Exception as exc:
            self.log(f"Infrastructure error: {exc}", "ERROR")
            self.manifest.update(error=str(exc), traceback=traceback.format_exc())
            exit_code = 2
        finally:
            self.manifest["exit_code"] = exit_code
            self.save_results()
            counts = Counter(result["status"] for result in self.results.values())
            self.log("Coverage results: " + ", ".join(f"{status}={count}" for status, count in sorted(counts.items())))
            self.log(f"Artifacts: {self.output}; exit status {exit_code}")
            self.report.close()
        return exit_code

    def run_case(self, case):
        self.current, self.serial, self.case_frames = case, 0, []
        result = self.results[case.id]
        started = time.monotonic()
        self.log(f"Starting {case.id}: {case.kind}")
        try:
            self.exercise(case)
            result["status"] = "PASS"
        except AssertionError as exc:
            result.update(status="FAIL", error=str(exc))
            self.log(f"{case.id}: {exc}", "FAIL")
            try:
                self.capture("failure")
            except Exception:
                pass
        except Exception as exc:
            result.update(status="ERROR", error=str(exc), traceback=traceback.format_exc())
            self.log(f"{case.id}: {exc}", "ERROR")
            try:
                self.capture("interrupted", no_frame=True)
            except Exception:
                pass
        finally:
            result["elapsed_host_seconds"] = time.monotonic() - started
            try:
                self.session.reset()
                self.require(self.session.read_memory(NVM_START, 1024) == self.original_nvm,
                             "NVM remains byte-for-byte unchanged")
            except Exception as exc:
                result.update(status="ERROR", cleanup_error=str(exc))
            # Plot after reset/NVM verification, with the target safely back
            # at its watchdog-fed boot gate. Rendering must not delay a hold,
            # frame transition, qualification interval or debugger transaction.
            try:
                graph = self.save_waveforms(result)
                if graph is not None:
                    result["adc_waveform"] = graph
                    self.log(f"{case.id}: ADC waveform saved to {graph['image']}")
            except Exception as exc:
                result["adc_waveform_error"] = str(exc)
                if result["status"] == "PASS":
                    result["status"] = "ERROR"
                self.log(f"{case.id}: ADC waveform evidence failed: {exc}", "ERROR")
            self.log(f"{case.id}: {result['status']}", result["status"])
            self.save_results()

    def save_waveforms(self, result):
        return save_adc_waveform(result, self.output / result["id"], self.session.halt_spans)

    def capture(self, label, *, no_frame=False, camera=True, require_new_frame=True):
        self.serial += 1
        stem = f"{self.serial:02d}_{label}"
        directory = self.output / self.current.id
        directory.mkdir(exist_ok=True)
        observation = {"label": label, "host_time": time.monotonic(),
                       "camera": "OFF" if self.args.camera == "off" else "UNAVAILABLE"}
        try:
            observation["state"] = self.target.status(registers=True)
            self.assert_outputs_inhibited(observation["state"])
            if getattr(self.target, "producer", None) is not None:
                observation["adc_progress"] = self.target.producer.status()
                observation["adc_host_time"] = time.monotonic()
                if observation["adc_progress"]["protocol_errors"]:
                    raise RuntimeError("ADC FIFO protocol error invalidates stimulus")
            if no_frame:
                observation["frame_status"] = "NO_FRAME"
            else:
                # Static setup screens send only when their contents change. Their
                # existing nonzero, even generation is valid evidence in this
                # boot (the mirror is cleared by reset). Other captures still
                # require a new send to establish a display transition.
                after_generation = observation["state"]["generation"] if require_new_frame else 0
                generation, framebuffer = self.target.frame(after_generation)
                self.last_generation = generation
                (directory / f"{stem}_framebuffer.bin").write_bytes(framebuffer)
                save_framebuffer_png(framebuffer, directory / f"{stem}_framebuffer_native.png", scale=1)
                save_framebuffer_png(framebuffer, directory / f"{stem}_framebuffer.png", scale=8)
                observation.update(frame_status="CAPTURED", generation=generation,
                                   image=f"{self.current.id}/{stem}_framebuffer.png",
                                   native_image=f"{self.current.id}/{stem}_framebuffer_native.png")
                if self.camera_available and not camera:
                    observation["camera"] = "DEFERRED_DURING_HOLD"
                if self.camera_available and camera:
                    try:
                        captured = self.camera.capture_rectified(directory / f"{stem}_camera.png",
                                                                 raw_output=directory / f"{stem}_full.jpg")
                        observation.update(camera="CAPTURED", camera_image=f"{self.current.id}/{stem}_camera.png")
                        try:
                            from .image_processing import compare_framebuffer_to_capture
                        except ImportError:
                            from image_processing import compare_framebuffer_to_capture
                        score = compare_framebuffer_to_capture(framebuffer, captured, directory / f"{stem}_comparison")
                        observation["camera_comparison"] = asdict(score)
                        observation["camera_comparison"]["note"] = "Diagnostic only: moving fault text may change during exposure"
                    except Exception as exc:
                        observation["camera"] = f"WARNING: {exc}"
                        self.log(f"Optional camera evidence unavailable: {exc}", "WARNING")
                        if self.args.require_camera:
                            raise RuntimeError("Required camera evidence failed") from exc
        except Exception as exc:
            observation.setdefault("frame_status", "NO_FRAME")
            observation["error"] = str(exc)
            self.results[self.current.id]["observations"].append(observation)
            self.json(directory / f"{stem}.json", observation)
            raise
        self.results[self.current.id]["observations"].append(observation)
        self.json(directory / f"{stem}.json", observation)
        self.log(f"{self.current.id}/{label}: {observation['frame_status']}")
        return observation

    def queue(self, frames, *, mark=False):
        frames = list(frames)

        def commit():
            if mark:
                self.target.command("mark")
            # Sample the counters in the same halt as publication: a separate
            # status read can be many ADC rounds behind the actual enqueue.
            before = self.target.producer.status()
            observed_at = time.monotonic()
            encoded = self.target.producer.enqueue(frames)
            return before, observed_at, encoded
        before, observed_at, encoded = self.session.inspect(commit)
        self.case_frames = frames
        entry = {"before": before, "adc_host_time": observed_at, "mark": mark,
                 "frames": [{**item, "predicted_settled": self.model.predict(item["actual"])} for item in encoded],
                 "empty_policy": "one intentional held-last episode after this complete atomic batch"}
        self.results[self.current.id].setdefault("stimulus", []).append(entry)
        self.log(f"{self.current.id}: queued {len(frames)} frames / {sum(f.repeat_rounds for f in frames)} rounds")
        return entry

    def healthy(self):
        self.target.wait(lambda s: s["stage"] == RUNTIME and 2475 <= s["reference_mv"] <= 4125 and s["pwm"] == 0,
                         timeout=8, description="healthy runtime/reference and settled logical attenuation")
        start, progress = self.target.status(), self.target.producer.status()
        time.sleep(0.3)
        state, end = self.target.status(), self.target.producer.status()
        self.require(state["stage"] == RUNTIME, "Normal supervision remains active")
        self.require(state["flags"] & 24 == 0, "No raw voltage/power-loss latch")
        self.require(state["flags"] & 66 == 64, "Physical output inhibition is active; RF remains off")
        self.assert_outputs_inhibited(self.target.status(registers=True))
        self.require(state["tick"] != start["tick"] and end["rounds_completed"] != progress["rounds_completed"],
                     "Foreground snapshots and ADC rounds continue advancing")
        self.results[self.current.id]["cadence"] = {
            "target_ms": (state["tick"] - start["tick"]) & 0xFFFFFFFF,
            "rounds": (end["rounds_completed"] - progress["rounds_completed"]) & 0xFFFFFFFF,
            "note": "measurement only; stimuli retain complete-round durations"}
        return state

    def boot(self, *, frame=None, settings=None, setup=False, injection=None, wait=True):
        self.last_generation = 0
        frame, settings = frame or self.model.frame(), settings or BootSettings()
        encoded = self.target.boot(frame, settings=settings, setup=setup, injection=injection)
        self.results[self.current.id]["boot"] = {"settings": asdict(settings), "encoding": encoded,
                                                "injection": injection, "boot_number": self.target.boot_number}
        if wait:
            self.healthy()
            self.capture("baseline")
        return encoded

    @staticmethod
    def assert_outputs_inhibited(state):
        regs = state["registers"]
        if state["flags"] & 66 != 64:
            raise AssertionError("Test image does not report RF/buck output inhibition")
        if regs["gpiob_mode"] >> 2 & 3 != 1 or regs["gpiob_odr"] & 2:
            raise AssertionError("RF PB1 is not GPIO-low")
        if regs["tim1_cr1"] & 1 or regs["tim1_bdtr"] & (1 << 15) or regs["tim1_ccer"] & ((1 << 8) | (1 << 10)):
            raise AssertionError("RF TIM1 output is enabled")
        if regs["gpioa_mode"] >> 12 & 3 != 1 or not regs["gpioa_odr"] & 64:
            raise AssertionError("Buck PA6 is not GPIO-high / forced minimum")

    def capture_reset(self, label):
        observation = self.capture(label, no_frame=True)
        state, regs = observation["state"], observation["state"]["registers"]
        self.require(state["stage"] == BOOT and not state["flags"] & 2 and
                     regs["gpiob_mode"] >> 2 & 3 == 1 and not regs["gpiob_odr"] & 2,
                     "Reset returns to the RF-off shared boot gate")
        self.require(state["pwm"] == 32 and regs["gpioa_mode"] >> 12 & 3 == 1 and regs["gpioa_odr"] & 64,
                     "Reset gate commands forced minimum buck output")
        return observation

    def quiet(self, seconds=1):
        initial = self.target.status()
        state = initial
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            time.sleep(min(0.3, max(0, deadline - time.monotonic())))
            state = self.target.status()
            if state["stage"] != RUNTIME:
                raise AssertionError(f"Control stimulus entered {state['message'] or state['stage']}")
            if state["flags"] & 90 != 64:
                raise AssertionError("Output inhibition missing, RF enabled, or a raw fault latched")
        if state["tick"] == initial["tick"]:
            raise AssertionError("Foreground snapshot stopped advancing during control observation")
        return state

    def ramp_dc(self, raw, **overrides):
        start = self.target.producer.status()["decoded"][0]
        self.queue(self.model.ramp(start, raw, **overrides))
        return self.target.wait(lambda s: s["stage"] != RUNTIME or abs(s["filtered"][0] - raw) <= 2,
                                timeout=self.current.timeout, description="filtered DC plateau")

    def terminal(self, *, expected=None, button_reset=None, recoverable=False):
        case = self.current
        expected = case.expected if expected is None else expected
        button_reset = case.button_reset if button_reset is None else button_reset
        state = self.target.wait(lambda s: s["stage"] == FAULT, timeout=case.timeout, description=expected)
        self.require(state["message"] == expected, f"First diagnosis is {expected.replace(chr(10), '/')}")
        self.require(bool(state["flags"] & 1) == button_reset, "Reset permission matches the case contract")
        trigger_frames = self.case_frames
        if recoverable:
            # Hold below recovery voltage before camera/frame capture can take
            # longer than the automatic qualification interval. First-diagnosis
            # ABI evidence stays frozen throughout this screen.
            self.queue([self.model.frame(dc=self.model.voltage_count(14000, side="below"))])
        first = self.capture("first_fault")["state"]
        regs = first["registers"]
        self.require(first["flags"] & 2 == 0 and regs["gpiob_mode"] >> 2 & 3 == 1 and regs["gpiob_odr"] & 2 == 0,
                     "RF inactive and PB1 commanded low as GPIO output")
        self.require(regs["tim1_cr1"] & 1 == 0 and regs["tim1_bdtr"] & (1 << 15) == 0 and
                     regs["tim1_ccer"] & ((1 << 8) | (1 << 10)) == 0, "TIM1 counter/channel/output disabled")
        self.require(regs["gpioa_odr"] & (1 << 6) != 0 and regs["gpioa_mode"] >> 12 & 3 == 1 and first["pwm"] == 32, "Buck at forced minimum")
        self.require(first["flags"] & 4 != 0, "Fan commanded stopped; PA13 remains SWD")
        if case.kind in ("sensor", "bucket", "short"):
            elapsed = (first["trip_ms"] - first["qualify_ms"]) & 0xFFFFFFFF
            threshold = 5000 if case.kind == "short" else 1000
            self.require(threshold <= elapsed < threshold + 1500, "Continuous qualification uses target milliseconds")
        if case.kind in ("spike", "fast_loss"):
            self.require(first["event_raw"] >> 16 == (1 if case.kind == "spike" else 2), "Raw ISR trigger recorded")
        if case.kind == "trend":
            self.require(first["event_raw"] >> 16 == 3 and first["flags"] & 16 == 0,
                         "Sustained-only detector won; fast ISR latch is clear")
            self.require(trend_matches(first["history"]), "Actual evaluated window satisfies every decline segment")
            possible = []
            for delta in (-1, 0, 1):
                possible += modeled_history(trigger_frames, first["qualify_ms"] + delta, 698)
            self.require(tuple(first["history"]) in possible, "Trigger window matches round-count averaging model")
        if recoverable:
            self.input_fault_recovery(first, expected=expected)
            return first
        last = self.target.producer.status()["decoded"][0]
        self.queue(self.model.ramp(last, self.model.base[0]))
        time.sleep(self.args.latch_hold)
        held = self.capture("held_fault")["state"]
        self.require(held["stage"] == FAULT and held["message"] == expected,
                     "Fault stays latched through input recovery and a watchdog interval")
        self.require(held["rf_attempts"] == held["rf_at_fault"], "No RF restart attempt from terminal screen")
        if button_reset:
            self.session.wait_state(1)
            self.session.write_u32(self.session.symbols["btn_swd_test_command_ms"].address, 200)
            self.results[case.id]["button_reset"] = self.target.expect_reset()
            self.capture_reset("button_reset")
        else:
            for duration in (200, 1250):
                self.session.press(duration)
                still = self.target.status()
                self.require(still["stage"] == FAULT and still["message"] == expected,
                             "Forbidden button reset ignored")
            self.capture("button_reset_forbidden")
        return first

    def input_fault_recovery(self, first, *, expected="POWER\nLOSS\nFAULT"):
        """F05/F07 return automatically; no button/reset is sent here."""
        time.sleep(max(1.2, self.args.latch_hold))
        held = self.capture("held_below_recovery", camera=False)["state"]
        self.require(held["stage"] == FAULT and held["message"] == expected,
                     "Input-fault screen stays stopped while voltage is below recovery threshold")
        self.require(held["rf_attempts"] == first["rf_at_fault"], "No RF retry during input-fault hold")
        self.queue([self.model.frame(dc=self.model.base[0])])
        time.sleep(0.4)
        qualifying = self.target.status()
        self.require(qualifying["stage"] == FAULT, "Input recovery does not immediately release outputs")
        resumed = self.target.wait(lambda state: state["stage"] == RUNTIME,
                                   timeout=self.current.timeout, description="automatic input-fault recovery")
        self.require(resumed["reset_flags"] == first["reset_flags"], "Recovery preserves this boot's reset flags")
        self.require(resumed["flags"] & 24 == 0, "Only the recovered raw power-loss latch is cleared")
        self.require(resumed["flags"] & 4 == 0, "Normal supervisor resumed the fan controller")
        self.require(resumed["rf_attempts"] > first["rf_at_fault"], "Normal RF start path is retried after qualification")
        self.capture("auto_recovered", camera=False)
        self.results[self.current.id]["automatic_recovery"] = True

    # Scenario implementations are below the high-level reporting/control flow.
    def exercise(self, case):
        p, kind, m = case.parameters, case.kind, self.model
        if kind in ("boot_fault", "boot_reset"):
            injection = p.get("injection")
            self.boot(settings=BootSettings(tip_absent=p.get("absent", False)), setup=injection == "graphics",
                      injection=injection, wait=False)
            if injection:
                self.target.wait(lambda s: s["ack"] == COMMANDS[injection], timeout=3,
                                 description="named injection acknowledgment")
            if kind == "boot_reset":
                self.capture("injected_before_display", no_frame=True)
                self.results[case.id]["reset"] = self.target.expect_reset(watchdog=True)
                self.capture_reset("watchdog_reset")
            else:
                self.terminal()
            return
        settings = BootSettings(mode=p.get("mode", 0), cells=p.get("cells", 0), power=p.get("power", 0))
        frame = m.frame(dc=698) if kind == "trend" else m.frame(dc=665) if kind == "fast_control" else m.frame(dc=676) if kind == "fast_loss" else m.frame()
        self.boot(frame=frame, settings=settings, setup=kind == "save", wait=kind != "save")
        if kind == "save":
            self.target.wait(lambda s: s["stage"] == SETUP, timeout=10, description="setup entry")
            self.session.release_boot_hold()
            self.capture("setup", require_new_frame=False)
            items = self.session.inspect(lambda: read_menu_items(self.session, self.session.symbols["setup_menu_items"].address))
            self.session.press(1250)  # Real startup-power edit; no flash write yet.
            for _ in range(len(items) - 2):
                self.session.press(200)
            self.target.command("nvm_save")
            self.session.press(1250)
            self.require(self.target.status()["ack"] == COMMANDS["nvm_save"], "Save failed at pre-write injection")
            self.terminal()
            return
        if kind == "healthy":
            self.quiet(1)
        elif kind == "tip":
            self.session.write_u32(self.session.symbols["tip_swd_test_command"].address, 0)
            self.target.wait(lambda s: s["stage"] == FAULT, description="qualified tip absence")
            self.require(self.session.read_u32(self.session.symbols["tip_swd_test_completed"].address) == 1,
                         "Tip mailbox completed one timed qualification")
            self.terminal()
        elif kind == "tip_control":
            address = self.session.symbols["tip_swd_test_completed"].address
            before = self.session.read_u32(address)
            self.session.write_u32(self.session.symbols["tip_swd_test_command"].address, 1)
            time.sleep(0.2)
            self.require(self.session.read_u32(address) == before + 1, "Same-level presence completes once")
            self.quiet()
        elif kind == "command_fault":
            command = self.target.command(p["command"])
            self.target.wait(lambda s: s["ack"] == command, description="runtime injection acknowledged")
            self.terminal()
        elif kind in ("spike", "spike_control"):
            self.queue([m.frame(buck=710 if kind == "spike" else 704), m.frame()])
            self.terminal() if kind == "spike" else self.quiet()
        elif kind in ("fast_loss", "fast_control"):
            self.queue([m.frame(dc=591 if kind == "fast_loss" else 581)])
            self.terminal(recoverable=True) if kind == "fast_loss" else self.quiet()
        elif kind == "trend":
            frames = [m.frame(dc=698, repeats=256)] + [
                m.frame(dc=value, repeats=128) for value in (698, 693, 687, 682, 676, 670, 665)]
            self.queue(frames, mark=True)
            self.terminal(recoverable=True)
        elif kind == "step_control":
            self.queue([m.frame(dc=650, repeats=1280), m.frame(dc=650)])
            self.quiet()
        elif kind.startswith("override") or kind == "battery_cutoff":
            self.battery_case(case)
        elif kind in ("undervoltage", "undervoltage_control"):
            raw = m.voltage_count(14000, side="below" if kind == "undervoltage" else "above")
            self.ramp_dc(raw)
            self.terminal(recoverable=True) if kind == "undervoltage" else self.quiet()
        elif kind in ("sensor", "sensor_control"):
            self.queue([m.frame(channel=p["channel"], temperature=130 if kind == "sensor" else 109)])
            if kind == "sensor":
                self.terminal()
            else:
                self.quiet(1.5)
                self.queue([m.frame()])
                self.quiet()
        elif kind == "reference":
            frame = m.frame(reference=p["reference"])
            if frame.channels[0] < m.base[0]:
                self.ramp_dc(frame.channels[0], buck=frame.channels[1])
            self.queue([frame])
            self.terminal()
        elif kind in ("short", "short_control", "current_limit"):
            current = m.current_count(5800, above=True) if kind != "current_limit" else m.current_count(5300)
            self.queue([m.frame(buck=m.voltage_count(1500), current=current)])
            self.target.wait(lambda s: s["reasons"] & 4, description="normal current limiting")
            self.capture("current_limited")
            if kind == "short":
                self.terminal()
            else:
                self.quiet(2 if kind == "short_control" else 0.5)
                self.queue([m.frame()])
                self.target.wait(lambda s: not s["reasons"] & 4 and s["pwm"] == 0, timeout=8,
                                 description="current-limit release and attenuation ramp-down")
                self.quiet()
        elif kind in ("bucket", "bucket_recover"):
            self.queue([m.frame(current=m.current_count(5000))])
            self.target.wait(lambda s: s["reasons"] & 1 or s["stage"] == FAULT, timeout=25,
                             description="model temperature derating")
            self.capture("bucket_derated")
            if kind == "bucket":
                self.terminal()
            else:
                self.queue([m.frame(current=m.current_count(2000))])
                self.target.wait(lambda s: not s["reasons"] & 1, timeout=20,
                                 description="responsive plant cools below hysteresis")
                self.quiet()
        elif kind in ("thermal_limit", "low_limit"):
            bit = 1 if kind == "thermal_limit" else 2
            if bit == 1:
                self.queue([m.frame(temperature=85)])
            else:
                self.ramp_dc(m.voltage_count(18500))
            self.target.wait(lambda s: s["reasons"] & bit and s["power"] == 2, description="Eco derating")
            self.capture("derated")
            if bit == 1:
                self.queue([m.frame(temperature=77)])
            else:
                self.ramp_dc(m.voltage_count(19500))
            state = self.quiet(0.5)
            self.require(state["reasons"] & bit, "Derating persists in hysteresis band")
            if bit == 1:
                self.queue([m.frame(temperature=74)])
            else:
                self.ramp_dc(m.voltage_count(20500))
            self.target.wait(lambda s: not s["reasons"] & bit and s["power"] == 0,
                             description="preferred power restored")
        elif kind == "power_limit":
            power = (100000, 75000, 50000)[p["power"]]
            buck = m.voltage_count(24000)
            current = m.current_count((power + 7000) * 1000 / m.voltage(buck))
            frame = m.frame(buck=buck, current=current)
            self.require(m.predict(frame.channels)["power_mw"] > power, "Stimulus exceeds selected power ceiling")
            self.queue([frame])
            self.target.wait(lambda s: s["pwm"] > 0, description="logical power-based attenuation")
            self.quiet(0.5)
            self.capture("power_limited")
            self.queue([m.frame()])
            self.target.wait(lambda s: s["pwm"] == 0, timeout=8, description="logical attenuation release")
        elif kind in ("ntc_warning", "ntc_late"):
            if kind == "ntc_late":
                self.quiet(10.5)
            values = list(m.base)
            for channel in ((3, 4) if p.get("channel") == -1 else (p.get("channel", 3),)):
                values[channel] = 1023
            self.queue([Frame(tuple(values))])
            if kind == "ntc_warning":
                self.target.wait(lambda s: s["flags"] & 32 and s["message"] == "NTC\nSENS\nFAULT",
                                 timeout=15, description="one-time NTC warning")
                self.capture("ntc_warning")
                self.target.wait(lambda s: not s["flags"] & 32, timeout=6, description="warning expiry")
            else:
                self.quiet(3.5)
                self.require(not self.target.status()["flags"] & 32, "Late NTC disconnect does not rearm warning")
        elif kind == "high_input":
            self.ramp_dc(m.voltage_count(32000, side="above"))
            self.target.wait(lambda s: s["reserved"] == 3, description="high-input overlay visible")
            self.capture("high_input_visible")
            self.target.wait(lambda s: s["reserved"] == 2, description="500 ms hidden phase")
            self.capture("high_input_hidden")
            self.ramp_dc(m.voltage_count(32000, side="below"))
            self.target.wait(lambda s: s["reserved"] == 0, description="high-input warning cleared")
        elif kind == "reset":
            command = self.target.command(p["command"])
            self.target.wait(lambda s: s["ack"] == command, timeout=2, description="fatal/hang acceptance")
            self.capture("accepted_before_reset", no_frame=True)
            self.results[case.id]["reset"] = self.target.expect_reset(watchdog=True)
            self.capture_reset("watchdog_reset")
            return
        else:
            raise RuntimeError(f"No implementation for {kind}")
        if not case.expected and kind not in ("battery_cutoff", "override_used", "override_boundary", "override_reduced"):
            self.quiet(0.15)
            self.capture("recovered")

    def battery_case(self, case):
        p, m, kind = case.parameters, self.model, case.kind
        cutoff = p["cells"] * BATTERY_MINIMUM[p["mode"]]
        requested = 15000 if kind == "override_reduced" else max(
            15000, (p["cells"] - 1) * BATTERY_MINIMUM[p["mode"]] + 500)
        raw = m.voltage_count(cutoff, side="below") if kind == "battery_cutoff" else m.voltage_count(requested)
        self.ramp_dc(raw)
        state = self.target.wait(lambda s: s["stage"] == FAULT or (s["stage"] == BATTERY and s["message"] == OVERRIDE_MESSAGE), timeout=case.timeout,
                                 description="battery cutoff")
        if state["stage"] == FAULT:
            self.terminal(expected="LOW\nBATT\n!!!!!", button_reset=False)
            return
        self.require(state["message"] == OVERRIDE_MESSAGE, "Eligible battery override prompt")
        self.capture("override_prompt")
        if kind == "battery_cutoff":
            return
        if kind == "override_early":
            self.session.press(1200)
            state = self.target.status()
            self.require(state["stage"] == BATTERY and state["cells"] == p["cells"],
                         "Early release leaves cell count unchanged")
            self.capture("early_release")
        self.session.wait_state(1)
        start = self.target.status()["tick"]
        self.session.write_u32(self.session.symbols["btn_swd_test_command_ms"].address, 6000)
        self.capture("override_partial", camera=False)  # Keep slow still transfer outside the qualification interval.
        if kind == "override_boundary":
            # Exact same-refresh alignment is covered by native C. The target
            # case crosses late in the real hold and records actual timing.
            self.target.wait(lambda s: ((s["tick"] - start) & 0xFFFFFFFF) >= 3500,
                             timeout=6, description="late override qualification")
            self.queue([m.frame(dc=m.voltage_count(14000, side="below"))])
        if kind in ("override_boundary", "override_reduced"):
            fault = self.target.wait(lambda s: s["stage"] == FAULT, timeout=10, description="override rejection")
            self.require(fault["message"] == case.expected and fault["cells"] == p["cells"],
                         "Rejected override preserves inferred count")
            self.require(fault["rf_attempts"] == state["rf_attempts"], "Rejected override makes zero RF attempts")
            self.capture("override_rejected")
            self.terminal()
            return
        resumed = self.target.wait(lambda s: s["stage"] in (RUNTIME, FAULT), timeout=10,
                                   description="override completion")
        self.require(resumed["stage"] == RUNTIME and resumed["cells"] == p["cells"] - 1,
                     "Override corrects exactly one inferred cell")
        time.sleep(1.2)
        self.session.wait_state(1)
        self.require(self.target.status()["stage"] == RUNTIME, "Override hold/release does not enter sleep")
        self.capture("override_resumed")
        if kind == "override_used":
            raw = m.voltage_count((p["cells"] - 1) * BATTERY_MINIMUM[p["mode"]], side="below")
            self.ramp_dc(raw)
            self.terminal()

    def setup_camera(self):
        if self.args.camera == "off":
            return
        try:
            # No camera/OpenCV import occurs in the default camera-off path.
            try:
                from .eyes_rx0 import Rx0Camera
            except ImportError:
                from eyes_rx0 import Rx0Camera
            self.target.boot(self.model.frame(), fiducial=True)
            self.target.frame()
            self.camera = Rx0Camera(camera_index=self.args.camera_index)
            if self.args.calibration:
                self.camera.use_calibration(self.args.calibration)
            else:
                self.camera.calibrate(self.output / "fiducial_calibration.png", crop_to=800, show_progress=False)
            self.camera_available = True
            self.session.dismiss_fiducial()
        except Exception as exc:
            self.manifest["camera_warning"] = str(exc)
            self.log(f"Optional camera setup failed: {exc}", "WARNING")
            if self.args.require_camera:
                raise RuntimeError("Required camera is unavailable") from exc
        finally:
            self.session.reset()

    def native(self):
        for suite in ("adc_sim", "tipdetect_swd", "button_modal", "harness", "faults"):
            result = subprocess.run([sys.executable, "-m", "unittest", "discover", "-s", f"test/{suite}", "-v"],
                                    cwd=ROOT, capture_output=True, text=True)
            (self.output / f"native-{suite}.log").write_text(result.stdout + result.stderr, encoding="utf-8")
            self.manifest.setdefault("native_suites", {})[suite] = "PASS" if result.returncode == 0 else "FAIL"
        coverage = {"F01.glitch": "tipdetect_swd", "F04.exact-705": "adc_sim",
                    "F05.exact-boundaries": "adc_sim", "F06.override-boundaries": "button_modal",
                    "F08.exact-duration": "faults", "F10.exact-duration": "faults",
                    "F11.flash-errors": "faults", "R03.watchdog-init": "faults"}
        for case_id, suite in coverage.items():
            self.results[case_id].update(status=self.manifest["native_suites"][suite],
                                         evidence=f"native-{suite}.log", source_suite=suite)
        passed = all(v == "PASS" for v in self.manifest["native_suites"].values())
        self.log("Native companion suites " + ("passed" if passed else "failed"), "PASS" if passed else "FAIL")
        return passed

    def save_results(self):
        self.manifest["results"] = list(self.results.values())
        self.manifest["complete_electrical_coverage"] = False
        self.manifest["summary"] = dict(Counter(result["status"] for result in self.results.values()))
        self.json(self.output / "results.json", self.manifest)
        rows = []
        for result in self.results.values():
            if result["status"] == "NOT_RUN" and result["level"] != "electrical" and not self.show_unselected:
                continue
            images = []
            if result.get("evidence"):
                evidence = html.escape(result["evidence"])
                images.append(f'<p><a href="{evidence}">Component test log</a></p>')
            graphs = ([result["adc_waveform"]] if result.get("adc_waveform") else []) + result.get("extra_adc_waveforms", [])
            for graph in graphs:
                source = html.escape(graph["image"])
                images.append(f'<div><a href="{source}"><img class="adc-waveform" width="420" src="{source}" alt="ADC waveform"></a>'
                              f'<br><a href="{html.escape(graph["csv"])}">ADC CSV</a> | '
                              f'<a href="{html.escape(graph["data"])}">ADC timeline JSON</a></div>')
                for sequence in graph["sequences"]:
                    source = html.escape(sequence["image"])
                    images.append(f'<a href="{source}">ADC sequence {sequence["number"]} zoom</a> ')
            if result.get("adc_waveform_error"):
                images.append(html.escape("ADC waveform error: " + result["adc_waveform_error"]))
            for observation in result["observations"]:
                if observation.get("image"):
                    image = html.escape(observation["image"])
                    images.append(f'<a href="{image}"><img width="128" src="{image}" alt="{html.escape(observation["label"])}"></a>')
                else:
                    images.append(html.escape(observation["label"] + ": NO_FRAME"))
            rows.append(f'<tr><td>{html.escape(result["id"])}</td><td>{result["status"]}</td>'
                        f'<td>{html.escape(result.get("error", result.get("note", "")))}</td>'
                        f'<td>{" ".join(images)}</td></tr>')
        (self.output / "index.html").write_text(
            f'<!doctype html><meta charset="utf-8"><title>{html.escape(self.report_title)}</title>'
            '<style>body{font:16px sans-serif;background:#eee}table{border-collapse:collapse}'
            'td,th{border:1px solid #aaa;padding:8px;vertical-align:top}img{image-rendering:pixelated}img.adc-waveform{image-rendering:auto}</style>'
            f'<h1>{html.escape(self.report_title)}</h1><p>Functional results and captured OLED pixels. Visual approval is pending human review. '
            'Electrical qualification remains separate.</p><p><a href="report.txt">Report</a> | '
            '<a href="results.json">Results and full coverage manifest</a></p>'
            '<table><tr><th>Case</th><th>Result</th><th>Notes</th><th>Waveforms and captured framebuffer</th></tr>' +
            ''.join(rows) + '</table>', encoding="utf-8")

    def log(self, message, status="INFO"):
        line = f"{datetime.now().astimezone().isoformat()} [{status}] {message}"
        print(line, flush=True)
        print(line, file=self.report, flush=True)

    def require(self, condition, message):
        if not condition:
            raise AssertionError(message)
        self.log(message, "PASS")

    @staticmethod
    def json(path, value):
        path.write_text(json.dumps(value, indent=2, default=str) + "\n", encoding="utf-8", newline="\r\n")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--elf", type=Path, default=TEST_ELF)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--case", action="append", default=[], help="Case ID, family prefix or glob; repeatable")
    parser.add_argument("--list", action="store_true", help="List coverage without touching a board")
    parser.add_argument("--camera", choices=("off", "rx0"), default="off")
    parser.add_argument("--require-camera", action="store_true")
    parser.add_argument("--calibration", type=Path)
    parser.add_argument("--camera-index", type=int)
    parser.add_argument("--gdb-port", type=int, default=3333)
    parser.add_argument("--no-flash", action="store_true", help="Verify an already flashed matching ELF")
    parser.add_argument("--fixture", default="unloaded", help="Fixture identity recorded in the manifest")
    parser.add_argument("--latch-hold", type=float, default=6.2, help="Observation interval, minimum 6 seconds")
    parser.add_argument("--native", action="store_true", help="Run native companions before target cases")
    parser.add_argument("--native-only", action="store_true", help="Offline companion suites, no SWD/camera")
    args = parser.parse_args(argv)
    if args.native_only and args.camera != "off":
        parser.error("--native-only has no camera stage; leave --camera off")
    if args.require_camera and args.camera == "off":
        parser.error("--require-camera needs --camera rx0")
    if args.latch_hold < 6:
        parser.error("--latch-hold must exceed the watchdog interval (at least 6 seconds)")
    cases = catalog()
    if args.list:
        for case in cases:
            print(f"{case.id:36} {case.level:10} {case.kind:20} {case.note}")
        return 0
    selected = [c for c in cases if c.level == "target"] if not args.case else []
    for pattern in args.case:
        matches = [c for c in cases if c.id == pattern or c.id.startswith(pattern + ".") or fnmatch.fnmatchcase(c.id, pattern)]
        if not matches:
            parser.error(f"Unknown case selection: {pattern}")
        selected.extend(c for c in matches if c not in selected)
    if args.native_only:
        selected = [c for c in selected if c.level == "native"] if args.case else [c for c in cases if c.level == "native"]
        if not selected:
            parser.error("--native-only selection contains no native cases; use --list to inspect coverage")
    return FaultRunner(args, selected).run()


if __name__ == "__main__":
    sys.exit(main())
