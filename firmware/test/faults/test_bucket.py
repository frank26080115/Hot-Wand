"""Independent fixed-step oracle, heat/cooling and warning-only cap variant."""
import ctypes
import os
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]


class BucketTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build = ROOT / ".cache/faults-native"
        build.mkdir(parents=True, exist_ok=True)
        cls.libraries = []
        for cap in (81, 111):
            dll = build / f"bucket-{cap}-{os.getpid()}.dll"
            subprocess.run(["clang", "-shared", "-nostdlib", "-fuse-ld=lld", "-O1",
                            "-DLEAKY_BUCKET_ENABLED=1", f"-DLEAKY_BUCKET_MAX_TEMPERATURE_C={cap}", "-Itest/faults/support",
                            "-Iinc", "-Ilib/u8g2/csrc", "test/faults/bucket_bridge.c", "-o", str(dll),
                            "-Wl,/noentry"], cwd=ROOT, check=True)
            lib = ctypes.CDLL(str(dll))
            lib.bucket_reset.argtypes = [ctypes.c_uint32, ctypes.c_uint64]
            lib.bucket_energy.restype = ctypes.c_uint64
            cls.libraries.append(lib)

    def test_disabled_model_has_no_energy_state_or_temperature_contribution(self):
        build = ROOT / ".cache/faults-native"
        dll = build / f"bucket-disabled-{os.getpid()}.dll"
        flags = ["-DLEAKY_BUCKET_ENABLED=0", "-Itest/faults/support", "-Iinc", "-Ilib/u8g2/csrc"]
        subprocess.run(["clang", "-shared", "-nostdlib", "-fuse-ld=lld", "-O1", *flags,
                        "test/faults/bucket_bridge.c", "-o", str(dll), "-Wl,/noentry"], cwd=ROOT, check=True)
        preprocessed = subprocess.check_output(["clang", "-E", *flags, "test/faults/bucket_bridge.c"],
                                               cwd=ROOT, text=True)
        self.assertNotIn("leaky_bucket_energy_uj", preprocessed)
        lib = ctypes.CDLL(str(dll))
        lib.bucket_reset.argtypes = [ctypes.c_uint32, ctypes.c_uint64]
        lib.bucket_energy.restype = ctypes.c_uint64
        lib.bucket_reset(0, 500000000)
        self.assertEqual(lib.bucket_step(120000, 100000), 0)
        self.assertEqual(lib.bucket_energy(), 0)

    def test_heat_matches_independent_integer_energy_model(self):
        for lib in self.libraries:
            for power in (60000, 80000, 100000):
                lib.bucket_reset(0, 0)
                energy = 0
                for tick in range(50, 40001, 50):
                    energy = max(0, energy + power * 50 - 60000 * 50 - energy * 50 // 20000)
                    lib.bucket_step(tick, power)
                    self.assertEqual(lib.bucket_energy(), energy)
                if power == 60000:
                    self.assertEqual(energy, 0)

    def test_preloaded_energy_and_responsive_plant_cool(self):
        lib = self.libraries[1]
        lib.bucket_reset(0, 500000000)
        hot = lib.bucket_step(50, 100000)
        self.assertGreater(hot, 110)
        for tick in range(100, 20001, 50):
            cooled = lib.bucket_step(tick, 40000)
        self.assertLessEqual(cooled, 75)
        self.assertLess(lib.bucket_energy(), 500000000)

    def test_missed_periods_use_latest_power_and_tick_wrap(self):
        lib = self.libraries[1]
        lib.bucket_reset(0xFFFFFF00, 0)
        lib.bucket_step(ctypes.c_uint32(0xFFFFFF00 + 1000), 100000)
        energy = 0
        for _ in range(20):
            energy += 40000 * 50 - energy * 50 // 20000
        self.assertEqual(lib.bucket_energy(), energy)

    def test_cap_81_cannot_generate_bucket_only_terminal_temperature(self):
        for lib, expected in zip(self.libraries, (81, 111)):
            lib.bucket_reset(0, 0)
            self.assertEqual(lib.bucket_step(120000, 100000), expected)


if __name__ == "__main__":
    unittest.main()
