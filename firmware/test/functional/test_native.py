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
        for name in ("main_text", "main_sleep", "format_voltage", "format_integer"):
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
                self.lib.fan_step(10099, threshold - 3)
                self.assertTrue(self.lib.fan_value(6))
                self.lib.fan_step(10100, threshold - 2)
                self.assertTrue(self.lib.fan_value(6))
                self.lib.fan_step(10101, threshold - 3)
                self.assertFalse(self.lib.fan_value(6))
                self.lib.fan_step(15001, threshold - 3)  # Finish a possible 100% ramp-down.
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
        for index, threshold in enumerate((1000, 2000, 5000, 10000, 20000, 30000, 40000)):
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
        self.assertEqual([self.lib.default_value(i) for i in range(9)], [0,5,0,0,3,0,1,0,0])
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
        finish = source.index("\nvoid HAL_I2C_MspInit", start)
        unit = BUILD / "oled_dim_unit.c"
        unit.write_text('#include "oled.h"\n' + source[start:finish] +
                        '\n#include "' + (ROOT / "test/functional/oled_bridge.inc").as_posix() + '"\n',
                        encoding="utf-8", newline="\r\n")
        dll = BUILD / f"oled-{os.getpid()}.dll"
        subprocess.run(["clang", "-shared", "-nostdlib", "-fuse-ld=lld", "-O1", "-Itest/faults/support",
                        "-Iinc", "-Ilib/u8g2/csrc", str(unit), "-o", str(dll), "-Wl,/noentry"], cwd=ROOT, check=True)
        cls.lib = ctypes.CDLL(str(dll))

    @covers("dim.delay")
    def test_real_contrast_commands_idempotence_and_transport_failure(self):
        self.lib.dim_reset()
        self.assertEqual(self.lib.dim_set(0), 1)
        self.assertEqual(self.lib.dim_value(2), 0)
        self.assertEqual(self.lib.dim_set(1), 1)
        self.assertEqual(self.lib.dim_value(1), 32)
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
