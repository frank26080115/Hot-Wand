"""Normal application tests with the shared, physically inhibited test image.

Build first: pio run -e stm32f042_test
Smoke: py tools/auto_testing/functional_test.py --case FUN.boot.healthy
Run: py tools/auto_testing/functional_test.py --native --camera off
Extended real-time sleep/endurance: add --long
Offline only: py tools/auto_testing/functional_test.py --native-only

No test enables RF or releases the physical buck clamp. Exit cleanup erases
saved settings so the next normal boot uses factory defaults.
"""
from __future__ import annotations

import argparse
import fnmatch
import json
from pathlib import Path
import subprocess
import sys
import time

try:
    from .faults_test import FaultRunner, ROOT
    from .adc_waveform import save_adc_waveform
    from .fault_target import BootSettings, BOOT, ADC, OLED, SETUP, RUNTIME, FAULT
    from .functional_cases import catalog, MODE_SEQUENCES, calibrate, guess_cells
    from .build_metadata import TEST_ELF, latest_nvm_record, settings_indices
except ImportError:
    from faults_test import FaultRunner, ROOT
    from adc_waveform import save_adc_waveform
    from fault_target import BootSettings, BOOT, ADC, OLED, SETUP, RUNTIME, FAULT
    from functional_cases import catalog, MODE_SEQUENCES, calibrate, guess_cells
    from build_metadata import TEST_ELF, latest_nvm_record, settings_indices


class SafetyViolation(RuntimeError):
    """Stop the entire run after cleanup, rather than trying another case."""


