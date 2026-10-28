"""Camera-free entry points and setup-menu framebuffer evidence; no board access."""
import argparse
from contextlib import redirect_stdout
import io
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/auto_testing"))
import setup_menu_test


class CameraOptionalTests(unittest.TestCase):
    def test_all_three_entry_points_work_with_camera_modules_unavailable(self):
        # A fresh process catches eager imports even when the developer has
        # OpenCV/gphoto2 installed. Test direct-script and package import paths.
        script = r'''
import importlib, importlib.abc, io, sys
from contextlib import redirect_stdout
from unittest.mock import patch
class NoCamera(importlib.abc.MetaPathFinder):
    def find_spec(self, fullname, path=None, target=None):
        if fullname.split(".")[-1] in ("cv2", "eyes_rx0", "fiducial", "gphoto2"):
            raise AssertionError("Camera dependency requested: " + fullname)
sys.meta_path.insert(0, NoCamera())
sys.path.insert(0, "tools/auto_testing")
prefix = sys.argv[1]
for name, runner in (("setup_menu_test", "TestRunner"), ("faults_test", "FaultRunner"),
                     ("functional_test", "FunctionalRunner")):
    module = importlib.import_module(prefix + name)
    cls = getattr(module, runner)
    selections = ([], ["--camera", "off"])
    if name == "setup_menu_test":
        selections += (["--no-camera"],)
    for flags in selections:
        with patch.object(cls, "__init__", return_value=None) as init, \
             patch.object(cls, "run", return_value=0) as run:
            assert module.main(flags) == 0
            assert init.call_args.args[0].camera == "off"
            run.assert_called_once()
    with patch.object(cls, "__init__", return_value=None) as init, \
         patch.object(cls, "run", return_value=0):
        assert module.main(["--camera", "rx0"]) == 0
        assert init.call_args.args[0].camera == "rx0"
    if name != "setup_menu_test":
        with redirect_stdout(io.StringIO()):
            assert module.main(["--list", "--camera", "off"]) == 0
'''
        for prefix in ("", "tools.auto_testing."):
            with self.subTest(import_prefix=prefix):
                result = subprocess.run([sys.executable, "-c", script, prefix], cwd=ROOT,
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def arguments(self, folder, camera="off"):
        return argparse.Namespace(output_dir=Path(folder) / "run", camera=camera,
                                  camera_index=None, calibration=Path("unused.json"),
                                  gphoto2=Path("missing-gphoto2.exe"), crop_to=800,
                                  show_progress=False, gdb_port=3333)

    def test_setup_run_keeps_all_phases_and_pngs_without_camera_setup(self):
        from PIL import Image
        from build_metadata import MenuItem
        with tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()):
            runner = setup_menu_test.TestRunner(self.arguments(folder))
            target = Mock()
            target.symbols = {}
            target.read_memory.return_value = b"\xff" * 1024
            target.framebuffer.return_value = bytes([0x55, 0xaa]) * 256
            target.inspect.side_effect = lambda operation: operation()
            context = Mock()
            context.__enter__ = Mock(return_value=target)
            context.__exit__ = Mock(return_value=False)
            metadata = SimpleNamespace(elf=Path("unused.elf"), symbols={}, sha256="offline",
                                       load_ranges=[], address=lambda name: 0)
            items = [MenuItem("START\nPOWER\nLEVEL", ("100", "75", "50")),
                     MenuItem("SAVE\nAND\nEXIT", ()), MenuItem("EXIT\nNO\nSAVE", ())]
            runner.setup_camera = Mock(side_effect=AssertionError("Camera must remain unused"))
            runner.start_menu_boot = Mock()
            runner.dismiss_and_enter = Mock()
            for phase in ("phase_a", "phase_b", "phase_c", "phase_d"):
                setattr(runner, phase, Mock())
            with patch.object(setup_menu_test, "BuildMetadata", return_value=metadata), \
                 patch.object(setup_menu_test, "GdbSession", return_value=context), \
                 patch.object(setup_menu_test, "read_menu_items", return_value=items), \
                 patch.object(setup_menu_test.time, "sleep"):
                self.assertEqual(runner.run(), 0)
            runner.setup_camera.assert_not_called()
            for phase in ("phase_a", "phase_b", "phase_c", "phase_d"):
                getattr(runner, phase).assert_called_once()
            self.assertTrue(target.factory_defaults_on_exit)
            self.assertEqual((runner.output / "0001_fiducial_gate_framebuffer.bin").stat().st_size, 512)
            with Image.open(runner.output / "0001_fiducial_gate_framebuffer.png") as image:
                self.assertEqual(image.size, (256, 1024))
            self.assertTrue((runner.output / "nvm_at_stop.bin").is_file())
            self.assertFalse(list(runner.output.glob("*_camera.png")))
            self.assertFalse(runner.camera_ok)

    def test_setup_camera_off_skips_explicit_setup_and_opt_in_still_works(self):
        with tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()):
            runner = setup_menu_test.TestRunner(self.arguments(folder))
            camera = Mock()
            factory = Mock(return_value=camera)
            try:
                with patch.dict(sys.modules, {"eyes_rx0": SimpleNamespace(Rx0Camera=factory)}):
                    runner.setup_camera()
                    factory.assert_not_called()
                    runner.args.camera = "rx0"
                    runner.setup_camera()
                factory.assert_called_once_with(camera_index=None, gphoto2=Path("missing-gphoto2.exe"))
                camera.use_calibration.assert_called_once_with(Path("unused.json"))
            finally:
                runner.report_file.close()


if __name__ == "__main__":
    unittest.main()
