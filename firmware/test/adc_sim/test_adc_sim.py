"""Run with: python -m unittest discover -s test/adc_sim -v

Requires native Windows LLVM clang/lld-link; builds a no-CRT DLL in .cache.
These tests run firmware C, not a Python reimplementation of the FIFO/ISR.
Peripheral mocks do not establish physical shutdown timing or ST calibration.
"""

import ctypes
import math
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import unittest
from unittest.mock import patch
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/auto_testing"))
from adc_sim import AdcSimProducer, Frame, FIFO_OFFSET, FRAME_SIZE
from target_gdb import GdbSession


class NativeTarget:
    """SWD memory transport stand-in that reads/writes the native firmware DLL."""
    def __init__(self):
        self.running = True
        self.transactions = 0
        self.writes = []
        self.fail_write = None

    def inspect(self, callback):
        running = self.running
        if running:
            self.transactions += 1
        self.running = False
        try:
            return callback()
        finally:
            self.running = running

    def read_memory(self, address, length):
        return ctypes.string_at(address, length)

    def write_memory(self, address, data):
        self.writes.append((address, bytes(data)))
        if len(self.writes) == self.fail_write:
            raise IOError("injected SWD write failure")
        ctypes.memmove(address, data, len(data))

    def write_u32(self, address, value):
        self.write_memory(address, struct.pack("<I", value))


class AdcSimulationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        clang = shutil.which("clang")
        if sys.platform != "win32" or not clang or not shutil.which("lld-link"):
            raise RuntimeError("The native ADC harness requires Windows LLVM clang and lld-link on PATH")
        build = ROOT / ".cache/adc_sim_test"
        build.mkdir(parents=True, exist_ok=True)
        # A process-specific path also allows repeated runs without Windows
        # rejecting replacement of a DLL loaded in another test process.
        import os
        dll = build / f"bridge-{os.getpid()}.dll"
        subprocess.run([clang, "-shared", "-nostdlib", "-fuse-ld=lld", "-O1", "-ffunction-sections",
                        "-fdata-sections", "-DPWRMGT_BUCK_VOLTAGE_SPIKE_MV=25000", "-DADC_SIM_ENABLED=1", "-DPWRMGT_POWER_LOSS_SUSTAINED_ENABLED=1", "-DPWRMGT_POWER_LOSS_FAST_DROP_ADC_COUNTS=85", "-Itest/adc_sim/support", "-Iinc",
                        "-Ilib/u8g2/csrc", "test/adc_sim/bridge.c", "-o", str(dll),
                        "-Wl,/noentry", "-Wl,/opt:ref", "-Wl,/export:adc_sim",
                        "-Wl,/export:adc_sim_decode_table"], cwd=ROOT, check=True)
        cls.lib = ctypes.CDLL(str(dll))
        for name in ("bridge_start", "bridge_boundary", "bridge_enqueue"):
            getattr(cls.lib, name).restype = ctypes.c_bool
        cls.lib.bridge_boundary.argtypes = [ctypes.c_bool]
        cls.lib.bridge_enqueue.argtypes = [ctypes.c_uint8, ctypes.c_uint32]
        cls.lib.bridge_conversion.argtypes = [ctypes.c_uint16]
        cls.lib.bridge_conversion.restype = ctypes.c_uint32
        cls.lib.bridge_value.argtypes = [ctypes.c_uint32, ctypes.c_uint32]
        cls.lib.bridge_value.restype = ctypes.c_uint32
        cls.lib.bridge_latches.restype = ctypes.c_uint32
        cls.lib.bridge_history.argtypes = [ctypes.c_uint32]
        cls.lib.bridge_history.restype = ctypes.c_uint32
        cls.address = ctypes.addressof(ctypes.c_uint32.in_dll(cls.lib, "adc_sim"))
        cls.table_address = ctypes.addressof(ctypes.c_uint16.in_dll(cls.lib, "adc_sim_decode_table"))

    def setUp(self):
        self.lib.bridge_reset()
        self.target = NativeTarget()
        self.producer = AdcSimProducer(self.target, {
            "adc_sim": SimpleNamespace(address=self.address),
            "adc_sim_decode_table": SimpleNamespace(address=self.table_address)})

    def value(self, selector, channel=0):
        return self.lib.bridge_value(selector, channel)

    def word(self, offset, value):
        self.target.write_u32(self.address + offset, value)

    def frame(self, dc=676, buck=560, current=100, vref=372, repeats=1):
        return Frame((dc, buck, current, 870, 870, 360, vref), repeats)

    def begin(self, frames):
        reports = self.producer.enqueue(frames)
        self.assertTrue(self.lib.bridge_start())
        self.lib.bridge_init_adc()
        # The preliminary MCU/VREFINT conversions are not complete rounds.
        warmup = []
        while self.value(11) != 0:
            warmup.append(self.lib.bridge_conversion(1023))
        self.assertEqual(warmup, [5, 6])
        self.assertEqual(self.producer.status()["rounds_completed"], 0)
        return reports

    def round(self, hardware=1023):
        self.assertEqual(self.value(11), 0)
        channels = []
        while True:
            channels.append(self.lib.bridge_conversion(hardware))
            if self.value(11) == 0:
                return channels

    def test_decode_table_and_encoding(self):
        expected = tuple(math.floor(341 * (4 ** (c / 255) - 1) + 0.5) for c in range(256))
        self.assertEqual(self.producer.table, expected)
        self.assertEqual(len(set(expected)), 256)
        self.assertEqual((expected[0], expected[-1]), (0, 1023))
        for value in range(1024):
            payload, report = self.producer.encode(Frame((value,) * 7))
            self.assertEqual(len(payload), 12)
            self.assertEqual(report["codes"][0], min(range(256), key=lambda c: (abs(expected[c] - value), c)))
        with self.assertRaises(ValueError):
            Frame((0,) * 7, 0)
        with self.assertRaises(ValueError):
            Frame((1024,) * 7)

    def test_streaming_connection_never_asserts_reset(self):
        import target_gdb
        fake_tool = SimpleNamespace(is_file=lambda: True, is_dir=lambda: True)
        for reset in (False, True):
            session = GdbSession(None, {}, reset_on_connect=reset)
            with patch.object(target_gdb, "GDB_EXE", fake_tool), \
                 patch.object(target_gdb, "OPENOCD_EXE", fake_tool), \
                 patch.object(target_gdb, "OPENOCD_SCRIPTS", fake_tool), \
                 patch.object(target_gdb.subprocess, "Popen", side_effect=RuntimeError("probe")) as launch:
                with self.assertRaisesRegex(RuntimeError, "probe"):
                    session.__enter__()
            command = launch.call_args.args[0]
            self.assertEqual(any("connect_assert_srst" in arg for arg in command), reset)
            self.assertEqual(command[-1], "reset halt" if reset else "halt")

    def test_full_queue_rejects_without_overwrite(self):
        self.assertFalse(self.lib.bridge_start())
        self.assertFalse(self.lib.bridge_enqueue(10, 0))
        for code in range(self.producer.capacity):
            self.assertTrue(self.lib.bridge_enqueue(code, 1))
        before = self.target.read_memory(self.address, FIFO_OFFSET + self.producer.capacity * FRAME_SIZE)
        self.assertFalse(self.lib.bridge_enqueue(255, 1))
        with self.assertRaises(BufferError):
            self.producer.enqueue([self.frame()])
        self.assertEqual(before, self.target.read_memory(self.address, len(before)))
        self.assertTrue(self.lib.bridge_start())
        self.assertTrue(self.lib.bridge_boundary(False))
        for code in range(self.producer.capacity):
            self.assertEqual(self.producer.status()["decoded"], (self.producer.table[code],) * 7)
            self.lib.bridge_boundary(True)

    def test_repeat_hold_refill_and_one_underrun_per_episode(self):
        self.assertTrue(self.lib.bridge_enqueue(90, 2))
        self.assertTrue(self.lib.bridge_enqueue(100, 1))
        self.assertTrue(self.lib.bridge_start())
        self.assertTrue(self.lib.bridge_boundary(False))
        self.assertTrue(self.lib.bridge_start())  # Idempotent, not a rewind.
        self.lib.bridge_boundary(False)
        self.assertEqual(self.producer.status()["rounds_remaining"], 2)
        self.lib.bridge_boundary(True)
        self.assertEqual(self.producer.status()["frames_dequeued"], 1)
        self.lib.bridge_boundary(True)
        self.assertEqual(self.producer.status()["decoded"], (self.producer.table[100],) * 7)
        for _ in range(10):
            self.lib.bridge_boundary(True)
        self.assertEqual(self.producer.status()["underruns"], 1)
        self.assertTrue(self.lib.bridge_enqueue(110, 1))
        self.lib.bridge_boundary(True)
        self.assertEqual(self.producer.status()["decoded"], (self.producer.table[110],) * 7)
        self.lib.bridge_boundary(True)
        self.assertEqual(self.producer.status()["underruns"], 2)

    def test_swd_batch_wrap_counter_wrap_and_commit_last(self):
        self.word(16, 0xFFFFFFFE)
        self.word(20, 0xFFFFFFFE)
        self.target.writes.clear()
        transactions = self.target.transactions
        reports = self.producer.enqueue([self.frame(dc=x) for x in (600, 610, 620)])
        self.assertEqual(self.target.transactions, transactions + 1)
        self.assertEqual(len(self.target.writes), 3)  # Two payload pieces, then commit.
        self.assertEqual(self.target.writes[-1], (self.address + 16, struct.pack("<I", 1)))
        self.assertEqual(self.producer.status()["queued"], 3)
        self.lib.bridge_start()
        self.lib.bridge_boundary(False)
        for report in reports:
            self.assertEqual(self.producer.status()["decoded"], report["actual"])
            self.lib.bridge_boundary(True)
        self.assertEqual(self.producer.status()["consumer_count"], 1)

    def test_failed_payload_write_never_publishes_partial_frame(self):
        self.word(16, self.producer.capacity - 1)
        self.word(20, self.producer.capacity - 1)
        self.target.writes.clear()
        self.target.fail_write = 2
        with self.assertRaises(IOError):
            self.producer.enqueue([self.frame(), self.frame(dc=600)])
        self.assertEqual(self.producer.status()["queued"], 0)
        self.target.fail_write = None
        self.producer.enqueue([self.frame(), self.frame(dc=600)])
        self.assertEqual(self.producer.status()["queued"], 2)

    def test_start_mailbox_and_malformed_frames(self):
        with self.assertRaises(RuntimeError):
            self.producer.start()
        self.producer.enqueue([self.frame()])
        self.producer.start()
        self.assertEqual(self.producer.status()["start_request"], 1)
        # The boot loop processes this request; invoking C here validates the
        # same start decision without bringing up the complete application.
        self.assertTrue(self.lib.bridge_start())
        self.lib.bridge_boundary(False)
        # A direct SWD producer violating the ABI cannot underflow repeats.
        self.target.write_memory(self.address + FIFO_OFFSET + FRAME_SIZE, bytes(FRAME_SIZE))
        self.word(16, 2)
        self.lib.bridge_boundary(True)
        self.assertEqual(self.producer.status()["protocol_errors"], 1)
        self.assertEqual(self.producer.status()["rounds_remaining"], 0)

    def test_warmup_frame_boundary_and_filtered_getters(self):
        reports = self.begin([self.frame(dc=650, repeats=1), self.frame(dc=620, repeats=10)])
        a, b = [report["actual"] for report in reports]
        self.assertEqual(self.producer.status()["frames_dequeued"], 1)
        self.assertEqual(self.value(0, 6), a[6])
        self.assertEqual(self.round(), [0, 1, 2, 3, 4, 5, 6])
        self.assertEqual(self.producer.status()["frames_dequeued"], 2)
        for channel in range(7):
            self.assertEqual(self.value(0, channel), a[channel])
            self.assertEqual(self.value(1, channel), a[channel])
        self.assertEqual(self.lib.bridge_conversion(1023), 0)
        self.assertEqual(self.value(0), b[0])
        self.assertTrue(b[0] < self.value(1) < a[0])
        self.assertEqual(self.value(3), 0)  # Hardware 1023 must not trip a buck spike.
        self.assertEqual(self.value(6), 1)

    def test_midround_activation_resets_signals_preserves_latches(self):
        self.lib.bridge_init_adc()
        while self.value(11) != 0:
            self.lib.bridge_conversion(600)
        for _ in range(130):
            self.round(hardware=600)
        self.assertGreater(self.value(9), 0)
        self.lib.bridge_conversion(600)  # Already partway through a real round.
        report = self.producer.enqueue([self.frame(repeats=10)])[0]
        self.lib.bridge_set_latches()
        self.lib.bridge_start()
        self.assertEqual(self.producer.status()["state"], 1)
        while self.value(11) != 0:
            self.lib.bridge_conversion(600)
        self.assertEqual(self.producer.status()["state"], 2)
        self.assertEqual(self.value(9), 0)
        self.assertEqual(self.value(2), 0)
        self.assertEqual(self.value(7), 0)
        self.assertEqual(self.lib.bridge_latches(), 7)
        self.round()
        self.assertEqual(self.value(0), report["actual"][0])
        self.assertEqual(self.value(6), 1)

    def test_vrefint_cadence_does_not_consume_extra_frames(self):
        reports = self.begin([self.frame(vref=372, repeats=1024), self.frame(vref=400), self.frame(repeats=20)])
        seen = 0
        for _ in range(1024):
            seen += self.round().count(6)
        self.assertEqual(seen, 1)
        self.assertEqual(self.value(0, 6), reports[0]["actual"][6])
        self.assertEqual(self.producer.status()["frames_dequeued"], 2)
        self.assertIn(6, self.round())
        self.assertEqual(self.value(0, 6), reports[1]["actual"][6])
        self.assertEqual(self.producer.status()["frames_dequeued"], 3)
        self.assertEqual(self.producer.status()["rounds_completed"], 1025)
        self.assertEqual(self.value(7), 1)

    def test_constant_zero_codes_still_produce_boot_seed(self):
        self.begin([Frame((0,) * 7, 10)])
        self.assertEqual(self.value(6), 0)
        self.round(hardware=0)
        self.assertEqual(self.value(6), 1)

    def test_exact_raw_spike_boundary_without_lut_quantization(self):
        self.lib.bridge_init_adc()
        while self.value(11) != 1:
            self.lib.bridge_conversion(676)
        self.lib.bridge_conversion(704)
        self.assertEqual(self.value(3), 0)
        while self.value(11) != 1:
            self.lib.bridge_conversion(676)
        self.lib.bridge_conversion(705)
        self.assertEqual(self.value(3), 1)

    def test_exact_fast_loss_84_and_85_counts(self):
        history = (ctypes.c_uint16 * 1)(680)
        for drop in (84, 85):
            self.setUp()
            self.lib.bridge_init_adc()
            while self.value(11) != 0:
                self.lib.bridge_conversion(676)
            self.lib.bridge_exact_history(history, 1)
            self.lib.bridge_conversion(680 - drop)
            self.assertEqual(self.value(4), int(drop == 85))

    def test_exact_sustained_total_segments_and_history_warmup(self):
        for values, expected in (
            ((672, 673, 675, 676, 678, 679, 680), 1),  # Total eight, segments 3/3/2.
            ((673, 674, 675, 676, 678, 679, 680), 0),  # Total seven.
            ((672, 673, 673, 676, 678, 679, 680), 0),  # One segment only one.
            ((672, 673, 675, 676, 678, 679), 0),       # Only six records.
        ):
            self.setUp()
            history = (ctypes.c_uint16 * len(values))(*values)
            self.lib.bridge_exact_history(history, len(values))
            self.assertEqual(self.value(5), expected, values)

    def test_no_history_and_upward_input_do_not_trip_fast_loss(self):
        self.lib.bridge_init_adc()
        while self.value(11) != 0:
            self.lib.bridge_conversion(600)
        self.lib.bridge_conversion(0)
        self.assertEqual(self.value(4), 0)
        while self.value(11) != 0:
            self.lib.bridge_conversion(600)
        history = (ctypes.c_uint16 * 1)(680)
        self.lib.bridge_exact_history(history, 1)
        self.lib.bridge_conversion(681)
        self.assertEqual(self.value(4), 0)

    def test_zero_reference_and_saturation_do_not_replace_cached_scale(self):
        self.assertEqual(self.value(7), 0)
        self.assertEqual(self.lib.bridge_reference(372), 3300)
        self.assertEqual(self.value(7), 1)
        self.assertEqual(self.value(8), 3300)
        self.assertEqual(self.lib.bridge_reference(0), 3300)
        self.assertEqual(self.value(8), 0)
        self.assertEqual(self.lib.bridge_reference(1), 3300)
        self.assertEqual(self.value(8), 65535)

    def test_raw_buck_spike_uses_simulated_sample_before_filtering(self):
        reports = self.begin([self.frame(repeats=1), self.frame(buck=710, repeats=10)])
        self.round()
        self.assertEqual(self.value(3), 0)
        self.lib.bridge_conversion(0)  # DC
        self.lib.bridge_conversion(0)  # Buck; physical 0 cannot produce the spike.
        self.assertEqual(self.value(0, 1), reports[1]["actual"][1])
        self.assertLess(self.value(1, 1), 705)
        self.assertEqual(self.value(3), 1)

    def test_enabled_buck_spike_threshold_is_inclusive_and_latches_once(self):
        threshold = (25000 * 1023 + 36300 - 1) // 36300
        self.lib.bridge_init_adc()
        while self.value(11) != 1:
            self.lib.bridge_conversion(600)
        self.lib.bridge_conversion(threshold - 1)
        self.assertEqual(self.value(3), 0)
        while self.value(11) != 1:
            self.lib.bridge_conversion(600)
        self.lib.bridge_conversion(threshold)
        self.assertEqual(self.value(3), 1)
        self.assertEqual(self.lib.bridge_latches() & 2, 2)
        while self.value(11) != 1:
            self.lib.bridge_conversion(600)
        self.lib.bridge_conversion(1023)
        self.assertEqual(self.value(3), 1)

    def test_zero_buck_spike_disables_raw_trip_even_with_adc_count_override(self):
        import os
        build = ROOT / ".cache/adc_sim_test"
        dll = build / f"no-buck-spike-{os.getpid()}.dll"
        flags = ["-DADC_SIM_ENABLED=1", "-DPWRMGT_BUCK_VOLTAGE_SPIKE_MV=0",
                 "-DPWRMGT_BUCK_VOLTAGE_SPIKE_ADC_COUNTS=1", "-DPWRMGT_POWER_LOSS_FAST_DROP_ADC_COUNTS=85",
                 "-Itest/adc_sim/support", "-Iinc", "-Ilib/u8g2/csrc"]
        subprocess.run(["clang", "-shared", "-nostdlib", "-fuse-ld=lld", "-O1",
                        "-ffunction-sections", "-fdata-sections", *flags,
                        "test/adc_sim/bridge.c", "-o", str(dll), "-Wl,/noentry", "-Wl,/opt:ref"],
                       cwd=ROOT, check=True)
        preprocessed = subprocess.check_output(["clang", "-E", *flags, "test/adc_sim/bridge.c"],
                                               cwd=ROOT, text=True)
        self.assertNotIn("adc_buck_voltage_spike_shutdown", preprocessed)
        lib = ctypes.CDLL(str(dll))
        lib.bridge_conversion.argtypes = [ctypes.c_uint16]
        lib.bridge_reset()
        lib.bridge_init_adc()
        for _ in range(7 * 256):
            lib.bridge_conversion(1023)
        self.assertEqual(lib.bridge_value(0, 1), 1023)  # Acquisition still runs at full scale.
        self.assertEqual(lib.bridge_value(3, 0), 0)     # No emergency shutdown.
        self.assertEqual(lib.bridge_latches() & 2, 0)
        while lib.bridge_value(11, 0) != 0:
            lib.bridge_conversion(1023)
        history = (ctypes.c_uint16 * 1)(700)
        lib.bridge_exact_history(history, 1)
        lib.bridge_conversion(0)                     # Independent fast-loss protection still works.
        self.assertEqual(lib.bridge_value(4, 0), 1)
        self.assertEqual(lib.bridge_value(3, 0), 0)
        self.assertEqual(lib.bridge_latches() & 1, 1)
        lib.bridge_rearm_conditions(0, 1, 1)          # A disabled spike latch cannot block recovery.
        self.assertEqual(lib.bridge_rearm(0), 1)
        self.assertEqual(lib.bridge_latches() & 3, 0)

    def test_fast_power_loss_uses_simulated_average_and_raw_sample(self):
        reports = self.begin([self.frame(dc=700, repeats=128), self.frame(dc=600, repeats=10)])
        for _ in range(128):
            self.round()
        self.assertEqual(self.lib.bridge_history(0), reports[0]["actual"][0])
        self.assertEqual(self.value(4), 0)
        self.lib.bridge_conversion(1023)  # Physical rising/high input is discarded.
        self.assertEqual(self.value(4), 1)
        self.assertEqual(self.value(5), 1)

    def test_production_rearm_only_clears_loss_and_preserves_acquisition(self):
        self.begin([self.frame(dc=700, repeats=256)])
        for _ in range(128):
            self.round()
        history = self.lib.bridge_history(0)
        self.lib.bridge_set_latches()  # Also sets voltage calibration, which rearming must preserve.
        for active, allowed, spike in ((1, 1, 0), (0, 0, 0), (0, 1, 1)):
            self.lib.bridge_rearm_conditions(active, allowed, spike)
            self.assertEqual(self.lib.bridge_rearm(0), 0)
            self.assertEqual(self.lib.bridge_latches() & 1, 1)
            self.assertEqual(self.lib.bridge_history(0), history)
        self.lib.bridge_rearm_conditions(0, 1, 0)
        self.assertEqual(self.lib.bridge_rearm(0), 1)
        self.assertEqual(self.lib.bridge_latches() & 4, 4)
        self.assertEqual(self.lib.bridge_latches() & 1, 0)
        self.assertEqual(self.lib.bridge_history(0), history)
        self.round()  # Pipeline was not reset or suspended by rearming.

    def test_diagnostic_rearm_keeps_its_history_and_voltage_guards(self):
        self.lib.bridge_rearm_conditions(0, 1, 0)
        self.assertEqual(self.lib.bridge_rearm(1), 0)  # No voltage history.
        self.begin([self.frame(dc=700, repeats=256)])
        for _ in range(128):
            self.round()
        self.lib.bridge_rearm_conditions(0, 1, 0)
        self.assertEqual(self.lib.bridge_rearm(1), 1)

    def test_zero_fast_drop_removes_detector_but_keeps_trend_and_buck_spike(self):
        import os
        build = ROOT / ".cache/adc_sim_test"
        dll = build / f"no-fast-drop-{os.getpid()}.dll"
        flags = ["-DADC_SIM_ENABLED=1", "-DPWRMGT_POWER_LOSS_SUSTAINED_ENABLED=1",
                 "-DPWRMGT_BUCK_VOLTAGE_SPIKE_MV=25000", "-DPWRMGT_POWER_LOSS_FAST_DROP_ADC_COUNTS=0",
                 "-Itest/adc_sim/support", "-Iinc", "-Ilib/u8g2/csrc"]
        subprocess.run(["clang", "-shared", "-nostdlib", "-fuse-ld=lld", "-O1",
                        "-ffunction-sections", "-fdata-sections", *flags,
                        "test/adc_sim/bridge.c", "-o", str(dll), "-Wl,/noentry", "-Wl,/opt:ref"],
                       cwd=ROOT, check=True)
        preprocessed = subprocess.check_output(["clang", "-E", *flags, "test/adc_sim/bridge.c"],
                                               cwd=ROOT, text=True)
        self.assertNotIn("adc_check_fast_power_loss(", preprocessed)
        self.assertNotIn("adc_power_loss_shutdown", preprocessed)
        lib = ctypes.CDLL(str(dll))
        lib.bridge_conversion.argtypes = [ctypes.c_uint16]
        lib.bridge_reset()
        lib.bridge_init_adc()
        while lib.bridge_value(11, 0) != 0:
            lib.bridge_conversion(600)
        history = (ctypes.c_uint16 * 1)(700)
        lib.bridge_exact_history(history, 1)
        lib.bridge_conversion(0)  # Largest possible downward step: no fast latch/stop.
        self.assertEqual(lib.bridge_value(4, 0), 0)
        self.assertEqual(lib.bridge_latches() & 1, 0)
        self.assertEqual(lib.bridge_history(0), 700)
        self.assertEqual(lib.bridge_rearm(1), 0)  # No fast latch exists to rearm.
        lib.bridge_conversion(710)  # Buck raw-spike protection still acts immediately.
        self.assertEqual(lib.bridge_value(3, 0), 1)
        self.assertEqual(lib.bridge_latches() & 2, 2)
        values = (ctypes.c_uint16 * 7)(670, 675, 680, 685, 690, 695, 700)
        lib.bridge_exact_history(values, 7)
        self.assertEqual(lib.bridge_value(5, 0), 1)  # Sustained-decline predicate is retained.

    def test_sustained_trend_uses_real_adc_history_pipeline(self):
        reports = self.begin([self.frame(dc=x, repeats=128) for x in (700, 695, 690, 685, 680, 675, 670)])
        for _ in range(7 * 128):
            self.round()
        self.assertEqual(self.value(4), 0)  # No fast-drop latch.
        self.assertEqual(self.value(9), 7)
        self.assertEqual([self.lib.bridge_history(i) for i in range(7)],
                         [report["actual"][0] for report in reversed(reports)])
        self.assertEqual(self.value(5), 1)  # Actual foreground sustained predicate.

    def test_load_step_plateau_rejected_by_sustained_detector(self):
        self.begin([self.frame(dc=700, repeats=256), self.frame(dc=680, repeats=640)])
        for _ in range(7 * 128):
            self.round()
        self.assertEqual(self.value(4), 0)
        self.assertEqual(self.value(5), 0)


if __name__ == "__main__":
    unittest.main()
