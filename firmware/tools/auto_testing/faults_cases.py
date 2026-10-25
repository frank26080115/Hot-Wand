"""Reviewed fault contracts and independent ADC stimulus calculations.

This catalog covers faults, their warnings/limits, and fatal recovery. Setup
menu behavior has its own runner; other application behavior is a later suite.
Expected diagnoses are literals here, independent of observed firmware strings.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from math import ceil

try:
    from .adc_sim import Frame
except ImportError:
    from adc_sim import Frame


@dataclass(frozen=True)
class Case:
    id: str
    kind: str
    expected: str = ""
    button_reset: bool | None = None
    parameters: dict = field(default_factory=dict)
    timeout: float = 15
    level: str = "target"
    note: str = ""


def catalog():
    cases = [Case("control.healthy", "healthy"),
             Case("F01.runtime", "tip", "TIP\nFAULT", True),
             Case("F01.present-control", "tip_control"),
             Case("F01.boot", "boot_fault", "TIP\nFAULT", True, {"absent": True}),
             Case("F02.activity", "command_fault", "TIP\nDETEC\nMALFU", True, {"command": "tip_storm"}),
             Case("F03.runtime", "command_fault", "CLOCK\nFAULT", True, {"command": "clock"}),
             Case("F04.spike", "spike", "VOLT\nSPIKE\nFAULT", True),
             Case("F04.below", "spike_control"),
             Case("F05.fast", "fast_loss", "POWER\nLOSS\nFAULT", True, note="Automatic recovery after >14 V for 1000 ms; manual reset remains allowed"),
             Case("F05.fast-below", "fast_control"),
             Case("F05.trend", "trend", "POWER\nLOSS\nFAULT", True, note="Automatic recovery after >14 V for 1000 ms; manual reset remains allowed"),
             Case("F05.step-control", "step_control"),
             Case("F06.override", "override", parameters={"cells": 5, "mode": 1}, timeout=25),
             Case("F06.override-early-release", "override_early", parameters={"cells": 5, "mode": 1}, timeout=25),
             Case("F06.override-used", "override_used", "LOW\nBATT\n!!!!!", False,
                  {"cells": 6, "mode": 1}, timeout=30),
             Case("F06.override-completion-drop", "override_boundary", "LOW\nBATT\n!!!!!", False,
                  {"cells": 5, "mode": 1}, timeout=25),
             Case("F06.override-reduced-cutoff", "override_reduced", "LOW\nBATT\n!!!!!", False,
                  {"cells": 7, "mode": 1}, timeout=25),
             Case("F07.undervoltage", "undervoltage", "LOW\nVOLT\nFAULT", True, note="Automatic recovery after filtered input >14 V for 1000 ms"),
             Case("F07.above", "undervoltage_control"),
             Case("F08.bucket", "bucket", "CIRKT\nTOO\nHOT\nFAULT", True, timeout=45),
             Case("F08.bucket-responsive", "bucket_recover", timeout=45),
             Case("F10.short", "short", "SHORT\nCIRKT\nFAULT", False, timeout=20),
             Case("F10.interrupted", "short_control", timeout=15),
             Case("F11.save", "save", "SAVE\nFAULT", True, timeout=30),
             Case("W01.NTC1", "ntc_warning", parameters={"channel": 3}, timeout=20),
             Case("W01.NTC2", "ntc_warning", parameters={"channel": 4}, timeout=20),
             Case("W01.both", "ntc_warning", parameters={"channel": -1}, timeout=20),
             Case("W01.late-disconnect", "ntc_late", timeout=20),
             Case("W02.high-input", "high_input", timeout=20),
             Case("L01.thermal", "thermal_limit"),
             Case("L02.low-input", "low_limit"),
             Case("L03.current", "current_limit"),
             Case("R01.foreground", "reset", parameters={"command": "hang"}),
             Case("R04.HardFault", "reset", parameters={"command": "hardfault"})]
    for name, command in (("base", "tip_base"), ("pulse", "tip_pulse")):
        cases.append(Case("F01.init." + name, "boot_fault", "TIP\nFAULT", True, {"injection": command}))
    for command in ("oscillator", "clock_switch", "clock_ready"):
        cases.append(Case("F03.boot." + command, "boot_fault", "CLOCK\nFAULT", True, {"injection": command}))
    for mode, minimum, cells in ((1, 3100, 4), (2, 3300, 4), (3, 3100, 4),
                                  (4, 3300, 4), (5, 2500, 6), (6, 2750, 6)):
        # Lipo's 4S cutoff is below 14 V. Use 5S for a visible eligible cutoff;
        # supported-floor refusal is a separate native/boundary contract.
        count = max(cells, ceil(14500 / minimum))
        cases.append(Case(f"F06.cutoff.mode{mode}", "battery_cutoff", parameters={"mode": mode, "cells": count}, timeout=20))
    for channel, name in ((3, "NTC1"), (4, "NTC2"), (5, "MCU")):
        cases.append(Case("F08.sensor." + name, "sensor", "CIRKT\nTOO\nHOT\nFAULT", True, {"channel": channel}))
        cases.append(Case("F08.sensor-control." + name, "sensor_control", parameters={"channel": channel}))
    for reference in (2300, 4400):
        cases.append(Case(f"F09.reference.{reference}", "reference", "VOLT\nREF\nFAULT", True, {"reference": reference}))
    for mode in range(3):
        cases.append(Case(f"L03.power.mode{mode}", "power_limit", parameters={"power": mode},
                          note="Default RF duty cycle; wattage alone must not activate buck PWM"))
    for command in ("adc_init", "adc_calibration", "adc_channel", "adc_vref", "adc_start", "oled_init", "graphics"):
        family = "R03" if command in ("oled_init", "graphics") else "R02"
        cases.append(Case(f"{family}.{command}", "boot_reset", parameters={"injection": command}, timeout=15))
    for id, note in (
        ("F01.glitch", "Exact physical debounce pulses/races: native tip suite; electrical latency needs probes."),
        ("F04.exact-705", "Native ADC suite exercises raw counts that the logarithmic FIFO cannot represent."),
        ("F05.exact-boundaries", "Native ADC/history suite exercises fast/trend boundaries and warm-up."),
        ("F06.override-boundaries", "Native button/battery suite checks 13999/14000, all chemistry cutoffs, and release bounce."),
        ("F08.exact-duration", "Exact 110/111 C, 999/1000 ms and tick wrap require native supervision tests."),
        ("F10.exact-duration", "Exact 5800/5801 mA, 4999/5000 ms and tick wrap require native supervision tests."),
        ("F11.flash-errors", "Actual nvm.c erase/program/readback/torn-write failures run in the native RAM flash model."),
        ("R03.watchdog-init", "Actual watchdog_init() propagates HAL startup failure; no automatic recovery is promised.")):
        cases.append(Case(id, "native", level="native", note=note))
    for id, note in (("F01.electrical", "Measure PA5 physical detection and PB1 shutdown latency."),
                     ("F03.crystal", "Real HSE loss, CSS NMI and HSI fallback need a physical clock fixture."),
                     ("R04.break", "CPU-lockup/SRAM-parity-to-TIM1 break needs physical output evidence."),
                     ("R01.pin-reset", "Scope PB1 and Q2 gate through a hang, HardFault and watchdog reset."),
                     ("L03.closed-loop", "Synthetic power is an open-loop plant; qualify real buck regulation electrically.")):
        cases.append(Case(id, "electrical", level="electrical", note=note))
    return cases


NTC_COUNTS = (960, 922, 870, 803, 723, 634, 543, 455, 374, 305, 246, 198, 160, 129, 105, 85)
BATTERY_MINIMUM = {1: 3100, 2: 3300, 3: 3100, 4: 3300, 5: 2500, 6: 2750}


class Stimulus:
    """Predict settled engineering values from actual firmware LUT entries.

    Sequences are expressed in complete ADC rounds. None of the threshold
    decisions below relies on a universal rounds-per-second approximation.
    """
    def __init__(self, table, calibration):
        self.table = tuple(table)
        self.calibration = calibration
        self.vref = self.nearest((calibration["vref_cal"] + 2) // 4)
        self.reference_mv = calibration["vref_cal"] * 3300 // (self.vref * 4)
        self.base = (self.voltage_count(24000), self.voltage_count(20000), 0, self.nearest(870), self.nearest(870),
                     self.nearest((calibration["mcu_cal"] + 2) // 4), self.vref)
        values = self.predict(self.base)
        if not 3000 <= self.reference_mv <= 3600 or any(t > 60 for t in values["temperatures"]):
            raise ValueError("Factory calibration/LUT cannot provide an isolated healthy baseline")

    def nearest(self, value):
        return min(self.table, key=lambda x: (abs(x - value), x))

    def count_matching(self, prediction, predicate, requested):
        candidates = [raw for raw in self.table if predicate(prediction(raw))]
        if not candidates:
            raise ValueError("Requested threshold has no representable isolated stimulus")
        return min(candidates, key=lambda raw: (abs(prediction(raw) - requested), raw))

    def voltage(self, raw, reference=None):
        return (raw * (reference or self.reference_mv) * 11 + 511) // 1023

    def voltage_count(self, millivolts, *, side=None):
        predicate = (lambda x: x < millivolts) if side == "below" else (lambda x: x >= millivolts) if side == "above" else (lambda x: True)
        return self.count_matching(self.voltage, predicate, millivolts)

    def current(self, raw, reference=None):
        full_scale = (6037 * (reference or self.reference_mv) + 1650) // 3300
        return (raw * full_scale + 511) // 1023

    def current_count(self, milliamps, *, above=False):
        return self.count_matching(self.current, lambda x: x > milliamps if above else True, milliamps)

    @staticmethod
    def ntc(raw):
        if raw >= NTC_COUNTS[0]:
            return 0
        for i in range(1, len(NTC_COUNTS)):
            if raw >= NTC_COUNTS[i]:
                span = NTC_COUNTS[i - 1] - NTC_COUNTS[i]
                return (i - 1) * 10 + ((NTC_COUNTS[i - 1] - raw) * 10 + span // 2) // span
        return 150

    def mcu(self, raw, reference=None):
        normalized = ((raw * 4) * (reference or self.reference_mv) + 1650) // 3300
        delta = (self.calibration["mcu_cal"] - normalized) * 33000
        change = (abs(delta) + (4095 * 43) // 2) // (4095 * 43)
        return max(0, 30 + change if delta >= 0 else 30 - change)

    def temperature_count(self, channel, degrees):
        prediction = self.mcu if channel == 5 else self.ntc
        return self.count_matching(prediction, lambda x: x > 110 if degrees > 110 else x <= 110, degrees)

    def frame(self, *, dc=None, buck=None, current=None, temperature=None, channel=3, reference=None, repeats=1):
        values = list(self.base)
        if dc is not None:
            values[0] = dc
        if buck is not None:
            values[1] = buck
        if current is not None:
            values[2] = current
        if temperature is not None:
            values[channel] = self.temperature_count(channel, temperature)
        if reference is not None:
            # Keep DC/buck/current/MCU engineering values healthy at the changed
            # reference. External NTC conversion is ratiometric and unchanged.
            values[6] = self.nearest(self.calibration["vref_cal"] * 3300 / (reference * 4))
            predicted = self.calibration["vref_cal"] * 3300 // (values[6] * 4)
            values[0] = self.nearest(24000 * 1023 / (predicted * 11))
            values[1] = self.voltage_count(12000)  # Raw-safe at both references.
            values[5] = max(self.base[5], self.nearest(self.calibration["mcu_cal"] * 3300 / (predicted * 4)))
        return Frame(tuple(self.nearest(value) for value in values), repeats)

    def predict(self, counts):
        reference = self.calibration["vref_cal"] * 3300 // (counts[6] * 4) if counts[6] else 0
        dc, buck = self.voltage(counts[0], reference), self.voltage(counts[1], reference)
        current = self.current(counts[2], reference)
        return {"dc_mv": dc, "buck_mv": buck, "current_ma": current,
                "power_mw": (buck * current + 500) // 1000, "reference_mv": reference,
                "temperatures": [self.ntc(counts[3]), self.ntc(counts[4]), self.mcu(counts[5], reference)]}

    def ramp(self, start, end, *, plateau_rounds=1280, max_drop=7, **overrides):
        # Each transition is separated by ten history publications. A falling
        # input cannot satisfy three consecutive declining 2-record segments.
        begin, finish = self.table.index(self.nearest(start)), self.table.index(self.nearest(end))
        direction = 1 if finish >= begin else -1
        frames = []
        index = begin
        while index != finish:
            next_index = index + direction
            while (next_index + direction - finish) * direction <= 0 and abs(self.table[next_index + direction] - self.table[index]) <= max_drop:
                next_index += direction
            index = next_index
            frames.append(self.frame(dc=self.table[index], repeats=plateau_rounds, **overrides))
        if not frames:
            frames = [self.frame(dc=self.table[index], repeats=plateau_rounds, **overrides)]
        if len(frames) > 128:
            raise ValueError("Isolated ramp exceeds the agreed FIFO capacity")
        return frames


def trend_matches(history):
    return len(history) >= 7 and history[6] - history[0] >= 8 and all(history[i + 2] - history[i] >= 2 for i in (0, 2, 4))


def modeled_history(frames, start_round, baseline, history_count=7):
    """All exact rounded 128-round windows possible during a bounded waveform."""
    previous = [baseline] * history_count
    samples = [baseline] * (start_round % 128)
    result = []
    round_index = start_round
    for frame in frames:
        for _ in range(frame.repeat_rounds):
            samples.append(frame.channels[0])
            round_index += 1
            if round_index % 128 == 0:
                mean = (sum(samples) + len(samples) // 2) // len(samples)
                previous = [mean] + previous[:history_count - 1]
                result.append(tuple(previous))
                samples = []
    return result
