"""Native regression tests for consuming the battery override gesture.

Run: python -m unittest discover -s test/button_modal -q
Both real EXTI input and SWD input execute the actual button/battery C code.
"""
import ctypes
import os
from pathlib import Path
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ModalGestureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        clang = shutil.which("clang")
        if not clang or not shutil.which("lld-link"):
            raise RuntimeError("Native tests require LLVM clang and lld-link")
        build = ROOT / ".cache/button_modal"
        build.mkdir(parents=True, exist_ok=True)
        cls.libraries = []
        for swd in (0, 1):
            dll = build / f"button-{swd}-{os.getpid()}.dll"
            subprocess.run([clang, "-shared", "-nostdlib", "-fuse-ld=lld", "-O1",
                            "-Wno-unknown-attributes", "-ffunction-sections", "-fdata-sections",
                            f"-DBTN_SWD_TEST_HARNESS={swd}", "-DGPIO_PULLUP=1", "-DGPIO_PIN_RESET=0",
                            "-Itest/tipdetect_swd/support", "-Iinc", "-Ilib/u8g2/csrc",
                            "test/button_modal/bridge.c", "-o", str(dll), "-Wl,/noentry", "-Wl,/opt:ref"],
                           cwd=ROOT, check=True)
            lib = ctypes.CDLL(str(dll))
            lib.bridge_read.argtypes = [ctypes.c_uint32]
            lib.bridge_read.restype = ctypes.c_uint32
            cls.libraries.append(lib)

    def release(self, lib):
        lib.bridge_edge(0)
        lib.bridge_tick(50)

    def test_override_consumes_pending_events_and_remaining_hold(self):
        for lib in self.libraries:
            with self.subTest(input=self.libraries.index(lib)):
                lib.bridge_reset(0)
                lib.bridge_edge(1)
                self.assertEqual(lib.bridge_read(0), 1)  # Initial short press.
                lib.bridge_override()  # Actual five-second battery dialog.
                self.assertGreaterEqual(lib.bridge_read(5), 4992)
                self.assertEqual(lib.bridge_read(3), 1)  # Normal RF resume path.
                self.assertEqual(lib.bridge_read(4), 6)  # Override committed once.
                self.assertEqual(lib.bridge_read(0), 0)
                self.assertEqual(lib.bridge_read(1), 0)
                lib.bridge_tick(5000)  # Keep holding after returning to main.
                self.assertEqual(lib.bridge_read(0), 0)
                self.release(lib)
                self.assertEqual(lib.bridge_read(0), 0)
                lib.bridge_edge(1)  # Fresh press works normally.
                self.assertEqual(lib.bridge_read(0), 1)
                lib.bridge_tick(1000)
                self.assertEqual(lib.bridge_read(0), 3)

    def assert_rejected_override(self, lib, original_count):
        self.assertEqual(lib.bridge_read(6), 1)  # Fatal low battery.
        self.assertEqual(lib.bridge_read(3), 0)  # No RF restart attempt.
        self.assertEqual(lib.bridge_read(8), 0)  # Fan remains stopped.
        self.assertEqual(lib.bridge_read(9), 0)  # Minimum attenuation retained.
        self.assertEqual(lib.bridge_read(4), original_count)
        self.assertEqual(lib.bridge_read(7), 0)  # Override remains uncommitted.

    def test_voltage_drop_on_exact_completion_refresh_rejects_override(self):
        for lib in self.libraries:
            lib.bridge_reset(0)
            lib.bridge_battery(5, 1, 15000)
            # 32 * 156 ms finishes at 4992 ms; the next 67-ms refresh is 5025.
            lib.bridge_voltage_drop(5025, 13999)
            lib.bridge_edge(1)
            lib.bridge_override()
            self.assertEqual(lib.bridge_read(5), 5025)
            self.assertEqual(lib.bridge_read(11), 32)
            self.assert_rejected_override(lib, 5)

    def test_final_display_transfer_cannot_hide_voltage_drop(self):
        for lib in self.libraries:
            lib.bridge_reset(0)
            lib.bridge_battery(5, 1, 15000)
            lib.bridge_final_frame_voltage(13999)
            lib.bridge_edge(1)
            lib.bridge_override()
            self.assertEqual(lib.bridge_read(11), 32)
            self.assert_rejected_override(lib, 5)

    def test_absolute_voltage_completion_boundary(self):
        for lib in self.libraries:
            for voltage in (13999, 14000, 14001):
                with self.subTest(input=self.libraries.index(lib), voltage=voltage):
                    lib.bridge_reset(0)
                    lib.bridge_battery(5, 1, 15000)
                    lib.bridge_final_frame_voltage(voltage)
                    lib.bridge_edge(1)
                    lib.bridge_override()
                    if voltage < 14000:
                        self.assert_rejected_override(lib, 5)
                    else:
                        self.assertEqual(lib.bridge_read(6), 0)
                        self.assertEqual(lib.bridge_read(3), 1)
                        self.assertEqual(lib.bridge_read(4), 4)
                        self.assertEqual(lib.bridge_read(7), 1)
                        self.assertEqual(lib.bridge_read(10), 1)

    def test_reduced_cell_cutoff_boundaries_for_each_chemistry(self):
        for lib in self.libraries:
            for mode, per_cell in ((1, 3100), (2, 3300), (3, 3100),
                                   (4, 3300), (5, 2500), (6, 2750)):
                cutoff = 6 * per_cell
                for delta in (-1, 0, 1):
                    with self.subTest(input=self.libraries.index(lib), mode=mode, delta=delta):
                        lib.bridge_reset(0)
                        lib.bridge_battery(7, mode, cutoff + 100)
                        self.assertEqual(lib.bridge_read(10), 0)  # Old count fails.
                        lib.bridge_final_frame_voltage(cutoff + delta)
                        lib.bridge_edge(1)
                        lib.bridge_override()
                        if delta < 0:
                            self.assert_rejected_override(lib, 7)
                        else:
                            self.assertEqual(lib.bridge_read(6), 0)
                            self.assertEqual(lib.bridge_read(3), 1)
                            self.assertEqual(lib.bridge_read(4), 6)
                            self.assertEqual(lib.bridge_read(7), 1)
                            self.assertEqual(lib.bridge_read(10), 1)

    def test_cutoff_equality_disabled_mode_and_supported_floor_refusal(self):
        for lib in self.libraries:
            for mode, minimum, floor in ((1, 3100, 4), (2, 3300, 4), (3, 3100, 4),
                                         (4, 3300, 4), (5, 2500, 6), (6, 2750, 6)):
                for difference in (-1, 0, 1):
                    lib.bridge_reset(0)
                    lib.bridge_battery(7, mode, 7 * minimum + difference)
                    self.assertEqual(lib.bridge_read(10), int(difference >= 0))
                lib.bridge_reset(0)
                lib.bridge_battery(floor, mode, max(14000, floor * minimum - 1))
                lib.bridge_override()
                self.assertEqual(lib.bridge_read(6), 1)
                self.assertEqual(lib.bridge_read(3), 0)
            lib.bridge_reset(0)
            lib.bridge_battery(0, 0, 10000)
            self.assertEqual(lib.bridge_read(10), 1)

    def test_successful_override_cannot_be_used_twice(self):
        for lib in self.libraries:
            lib.bridge_reset(0)
            lib.bridge_edge(1)
            lib.bridge_override()
            self.assertEqual(lib.bridge_read(3), 1)
            lib.bridge_override()
            self.assertEqual(lib.bridge_read(6), 1)
            self.assertEqual(lib.bridge_read(3), 1)  # No second restart.
            self.assertEqual(lib.bridge_read(4), 6)

    def test_consumed_short_gesture_does_not_emit_on_release(self):
        for lib in self.libraries:
            lib.bridge_reset(1)
            lib.bridge_edge(1)
            lib.bridge_tick(100)
            lib.bridge_consume()
            self.release(lib)
            self.assertEqual(lib.bridge_read(0), 0)
            self.assertEqual(lib.bridge_read(2), 0)
            lib.bridge_edge(1)
            lib.bridge_tick(100)
            self.release(lib)
            self.assertEqual(lib.bridge_read(0), 1)

    def test_release_pending_is_consumed_through_debounce(self):
        for lib in self.libraries:
            lib.bridge_reset(1)
            lib.bridge_edge(1)
            lib.bridge_tick(100)
            lib.bridge_edge(0)
            lib.bridge_consume()
            lib.bridge_tick(49)
            self.assertEqual(lib.bridge_read(2), 1)
            lib.bridge_tick(1)
            self.assertEqual(lib.bridge_read(2), 0)
            self.assertEqual(lib.bridge_read(0), 0)

    def test_physical_release_bounce_does_not_rearm_events(self):
        lib = self.libraries[0]
        lib.bridge_reset(0)
        lib.bridge_edge(1)
        lib.bridge_consume()
        lib.bridge_edge(0)
        lib.bridge_tick(20)
        lib.bridge_edge(1)  # Bounce back down before the release qualifies.
        lib.bridge_tick(1500)
        self.assertEqual(lib.bridge_read(2), 1)
        self.assertEqual(lib.bridge_read(0), 0)
        self.release(lib)
        self.assertEqual(lib.bridge_read(0), 0)
        lib.bridge_edge(1)
        self.assertEqual(lib.bridge_read(0), 1)

    def test_idle_consumption_does_not_swallow_next_press(self):
        for lib in self.libraries:
            lib.bridge_reset(0)
            lib.bridge_consume()
            self.assertEqual(lib.bridge_read(2), 0)
            lib.bridge_edge(1)
            self.assertEqual(lib.bridge_read(0), 1)


if __name__ == "__main__":
    unittest.main()
