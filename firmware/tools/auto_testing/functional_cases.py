"""Functional contracts, independent of target observations.

Target parameters use the existing shared, physically inhibited image. Native
IDs name actual C companions; electrical and undecided requirements stay
visible in the manifest rather than being replaced by software PASS results.
"""
from dataclasses import dataclass, field


@dataclass(frozen=True)
class FunctionalCase:
    id: str
    kind: str
    parameters: dict = field(default_factory=dict)
    level: str = "target"
    note: str = ""
    long: bool = False
    timeout: float = 15


def catalog():
    cases = []

    def add(identifier, kind, **parameters):
        cases.append(FunctionalCase(identifier, kind, parameters))

    for power in range(3):
        add(f"FUN.boot.healthy.{power}", "healthy", power=power)
        add(f"FUN.button.mode.{power}", "mode", power=power)
        add(f"FUN.power.request.{power}", "power", power=power)
        add(f"FUN.power.preference.{power}", "preference", power=power)
    for behavior in ("early", "setup"):
        add(f"FUN.boot.hold.{behavior}", "hold", behavior=behavior)
    add("FUN.settings.runtime", "settings")
    add("FUN.boot.random", "random")
    for duration in (850, 1150):
        add(f"FUN.button.edges.{duration}", "edge", duration=duration)
    add("FUN.button.repeat", "repeat")
    add("FUN.idle.threshold.saved", "idle")
    for dim in range(4):
        add(f"FUN.dim.delay.{dim}", "dim", dim=dim)
    add("FUN.sleep.manual", "sleep")
    add("FUN.sleep.display", "sleep_display")
    for mode, seconds in ((0, 310), (1, 300), (2, 900), (3, 1800)):
        cases.append(FunctionalCase(f"FUN.sleep.auto.{mode}", "auto_sleep", {"mode": mode, "seconds": seconds},
                                    long=mode != 1, timeout=seconds + 30))
    cases.append(FunctionalCase("FUN.sleep.activity", "sleep_activity", long=True, timeout=650))
    for calibration in range(11):
        add(f"FUN.adc.calibration.{calibration}", "calibration", calibration=calibration)
    add("FUN.adc.conversions", "conversions")
    add("FUN.adc.power", "adc_power")
    for mode in range(1, 7):
        add(f"FUN.battery.guess.{mode}", "battery", mode=mode)
    add("FUN.battery.cutoff.disabled", "healthy")
    add("FUN.battery.reboot", "battery_reboot")
    for kind in ("voltage", "graph", "peaks", "ceiling", "service", "static", "hot"):
        add(f"FUN.display.{kind}", "display_" + kind)
    add("FUN.uart.permission.suppressed", "swd")
    cases.append(FunctionalCase("FUN.endurance", "endurance", long=True, timeout=650))

    # Per-method native results are mapped to these IDs, never inferred from
    # an unrelated suite's aggregate success. Exact controls remain separate.
    for family in ("boot.wait", "boot.hold", "boot.splash", "boot.random", "settings.defaults", "settings.journal",
                   "button.mode", "button.edges", "button.repeat", "power.request", "power.preference",
                   "idle.threshold", "dim.delay", "sleep.auto", "sleep.display", "display.voltage",
                   "display.graph", "display.peaks", "display.ceiling", "display.service", "display.static",
                   "display.hot", "adc.conversions", "adc.sensors", "adc.power", "adc.calibration",
                   "battery.guess", "battery.cutoff", "fan.fixed", "fan.binary", "fan.adaptive", "fan.sources",
                   "fan.hardware", "outputs.production", "uart.permission", "uart.payload", "components", "time.wrap"):
        cases.append(FunctionalCase(f"FUN.{family}.native", "native", {"family": family}, level="native"))
    cases.append(FunctionalCase("FUN.power.plant", "blocked", level="deferred",
                                note="Optional responsive-plant model deferred; fixed-waveform regulation has separate cases."))
    cases.append(FunctionalCase("FUN.boot.wait.requirement", "blocked", level="requirement",
                                note="Unresolved wording: design says up to 300 ms; code permits a two-second wait. Native tests characterize code."))
    for family in ("rf", "buck", "fan", "uart", "physical_inputs", "usb65w", "splash", "analog_accuracy"):
        cases.append(FunctionalCase(f"FUN.electrical.{family}", "blocked", level="electrical",
                                    note="Requires a separate physical fixture/procedure; shared image keeps outputs inhibited."))
    return cases


MODE_SEQUENCES = {0: (1, 2, 1, 0, 1), 1: (2, 1, 0, 1), 2: (1, 0, 1, 2)}
CALIBRATION = ((1024, 0), (1029, 5), (1034, 10), (1039, 15), (1044, 20), (1050, 25),
               (1019, -5), (1014, -10), (1009, -15), (1004, -20), (998, -25))
CELL_RANGES = {1: (3100, 4200, 4, 8), 2: (3300, 4200, 4, 8),
               3: (3100, 4350, 4, 8), 4: (3300, 4350, 4, 8),
               5: (2500, 3650, 6, 9), 6: (2750, 3650, 6, 9)}


def calibrate(millivolts, selection):
    slope, offset = CALIBRATION[selection]
    return max(0, min(65535, ((millivolts * slope + 512) >> 10) + offset))


def guess_cells(millivolts, mode):
    """Independent healthy-pack oracle; invalid ranges are exercised in C tests."""
    minimum, maximum, floor, ceiling = CELL_RANGES[mode]
    optimistic = max(floor, (millivolts + maximum - 1) // maximum)
    if optimistic > ceiling or millivolts // minimum < optimistic:
        raise ValueError("Functional healthy-pack request falls outside a supported range")
    return optimistic
