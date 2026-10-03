"""Take a full-resolution Sony RX0 II still over PTP using gphoto2.

Install the gphoto2 command-line program and Pillow, put the camera in PC
Remote mode, and set JPEG Image Size to L on the camera. The camera's PC+Camera
setting and gphoto2's --keep option preserve the card copy. This script does
not replace USB drivers or use the low-resolution webcam stream.
"""

from __future__ import annotations

import argparse
from datetime import datetime
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tempfile

try:
    from PIL import Image, ImageOps
except ImportError as exc:
    raise SystemExit("Pillow is required: py -m pip install Pillow") from exc


# Sony's L JPEG dimensions for the four aspect ratios. A warning is emitted if
# the downloaded still is smaller; a PTP still is never upscaled by this script.
FULL_SIZE_DIMENSIONS = {
    (4800, 3200),  # 3:2
    (4272, 3200),  # 4:3
    (4800, 2704),  # 16:9
    (3200, 3200),  # 1:1
}

# These are common libgphoto2 configuration keys. Use --list-config and the
# repeatable --set-config option if the connected camera exposes other names.
SETTING_KEYS = {
    "iso": "iso",
    "shutter_speed": "shutterspeed",
    "aperture": "aperture",
    "exposure_compensation": "exposurecompensation",
    "white_balance": "whitebalance",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Capture and download a full-resolution RX0 II still using PTP/gphoto2.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("--camera-index", type=int, help="Detected camera number; omit for the first available camera")
    parser.add_argument("--output", type=Path, help="Output path; omit for a timestamped JPEG in a new temporary directory")
    parser.add_argument("--crop-to", type=int, metavar="PIXELS", help="Save a centered PIXELS-by-PIXELS crop")
    parser.add_argument("--view", action="store_true", help="Open the saved file in the default viewer")
    parser.add_argument(
        "--focus-mode",
        choices=("manual", "automatic", "preset", "unchanged"),
        default="manual",
        help="Focus mode during capture; the original mode is restored afterward",
    )
    parser.add_argument("--gphoto2", type=Path, help="Path to gphoto2.exe if it is not on PATH")
    parser.add_argument("--list-config", action="store_true", help="Show camera settings and exit without a photo")
    parser.add_argument("--get-config", action="append", metavar="KEY", help="Show a camera setting and exit; repeatable")
    parser.add_argument(
        "--set-config", action="append", default=[], metavar="KEY=VALUE", help="Set any supported camera setting before capture; repeatable"
    )
    for option in SETTING_KEYS:
        parser.add_argument(
            "--" + option.replace("_", "-"),
            metavar="VALUE",
            help="Camera " + option.replace("_", " ") + " (driver-supported values vary)",
        )
    args = parser.parse_args()
    if args.camera_index is not None and args.camera_index < 0:
        parser.error("--camera-index must be nonnegative")
    if args.crop_to is not None and args.crop_to <= 0:
        parser.error("--crop-to must be positive")
    if args.output is not None and args.output.suffix.lower() not in (".jpg", ".jpeg", ".png", ".tif", ".tiff", ".bmp", ".webp"):
        parser.error("--output must have a JPEG, PNG, TIFF, BMP, or WebP extension")
    for entry in args.set_config:
        if "=" not in entry or not entry.split("=", 1)[0]:
            parser.error("--set-config must be KEY=VALUE")
    return args


def find_gphoto2(explicit_path: Path | None) -> str:
    if explicit_path is not None:
        path = explicit_path.expanduser().resolve()
        if not path.is_file():
            raise RuntimeError(f"gphoto2 executable not found: {path}")
        return str(path)
    found = shutil.which("gphoto2")
    if found:
        return found
    if platform.system() == "Windows":
        for root in (Path("C:/msys64"), Path("C:/tools/msys64")):
            for variant in ("ucrt64", "mingw64", "clang64"):
                path = root / variant / "bin" / "gphoto2.exe"
                if path.is_file():
                    return str(path)
    raise RuntimeError(
        "gphoto2 was not found. Install the MSYS2 gphoto2 package, put it on PATH, "
        "or pass --gphoto2 PATH. No camera or USB driver was changed."
    )


def gphoto_environment(executable: str) -> dict[str, str]:
    environment = os.environ.copy()
    if platform.system() != "Windows":
        return environment

    # MSYS2's libgphoto2 package places these plug-ins next to its executable,
    # but a native Windows Python process does not source MSYS2's profile.d
    # script. Without these variables, a relocated MSYS2 installation may look
    # for plug-ins at the package builder's original path and detect no camera.
    prefix = Path(executable).resolve().parent.parent
    for variable, directory in (
        ("CAMLIBS", "libgphoto2"),
        ("IOLIBS", "libgphoto2_port"),
    ):
        parent = prefix / "lib" / directory
        versions = sorted(path for path in parent.iterdir() if path.is_dir()) if parent.is_dir() else []
        if versions:
            environment[variable] = str(versions[-1])
    return environment


def run_gphoto(executable: str, arguments: list[str], *, timeout: int = 90) -> str:
    command = [executable, *arguments]
    result = subprocess.run(
        command,
        capture_output=True,
        text=True,
        errors="replace",
        timeout=timeout,
        check=False,
        env=gphoto_environment(executable),
    )
    if result.returncode != 0:
        detail = (result.stderr + "\n" + result.stdout).strip()
        raise RuntimeError(f"gphoto2 exited with code {result.returncode}:\n{detail}")
    return result.stdout


def detect_cameras(executable: str) -> list[tuple[str, str]]:
    output = run_gphoto(executable, ["--auto-detect"], timeout=30)
    cameras = []
    for line in output.splitlines():
        # The table has a model column followed by a port such as usb:001,004.
        match = re.match(r"^(.+?)\s{2,}((?:usb|ptpip):\S*)\s*$", line, re.IGNORECASE)
        if match:
            cameras.append((match.group(1).strip(), match.group(2)))
    return cameras


def select_camera(executable: str, index: int | None) -> tuple[str, str]:
    cameras = detect_cameras(executable)
    if not cameras:
        raise RuntimeError(
            "gphoto2 found no camera. Check PC Remote mode, USB connection, and that Sony Remote/Imaging Edge is closed. "
            "On Windows, gphoto2 may also need a compatible USB driver."
        )
    chosen = index if index is not None else 0
    if chosen >= len(cameras):
        detail = ", ".join(f"{number}: {name} ({port})" for number, (name, port) in enumerate(cameras))
        raise RuntimeError(f"Camera index {chosen} is unavailable; detected: {detail}")
    return cameras[chosen]


def output_path(requested: Path | None) -> Path:
    if requested is not None:
        return requested.expanduser().resolve()
    directory = Path(tempfile.mkdtemp(prefix="hot-wand-rx0-"))
    timestamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S-%f")
    return directory / f"{timestamp}.jpg"


def select_jpeg(directory: Path) -> Path:
    candidates = [path for path in directory.iterdir() if path.suffix.lower() in (".jpg", ".jpeg")]
    if not candidates:
        names = ", ".join(path.name for path in directory.iterdir()) or "none"
        raise RuntimeError(
            f"Camera did not download a JPEG (downloaded: {names}). "
            "Check still-photo mode and whether the shutter fired; autofocus can block RX0 II remote capture. "
            "Also set image format to JPEG or RAW & JPEG."
        )
    # A multi-file transfer can contain more than one JPEG. Use the largest
    # image rather than a thumbnail or preview.
    def image_area(path: Path) -> int:
        with Image.open(path) as image:
            return image.width * image.height

    return max(candidates, key=image_area)


def save_image(source: Path, destination: Path, crop_to: int | None) -> tuple[int, int]:
    with Image.open(source) as image:
        width, height = image.size
        if crop_to is not None and crop_to > min(width, height):
            raise RuntimeError(f"--crop-to {crop_to} exceeds the {width} x {height} still")
        destination.parent.mkdir(parents=True, exist_ok=True)
        if crop_to is None and destination.suffix.lower() in (".jpg", ".jpeg"):
            # Preserve the camera's exact JPEG bytes and EXIF when no crop or
            # format conversion is requested.
            shutil.copyfile(source, destination)
        else:
            output = ImageOps.exif_transpose(image)
            if crop_to is not None:
                left = (output.width - crop_to) // 2
                top = (output.height - crop_to) // 2
                output = output.crop((left, top, left + crop_to, top + crop_to))
            if destination.suffix.lower() in (".jpg", ".jpeg"):
                output.convert("RGB").save(destination, quality=95, subsampling=0)
            else:
                output.save(destination)
    return width, height


def view_image(path: Path) -> None:
    if platform.system() == "Windows":
        os.startfile(path)
    elif platform.system() == "Darwin":
        subprocess.Popen(["open", str(path)])
    else:
        subprocess.Popen(["xdg-open", str(path)])


def main() -> int:
    args = parse_args()
    try:
        executable = find_gphoto2(args.gphoto2)
        model, port = select_camera(executable, args.camera_index)
        print(f"Camera: {model} ({port})", file=sys.stderr)
        camera_args = ["--port", port]

        if args.list_config:
            print(run_gphoto(executable, [*camera_args, "--list-config"]))
            return 0
        if args.get_config:
            for key in args.get_config:
                print(run_gphoto(executable, [*camera_args, "--get-config", key]))
            return 0

        settings = list(args.set_config)
        for option, key in SETTING_KEYS.items():
            value = getattr(args, option)
            if value is not None:
                settings.append(f"{key}={value}")
        # On the RX0 II, a remote shutter command can stop at autofocus without
        # taking a photo. Manual focus is the default for reliable snapshots.
        # A caller-supplied focusmode setting takes precedence and persists.
        explicit_focus = any(setting.split("=", 1)[0].rsplit("/", 1)[-1].lower() == "focusmode" for setting in settings)
        restore_focus = None
        if args.focus_mode != "unchanged" and not explicit_focus:
            focus_info = run_gphoto(executable, [*camera_args, "--get-config", "focusmode"])
            current_match = re.search(r"^Current:\s*(.+)$", focus_info, re.MULTILINE)
            if not current_match:
                raise RuntimeError("Could not read the RX0 II's current focus mode")
            original_focus = current_match.group(1).strip()
            requested_focus = {
                "manual": "Manual",
                "automatic": "Automatic",
                "preset": "Preset Focus",
            }[args.focus_mode]
            if requested_focus != original_focus:
                settings.append(f"focusmode={requested_focus}")
                restore_focus = original_focus
        setting_args = [part for setting in settings for part in ("--set-config", setting)]

        destination = output_path(args.output)
        # Download into a temporary staging directory so gphoto2 can retain
        # the camera's own extension, including when RAW & JPEG is selected.
        try:
            with tempfile.TemporaryDirectory(prefix="hot-wand-rx0-download-") as staging:
                pattern = str(Path(staging) / "capture.%C")
                output = run_gphoto(
                    executable,
                    [*camera_args, *setting_args, "--keep", "--filename", pattern, "--capture-image-and-download"],
                    timeout=120,
                )
                if output.strip():
                    print(output.strip(), file=sys.stderr)
                source = select_jpeg(Path(staging))
                width, height = save_image(source, destination, args.crop_to)
        finally:
            if restore_focus is not None:
                try:
                    run_gphoto(executable, [*camera_args, "--set-config", f"focusmode={restore_focus}"])
                except (OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
                    print(f"Warning: could not restore focus mode to {restore_focus}: {exc}", file=sys.stderr)

        if (width, height) not in FULL_SIZE_DIMENSIONS and (height, width) not in FULL_SIZE_DIMENSIONS:
            print(
                f"Warning: downloaded {width} x {height}, below or different from an RX0 II L JPEG. "
                "Set JPEG Image Size to L on the camera for maximum resolution.",
                file=sys.stderr,
            )
        else:
            print(f"Full-resolution still: {width} x {height}", file=sys.stderr)
        print(destination)
        if args.view:
            view_image(destination)
        return 0
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
        print(f"eyes_rx0.py: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