class FunctionalRunner(FaultRunner):
    # Inherit the common lifecycle/report, rather than maintaining a second
    # flasher, camera pipeline, framebuffer protocol or waveform exporter.
    case_catalog = staticmethod(catalog)
    report_prefix = "functional"
    report_title = "Functional test evidence"
    show_unselected = True
    scope = "normal application; native/target/electrical coverage kept separate"

    def __init__(self, args, cases):
        super().__init__(args, cases)
        self.safety_error = None
        self.oled_dim_address = None
        self.manifest.update(contract_version=1, accepted_quirks=[
            "Power selection reverses at 50/100 W",
            "Runtime long hold may change power before sleep",
            "Sleep is watchdog-fed standby; waking reboots"],
            unresolved_requirements=["Boot wait can exceed the design's up-to-300-ms wording"],
            long_enabled=args.long)

    def run_case(self, case):
        super().run_case(case)
        # The base writes all partial evidence and resets in finally. A safety
        # error must then escape its ordinary per-case failure containment.
        if self.safety_error:
            raise SafetyViolation(self.safety_error)

    def assert_outputs_inhibited(self, state):
        try:
            FaultRunner.assert_outputs_inhibited(state)
            # PA13/14 must remain AF0 (SWD), never fan/UART ownership.
            mode = state["registers"]["gpioa_mode"]
            if (mode >> 26 & 3, mode >> 28 & 3) != (2, 2) or state["registers"]["gpioa_afrh"] & 0x0FF00000:
                raise AssertionError("PA13/PA14 no longer belong to SWD")
        except AssertionError as exc:
            self.safety_error = str(exc)
            raise SafetyViolation(str(exc)) from exc

    def require(self, condition, message):
        if not condition and "NVM" in message:
            self.safety_error = message
            raise SafetyViolation(message)
        return super().require(condition, message)

    def status(self):
        state = self.target.status(registers=True)
        self.assert_outputs_inhibited(state)
        if state["stage"] not in (BOOT, ADC, OLED, SETUP, RUNTIME, FAULT):
            raise AssertionError(f"Unexpected application stage {state['stage']}: {state['message']}")
        return state

    def wait(self, predicate, *, seconds=15, description="functional condition", target_time=False, interval=0.12):
        # Boot/menu/transport waits retain their host deadline: the runtime
        # snapshot tick is not published in every application stage. Runtime
        # timer checks instead measure MCU time, which pauses during SWD reads.
        deadline = time.monotonic() + (max(60, seconds * 4 + 30) if target_time else seconds)
        start_tick = last_tick = None
        stall_deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            state = self.status()
            if predicate(state):
                return state
            if state["stage"] == FAULT:
                raise AssertionError(f"Unexpected terminal diagnosis: {state['message']}")
            if target_time:
                self.require_runtime(state)
                tick = state["tick"]
                if start_tick is None:
                    start_tick = tick
                if tick != last_tick:
                    last_tick = tick
                    stall_deadline = time.monotonic() + 30
                elif time.monotonic() >= stall_deadline:
                    raise TimeoutError(f"MCU tick stopped while waiting for {description}; last state {state}")
                if ((tick - start_tick) & 0xFFFFFFFF) >= seconds * 1000:
                    raise TimeoutError(f"Timed out after {seconds:g} MCU seconds waiting for {description}; last state {state}")
            time.sleep(interval)
        clock = "host safety deadline" if target_time else "host deadline"
        raise TimeoutError(f"Timed out at {clock} waiting for {description}; last state {state}")

    def hold_runtime(self, milliseconds):
        start = self.status()["tick"]
        def completed(state):
            self.require_runtime(state)
            return ((state["tick"] - start) & 0xFFFFFFFF) >= milliseconds
        # Both the hold and its timeout use MCU elapsed time. Keep a small
        # target-time margin for the first observation and bounded polling.
        return self.wait(completed, seconds=milliseconds / 1000 + 2, target_time=True,
                         description="runtime observation interval",
                         interval=min(1.0, max(0.1, milliseconds / 10000)))

    @staticmethod
    def require_runtime(state):
        if state["stage"] != RUNTIME or state["flags"] & 24:
            raise AssertionError(f"Expected normal runtime, got {state['message'] or state['stage']}")

    def boot_normal(self, *, frame=None, settings=None, saved=False):
        if saved:
            encoded = self.target.boot(frame or self.model.frame(), settings=None)
            self.results[self.current.id]["boot"] = {"settings": "saved NVM", "encoding": encoded}
            self.healthy()
            self.results[self.current.id]["boot"]["boot_number"] = self.target.boot_number
            self.results[self.current.id].setdefault("boots", []).append(dict(self.results[self.current.id]["boot"]))
            self.queue([frame or self.model.frame()])
            self.capture("saved_baseline")
        else:
            self.boot(frame=frame, settings=settings or BootSettings())
        self.status()

    def boot(self, **kwargs):
        # Separate waveforms by boot: counter resets must never be plotted as
        # one continuous timeline. Queue one identical held baseline after
        # acquisition is ready, giving even baseline-only cases an ADC graph.
        self.results[self.current.id].pop("cadence", None)
        encoded = super().boot(**kwargs)
        self.results[self.current.id].setdefault("boots", []).append(dict(self.results[self.current.id]["boot"]))
        deadline = time.monotonic() + 10
        while True:
            progress = self.target.producer.status()
            if progress["state"] == 2 and progress["queued"] == 0:
                break
            if time.monotonic() > deadline:
                raise TimeoutError("Boot frame was not consumed by the real ADC round robin")
            time.sleep(0.02)
        self.queue([kwargs.get("frame") or self.model.frame()])
        return encoded

    def queue(self, frames, **kwargs):
        self.status()
        entry = super().queue(frames, **kwargs)
        entry["boot_number"] = self.target.boot_number
        entry["cadence"] = dict(self.results[self.current.id].get("cadence", {}))
        settings = self.results[self.current.id].get("boot", {}).get("settings", {})
        selection = (settings_indices(latest_nvm_record(self.original_nvm))[7]
                     if settings == "saved NVM" else settings.get("calibration", 0))
        for frame in entry["frames"]:
            frame["predicted_settled"]["dc_mv"] = calibrate(frame["predicted_settled"]["dc_mv"], selection)
            frame["input_calibration_selection"] = selection
        return entry

    def save_waveforms(self, result):
        graphs = []
        epochs = sorted({e["boot_number"] for e in result.get("stimulus", [])})
        for boot in epochs:
            entries = [e for e in result["stimulus"] if e["boot_number"] == boot]
            segment = {**result, "boot": {"boot_number": boot}, "stimulus": entries,
                       "cadence": entries[-1].get("cadence", {})}
            directory = self.output / result["id"]
            if len(epochs) > 1:
                directory = directory / f"boot_{boot}"
            graph = save_adc_waveform(segment, directory, self.session.halt_spans)
            if graph:
                # Exporter paths normally use CASE/file; epoch subdirectories
                # need paths relative to the run root for the HTML gallery.
                for key in ("image", "csv", "data"):
                    graph[key] = str((directory / Path(graph[key]).name).relative_to(self.output)).replace(chr(92), "/")
                for sequence in graph["sequences"]:
                    for key in ("image", "csv", "data"):
                        if key in sequence:
                            sequence[key] = str((directory / Path(sequence[key]).name).relative_to(self.output)).replace(chr(92), "/")
                graph["boot_number"] = boot
                graphs.append(graph)
        result["extra_adc_waveforms"] = graphs[1:]
        return graphs[0] if graphs else None

    def settle(self, frame):
        self.queue([frame])
        # The final frame intentionally holds. All regular channels pass
        # through the production filters before engineering assertions.
        self.wait(lambda s: all(abs(s["filtered"][i] - frame.channels[i]) <= 1 for i in range(6)),
                  description="all six filtered channels at the requested plateau")
        return self.status()

    def measured(self, state):
        values = self.model.predict(tuple(state["filtered"]) + (self.target.producer.status()["decoded"][6],))
        values["dc_mv"] = calibrate(values["dc_mv"], self.current.parameters.get("calibration", 0))
        return values

    def image(self, observation):
        path = self.output / observation["native_image"]
        from PIL import Image
        import numpy as np
        with Image.open(path) as picture:
            return np.asarray(picture).copy()

    def dimmed(self):
        if self.oled_dim_address is None:
            def resolve():
                base = self.session.symbols["oled"].address
                address = self.session.expression_address("&oled.dimmed")
                end = self.session.expression_address("&oled + 1")
                # Resolve type layout from this ELF, never a hard-coded offset.
                if not base <= address < end or end - base > 4096:
                    raise RuntimeError("ELF OLED dim-state layout is invalid")
                return address
            self.oled_dim_address = self.session.inspect(resolve)
            self.manifest["oled_dim_read_address"] = self.oled_dim_address
        return bool(self.session.read_memory(self.oled_dim_address, 1)[0])

    def observe_dim(self, label):
        observation = self.capture(label)
        observation["oled_dimmed"] = self.dimmed()
        # The normal capture JSON precedes this extra read; save the contrast
        # observation separately as well as in the manifest.
        self.json(self.output / self.current.id / f"{label}_contrast.json", observation)
        return observation

    def press_mode(self, expected):
        self.session.press(200)
        self.wait(lambda s: s["power"] == expected, description=f"applied power mode {expected}")

    def exercise(self, case):
        self._exercise(case)
        expected = RUNTIME
        if case.kind == "hold" and case.parameters["behavior"] == "setup" or case.kind == "display_static":
            expected = SETUP
        elif case.kind == "sleep" or case.kind == "edge" and case.parameters["duration"] >= 1000:
            expected = BOOT
        elif case.kind in ("sleep_display", "sleep_activity") or case.kind == "auto_sleep" and case.parameters["mode"]:
            expected = FAULT
        state = self.status()
        self.require(state["stage"] == expected,
                     f"Case ends in expected application stage {expected}, observed {state['stage']}: {state['message']}")
        if expected == RUNTIME:
            self.require_runtime(state)

    def _exercise(self, case):
        p, kind = case.parameters, case.kind
        if kind == "hold":
            self.boot(settings=BootSettings(), setup=True, wait=False)
            if p["behavior"] == "setup":
                self.wait(lambda s: s["stage"] == SETUP, seconds=10, description="setup hold entry")
                self.session.release_boot_hold()
                self.capture("setup", require_new_frame=False)
            else:
                self.session.wait_state(0, timeout=10)
                # A nonzero mirror proves the hold prompt exists before release.
                self.target.frame()
                self.capture("holding_prompt", require_new_frame=False)
                self.session.release_boot_hold()
                self.healthy()
                self.require(self.status()["power"] == 0, "Boot-release gesture did not change runtime power")
                self.capture("early_release_runtime")
            return
        if kind == "settings":
            self.boot_normal(settings=BootSettings(power=2, calibration=5, auto_dim=1))
            self.require(self.status()["power"] == 2, "RAM startup preference applied")
            self.boot_normal(saved=True)
            indices = settings_indices(latest_nvm_record(self.original_nvm))
            self.require(self.status()["power"] == indices[0], "Saved startup preference applied without RAM override")
            self.results[case.id]["saved_settings"] = indices
            return
        if kind == "battery":
            self.boot_normal(settings=BootSettings(mode=p["mode"]))
            state = self.status()
            self.require(state["cells"] == guess_cells(state["dc_mv"], p["mode"]) and state["mode"] == p["mode"],
                         "Real startup cell guess matches the independent healthy-pack calculation")
            self.hold_runtime(1000)
            self.capture("healthy_pack")
            return
        if kind == "battery_reboot":
            self.boot_normal(settings=BootSettings(mode=1))
            old = self.status()["cells"]
            raw = self.model.voltage_count(26000)
            self.ramp_dc(raw)
            self.require(self.status()["cells"] == old, "Cell count stays fixed during healthy runtime")
            self.boot_normal(frame=self.model.frame(dc=raw), settings=BootSettings(mode=1))
            state = self.status()
            self.require(state["cells"] == guess_cells(state["dc_mv"], 1) and state["cells"] != old,
                         "Reboot re-guesses the changed pack voltage")
            return

        settings = BootSettings(power=p.get("power", 0), calibration=p.get("calibration", 0),
                                auto_dim=p.get("dim", 1 if kind == "idle" else 0),
                                auto_sleep=p.get("mode", 0) if kind == "auto_sleep" else 1 if kind == "sleep_activity" else 0)
        self.boot_normal(settings=settings)

        if kind == "healthy":
            initial = self.status()
            self.hold_runtime(6200)
            state = self.status()
            self.require(state["rf_attempts"] > initial["rf_attempts"], "Supervisor continues requesting eligible RF starts; physical RF remains off")
            self.capture("running_beyond_watchdog_interval")
        elif kind == "mode":
            for index, expected in enumerate(MODE_SEQUENCES[p["power"]]):
                self.press_mode(expected)
                self.capture(f"press_{index + 1}_mode_{expected}")
            self.boot_normal(settings=settings)
            self.require(self.status()["power"] == p["power"], "Reset restores startup preference")
        elif kind == "edge":
            if p["duration"] < 1000:
                self.session.press(p["duration"])
                self.wait(lambda s: s["power"] == 1)
                self.capture("short_on_press_no_sleep")
            else:
                self.manual_sleep(p["duration"])
        elif kind == "repeat":
            for _ in range(5):
                self.session.press(200)
            self.session.press(200)
            state = self.wait(lambda s: bool(s["flags"] & 32), description="six-press novelty message")
            allowed = {"SKILL\nISSUE\n  ?  ", "BRUH.\nSTOP.", "GIT\nGOOD", "IT'S\nOVER\n9000\n!!!!!", "THE\nCAKE\nIS A\nLIE !!"}
            self.require(state["message"] in allowed, "Novelty message is one of the five permitted messages")
            self.capture("novelty")
            self.wait(lambda s: not s["flags"] & 32)
            self.capture("novelty_expired")
        elif kind in ("power", "preference"):
            if kind == "preference":
                self.settle(self.model.frame(temperature=85))
                self.wait(lambda s: s["power"] == 2 and s["reasons"] & 1)
                self.capture("thermal_preference_temporarily_limited")
                self.session.press(200)  # Blocked request must not overwrite preference.
                self.settle(self.model.frame())
                self.wait(lambda s: s["power"] == p["power"] and not s["reasons"] & 1)
                self.capture("preference_restored")
            else:
                # High wattage below 5.2 A must not activate buck attenuation;
                # the applied mode is enforced through foreground RF phases.
                high = self.model.frame(buck=self.model.voltage_count(22000), current=self.model.current_count(4900))
                self.settle(high)
                self.wait(lambda s: s["pwm"] == 0)
                self.check_rf_cycle(p["power"])
                self.require(self.status()["pwm"] == 0, "Wattage alone leaves buck PWM inactive")
                self.capture("rf_duty_mode")
                self.settle(self.model.frame())
                self.wait(lambda s: s["pwm"] == 0)
                self.capture("logical_attenuation_relaxed")
        elif kind in ("dim", "idle"):
            if kind == "idle":
                saved = settings_indices(latest_nvm_record(self.original_nvm))[5]
                self.results[case.id]["saved_idle_threshold_w"] = (1, 2, 5, 10, 20, 30, 40)[saved]
                self.wait(lambda s: self.dimmed(), seconds=25, description="low measured power becomes inactive", target_time=True, interval=0.5)
                self.observe_dim("saved_threshold_low_power_dimmed")
                watts = self.results[case.id]["saved_idle_threshold_w"] + 1
                self.settle(self.model.frame(current=self.model.current_count(watts * 50)))
                self.wait(lambda s: not self.dimmed(), description="power above saved idle threshold restores activity")
                self.hold_runtime(16000)
                self.require(not self.dimmed(), "Continuous measured power prevents inactivity dimming")
                self.observe_dim("saved_threshold_active_power")
                return
            delay = (0, 15, 30, 60)[p["dim"]]
            self.require(not self.dimmed(), "Runtime begins bright")
            if delay:
                self.session.press(200)  # Establish a fresh, observed activity interval.
                self.hold_runtime((delay - 3) * 1000)
                self.require(not self.dimmed(), "Display remains bright before the configured inactivity delay")
                self.wait(lambda s: self.dimmed(), seconds=10, description="runtime dim delay", target_time=True, interval=0.5)
                self.observe_dim("dimmed")
                self.session.press(200)
                self.wait(lambda s: not self.dimmed(), description="button brightens display")
                self.observe_dim("bright_after_button")
                self.wait(lambda s: self.dimmed(), seconds=delay + 10, target_time=True,
                          description="repeat inactivity dim delay", interval=0.5)
                self.settle(self.model.frame(current=self.model.current_count(2250)))  # ~45 W, above every idle setting.
                self.wait(lambda s: not self.dimmed(), description="measured power brightens display")
                self.observe_dim("bright_after_power")
            else:
                self.hold_runtime(16000)
                self.require(not self.dimmed(), "Dim-off remains bright after the shortest enabled delay")
                self.observe_dim("dim_off")
        elif kind in ("sleep", "sleep_display"):
            self.manual_sleep(6000, wake=kind == "sleep")
            if kind == "sleep_display":
                before = self.capture("sleep_start"); start = before["state"]["rf_attempts"]
                time.sleep(5.3)
                after = self.capture("sleep_shifted")
                self.require(after["state"]["rf_attempts"] == start, "Terminal sleep makes no RF restart requests")
                self.require((self.image(before) != self.image(after)).any(), "Sleep text moves for burn-in protection")
                saved_delay = (0, 15, 30, 60)[settings_indices(latest_nvm_record(self.original_nvm))[4]]
                self.results[case.id]["saved_sleep_dim_delay_seconds"] = saved_delay
                if saved_delay:
                    deadline = time.monotonic() + saved_delay + 10
                    while not self.dimmed():
                        self.require(self.status()["stage"] == FAULT, "Sleep remains terminal while awaiting saved dim delay")
                        if time.monotonic() >= deadline:
                            raise TimeoutError("Sleep screen did not follow saved NVM dim delay")
                        time.sleep(0.5)
                else:
                    self.require(not self.dimmed(), "Saved dim-off also keeps the sleep screen bright")
                self.observe_dim("sleep_saved_contrast")
        elif kind in ("auto_sleep", "sleep_activity"):
            self.auto_sleep(case)
        elif kind == "calibration":
            state = self.status()
            uncalibrated = self.model.voltage(state["filtered"][0], state["reference_mv"])
            self.require(abs(state["dc_mv"] - calibrate(uncalibrated, p["calibration"])) <= 2,
                         "Input voltage matches independently calculated Q10 calibration")
            self.capture("calibrated_voltage")
        elif kind in ("conversions", "adc_power"):
            for name, frame in (
                ("buck_current", self.model.frame(buck=self.model.voltage_count(18000), current=self.model.current_count(1700))),
                ("ntc1", self.model.frame(temperature=50, channel=3)),
                ("ntc2", self.model.frame(temperature=55, channel=4)),
                ("mcu", self.model.frame(temperature=60, channel=5))):
                state = self.settle(frame)
                predicted = self.measured(state)
                self.require(abs(state["current_ma"] - predicted["current_ma"]) <= 10 and
                             abs(state["dc_mv"] - predicted["dc_mv"]) <= 40,
                             f"{name}: settled engineering values agree with independent conversion")
                self.require(abs(state["temperature_c"] - max(25, *predicted["temperatures"])) <= 2,
                             f"{name}: hottest converted temperature matches the independent model")
                self.capture(name)
        elif kind.startswith("display_"):
            self.display_case(kind)
        elif kind in ("random", "swd"):
            self.capture("healthy_swd_and_shift")
            if kind == "random":
                shifts = [self.session.read_memory(self.session.symbols[name].address, 1)[0]
                          for name in ("pixshift_x", "pixshift_y")]
                self.require(all(0 <= value <= 3 for value in shifts), "Startup text shifts stay within display bounds")
                self.results[case.id]["startup_shifts"] = shifts
                self.boot_normal()
                self.capture("repeat_boot_deterministic_seed_allowed")
        elif kind == "endurance":
            for index in range(60):
                self.settle(self.model.frame(current=self.model.current_count(500 + index % 4 * 250)))
                if index % 5 == 0:
                    self.session.press(200)
                    self.capture(f"endurance_{index:02}")
                self.hold_runtime(10000)
        else:
            raise RuntimeError(f"No functional exercise for {kind}")

    def manual_sleep(self, duration, *, wake=True):
        before = self.session.wait_state(1, timeout=10)
        self.session.write_u32(self.session.symbols["btn_swd_test_command_ms"].address, duration)
        state = self.wait(lambda s: s["stage"] == FAULT, description="manual sleep")
        self.require(state["message"] == "ZZZZZ" and state["flags"] & 5 == 5,
                     "Long gesture enters resettable sleep with fan stopped; initial mode change is allowed")
        if duration >= 3000:
            self.require(self.session.button_state()[1] == 2, "Sleep-entry hold remains down without rebooting")
            self.capture("sleep_while_held", camera=False)
        after = self.session.wait_state(1, timeout=duration / 1000 + 5)
        self.require(after[2] == before[2] + 1, "Sleep gesture completes one debounced release")
        # The terminal screen additionally arms reset after a released-button
        # interval. Do not race that arming with the next synthetic press.
        time.sleep(0.15)
        self.require(self.status()["stage"] == FAULT, "Remaining hold and release did not wake sleep")
        self.capture("sleep")
        if wake:
            self.session.press(200)
            self.target.expect_reset()
            self.capture_reset("wake_reboot")

    def auto_sleep(self, case):
        if case.kind == "sleep_activity":
            self.hold_runtime(270000)
            self.session.press(200)
            self.capture("activity_restarts_timeout")
            self.hold_runtime(45000)
            self.capture("still_awake_after_old_deadline")
            state = self.wait(lambda s: s["stage"] == FAULT, seconds=280, description="new full sleep timeout", target_time=True, interval=1.0)
        elif case.parameters["mode"] == 0:
            self.hold_runtime(310000)
            self.capture("auto_sleep_off")
            return
        else:
            seconds = case.parameters["seconds"]
            # Poll live target ticks and register safety throughout the wait.
            self.hold_runtime((seconds - 10) * 1000)
            self.capture("before_auto_sleep")
            state = self.wait(lambda s: s["stage"] == FAULT, seconds=25, description="automatic sleep", target_time=True, interval=1.0)
        self.require(state["message"] == "ZZZZZ", "Inactivity enters sleep, not a fault diagnosis")
        self.capture("automatic_sleep")

    def display_case(self, kind):
        if kind == "display_hot":
            self.settle(self.model.frame(temperature=85))
            self.wait(lambda s: s["reasons"] & 1)
            signatures = set()
            for index in range(8):
                observation = self.capture(f"hot_phase_{index}")
                signatures.add(self.image(observation)[:20].tobytes())
                time.sleep(0.25)
            self.require(len(signatures) == 2, "Thermal top line alternates between two stable text images")
            self.settle(self.model.frame())
            self.wait(lambda s: not s["reasons"] & 1)
            self.capture("hot_cleared")
        elif kind == "display_static":
            self.boot(settings=BootSettings(), setup=True, wait=False)
            self.wait(lambda s: s["stage"] == SETUP, seconds=10)
            self.session.release_boot_hold()
            first = self.capture("static_setup", require_new_frame=False)
            self.session.press(200)
            # The menu sends on the button action, not continuously. Explicitly
            # compare with the pre-action generation instead of waiting again.
            generation, pixels = self.target.frame(first["generation"])
            self.require(generation != first["generation"], "Menu action published a newer completed frame")
            self.capture("changed_setup", require_new_frame=False)
        elif kind in ("display_graph", "display_peaks"):
            for watts in (0, 25, 50, 75, 100, 0):
                frame = self.model.frame(buck=self.model.voltage_count(22000),
                                         current=self.model.current_count(watts * 1000 / 22))
                state = self.settle(frame)
                self.hold_runtime(350)
                observation = self.capture(f"graph_{watts}w")
                expected_power = self.measured(state)["power_mw"]
                height = min(100, expected_power * 100 // 100000)
                center = self.image(observation)[28:, 15:17]
                self.require(abs(int((center[:, 0] != 0).sum()) - height) <= 1 and
                             (center[1:, 0] == center[1:, 1]).all(), "Live center pair has the independently calculated power height")
            if kind == "display_peaks":
                self.hold_runtime(4500)
                observation = self.capture("history_evicted")
                self.require(not self.image(observation)[29:].any(), "Zero-power segments evict old graph peaks, retaining the 100 W ceiling")
        elif kind == "display_ceiling":
            for expected, row in ((1, 53), (2, 78), (1, 53), (0, 28)):
                self.press_mode(expected)
                observation = self.capture(f"ceiling_mode_{expected}")
                graph = self.image(observation)[28:]
                line = self.image(observation)[row] != 0
                # One continuous fixed pattern spans the width, without
                # mirroring or grouping two dots at the graph's center.
                pattern = tuple(int(bit) for bit in ("100" * 10 + "10"))
                self.require(tuple(int(pixel) for pixel in line) == pattern,
                             "Ceiling has static full-width one-dot/two-space spacing")
                self.require(int((graph != 0).sum()) == int(line.sum()),
                             "Ceiling is only on its specified 100/75/50 W row")
                self.require(not self.image(observation)[27].any(),
                             "Black separator row remains between voltage text and the power graph")
        elif kind == "display_service":
            before = self.status()
            self.hold_runtime(1000)
            after = self.status()
            self.require(after["generation"] != before["generation"], "Display continues sending during healthy supervision")
            for _ in range(6):
                self.session.press(200)
            self.wait(lambda s: not s["flags"] & 32)
            self.capture("normal_display_resumed")
        elif kind == "display_voltage":
            for millivolts in (24000, 26000):
                self.ramp_dc(self.model.voltage_count(millivolts))
                self.capture(f"voltage_{millivolts}")

    def native(self):
        # Machine-readable per-test results prevent one passing unrelated
        # native suite from being credited as every functional requirement.
        destination = self.output / "native-results.json"
        command = [sys.executable, str(ROOT / "tools/auto_testing/functional_native.py"), str(destination)]
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        (self.output / "native-functional.log").write_text(result.stdout + result.stderr, encoding="utf-8")
        evidence = json.loads(destination.read_text(encoding="utf-8")) if destination.exists() else {}
        selected = {case.id for case in self.cases}
        for case in self.case_catalog():
            if case.level != "native" or not (self.args.native or case.id in selected):
                continue
            checks = evidence.get(case.parameters["family"], [])
            self.results[case.id].update(status="PASS" if checks and all(c["status"] == "PASS" for c in checks) else "ERROR",
                                         evidence="native-functional.log", native_checks=checks)
            if not checks:
                self.results[case.id]["error"] = "No completed native checks for this family"
        self.manifest["native_exit_code"] = result.returncode
        return result.returncode == 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--elf", type=Path, default=TEST_ELF)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--case", action="append", default=[])
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--long", action="store_true", help="Include 15/30-minute sleep, sleep-off and endurance cases")
    parser.add_argument("--camera", choices=("off", "rx0"), default="off")
    parser.add_argument("--require-camera", action="store_true")
    parser.add_argument("--calibration", type=Path)
    parser.add_argument("--camera-index", type=int)
    parser.add_argument("--gdb-port", type=int, default=3333)
    parser.add_argument("--no-flash", action="store_true")
    parser.add_argument("--fixture", default="physically inhibited board")
    parser.add_argument("--native", action="store_true")
    parser.add_argument("--native-only", action="store_true")
    args = parser.parse_args(argv)
    if args.require_camera and args.camera == "off":
        parser.error("--require-camera needs --camera rx0")
    if args.native_only and args.camera != "off":
        parser.error("--native-only has no camera stage")
    cases = catalog()
    if args.list:
        for case in cases:
            print(f"{case.id:42} {case.level:10} {'LONG' if case.long else '':5} {case.note}")
        return 0
    selected = []
    if args.case:
        for pattern in args.case:
            matches = [c for c in cases if c.id == pattern or c.id.startswith(pattern + ".") or fnmatch.fnmatchcase(c.id, pattern)]
            if not matches:
                parser.error(f"Unknown case: {pattern}")
            selected.extend(c for c in matches if c not in selected)
    else:
        selected = [c for c in cases if c.level == ("native" if args.native_only else "target") and (args.long or not c.long)]
    if args.native_only:
        selected = [c for c in selected if c.level == "native"]
        if not selected:
            parser.error("No native cases selected")
    return FunctionalRunner(args, selected).run()


if __name__ == "__main__":
    sys.exit(main())
