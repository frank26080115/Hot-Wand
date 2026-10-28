"""Actual-C functional component tests. No board, probe or camera is used.

Decorators identify the precise report families supported by each test. The
main bridge runs actual helpers and the unchanged main loop with peripheral
mocks; target cases cover MCU integration and electrical cases remain separate.
"""
import ctypes
import os
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / ".cache/functional-native"


def covers(*families):
    def decorate(function):
        function.functional_families = families
        return function
    return decorate


def library(name, *, support="faults", flags=(), memory=True):
    if name in ("main", "message", "recovery", "storage") and not any("OLED_DIMMING_ENABLED=" in f for f in flags):
        flags = ("-DOLED_DIMMING_ENABLED=1", *flags)  # Explicit positive-feature fixtures.
    if name == "power" and not any("PWRLVL_PWM_JUMP_LEVEL=" in flag for flag in flags):
        flags = ("-DPWRLVL_PWM_JUMP_LEVEL=16", *flags)  # Fixed fixture tuning; preserve bench experiments.
    if name == "power" and not any("PWRLVL_POWER_LIMIT_ENABLED=" in flag for flag in flags):
        flags = ("-DPWRLVL_POWER_LIMIT_ENABLED=1", *flags)  # Legacy wattage regression fixture.
    if name in ("power", "adc"):
        for feature in ("LEAKY_BUCKET_ENABLED", "PWRMGT_POWER_LOSS_SUSTAINED_ENABLED"):
            if not any(feature + "=" in flag for flag in flags):
                flags = (f"-D{feature}=1", *flags)
    BUILD.mkdir(parents=True, exist_ok=True)
    destination = BUILD / f"{name}-{'-'.join(flags).replace('=', '')}-{os.getpid()}.dll"
    command = ["clang", "-shared", "-nostdlib", "-fuse-ld=lld", "-O1", "-fno-builtin",
               "-Wno-pointer-to-int-cast", "-ffunction-sections", "-fdata-sections",
               "-DHOT_WAND_TARGET_STM32F042", f"-Itest/{support}/support", "-Iinc", "-Ilib/u8g2/csrc",
               *flags, f"test/functional/{name}_bridge.c"]
    if memory:
        command.append("test/functional/memory.c")
    if name == "adc":
        # Only the factory calibration memory address is mapped to native RAM.
        # Conversion/filter/ISR source text is otherwise compiled unchanged.
        adc = (ROOT / "src/adc.c").read_text(encoding="utf-8")
        adc = adc.replace("#define MCU_TEMP_CAL1_ADDRESS    0x1FFFF7B8",
                          "#define MCU_TEMP_CAL1_ADDRESS ((uintptr_t)&native_factory_cal)")
        mapped = BUILD / "adc_mapped.c"
        mapped.write_text("#include <stdint.h>\nstatic const uint16_t native_factory_cal = 1480;\n" + adc,
                          encoding="utf-8", newline="\r\n")
        bridge = (ROOT / "test/adc_sim/bridge.c").read_text(encoding="utf-8")
        for source_name in ("adc", "adc_sim", "pwrmgt"):
            actual = mapped if source_name == "adc" else ROOT / f"src/{source_name}.c"
            bridge = bridge.replace(f'"../../../src/{source_name}.c"', '"' + actual.as_posix() + '"')
        unit = BUILD / "adc_normal_unit.c"
        unit.write_text((ROOT / "test/functional/adc_bridge.c").read_text(encoding="utf-8").replace(
                        '#include "../adc_sim/bridge.c"', bridge), encoding="utf-8", newline="\r\n")
        command[command.index("test/functional/adc_bridge.c")] = str(unit)
    subprocess.run(command + ["-o", str(destination), "-Wl,/noentry", "-Wl,/opt:ref"], cwd=ROOT, check=True)
    return ctypes.CDLL(str(destination))


class MainTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = library("main")
        for name in ("main_text", "main_sleep", "main_row", "format_voltage", "format_integer"):
            getattr(cls.lib, name).restype = ctypes.c_char_p

    def setUp(self):
        self.lib.main_reset(0)

    @covers("boot.wait")
    def test_stability_is_no_new_maximum_and_timeout_continues(self):
        self.assertEqual(self.lib.main_wait(0), 100)
        self.lib.main_reset(0)
        self.assertEqual(self.lib.main_wait(2), 100)  # Falling voltage also qualifies.
        self.lib.main_reset(0)
        self.assertEqual(self.lib.main_wait(1), 2000)
        self.assertGreaterEqual(self.lib.main_value(2), 2000)
        self.assertEqual(self.lib.main_text(), b".....")

    @covers("boot.hold")
    def test_boot_hold_release_and_entry_use_actual_helper(self):
        self.lib.main_hold(1000)
        self.assertEqual(self.lib.main_value(3), 0)
        self.assertGreaterEqual(self.lib.main_value(4), 1050)
        self.lib.main_reset(0)
        self.lib.main_hold(4000)
        self.assertEqual(self.lib.main_value(3), 1)
        self.assertEqual(self.lib.main_value(4), 3000)

    @covers("display.hot", "display.voltage")
    def test_hot_alternates_one_second_only_for_temperature_reason(self):
        for now, reasons, text in ((0, 1, b"24.0V"), (999, 1, b"24.0V"), (1000, 1, b"!HOT!"),
                                   (1999, 1, b"!HOT!"), (2000, 1, b"24.0V"), (3000, 2, b"24.0V"),
                                   (3000, 4, b"24.0V"), (3000, 3, b"!HOT!"), (3000, 0, b"24.0V")):
            with self.subTest(now=now, reasons=reasons):
                self.lib.main_render(now, reasons)
                self.assertEqual(self.lib.main_text(), text)
        self.assertEqual(self.lib.main_value(1), 9)  # Graph still serviced in either phase.

    @covers("boot.splash", "display.service")
    def test_splash_expiry_allows_normal_renderer(self):
        self.lib.main_splash(0, 1)
        self.lib.main_render(1999, 0)
        self.assertEqual(self.lib.main_value(0), 0)
        self.lib.main_render(2000, 0)
        self.assertEqual(self.lib.main_value(0), 1)
        self.assertEqual(self.lib.main_text(), b"24.0V")
        self.lib.main_render(2001, 0)
        self.assertEqual(self.lib.main_value(0), 2)

    @covers("sleep.auto", "dim.delay", "sleep.display")
    def test_actual_delay_tables_and_sleep_terminal_delegation(self):
        self.assertEqual([self.lib.main_delay(0, i) for i in range(4)], [0, 300000, 900000, 1800000])
        self.assertEqual([self.lib.main_delay(1, i) for i in range(4)], [0, 15000, 30000, 60000])
        self.assertEqual(self.lib.main_sleep(), b"ZZZZZ")

    @covers("components", "display.voltage", "boot.random")
    def test_formatter_carries_checksum_vectors_and_deterministic_prng(self):
        for value, expected in ((0, b"0.0"), (24049, b"24.0"), (24050, b"24.1"), (9999, b"10.0")):
            self.assertEqual(self.lib.format_voltage(value), expected)
        for value, base, expected in ((-2147483648,10,b"-2147483648"), (255,16,b"ff"),
                                      (31,2,b"11111"), (35,36,b"z"), (1,1,b""), (1,37,b"")):
            self.assertEqual(self.lib.format_integer(value, base), expected)
        self.assertEqual(self.lib.utility_checksum(b"abcde", 5) & 65535, 0xC8F0)
        self.assertEqual(self.lib.utility_checksum(b"", 0), 0)
        first = self.lib.utility_random(1234)
        self.assertEqual(self.lib.utility_random(1234), first)
        # A nonzero seed does not imply every PRNG output is nonzero.
        self.assertEqual(self.lib.utility_random(0), 0)

    @covers("time.wrap")
    def test_boot_wait_and_splash_across_tick_wrap(self):
        self.lib.main_reset(0xFFFFFFC0)
        self.assertEqual(self.lib.main_wait(0), 100)
        self.lib.main_reset(0)
        self.lib.main_splash(0xFFFFFC18, 1)  # Started 1000 ms before wrap.
        self.lib.main_render(999, 0)
        self.assertEqual(self.lib.main_value(0), 0)
        self.lib.main_render(1000, 0)
        self.assertEqual(self.lib.main_value(0), 1)


    @covers("sleep.auto", "dim.delay", "time.wrap")
    def test_unchanged_main_loop_dim_sleep_and_activity_decisions(self):
        never = 0xFFFFFFFF
        self.assertEqual(self.lib.main_run(14999, 1, 0, 0, never, never, 0), 0)
        self.assertEqual(self.lib.main_loop_value(0), 0)
        self.lib.main_run(15001, 1, 0, 0, never, never, 0)
        self.assertEqual(self.lib.main_loop_value(0), 1)
        self.lib.main_run(20000, 1, 0, 0, 16000, never, 0)
        self.assertEqual(self.lib.main_loop_value(0), 0)
        for selection, delay in ((1,300000), (2,900000), (3,1800000)):
            self.assertEqual(self.lib.main_run(delay - 1, 0, selection, 0, never, never, 0), 0)
            self.assertEqual(self.lib.main_run(delay + 1, 0, selection, 0, never, never, 0), 1)
            self.assertEqual(self.lib.main_loop_value(3), delay)
        self.assertEqual(self.lib.main_run(1801000, 0, 0, 0, never, never, 0), 0)
        self.assertEqual(self.lib.main_run(310000, 0, 1, 0, 290000, never, 0), 0)
        self.assertEqual(self.lib.main_run(590001, 0, 1, 0, 290000, never, 0), 1)

    @covers("boot.splash", "display.service")
    def test_main_loop_splash_press_consumption_frame_rate_and_message_ownership(self):
        never = 0xFFFFFFFF
        self.lib.main_run(1000, 0, 0, 1, 10, 300, 0)
        self.assertEqual(self.lib.main_loop_value(2), 1)  # First dismisses; second changes mode.
        self.lib.main_run(1000, 0, 0, 0, never, never, 0)
        self.assertEqual(self.lib.main_value(0), 15)
        self.lib.main_run(1000, 0, 0, 0, never, never, 500)
        self.assertEqual(self.lib.main_value(0), 8)

    def press_debug(self, start=100, count=10, interval=100):
        for i in range(count):
            self.lib.main_debug_press((start + i * interval) & 0xFFFFFFFF)

    @covers("button.repeat", "display.debug")
    def test_debug_entry_latches_until_reboot(self):
        self.press_debug(count=9)
        self.assertEqual(self.lib.main_value(7), 0)
        self.lib.main_debug_press(1000)
        self.assertEqual(self.lib.main_value(7), 1)
        self.assertEqual(self.lib.main_value(8), 0)
        self.lib.main_debug_sample()
        self.lib.main_render(1000, 0)
        self.assertEqual(self.lib.main_value(1), 0)
        self.press_debug(start=1100, count=30)
        self.assertEqual(self.lib.main_value(7), 1)
        self.assertEqual(self.lib.main_value(8), 0)
        self.lib.main_render(4100, 0)
        self.assertEqual(self.lib.main_value(1), 0)
        self.lib.main_reset(0)
        self.lib.main_render(0, 0)
        self.assertEqual(self.lib.main_value(7), 0)
        self.assertEqual(self.lib.main_value(1), 1)

    @covers("button.repeat", "time.wrap", "display.debug")
    def test_debug_timeout_boundary_and_tick_wrap(self):
        for start in (100, 0xFFFFF000):
            for gap, enabled, count in ((5000, 1, 0), (5001, 0, 1)):
                with self.subTest(start=start, gap=gap):
                    self.lib.main_reset(start)
                    self.press_debug(start=start, count=9)
                    self.lib.main_debug_press((start + 800 + gap) & 0xFFFFFFFF)
                    self.assertEqual(self.lib.main_value(7), enabled)
                    self.assertEqual(self.lib.main_value(8), count)

    @covers("boot.splash", "boot.hold", "sleep.display", "display.debug")
    def test_splash_setup_and_long_press_do_not_complete_debug_gesture(self):
        self.lib.main_splash(0, 1)
        self.press_debug(count=10)
        self.assertEqual(self.lib.main_value(7), 0)
        self.assertEqual(self.lib.main_value(8), 9)
        self.lib.main_sleep()
        self.assertEqual(self.lib.main_value(8), 0)
        self.press_debug(start=1100, count=9)
        self.assertEqual(self.lib.main_value(7), 0)
        self.lib.main_reset(0)
        self.lib.main_hold(4000)
        self.assertEqual(self.lib.main_value(8), 0)
        self.assertEqual(self.lib.main_repeat_run(2000, 9, 100, 1000, 0), 1)
        self.assertEqual(self.lib.main_value(8), 0)
        self.assertEqual(self.lib.main_value(7), 0)

    @covers("display.debug")
    def test_debug_values_units_precision_and_thermistor_order(self):
        for values, expected in (
            ((20000, 3200, 64000, 42, 38), (b"20.0V", b"3.2A", b"64.0W", b"42C", b"38C")),
            ((0, 0, 0, 0, 0), (b"0.0V", b"0.0A", b"0.0W", b"0C", b"0C")),
            ((9999, 9950, 99950, 38, 42), (b"10.0V", b"10.0A", b"100.0W", b"38C", b"42C")),
            ((36300, 6037, 219143, 65535, 100), (b"36.3V", b"6.0A", b"219.1W", b"65535C", b"100C")),
        ):
            with self.subTest(values=values):
                self.lib.main_reset(0)
                self.press_debug()
                self.lib.main_debug_inputs(*values)
                self.lib.main_debug_sample()
                self.lib.main_render(1000, 0)
                self.assertEqual(self.lib.main_value(9), 14)
                self.assertEqual(tuple(self.lib.main_row(i) for i in range(3, 8)), expected)
                self.assertEqual(tuple(self.lib.main_row(i) for i in range(9, 14)), expected)
                self.assertEqual(self.lib.main_row(2), b"NOW")
                self.assertEqual(self.lib.main_row(8), b"MAX")

    @covers("display.debug", "display.voltage")
    def test_debug_input_voltage_is_prefixed_and_rounded_to_whole_volts(self):
        self.press_debug()
        for mv, expected in ((0, b">0V"), (19999, b">20V"), (20499, b">20V"),
                             (20500, b">21V"), (27999, b">28V"), (28499, b">28V"),
                             (28500, b">29V"), (65535, b">66V")):
            self.lib.main_debug_header(mv, 0)
            self.lib.main_render(1000, 0)
            self.assertEqual(self.lib.main_row(0), expected)
            self.assertEqual(self.lib.main_value(11), mv)

    @covers("display.debug", "power.preference")
    def test_debug_mode_header_uses_applied_power_level(self):
        self.press_debug()
        for mode, expected in ((0, b"M:100"), (1, b"M:75"), (2, b"M:50")):
            self.lib.main_debug_header(20000, mode)
            self.lib.main_render(1000, 0)
            self.assertEqual(self.lib.main_row(1), expected)

    @covers("display.debug")
    def test_debug_baselines_font_and_pixel_shift_bounds(self):
        self.press_debug()
        self.lib.main_debug_inputs(36300, 6037, 100000, 65535, 38)
        self.lib.main_debug_sample()
        for x in range(8):
            for y in range(4):
                self.lib.main_shift(x, y)
                self.lib.main_render(1000, 0)
                for row in range(14):
                    width = len(self.lib.main_row(row)) * 5
                    self.assertEqual(self.lib.main_row_value(row, 0), min(x, 32 - width))
                    self.assertEqual(self.lib.main_row_value(row, 1), 9 + 8 * row + y)
                    self.assertEqual(self.lib.main_row_value(row, 2), 5)
                    self.assertLess(self.lib.main_row_value(row, 1) + 1, 128)

    @covers("display.debug", "display.hot", "display.service")
    def test_debug_preserves_readings_during_thermal_warning_and_delegates_voltage_overlay(self):
        self.press_debug()
        self.lib.main_debug_sample()
        for now, reason, hot in ((999, 1, False), (1000, 1, True), (1999, 1, True),
                                  (2000, 1, False), (3000, 2, False), (3000, 3, True)):
            self.lib.main_shift(7, 3)
            self.lib.main_render(now, reason)
            self.assertEqual(self.lib.main_row(0), b">24V")
            self.assertEqual(self.lib.main_row(7), b"38C")
            self.assertEqual(self.lib.main_row(13), b"38C")
            self.assertEqual(self.lib.main_value(9), 15 if hot else 14)
            if hot:
                self.assertEqual(self.lib.main_row(14), b"!HOT!")
                self.assertEqual(self.lib.main_row_value(14, 0), 7)
                self.assertEqual(self.lib.main_row_value(14, 1), 124)
                self.assertEqual(self.lib.main_row_value(14, 2), 5)
        self.assertEqual(self.lib.main_value(10), 6)
        self.assertEqual(self.lib.main_value(11), 24000)

    @covers("display.debug")
    def test_independent_peaks_use_calculated_sample_power_not_peak_voltage_times_peak_current(self):
        self.press_debug()
        # Peak voltage and peak current occur at different times. Their product
        # would invent 150 W; the largest actual sample here is only 50 W.
        for values in ((30000, 1000, 30000, 42, 38), (10000, 5000, 50000, 45, 37),
                       (20000, 2000, 40000, 43, 40)):
            self.lib.main_debug_inputs(*values)
            self.lib.main_debug_button(1000, 0, 0, 0)
        self.assertEqual([self.lib.main_debug_peak(i) for i in range(5)], [30000, 5000, 50000, 45, 40])
        self.lib.main_render(1000, 0)
        self.assertEqual([self.lib.main_row(i) for i in range(3, 8)],
                         [b"20.0V", b"2.0A", b"40.0W", b"43C", b"40C"])
        self.assertEqual([self.lib.main_row(i) for i in range(9, 14)],
                         [b"30.0V", b"5.0A", b"50.0W", b"45C", b"40C"])

    @covers("display.debug", "sleep.display")
    def test_long_press_zeros_peaks_keeps_now_and_resumes_accumulation(self):
        self.press_debug()
        self.lib.main_debug_inputs(20000, 3200, 64000, 42, 38)
        self.lib.main_debug_button(1100, 1, 0, 1)
        self.assertEqual(self.lib.main_value(7), 1)
        self.assertEqual([self.lib.main_debug_peak(i) for i in range(5)], [0] * 5)
        self.lib.main_render(1100, 0)
        self.assertEqual([self.lib.main_row(i) for i in range(9, 14)],
                         [b"0.0V", b"0.0A", b"0.0W", b"0C", b"0C"])
        self.assertEqual(self.lib.main_row(5), b"64.0W")
        self.lib.main_debug_button(1101, 1, 0, 0)
        self.assertEqual(self.lib.main_debug_peak(2), 64000)
        self.assertEqual(self.lib.main_repeat_run(1000, 10, 10, 200, 0), 0)
        self.assertEqual(self.lib.main_value(7), 1)

    @covers("display.debug", "sleep.auto", "dim.delay")
    def test_debug_disables_auto_sleep_but_keeps_dimming(self):
        for sleep in (1, 2, 3):
            self.assertEqual(self.lib.main_debug_idle_run(1900000, sleep, 1), 0)
            self.assertEqual(self.lib.main_value(7), 1)
            self.assertEqual(self.lib.main_loop_value(0), 1)

    @covers("display.debug", "display.service", "display.graph", "button.mode")
    def test_debug_main_loop_keeps_cadence_sampling_supervision_and_message_priority(self):
        self.lib.main_repeat_run(1000, 10, 10, 0, 0)
        self.assertEqual(self.lib.main_value(7), 1)
        self.assertEqual(self.lib.main_loop_value(2), 10)
        self.assertEqual(self.lib.main_value(0), 6)  # Two graph frames, then four debug frames.
        self.assertEqual(self.lib.main_value(12), 1000)
        self.assertEqual(self.lib.main_value(14), 900)
        self.assertEqual(self.lib.main_value(13), 1)
        self.assertEqual(self.lib.main_value(9), 14)
        self.lib.main_repeat_run(1000, 10, 10, 0, 500)
        self.assertEqual(self.lib.main_value(0), 3)  # Debug frames at 500, 700, and 900 ms.
        self.assertEqual(self.lib.main_value(9), 14)
        self.lib.main_repeat_run(1000, 10, 10, 0, 1000)
        self.assertEqual(self.lib.main_value(7), 1)
        self.assertEqual(self.lib.main_value(0), 0)
        self.assertEqual(self.lib.main_value(14), 900)
        self.assertEqual(self.lib.main_debug_peak(2), 64000)
        self.lib.main_repeat_run(1000, 20, 10, 0, 0)
        self.assertEqual(self.lib.main_value(7), 1)
        self.assertEqual(self.lib.main_loop_value(2), 20)
        self.assertEqual(self.lib.main_value(9), 14)

        self.lib.main_repeat_run(2000, 10, 10, 0, 0)
        self.assertEqual(self.lib.main_value(0), 11)  # Five additional debug frames in the second second.
        self.assertEqual(self.lib.main_value(14), 1900)  # Sampling still runs every foreground pass.


class PowerLossRecoveryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = library("recovery")

    def run_screen(self, voltage=24000, rise=0, dip=0xFFFFFFFF, length=0,
                   limit=3000, start=0, failure=0xFFFFFFFF, kind=0, options=0):
        return self.lib.recovery_run(start, voltage, rise, dip, length, limit, failure, kind, options)

    def test_exact_continuous_voltage_and_time_boundaries(self):
        self.assertEqual(self.run_screen(limit=999), 0)
        self.assertEqual(self.lib.recovery_value(7), 0)
        self.assertEqual(self.run_screen(limit=1001), 1)
        self.assertEqual(self.lib.recovery_value(0), 1000)
        self.assertEqual(self.run_screen(voltage=14000, limit=3000), 0)
        self.assertEqual(self.run_screen(voltage=13999, limit=3000), 0)
        self.assertEqual(self.run_screen(voltage=14001, limit=1001), 1)
        self.assertEqual(self.run_screen(rise=2000, limit=3001), 1)
        self.assertEqual(self.lib.recovery_value(0), 3000)

    def test_dip_resets_qualification_and_wrap_is_safe(self):
        self.assertEqual(self.run_screen(dip=999, length=1), 1)
        self.assertEqual(self.lib.recovery_value(0), 2000)
        self.assertEqual(self.run_screen(start=0xFFFFFE00), 1)
        self.assertEqual(self.lib.recovery_value(0), 1000)
        for _ in range(3):
            self.assertEqual(self.run_screen(), 1)
            self.assertEqual(self.lib.recovery_value(0), 1000)

    def test_outputs_stay_off_screen_feeds_watchdog_and_button_mode_is_preserved(self):
        self.assertEqual(self.run_screen(), 1)
        self.assertEqual(self.lib.recovery_value(1), 1)
        self.assertGreaterEqual(self.lib.recovery_value(2), 1000)
        self.assertGreaterEqual(self.lib.recovery_value(3), 5)
        self.assertEqual(self.lib.recovery_value(4), 0)
        self.assertEqual(self.lib.recovery_value(5), 1)
        self.assertEqual(self.lib.recovery_value(6), 0)
        self.assertEqual(self.lib.recovery_value(8), 1)
        self.assertEqual(self.lib.recovery_value(9), 0)
        self.assertEqual(self.run_screen(options=1), 1)  # Recover even without graphics.
        self.assertEqual(self.run_screen(options=2, limit=16000), 0)
        self.assertEqual(self.lib.recovery_value(11), 1)  # Saved dim policy still runs.
        self.assertEqual(self.run_screen(options=4 | 1), 1)  # Held entry gesture doesn't reset.

    def test_undervoltage_screen_uses_the_same_continuous_recovery_and_restart_gate(self):
        self.assertEqual(self.run_screen(voltage=14000, options=8), 0)
        self.assertEqual(self.lib.recovery_value(7), 0)
        self.assertEqual(self.run_screen(voltage=14001, limit=999, options=8), 0)
        self.assertEqual(self.run_screen(voltage=14001, limit=1001, options=8), 1)
        self.assertEqual(self.lib.recovery_value(0), 1000)
        self.assertEqual(self.lib.recovery_value(1), 1)
        self.assertEqual(self.lib.recovery_value(8), 1)
        self.assertEqual(self.run_screen(rise=2000, limit=3001, options=8), 1)
        self.assertEqual(self.lib.recovery_value(0), 3000)
        self.assertEqual(self.run_screen(dip=999, length=1, options=8), 1)
        self.assertEqual(self.lib.recovery_value(0), 2000)
        self.assertEqual(self.run_screen(start=0xFFFFFE00, options=8), 1)
        self.assertEqual(self.lib.recovery_value(0), 1000)
        self.assertEqual(self.run_screen(options=9), 1)  # No OLED graphics.

    def test_existing_manual_button_reset_remains_available(self):
        self.assertEqual(self.run_screen(options=16), 0)
        self.assertEqual(self.lib.recovery_value(4), 1)
        self.assertEqual(self.lib.recovery_value(7), 0)

    def test_independent_faults_preempt_and_cannot_rearm(self):
        for options in (0, 8):
            for kind in (1, 2, 3):
                self.assertEqual(self.run_screen(failure=100, kind=kind, limit=2000, options=options), 0)
                self.assertEqual(self.lib.recovery_value(7), 0)
                self.assertEqual(self.lib.recovery_value(1), 1)
                self.assertEqual(self.lib.recovery_value(6), 1)  # Entered terminal fault path.


class DebugButtonTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = library("debug_button", support="tipdetect_swd",
                          flags=("-DBTN_SWD_TEST_HARNESS=0", "-DGPIO_PULLUP=1", "-DGPIO_PIN_RESET=0"))

    def setUp(self):
        self.lib.debug_button_reset()

    def tick(self, elapsed):
        self.lib.bridge_tick(elapsed)
        self.lib.debug_button_poll()

    def edge(self, down):
        self.lib.bridge_edge(down)
        self.lib.debug_button_poll()

    @covers("display.debug", "button.edges", "button.repeat")
    def test_actual_entry_release_is_not_counted_twice_and_debug_shorts_are_on_release(self):
        for press in range(10):
            self.edge(1)
            self.tick(100)
            self.assertEqual(self.lib.debug_button_value(1), press + 1)
            self.edge(0)
            self.tick(50)
            self.assertEqual(self.lib.debug_button_value(1), press + 1)
        self.assertEqual(self.lib.debug_button_value(0), 1)
        self.assertEqual(self.lib.debug_button_value(3), 1)
        self.assertEqual(self.lib.debug_button_value(5), 10)
        for press in range(10):
            self.edge(1)
            self.tick(100)
            self.assertEqual(self.lib.debug_button_value(1), 10 + press)
            self.edge(0)
            self.tick(50)
            self.assertEqual(self.lib.debug_button_value(1), 11 + press)
        self.assertEqual(self.lib.debug_button_value(0), 1)

    @covers("display.debug", "button.edges", "sleep.display")
    def test_actual_long_hold_clears_without_power_selection_sleep_or_release_event(self):
        for _ in range(10):
            self.edge(1)
            self.tick(100)
            self.edge(0)
            self.tick(50)
        self.assertEqual(self.lib.debug_button_value(2), 20000)
        self.edge(1)
        self.tick(1000)
        self.assertEqual(self.lib.debug_button_value(2), 0)
        self.assertEqual(self.lib.debug_button_value(1), 10)
        self.assertEqual(self.lib.bridge_read(6), 0)  # No sleep/fault delegation.
        self.tick(1)
        self.assertEqual(self.lib.debug_button_value(2), 20000)
        self.edge(0)
        self.tick(50)
        self.assertEqual(self.lib.debug_button_value(1), 10)
        self.assertEqual(self.lib.debug_button_value(0), 1)

    @covers("display.debug", "button.edges")
    def test_holding_the_tenth_press_defers_mode_change_until_release(self):
        for _ in range(9):
            self.edge(1)
            self.tick(100)
            self.edge(0)
            self.tick(50)
        self.edge(1)
        self.tick(1000)
        self.assertEqual(self.lib.debug_button_value(0), 1)
        self.assertEqual(self.lib.debug_button_value(2), 0)
        self.assertEqual(self.lib.debug_button_value(3), 0)
        self.assertEqual(self.lib.debug_button_value(4), 1)
        self.edge(0)
        self.tick(50)
        self.assertEqual(self.lib.debug_button_value(1), 10)
        self.assertEqual(self.lib.debug_button_value(3), 1)


    @covers("display.debug", "button.edges", "time.wrap")
    def test_entry_waits_for_stable_release_through_bounce_and_tick_wrap(self):
        for wrap in (False, True):
            self.lib.debug_button_reset()
            if wrap:
                self.tick(0xFFFFFFFF - 1480)
            for _ in range(9):
                self.edge(1)
                self.tick(100)
                self.edge(0)
                self.tick(50)
            self.edge(1)
            self.tick(100)
            self.edge(0)
            self.tick(25)
            self.assertEqual(self.lib.debug_button_value(3), 0)
            self.edge(1)  # Release bounce belongs to the same entry press.
            self.tick(20)
            self.edge(0)
            self.tick(50)
            self.assertEqual(self.lib.debug_button_value(1), 10)
            self.assertEqual(self.lib.debug_button_value(3), 1)
            self.assertEqual(self.lib.debug_button_value(5), 10)


class FanTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = library("fan", flags=("-DFAN_STARTUP_OFF_TIME_MS=0",))
        cls.delayed = library("fan")
        cls.fallback = library("fan", support="functional", flags=("-DFAN_PWM_ENABLED=0", "-DFAN_STARTUP_OFF_TIME_MS=0"))

    @covers("fan.fixed")
    def test_fixed_duties_boost_and_ramped_profile(self):
        for mode, target in ((2, 25), (3, 50), (4, 75)):
            self.lib.fan_reset(mode, 0)
            self.lib.fan_step(0, 25)
            self.assertEqual(self.lib.fan_value(0), 100)
            self.lib.fan_step(499, 25)
            self.assertEqual(self.lib.fan_value(0), 100)
            self.lib.fan_step(500, 25)
            self.assertEqual(self.lib.fan_value(0), target)
        self.lib.fan_reset(1, 0)
        for now, expected in ((1000, 25), (2000, 50), (3000, 75), (4000, 100)):
            self.lib.fan_step(now, 25)
            self.assertEqual(self.lib.fan_value(0), expected)
        self.lib.fan_reset(0, 0)
        self.lib.fan_step(50000, 100)
        self.assertEqual(self.lib.fan_value(0), 0)

    @covers("fan.binary")
    def test_all_binary_profiles_thresholds_hysteresis_and_dwell(self):
        # conf.h is authoritative: verify equality and one degree below the
        # configured OFF boundary without assuming the former 2 C hysteresis.
        hysteresis = self.lib.fan_value(7)
        for mode, threshold, duty in ((5, 50, 100), (6, 70, 100), (7, 50, 50),
                                      (8, 70, 50), (9, 50, 25), (10, 70, 25)):
            with self.subTest(mode=mode):
                self.lib.fan_reset(mode, 0)
                self.lib.fan_step(0, threshold - 1)
                self.assertEqual(self.lib.fan_value(0), 0)
                self.lib.fan_step(100, threshold)
                self.assertTrue(self.lib.fan_value(6))
                self.lib.fan_step(4100, threshold)
                self.assertEqual(self.lib.fan_value(0), duty)
                self.lib.fan_step(10099, threshold - hysteresis - 1)
                self.assertTrue(self.lib.fan_value(6))
                self.lib.fan_step(10100, threshold - hysteresis)
                self.assertTrue(self.lib.fan_value(6))
                self.lib.fan_step(10101, threshold - hysteresis - 1)
                self.assertFalse(self.lib.fan_value(6))
                self.lib.fan_step(15001, threshold - hysteresis - 1)  # Finish a possible 100% ramp-down.
                self.assertEqual(self.lib.fan_value(0), 0)
                self.lib.fan_step(15002, threshold)
                self.assertFalse(self.lib.fan_value(6))
                self.lib.fan_step(20001, threshold)
                self.assertTrue(self.lib.fan_value(6))

    @covers("fan.adaptive")
    def test_all_gains_update_interval_and_clamping(self):
        for mode, expected in zip(range(11, 16), (44, 63, 81, 100, 100)):
            self.lib.fan_reset(mode, 0)
            self.lib.fan_step(0, 40)
            self.assertEqual(self.lib.fan_value(0), 100)  # Startup boost.
            self.lib.fan_step(500, 40)
            self.assertEqual(self.lib.fan_value(0), 25)
            self.lib.fan_step(999, 80)
            self.assertEqual(self.lib.fan_value(0), 25)
            self.lib.fan_step(1000, 80)
            self.assertEqual(self.lib.fan_value(0), expected)
            self.lib.fan_step(2000, 200)
            self.assertLessEqual(self.lib.fan_value(0), 100)

    @covers("fan.sources")
    def test_sensor_selection_stop_resume_topology_and_fallback(self):
        for sensor in (3, 4, 5):
            self.lib.fan_reset(7, 1)
            self.lib.fan_sensor(sensor, 60)
            self.lib.fan_step(0, 60 if sensor == 3 else 25)
            self.lib.fan_step(500, 60 if sensor == 3 else 25)
            self.assertEqual(self.lib.fan_value(0), 50)
            self.assertEqual(self.lib.fan_value(3), 1)
        self.lib.fan_reset(7, 0)
        self.lib.fan_bucket(200)
        self.lib.fan_step(0, 25)
        self.assertEqual(self.lib.fan_value(0), 0)  # Virtual bucket does not activate fan.
        self.lib.fan_control(0)
        self.lib.fan_step(10000, 90)
        self.assertEqual(self.lib.fan_value(0), 0)
        self.lib.fan_control(1)
        self.lib.fan_step(15000, 60)
        self.assertGreater(self.lib.fan_value(0), 0)
        for mode in range(16):
            self.fallback.fan_reset(mode, 0)
            self.assertEqual(self.fallback.fan_value(4), mode if mode in (0, 1, 5, 6) else 5)

    @covers("fan.fixed", "time.wrap")
    def test_startup_hold_does_not_claim_pin_early(self):
        self.delayed.fan_reset(3, 0)
        self.delayed.fan_step(4999, 25)
        self.assertEqual(self.delayed.fan_value(1), 0)
        self.delayed.fan_step(5000, 25)
        self.assertEqual(self.delayed.fan_value(1), 1)


class PowerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = library("power")
        cls.lib.normal_graph.restype = ctypes.POINTER(ctypes.c_uint8)
        cls.lib.normal_message.restype = ctypes.c_char_p
        cls.lib.normal_warning_text.restype = ctypes.c_char_p

    def setUp(self):
        self.lib.normal_reset(100)

    def step(self, now, power=0):
        self.lib.bridge_input(4, power)
        self.lib.bridge_step(now, 1)

    @covers("button.mode", "power.preference")
    def test_back_and_forth_selection_and_temporary_limit_preserve_preference(self):
        for expected in (1, 2, 1, 0, 1):
            self.lib.normal_press()
            self.step(200)
            self.assertEqual(self.lib.bridge_value(3), expected)
        self.lib.bridge_input(1, 85)
        self.step(300)
        self.assertEqual(self.lib.bridge_value(3), 2)
        self.lib.normal_press()
        self.assertEqual(self.lib.normal_value(0), 1)
        self.lib.bridge_input(1, 75)
        self.step(400)
        self.assertEqual(self.lib.bridge_value(3), 1)

    @covers("idle.threshold", "time.wrap")
    def test_all_idle_thresholds_equality_and_activity_timer(self):
        for index, threshold in enumerate((20000, 30000, 40000, 20000, 30000, 40000, 25000, 40000)):
            self.lib.normal_reset(100)
            self.lib.normal_threshold(index)
            self.step(200, threshold - 1)
            self.step(1200, threshold - 1)
            self.assertEqual(self.lib.normal_value(1), 1100)
            self.assertEqual(self.lib.normal_value(2), 1000)
            self.step(1300, threshold)
            self.assertEqual(self.lib.normal_value(1), 0)
            self.assertEqual(self.lib.normal_value(2), 0)
            self.step(2300)
            self.lib.normal_press()
            self.assertEqual(self.lib.normal_value(1), 0)
        self.lib.normal_reset(0xFFFFFF00)
        self.step(100)
        self.assertEqual(self.lib.normal_value(1), 356)

    @covers("power.request", "outputs.production")
    def test_attenuation_jump_fast_saturation_and_slow_release(self):
        self.step(102, 100001)
        self.assertEqual(self.lib.bridge_value(0), 16)
        for i in range(15): self.step(104 + i * 2, 100001)
        self.assertEqual(self.lib.bridge_value(0), 31)
        self.step(200, 0)  # Previous high sample still requests fast attenuation.
        self.step(249, 0)
        self.assertEqual(self.lib.bridge_value(0), 31)
        self.step(250, 0)
        self.assertEqual(self.lib.bridge_value(0), 30)

    @covers("power.request", "outputs.production")
    def test_configured_attenuation_steps_keep_jump_timing_saturation_and_slow_release(self):
        for step in (2, 31):
            lib = library("power", flags=(f"-DPWRLVL_RAMP_UP_STEP={step}",))
            for use_current in (False, True):
                with self.subTest(step=step, current_limit=use_current):
                    lib.normal_reset(100)
                    lib.bridge_input(4, 0 if use_current else 100001)
                    lib.bridge_input(0, 5200 if use_current else 0)
                    lib.bridge_step(102, 1)
                    self.assertEqual(lib.bridge_value(0), 16)  # Initial jump is unchanged.
                    lib.bridge_step(103, 1)
                    self.assertEqual(lib.bridge_value(0), 16)  # No step before 2 ms.
                    for i in range(1, 17):
                        lib.bridge_step(102 + 2 * i, 1)
                        self.assertEqual(lib.bridge_value(0), min(31, 16 + step * i))
                    lib.bridge_input(4, 0)
                    lib.bridge_input(0, 0)
                    lib.bridge_step(200, 1)  # Previous over-limit sample still requests attenuation.
                    lib.bridge_step(249, 1)
                    self.assertEqual(lib.bridge_value(0), 31)
                    lib.bridge_step(250, 1)
                    self.assertEqual(lib.bridge_value(0), 30)  # Release is still one count per 50 ms.

    @covers("power.request", "outputs.production")
    def test_limiter_tuning_controls_can_be_overridden_together(self):
        lib = library("power", flags=("-DPWRLVL_PWM_JUMP_LEVEL=8", "-DPWRLVL_RAMP_UP_STEP=3",
                                      "-DPWRLVL_RAMP_UP_PERIOD_MS=4", "-DPWRLVL_UPDATE_PERIOD_MS=60"))
        lib.normal_reset(100)
        lib.bridge_input(4, 100001)
        for now, expected in ((102, 0), (104, 8), (107, 8), (108, 11)):
            lib.bridge_step(now, 1)
            self.assertEqual(lib.bridge_value(0), expected)
        lib.bridge_input(4, 0)
        lib.bridge_step(112, 1)  # Previous sample still requests attenuation.
        self.assertEqual(lib.bridge_value(0), 14)
        lib.bridge_step(171, 1)
        self.assertEqual(lib.bridge_value(0), 14)
        lib.bridge_step(172, 1)
        self.assertEqual(lib.bridge_value(0), 13)

    @covers("power.preference", "outputs.production")
    def test_optional_bucket_and_sustained_loss_switches_are_independent(self):
        for bucket in (0, 1):
            for sustained in (0, 1):
                with self.subTest(bucket=bucket, sustained=sustained):
                    flags = (f"-DLEAKY_BUCKET_ENABLED={bucket}",
                             f"-DPWRMGT_POWER_LOSS_SUSTAINED_ENABLED={sustained}")
                    lib = library("power", flags=flags)
                    lib.bridge_fault.restype = ctypes.c_char_p
                    lib.normal_reset(100)
                    lib.bridge_input(10, 111)  # Virtual temperature only.
                    lib.bridge_step(100, 1)
                    lib.bridge_step(1100, 1)
                    self.assertEqual(lib.bridge_fault(), b"CIRKT\nTOO\nHOT\nFAULT" if bucket else None)
                    lib.normal_reset(100)
                    lib.bridge_input(11, 3)  # Three qualifying sustained-history windows.
                    lib.bridge_step(100, 1)
                    self.assertEqual(lib.bridge_value(10), 3 if sustained else 0)
                    lib.normal_reset(100)
                    lib.bridge_input(7, 1)  # Independent fast-ISR latch still trips.
                    lib.bridge_step(100, 1)
                    self.assertEqual(lib.bridge_value(10), 1)
                    lib.normal_reset(100)
                    lib.bridge_input(1, 81)  # Physical temperature still derates.
                    lib.bridge_step(100, 1)
                    self.assertEqual(lib.bridge_value(3), 2)
                    lib.bridge_input(1, 111)
                    lib.bridge_step(200, 1)
                    lib.bridge_step(1200, 1)
                    self.assertEqual(lib.bridge_fault(), b"CIRKT\nTOO\nHOT\nFAULT")

    @covers("display.graph", "display.ceiling")
    def test_real_graph_center_scaling_mirroring_and_xor_ceiling(self):
        self.step(200, 25000)
        pixels = bytes(self.lib.normal_graph()[:4096])
        self.assertEqual(sum(pixels[y * 32 + 15] for y in range(29, 128)), 25)
        self.assertEqual([pixels[y * 32 + 15] for y in range(29, 128)], [pixels[y * 32 + 16] for y in range(29, 128)])
        self.assertFalse(any(pixels[:28 * 32]))
        self.lib.normal_reset(100)
        for mode, row in ((0, 28), (1, 53), (2, 78)):
            self.lib.normal_mode(mode)
            self.step(200, 0)
            pixels = bytes(self.lib.normal_graph()[:4096])
            self.assertEqual(sum(pixels), 11)
            self.assertEqual(sum(pixels[row * 32:(row + 1) * 32]), 11)
            self.assertFalse(any(pixels[27 * 32:28 * 32]))  # Black gap above the full-power line.

    @covers("display.ceiling")
    def test_ceiling_dots_stay_fixed_across_history_updates(self):
        self.lib.normal_mode(1)
        self.step(200, 0)
        initial = bytes(self.lib.normal_graph()[53 * 32:54 * 32])
        pattern = bytes(int(bit) for bit in ("100" * 10 + "10"))
        self.assertEqual(initial, pattern)
        self.assertFalse(any(initial[x] and initial[x + 1] for x in range(31)))
        for now in (349, 350, 600, 850, 1100, 4350):
            self.step(now, 0)
            current = bytes(self.lib.normal_graph()[53 * 32:54 * 32])
            self.assertEqual(current, initial)
            self.assertEqual(sum(current), 11)
            self.assertFalse(current[15] and current[16])

    @covers("display.peaks")
    def test_interval_peak_and_history_wrap(self):
        self.step(200, 25000)
        self.step(250, 1000)
        self.assertEqual(self.lib.normal_peak(0), 25000)
        self.step(350, 0)
        self.assertEqual(self.lib.normal_peak(1), 25000)
        for now in range(600, 4600, 250): self.step(now, 0)
        self.assertTrue(all(self.lib.normal_peak(age) == 0 for age in range(16)))

    @covers("power.preference", "button.repeat")
    def test_blocked_feedback_waits_for_release_and_repeated_attempts_escalate(self):
        self.lib.bridge_input(1, 85)
        self.step(200)
        for attempt in range(5):
            now = 300 + attempt * 100
            self.lib.normal_button(1, 0, 1)
            self.lib.normal_press()
            self.step(now)
            self.lib.normal_button(0, 0, 0)
            self.step(now + 1)
            self.step(now + 50)
            if attempt == 0: self.assertIsNone(self.lib.normal_message())
            self.step(now + 51)
            self.assertEqual(self.lib.normal_message(), b"NOT\nNOW\nDUMB-\nASS" if attempt == 4 else b"CAN'T\nTOO\nHOT")
            self.assertEqual(self.lib.normal_duration(), 300)
        self.assertEqual(self.lib.normal_value(0), 0)

    @covers("button.repeat")
    def test_sixth_press_novelty_still_changes_power(self):
        self.lib.normal_button(0, 0, 6)
        self.lib.normal_press()
        self.assertEqual(self.lib.normal_value(0), 1)
        self.assertEqual(self.lib.normal_message(), b"SKILL\nISSUE\n  ?  ")
        self.assertEqual(self.lib.normal_duration(), 1000)

    @covers("button.repeat", "button.mode", "display.debug", "display.graph")
    def test_actual_debug_and_six_press_counters_are_independent_with_unchanged_power_cycle(self):
        for press in range(1, 21):
            now = 100 + press * 250
            self.assertEqual(self.lib.normal_debug_press(now), int(press >= 10))
            self.assertEqual(self.lib.normal_value(0), (1, 2, 1, 0)[(press - 1) % 4])
            self.assertEqual(self.lib.normal_value(4), press % 6)
            if press < 6:
                self.assertIsNone(self.lib.normal_message())
            else:
                self.assertEqual(self.lib.normal_message(), b"SKILL\nISSUE\n  ?  ")
            self.step(now, 25000)
        # Normal supervision continues updating real graph history while debug is latched.
        self.assertEqual(self.lib.normal_peak(1), 25000)
        self.assertTrue(any(self.lib.normal_graph()[:4096]))

    @covers("button.repeat", "power.preference", "display.debug")
    def test_actual_derating_counter_resets_do_not_block_debug_entry(self):
        for temperature, dc in ((85, 24000), (25, 18000)):
            self.lib.normal_reset(100)
            self.lib.bridge_input(1, temperature)
            self.lib.bridge_input(2, dc)
            self.step(200)
            for press in range(1, 11):
                self.assertEqual(self.lib.normal_debug_press(200 + press * 100), int(press == 10))
                self.assertEqual(self.lib.normal_value(0), 0)

    @covers("display.debug", "display.service")
    def test_real_high_voltage_overlay_owns_only_the_existing_bottom_region(self):
        self.assertEqual(self.lib.normal_warning(0, 31999), 0)
        for now, rows in ((100, 3), (599, 3), (600, 0), (1100, 3)):
            self.assertEqual(self.lib.normal_warning(now, 32000), rows)
            if rows:
                self.assertEqual([self.lib.normal_warning_text(i) for i in range(3)],
                                 [b"WARN", b"HIGH", b"VOLT"])
                self.assertEqual([self.lib.normal_warning_value(i) for i in range(3)], [107, 117, 127])
                self.assertTrue(all(self.lib.normal_warning_pixel(y) for y in range(98)))
                self.assertFalse(any(self.lib.normal_warning_pixel(y) for y in range(98, 128)))
        self.assertEqual(self.lib.normal_warning(1200, 31999), 0)


class ButtonBatteryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = library("button", support="tipdetect_swd", flags=("-DBTN_SWD_TEST_HARNESS=0", "-DGPIO_PULLUP=1", "-DGPIO_PIN_RESET=0"))

    @covers("button.edges", "button.repeat")
    def test_on_press_long_event_and_consecutive_timeout(self):
        self.lib.bridge_reset(0)
        self.lib.bridge_edge(1)
        self.assertEqual(self.lib.button_event(0), 1)
        self.lib.bridge_tick(999)
        self.assertEqual(self.lib.button_event(1), 0)
        self.lib.bridge_tick(1)
        self.assertEqual(self.lib.button_event(1), 1)
        self.assertEqual(self.lib.button_event(1), 0)
        self.lib.bridge_edge(0)
        self.lib.bridge_tick(50)
        self.assertEqual(self.lib.button_event(0), 0)
        for gap, expected in ((5000, 2), (5001, 1)):
            self.lib.bridge_reset(0)
            self.lib.bridge_edge(1)
            self.lib.bridge_tick(200)
            self.lib.bridge_edge(0)
            self.lib.bridge_tick(50)
            self.lib.bridge_tick(gap - 250)
            self.lib.bridge_edge(1)
            self.assertEqual(self.lib.button_shorts(), expected)

    @covers("battery.guess", "battery.cutoff")
    def test_all_chemistries_guess_ranges_and_exact_cutoff(self):
        for mode, low, high, floor in ((1,3100,4200,4),(2,3300,4200,4),(3,3100,4350,4),
                                        (4,3300,4350,4),(5,2500,3650,6),(6,2750,3650,6)):
            self.lib.bridge_reset(0)
            self.assertEqual(self.lib.battery_guess_value(floor * high, mode, 0), floor)
            self.assertEqual(self.lib.battery_guess_value(14000, mode, 0) >= floor, True)
            ceiling = 9 if mode >= 5 else 8
            for count in range(floor, ceiling + 1):
                self.assertEqual(self.lib.battery_guess_value(count * high, mode, 0), count)
            excessive = ceiling * high + 1
            guessed = self.lib.battery_guess_value(excessive, mode, 0)
            self.assertGreater(guessed, ceiling)
            self.lib.bridge_battery(guessed, mode, excessive)
            self.assertEqual(self.lib.battery_ok(), 0)
            self.lib.bridge_battery(floor, mode, floor * low)
            self.assertEqual(self.lib.battery_ok(), 1)
            self.lib.bridge_battery(floor, mode, floor * low - 1)
            self.assertEqual(self.lib.battery_ok(), 0)
        self.lib.bridge_battery(0, 0, 0)
        self.assertEqual(self.lib.battery_ok(), 1)


class AdcTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = library("adc", support="adc_sim", flags=("-DADC_SIM_ENABLED=1",))

    def setUp(self):
        self.lib.bridge_reset()

    @covers("adc.calibration", "adc.conversions", "adc.power")
    def test_affine_dc_only_calibration_current_and_buck_power(self):
        pairs = ((1024,0),(1029,5),(1034,10),(1039,15),(1044,20),(1050,25),
                 (1019,-5),(1014,-10),(1009,-15),(1004,-20),(998,-25))
        for channel, sample in ((0, 676), (1, 561), (2, 250)):
            self.lib.adc_normal_values(channel, sample)
        base = (676 * 3300 * 11 + 511) // 1023
        buck = self.lib.adc_normal_convert(0, 1)
        current = self.lib.adc_normal_convert(1, 2)
        for index, (slope, offset) in enumerate(pairs):
            self.lib.adc_normal_calibration(index)
            self.assertEqual(self.lib.adc_normal_convert(0, 0), ((base * slope + 512) >> 10) + offset)
            self.assertEqual(self.lib.adc_normal_convert(0, 1), buck)
            self.assertEqual(self.lib.adc_normal_convert(3, 0), (buck * current + 500) // 1000)
        self.lib.adc_normal_calibration(255)
        self.assertEqual(self.lib.adc_normal_convert(0, 0), base)
        self.assertEqual(self.lib.adc_normal_convert(0, 3), 0)

    @covers("adc.sensors")
    def test_ntc_table_endpoints_and_interpolation(self):
        for raw, temperature in ((960, 0), (922, 10), (870, 20), (803, 30), (85, 150), (0, 150), (941, 5)):
            self.lib.adc_normal_values(3, raw)
            self.assertEqual(self.lib.adc_normal_convert(2, 3), temperature)
        self.assertEqual(self.lib.adc_normal_convert(2, 0), 0)

    @covers("boot.random", "adc.conversions")
    def test_zero_entropy_simulation_seed_and_reference_scaling(self):
        self.lib.bridge_enqueue(0, 1)
        self.lib.bridge_start()
        for _ in range(30): self.lib.bridge_conversion(0)
        self.assertEqual(self.lib.bridge_value(6, 0), 1)
        self.lib.adc_normal_values(1, 500)
        self.lib.bridge_reference(330)
        self.assertEqual(self.lib.adc_normal_convert(0, 1), (500 * 3720 * 11 + 511) // 1023)

    @covers("adc.sensors")
    def test_mcu_factory_calibration_conversion(self):
        for raw, expected in ((350,45), (370,30), (400,8), (500,0)):
            self.lib.adc_normal_values(5, raw)
            self.assertEqual(self.lib.adc_normal_convert(2, 5), expected)


class StorageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = library("storage", memory=False)

    @covers("settings.defaults", "settings.journal", "components")
    def test_defaults_normal_journal_rollover_and_factory_reset_in_ram(self):
        self.lib.storage_reset()
        self.assertEqual([self.lib.default_value(i) for i in range(9)], [0,5,0,0,1,0,1,0,0])
        self.assertEqual(self.lib.storage_read(), 255)
        self.assertEqual(self.lib.storage_save(0), 1)
        calls = self.lib.storage_value(0)
        self.assertEqual(self.lib.storage_save(0), 1)
        self.assertEqual(self.lib.storage_value(0), calls)
        for index in range(175): self.assertEqual(self.lib.storage_save(index % 3), 1)
        self.lib.storage_reboot()
        self.assertEqual(self.lib.storage_read(), 174 % 3)
        self.assertEqual(self.lib.storage_erase(), 1)
        self.lib.storage_reboot()
        self.assertEqual(self.lib.storage_read(), 255)
        self.assertEqual(self.lib.storage_save(2), 1)
        self.lib.storage_reboot()
        self.assertEqual(self.lib.storage_read(), 2)
        self.assertEqual(self.lib.storage_watchdog(), 1)
        self.lib.storage_feed()
        self.assertEqual(self.lib.storage_value(4), 1)


class PeripheralTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.pwm = library("fan_pwm", support="functional")
        cls.uart = library("uart", support="functional")
        cls.swd = library("uart", support="functional", flags=("-DHOT_WAND_SWD_DEBUG=1",))
        cls.rf = library("rf", support="functional", flags=("-DNATIVE_OUTPUT_MODEL=1",))
        cls.uart.uart_text.restype = cls.swd.uart_text.restype = ctypes.c_char_p

    @covers("outputs.production")
    def test_rearming_power_loss_does_not_clear_other_rf_latches_or_start_outputs(self):
        self.assertEqual(self.rf.rf_rearm_case(8), 1)
        self.assertEqual(self.rf.rf_latches(), 0)
        for flags in (0, 9, 10, 12, 14):
            self.assertEqual(self.rf.rf_rearm_case(flags), 0)
            self.assertEqual(self.rf.rf_latches(), flags)

    @covers("fan.hardware")
    def test_actual_pwm_period_topology_and_clamping(self):
        for mode in (0, 1):
            self.assertEqual(self.pwm.pwm_init(mode), 1)
            self.assertEqual(self.pwm.pwm_value(1), 1084)
            for duty in (0, 25, 50, 75, 100, 200):
                self.pwm.pwm_set(duty)
                percent = min(duty, 100)
                expected = ((100 - percent if mode else percent) * 1085 + 50) // 100
                self.assertEqual(self.pwm.pwm_value(0), expected)
            before = self.pwm.pwm_value(0)
            self.assertEqual(self.pwm.pwm_init(9), 0)
            self.assertEqual(self.pwm.pwm_value(0), before)

    @covers("uart.permission", "uart.payload")
    def test_uart_permission_five_hz_payload_and_swd_suppression(self):
        self.uart.uart_reset()
        self.uart.uart_step(500, 0)
        self.assertEqual(self.uart.uart_count(), 0)
        self.uart.uart_step(199, 1)
        self.assertEqual(self.uart.uart_text(), b"")
        self.uart.uart_step(200, 1)
        text = self.uart.uart_text()
        self.assertIn(b"24.0V 20.0V VREFINT=3.30V 1.50A", text)
        self.assertTrue(text.endswith(b"100W 0x5\r\n"))
        self.uart.uart_options(2, 0)
        self.uart.uart_step(399, 1)
        self.assertEqual(self.uart.uart_text(), b"")
        self.uart.uart_step(400, 1)
        self.assertIn(b"VREFINT=NA", self.uart.uart_text())
        self.assertIn(b"50W", self.uart.uart_text())
        self.swd.uart_reset()
        self.swd.uart_step(1000, 1)
        self.assertEqual(self.swd.uart_count(), 0)
        self.assertEqual(self.swd.uart_text(), b"")

    @covers("outputs.production")
    def test_production_rf_start_stop_and_tip_gates(self):
        for present, qualifying in ((0,0), (1,1), (1,0)):
            self.rf.rf_reset(present, qualifying)
            self.assertEqual(self.rf.rf_run(0), int(present and not qualifying))
            self.assertEqual(self.rf.rf_run(1), 0)
        self.rf.rf_reset(1, 0)
        self.assertEqual(self.rf.rf_run(0), 1)
        self.rf.rf_run(2)
        self.assertEqual(self.rf.rf_run(0), 0)


if __name__ == "__main__":
    unittest.main()


class MessageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = library("message")

    @covers("display.service", "sleep.display", "time.wrap")
    def test_transient_ownership_refresh_expiry_and_saved_dim(self):
        for start in (0, 0xFFFFFF00):
            self.lib.message_reset(start)
            self.lib.message_start(1000)
            self.assertEqual(self.lib.message_step(start), 1)
            sends = self.lib.message_sends()
            self.assertEqual(self.lib.message_step((start + 99) & 0xFFFFFFFF), 1)
            self.assertEqual(self.lib.message_sends(), sends)
            self.assertEqual(self.lib.message_step((start + 100) & 0xFFFFFFFF), 1)
            self.assertEqual(self.lib.message_sends(), sends + 1)
            self.assertEqual(self.lib.message_step((start + 1000) & 0xFFFFFFFF), 0)
        self.assertEqual([self.lib.message_dim_delay(i) for i in range(4)], [0,15000,30000,60000])


class OledDimTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Compile the exact public function body from the current source. Only
        # its I2C/u8g2 boundary is mocked; no second dimming implementation.
        source = (ROOT / "src/oled.c").read_text(encoding="utf-8")
        start = source.index("bool OLED_SetDimMode(")
        finish = source.index("\n#endif", start)
        unit = BUILD / "oled_dim_unit.c"
        unit.write_text('#include "oled.h"\n' + source[start:finish] +
                        '\n#include "' + (ROOT / "test/functional/oled_bridge.inc").as_posix() + '"\n',
                        encoding="utf-8", newline="\r\n")
        dll = BUILD / f"oled-{os.getpid()}.dll"
        subprocess.run(["clang", "-shared", "-nostdlib", "-fuse-ld=lld", "-O1", "-Itest/faults/support",
                        "-Iinc", "-Ilib/u8g2/csrc", "-DOLED_DIMMING_ENABLED=1", str(unit), "-o", str(dll), "-Wl,/noentry"], cwd=ROOT, check=True)
        cls.lib = ctypes.CDLL(str(dll))

    @covers("dim.delay")
    def test_real_contrast_commands_idempotence_and_transport_failure(self):
        self.lib.dim_reset()
        self.assertEqual(self.lib.dim_set(0), 1)
        self.assertEqual(self.lib.dim_value(2), 0)
        self.assertEqual(self.lib.dim_set(1), 1)
        self.assertEqual(self.lib.dim_value(1), 8)
        self.assertEqual(self.lib.dim_value(0), 1)
        self.lib.dim_set(1)
        self.assertEqual(self.lib.dim_value(2), 1)
        self.lib.dim_fail(1)
        self.assertEqual(self.lib.dim_set(0), 0)
        self.assertEqual(self.lib.dim_value(0), 1)
        self.lib.dim_fail(0)
        self.assertEqual(self.lib.dim_set(0), 1)
        self.assertEqual(self.lib.dim_value(1), 255)
        self.assertEqual(self.lib.dim_value(0), 0)
