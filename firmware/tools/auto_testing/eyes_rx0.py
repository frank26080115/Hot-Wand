"""Take a full-resolution Sony RX0 II still over PTP using gphoto2.

Install the gphoto2 command-line program and Pillow, put the camera in PC
Remote mode, and set JPEG Image Size to L on the camera. Fiducial calibration
also needs numpy and opencv-python. The camera's PC+Camera setting and
gphoto2's --keep option preserve the card copy. Calibration leaves the camera
in Manual focus at its selected focus/exposure-compensation settings. This
script does not replace USB drivers or use the low-resolution webcam stream.
"""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import time

try:
    from PIL import Image, ImageOps
except ImportError as exc:
    raise SystemExit("Pillow is required: py -m pip install Pillow") from exc

# OpenCV is loaded only for calibration modes. Ordinary snapshots still need
# only Pillow and gphoto2.


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


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Capture and download a full-resolution RX0 II still using PTP/gphoto2.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("--camera-index", type=int, help="Detected camera number; omit for the first available camera")
    parser.add_argument("--output", type=Path, help="Output path; omit for a timestamped JPEG in a new temporary directory")
    parser.add_argument(
        "--crop-to",
        type=int,
        metavar="PIXELS",
        help="Ordinary mode: save a centered square; fiducial search: limit the central search zone",
    )
    calibration = parser.add_mutually_exclusive_group()
    calibration.add_argument(
        "--find-fiducial",
        action="store_true",
        help="Sweep RX0 II exposure/focus, rectify the OLED, and save a reusable .json calibration beside the image",
    )
    calibration.add_argument(
        "--apply-calibration",
        type=Path,
        metavar="JSON",
        help="Take a new still using a saved fiducial focus/exposure and crop/deskew recipe",
    )
    parser.add_argument("--focus-samples", type=int, default=2, metavar="COUNT", help="Stills to compare at each focus position")
    parser.add_argument("--max-focus-steps", type=int, default=40, metavar="COUNT", help="Maximum focus positions sampled during the coarse-to-fine search")
    parser.add_argument("--max-near-pulses", type=int, default=60, metavar="COUNT", help="Maximum coarse moves toward near focus")
    parser.add_argument("--view", action="store_true", help="Open the saved file in the default viewer")
    parser.add_argument(
        "--no-progress-window",
        action="store_true",
        help="Disable the live deskewed OpenCV preview during fiducial calibration",
    )
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
    args = parser.parse_args(argv)
    if args.camera_index is not None and args.camera_index < 0:
        parser.error("--camera-index must be nonnegative")
    if args.crop_to is not None and args.crop_to <= 0:
        parser.error("--crop-to must be positive")
    if args.focus_samples < 2 or args.max_focus_steps < 1 or args.max_near_pulses < 1:
        parser.error("--focus-samples must be at least 2 and focus step limits must be positive")
    if args.apply_calibration is not None and args.crop_to is not None:
        parser.error("--crop-to is a search-zone aid for --find-fiducial; a saved calibration already specifies the crop")
    if (args.find_fiducial or args.apply_calibration is not None) and args.focus_mode != "manual":
        parser.error("fiducial modes require manual focus")
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
            "If the shutter fired and the card has the new photo, set the camera's "
            "PC Remote Settings > Still Img. Save Dest. to PC+Camera. "
            "Also check still-photo mode and JPEG or RAW & JPEG image quality."
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


def _fiducial_module():
    # Support both `py eyes_rx0.py` and imports from another automation script.
    if __package__:
        from . import fiducial
    else:
        import fiducial
    return fiducial


def config_current(executable: str, camera_args: list[str], key: str) -> str:
    output = run_gphoto(executable, [*camera_args, "--get-config", key])
    match = re.search(r"^Current:\s*(.+)$", output, re.MULTILINE)
    if not match:
        raise RuntimeError(f"Camera did not report a current value for {key}")
    return match.group(1).strip()


def set_camera_value(executable: str, camera_args: list[str], key: str, value: str | int) -> None:
    run_gphoto(executable, [*camera_args, "--set-config", f"{key}={value}"])


def read_focus_position(executable: str, camera_args: list[str]) -> float:
    return float(config_current(executable, camera_args, "focalposition"))


def drive_focus(executable: str, camera_args: list[str], amount: int) -> None:
    """Allow the RX0 II time to expose manualfocus after switching to MF."""
    for attempt in range(4):
        try:
            set_camera_value(executable, camera_args, "manualfocus", amount)
            return
        except RuntimeError as exc:
            if "Property 'manualfocus' not found" not in str(exc) or attempt == 3:
                raise
            time.sleep(0.5)


def wait_focus_settled(executable: str, camera_args: list[str], timeout: float = 8.0) -> float:
    """Wait for Sony's asynchronous focus drive to stop changing position."""
    deadline = time.monotonic() + timeout
    previous = read_focus_position(executable, camera_args)
    stable_reads = 0
    while time.monotonic() < deadline:
        time.sleep(0.3)
        current = read_focus_position(executable, camera_args)
        stable_reads = stable_reads + 1 if current == previous else 0
        if stable_reads >= 3:
            return current
        previous = current
    raise RuntimeError(f"Focus position did not settle within {timeout:g} seconds (last position {previous:g})")


def move_focus_to(
    executable: str, camera_args: list[str], target: float, *, tolerance: float = 0.5, max_commands: int = 60
) -> float:
    """Drive toward a reported focus position using Sony's relative -7..7 control.

    A drive value is not an absolute position or a guaranteed displacement.
    Read back after every command so a coarse search can use positions 20,
    10, 5, and 1 apart without passing unsupported values to gPhoto2.
    """
    target = max(0.0, min(100.0, float(target)))
    position = wait_focus_settled(executable, camera_args)
    unchanged = 0
    crossed = 0
    previous_error = target - position
    for number in range(1, max_commands + 1):
        error = target - position
        if abs(error) <= tolerance:
            return position
        # Near the target, use the smallest drive. Large errors can use the
        # strongest supported command, but its actual travel is camera-specific.
        magnitude = 1 if crossed else min(7, max(1, int(abs(error))))
        drive_focus(executable, camera_args, magnitude if error > 0 else -magnitude)
        next_position = wait_focus_settled(executable, camera_args)
        unchanged = unchanged + 1 if next_position == position else 0
        if (target - next_position) * previous_error < 0:
            crossed += 1
        if number % 5 == 0:
            print(f"Focus move toward {target:g}: position {next_position:g} ({number} commands)", file=sys.stderr)
        position = next_position
        previous_error = target - position
        # The integer position can remain unchanged while the lens moves
        # within a readback interval. A two-unit miss is close enough to take
        # and score a real photo; rejecting it aborts the entire search.
        if unchanged >= 3 or crossed >= 4:
            if abs(target - position) <= max(2.0, tolerance):
                print(
                    f"Warning: focus settled at {position:g}, near requested {target:g}; "
                    "scoring the position reached",
                    file=sys.stderr,
                )
                return position
            raise RuntimeError(f"Focus stalled at {position:g} while moving toward {target:g}")
    if abs(target - position) <= max(2.0, tolerance):
        print(
            f"Warning: focus reached {position:g} after {max_commands} commands toward {target:g}; "
            "scoring the position reached",
            file=sys.stderr,
        )
        return position
    raise RuntimeError(f"Focus target {target:g} was not reached after {max_commands} commands (position {position:g})")


def focus_near(executable: str, camera_args: list[str], max_pulses: int) -> tuple[int, float]:
    """Find the near endpoint from stalled movement, not the reported 0."""
    position = wait_focus_settled(executable, camera_args)
    pulses = 0
    stagnant = 0
    while pulses < max_pulses:
        drive_focus(executable, camera_args, -7)
        pulses += 1
        next_position = wait_focus_settled(executable, camera_args)
        stagnant = stagnant + 1 if next_position >= position else 0
        position = next_position
        if stagnant >= 3:
            return pulses, position
    raise RuntimeError(
        f"Near focus did not stop moving after {max_pulses} -7 commands (position {position:g}); "
        "increase --max-near-pulses only if the position was still decreasing"
    )


def exposure_choices(executable: str, camera_args: list[str]) -> tuple[str, list[str]]:
    output = run_gphoto(executable, [*camera_args, "--get-config", "exposurecompensation"])
    current_match = re.search(r"^Current:\s*(.+)$", output, re.MULTILINE)
    choices = re.findall(r"^Choice:\s+\d+\s+([^\r\n]+)$", output, re.MULTILINE)
    if not current_match or not choices:
        raise RuntimeError("Camera did not expose exposure-compensation choices")
    current = current_match.group(1).strip()
    choices = [choice.strip() for choice in choices]
    if current not in choices:
        raise RuntimeError(f"Current exposure compensation {current!r} is not among the reported choices")
    return current, choices


def capture_full_bgr(executable: str, camera_args: list[str], archive_dir: Path | None = None):
    """Capture a new full JPEG, keep the card copy, and orient it for OpenCV."""
    fiducial = _fiducial_module()

    for attempt in range(1, 4):
        with tempfile.TemporaryDirectory(prefix="hot-wand-rx0-download-") as staging:
            pattern = str(Path(staging) / "capture.%C")
            output = run_gphoto(
                executable,
                [*camera_args, "--keep", "--filename", pattern, "--capture-image-and-download"],
                timeout=120,
            )
            try:
                source = select_jpeg(Path(staging))
            except RuntimeError as exc:
                if attempt == 3:
                    raise RuntimeError(
                        f"No JPEG after {attempt} capture attempts. "
                        f"Last gPhoto2 output: {output.strip() or '(none)'}. {exc}"
                    ) from exc
                print(
                    f"Capture {attempt} produced no PC JPEG; waiting before retry. "
                    "A card copy may have been saved.",
                    file=sys.stderr,
                )
                time.sleep(5)
                continue
            if archive_dir is not None:
                # Preserve the original downloaded JPEG before the temporary
                # staging directory disappears. Calibration can take dozens
                # of stills, all of which are useful if focus search is odd.
                archive_dir.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, archive_dir / f"capture_{time.time_ns()}{source.suffix.lower()}")
            image = fiducial.load_oriented_bgr(source)
            size = (image.shape[1], image.shape[0])
            if size not in FULL_SIZE_DIMENSIONS and size[::-1] not in FULL_SIZE_DIMENSIONS:
                raise RuntimeError(f"Downloaded {size[0]} x {size[1]}, not an RX0 II L JPEG; set JPEG Image Size to L")
            # Sony PC Remote can briefly drop the PC indicator while finishing
            # the card copy. Give it time before another gPhoto2 shutter call.
            time.sleep(3)
            return image


def _calibration_settings(args: argparse.Namespace) -> list[str]:
    settings = list(args.set_config)
    for option, key in SETTING_KEYS.items():
        value = getattr(args, option)
        if value is not None:
            settings.append(f"{key}={value}")
    for setting in settings:
        key = setting.split("=", 1)[0].rsplit("/", 1)[-1].lower()
        if key in ("manualfocus", "focalposition", "focusmode"):
            raise RuntimeError("Fiducial calibration controls focus; remove custom focus settings")
    return settings


def _prepare_manual_camera(executable: str, camera_args: list[str], args: argparse.Namespace) -> None:
    for setting in _calibration_settings(args):
        run_gphoto(executable, [*camera_args, "--set-config", setting])
    if config_current(executable, camera_args, "focusmode") != "Manual":
        set_camera_value(executable, camera_args, "focusmode", "Manual")
    reported = config_current(executable, camera_args, "focusmode")
    if reported != "Manual":
        # RX0 II PC Remote can accept Manual and still report Automatic over
        # PTP while the camera display says MF. The focus-position checks below
        # will verify that manual drive actually moves the lens.
        print(
            f"Warning: gPhoto2 reports focus mode {reported!r} after accepting Manual. "
            "Check that the camera display says MF; focus-drive checks will verify movement.",
            file=sys.stderr,
        )
    # The PTP property list can lag the mode change across gPhoto2 processes.
    # The drive command also retries if the property briefly disappears.
    time.sleep(0.5)


def _save_rectified(path: Path, bgr) -> None:
    fiducial = _fiducial_module()

    path.parent.mkdir(parents=True, exist_ok=True)
    options = [fiducial.cv2.IMWRITE_JPEG_QUALITY, 97] if path.suffix.lower() in (".jpg", ".jpeg") else []
    if not fiducial.cv2.imwrite(str(path), bgr, options):
        raise RuntimeError(f"Could not save corrected image: {path}")


def find_fiducial_mode(args: argparse.Namespace, executable: str, camera_args: list[str], model: str) -> Path:
    """Bracket exposure, search focus from near, then save image and recipe."""
    try:
        fiducial = _fiducial_module()
    except (ImportError, RuntimeError) as exc:
        raise RuntimeError("--find-fiducial requires numpy and opencv-python") from exc

    progress_window = "RX0 II fiducial calibration"
    progress_open = False
    last_alignment = None

    def show_progress(rectified, label: str) -> None:
        nonlocal progress_open
        if args.no_progress_window:
            return
        try:
            if not progress_open:
                # HighGUI starts normal windows square. Size this one to the
                # tall OLED image and preserve its aspect ratio when resized.
                flags = fiducial.cv2.WINDOW_NORMAL | fiducial.cv2.WINDOW_KEEPRATIO
                fiducial.cv2.namedWindow(progress_window, flags)
                height, width = rectified.shape[:2]
                scale = min(1.0, 900.0 / height)
                fiducial.cv2.resizeWindow(progress_window, round(width * scale), round(height * scale))
                progress_open = True
            fiducial.cv2.imshow(progress_window, rectified)
            if hasattr(fiducial.cv2, "setWindowTitle"):
                fiducial.cv2.setWindowTitle(progress_window, f"{progress_window} - {label}")
            # HighGUI needs waitKey to paint and handle window events. A short
            # timeout advances automatically; no click or key is required.
            fiducial.cv2.waitKey(20)
        except fiducial.cv2.error as exc:
            raise RuntimeError(
                "OpenCV could not display the calibration preview; install GUI-enabled opencv-python "
                "or use --no-progress-window"
            ) from exc

    def rectify_preview(raw_frame, label: str, known_alignment=None):
        nonlocal last_alignment
        try:
            # Recompute the crop and deskew for each still. Focus breathing
            # changes the OLED scale even when the camera itself stays fixed.
            current_alignment = known_alignment or fiducial.find_fiducial(raw_frame, template, args.crop_to)
            last_alignment = current_alignment
        except RuntimeError as exc:
            if last_alignment is None:
                raise
            print(f"Warning: could not align {label}: {exc}; using the previous alignment", file=sys.stderr)
            current_alignment = last_alignment
        rectified = fiducial.apply_alignment(raw_frame, current_alignment)
        show_progress(rectified, label)
        return rectified, current_alignment

    def camera_operation(operation, *operation_args, **operation_kwargs):
        # Keep HighGUI's event loop on the main thread while gPhoto2 waits for
        # a shutter transfer or several focus commands on a worker thread.
        if not progress_open:
            return operation(*operation_args, **operation_kwargs)
        with ThreadPoolExecutor(max_workers=1) as worker:
            pending = worker.submit(operation, *operation_args, **operation_kwargs)
            while not pending.done():
                fiducial.cv2.waitKey(50)
            return pending.result()

    def capture_calibration():
        return capture_full_bgr(executable, camera_args, getattr(args, "archive_stills", None))

    _prepare_manual_camera(executable, camera_args, args)
    near_pulses, near_position = focus_near(executable, camera_args, args.max_near_pulses)
    print(f"Near focus: position {near_position:g}, {near_pulses} coarse steps", file=sys.stderr)
    template = fiducial.load_template()
    exposure_control_works = True
    try:
        base_exposure, choices = exposure_choices(executable, camera_args)
    except RuntimeError as exc:
        # Full manual control may omit the EV property entirely. The camera's
        # shutter, aperture, and ISO settings still govern the captured stills.
        print(f"Warning: EV choices unavailable: {exc}; using camera exposure as set", file=sys.stderr)
        base_exposure, choices = None, []
        exposure_control_works = False
    # gPhoto lists this RX0 II's choices from bright to dark. Parse numeric EV
    # values so a changed driver list order cannot invert the bracket.
    numeric_choices = sorted(choices, key=float)
    bracket = [base_exposure]
    if exposure_control_works:
        numeric_index = numeric_choices.index(base_exposure)
        if numeric_index + 1 < len(numeric_choices):
            bracket.append(numeric_choices[numeric_index + 1])
        if numeric_index > 0:
            bracket.append(numeric_choices[numeric_index - 1])

    first_frame = camera_operation(capture_calibration)
    alignment = fiducial.find_fiducial(first_frame, template, args.crop_to)
    print(f"Fiducial match: {alignment.match_score:.3f}; search zone {alignment.search_zone}", file=sys.stderr)
    exposure_records = []
    for number, value in enumerate(bracket):
        if number == 0:
            frame = first_frame
        else:
            try:
                set_camera_value(executable, camera_args, "exposurecompensation", value)
            except RuntimeError as exc:
                print(f"Warning: camera rejected EV {value}: {exc}; using camera exposure as set", file=sys.stderr)
                exposure_control_works = False
                break
            frame = camera_operation(capture_calibration)
        exposure_label = f"EV {value}" if value is not None else "camera manual exposure"
        rectified, _ = rectify_preview(
            frame, f"Exposure {exposure_label} ({number + 1}/{len(bracket)})", alignment if number == 0 else None
        )
        quality = fiducial.exposure_quality(rectified, template)
        exposure_records.append({"value": value, "quality": quality, "frame": rectified, "raw_frame": frame})
        print(f"EV {value}: contrast {quality['contrast']:.1f}, clipped {quality['clipped_fraction']:.3%}", file=sys.stderr)

    if exposure_control_works and len(exposure_records) >= 2:
        medians = [record["quality"]["white_median"] for record in exposure_records]
        contrasts = [record["quality"]["contrast"] for record in exposure_records]
        if max(medians) - min(medians) < 2 and max(contrasts) - min(contrasts) < 2:
            print(
                "Warning: EV changes produced no measurable image change; "
                "using the camera's manually selected exposure.",
                file=sys.stderr,
            )
            exposure_control_works = False

    # Continue downward if even the darker bracket member clips the white
    # fiducial blocks. If every setting clips, retain the lowest EV.
    tried = {record["value"] for record in exposure_records}
    while exposure_control_works and all(record["quality"]["clipped_fraction"] > 0.01 for record in exposure_records):
        darker = [value for value in numeric_choices if value not in tried and float(value) < min(map(float, tried))]
        if not darker:
            break
        value = darker[-1]
        tried.add(value)
        try:
            set_camera_value(executable, camera_args, "exposurecompensation", value)
        except RuntimeError as exc:
            print(f"Warning: camera rejected EV {value}: {exc}; using camera exposure as set", file=sys.stderr)
            exposure_control_works = False
            break
        raw_frame = camera_operation(capture_calibration)
        rectified, _ = rectify_preview(raw_frame, f"Exposure EV {value}")
        quality = fiducial.exposure_quality(rectified, template)
        exposure_records.append({"value": value, "quality": quality, "frame": rectified, "raw_frame": raw_frame})
        print(f"EV {value}: contrast {quality['contrast']:.1f}, clipped {quality['clipped_fraction']:.3%}", file=sys.stderr)

    if not exposure_control_works:
        # A full-manual camera can ignore EV while its shutter/aperture/ISO
        # remain under the user's control. Restore the starting EV if the
        # camera accepted changes, then stop sending compensation commands.
        chosen = exposure_records[0]
        if len(exposure_records) > 1:
            try:
                set_camera_value(executable, camera_args, "exposurecompensation", base_exposure)
            except RuntimeError:
                pass
    else:
        eligible = [record for record in exposure_records if record["quality"]["clipped_fraction"] <= 0.01]
        if eligible:
            chosen = max(eligible, key=lambda record: record["quality"]["contrast"])
        else:
            chosen = min(exposure_records, key=lambda record: float(record["value"]))
            print(
                f"Warning: OLED highlights clip even at minimum EV {chosen['value']}; continuing at that setting.",
                file=sys.stderr,
            )
    best_exposure = chosen["value"]
    if exposure_control_works:
        set_camera_value(executable, camera_args, "exposurecompensation", best_exposure)
        print(f"Selected EV {best_exposure}", file=sys.stderr)
    else:
        print("Using camera-controlled exposure; no further EV changes will be sent", file=sys.stderr)

    def sample_focus(stage: int, target: float, first_raw=None):
        raw_frames = [first_raw] if first_raw is not None else []
        if not raw_frames:
            raw_frames.append(camera_operation(capture_calibration))
        frames = []
        scores = []
        for number in range(args.focus_samples):
            if number >= len(raw_frames):
                raw_frames.append(camera_operation(capture_calibration))
            raw_frame = raw_frames[number]
            # Each still gets its own perspective transform, including the
            # second sample at the same reported focus position.
            frame, _ = rectify_preview(
                raw_frame, f"Focus {stage:g} target {target:g} - photo {number + 1}/{args.focus_samples}"
            )
            frames.append(frame)
            scores.append(fiducial.focus_quality(frame, template))
        sharpest = max(range(len(scores)), key=scores.__getitem__)
        return {
            "stage": stage,
            "target_position": target,
            "focal_position": read_focus_position(executable, camera_args),
            "score": statistics.median(scores),
            "shot_scores": scores,
            "frame": frames[sharpest],
            "raw_frame": raw_frames[sharpest],
        }

    scan = [sample_focus(20, near_position, chosen["raw_frame"])]
    best = scan[0]
    reached_far_end = False
    stopped_on_peak = False

    def probe_focus(spacing: int, target: float):
        # Reuse a still already scored at this reported position. This avoids
        # another two-photo capture when finer stages revisit a coarse point.
        previous = next((point for point in scan if abs(point["focal_position"] - target) < 0.5), None)
        if previous is not None:
            print(
                f"Focus {spacing:g} target {target:g}: reusing position "
                f"{previous['focal_position']:g}, sharpness {previous['score']:.1f}",
                file=sys.stderr,
            )
            return previous, previous["focal_position"]
        if len(scan) >= args.max_focus_steps:
            raise RuntimeError("Focus sample limit reached; increase --max-focus-steps")
        actual = camera_operation(
            move_focus_to, executable, camera_args, target, tolerance=1.0 if spacing > 1 else 0.5
        )
        point = sample_focus(spacing, target)
        scan.append(point)
        print(
            f"Focus {spacing:g} target {target:g}: position {actual:g}, "
            f"sharpness {point['score']:.1f} ({args.focus_samples} photos)",
            file=sys.stderr,
        )
        return point, actual

    for spacing in (20, 10, 5, 1):
        if spacing == 20:
            print("Focus search: 20-unit sweep from near focus", file=sys.stderr)
            for target in range(20, 101, 20):
                point, actual = probe_focus(spacing, target)
                if point["score"] > best["score"]:
                    best = point
                elif point["score"] < best["score"]:
                    # One decline brackets the peak; the 10-unit stage will
                    # immediately return to and probe around the best point.
                    stopped_on_peak = True
                    break
                if actual >= 99:
                    reached_far_end = True
                    break
            continue

        # Each finer stage begins at its best point. Probe toward near focus
        # first; one non-improving neighbor switches direction immediately.
        center = best["focal_position"]
        print(f"Focus search: returning to best position {center:g} for {spacing:g}-unit spacing", file=sys.stderr)
        centered = camera_operation(move_focus_to, executable, camera_args, center, tolerance=0.5)
        print(f"Focus search: {spacing:g}-unit stage begins at position {centered:g}", file=sys.stderr)
        for direction in (-1, 1):
            origin = best["focal_position"]
            radius = 5 if spacing == 1 else 2
            for offset in range(1, radius + 1):
                target = origin + direction * offset * spacing
                if target < 0 or target > 100:
                    break
                point, _ = probe_focus(spacing, target)
                if point["score"] > best["score"]:
                    best = point
                else:
                    print(
                        f"Focus {spacing:g}: no improvement toward {target:g}; "
                        "trying the other side or finer spacing",
                        file=sys.stderr,
                    )
                    break

    if (
        all(point["focal_position"] == near_position for point in scan)
        and max(point["score"] for point in scan) - min(point["score"] for point in scan) < best["score"] * 0.05
    ):
        raise RuntimeError("Focus drive did not show measurable movement; cannot claim a best focus")

    # Return using readback feedback. The old home-and-replay route caused a
    # long silent delay after the search and is unnecessary for this capture.
    print(f"Focus search complete: best reported position {best['focal_position']:g}", file=sys.stderr)
    restored_position = camera_operation(move_focus_to, executable, camera_args, best["focal_position"], tolerance=0.5)
    print(f"Focus returned to position {restored_position:g}", file=sys.stderr)
    if exposure_control_works:
        set_camera_value(executable, camera_args, "exposurecompensation", best_exposure)

    # A sharper focus can concentrate light into a smaller area than the
    # exposure-bracket frames. Recheck clipping at the winner and, if needed,
    # walk compensation down until the final still has intact highlights.
    # Search the selected raw still again so the saved transform follows
    # focus breathing instead of retaining the near-focus crop and deskew.
    final_alignment = fiducial.find_fiducial(best["raw_frame"], template, args.crop_to)
    final_frame, _ = rectify_preview(
        best["raw_frame"], f"Best focus {best['focal_position']:g} - final alignment", final_alignment
    )
    print(f"Final fiducial match after focus: {final_alignment.match_score:.3f}", file=sys.stderr)
    final_quality = fiducial.exposure_quality(final_frame, template)
    while exposure_control_works and final_quality["clipped_fraction"] > 0.01:
        lower = [value for value in numeric_choices if float(value) < float(best_exposure)]
        if not lower:
            print(
                f"Warning: best-focus image still clips highlights at minimum EV {best_exposure}; saving it anyway.",
                file=sys.stderr,
            )
            break
        best_exposure = lower[-1]
        set_camera_value(executable, camera_args, "exposurecompensation", best_exposure)
        best["raw_frame"] = camera_operation(capture_calibration)
        final_alignment = fiducial.find_fiducial(best["raw_frame"], template, args.crop_to)
        final_frame, _ = rectify_preview(
            best["raw_frame"], f"Best focus {best['focal_position']:g} - EV {best_exposure}", final_alignment
        )
        final_quality = fiducial.exposure_quality(final_frame, template)

    destination = output_path(args.output)
    if args.output is None:
        destination = destination.with_suffix(".png")
    _save_rectified(destination, final_frame)
    recipe = {
        "schema": "hot-wand-rx0-fiducial-v1",
        "created_at": datetime.now().astimezone().isoformat(),
        "camera_model": model,
        "image_file": destination.name,
        "template_file": str(fiducial.TEMPLATE_PATH),
        "template_sha256": hashlib.sha256(fiducial.TEMPLATE_PATH.read_bytes()).hexdigest(),
        "source_size": list(final_alignment.source_size),
        "output_size": list(final_alignment.template_size),
        "output_to_source": final_alignment.output_to_source.tolist(),
        "exif_transposed": True,
        "search_zone": list(final_alignment.search_zone),
        "match_score": final_alignment.match_score,
        "focus": {
            "mode": "Manual",
            "near_drive": -7,
            "near_homing_pulses": near_pulses,
            "near_position": near_position,
            "search_spacings": [20, 10, 5, 1],
            "target_position": best["focal_position"],
            "best_reported_position": best["focal_position"],
            "returned_position": restored_position,
            "far_endpoint_seen": reached_far_end,
            "peak_or_plateau_seen": stopped_on_peak,
            "samples_per_position": args.focus_samples,
            "scan": [
                {key: value for key, value in point.items() if key not in ("frame", "raw_frame")}
                for point in scan
            ],
        },
        "exposure_compensation": best_exposure,
        "exposure_control_works": exposure_control_works,
        "exposure_bracket": [
            {key: value for key, value in record.items() if key not in ("frame", "raw_frame")}
            for record in exposure_records
        ],
        "final_sharpness": fiducial.focus_quality(final_frame, template),
        "final_histogram": final_quality,
        "highlights_clipped": final_quality["clipped_fraction"] > 0.01,
    }
    recipe_path = destination.with_suffix(".json")
    recipe_path.write_text(json.dumps(recipe, indent=2) + "\n", encoding="utf-8")
    print(f"Calibration: {recipe_path}", file=sys.stderr)
    if progress_open:
        fiducial.cv2.destroyWindow(progress_window)
    return destination


def apply_calibration_mode(args: argparse.Namespace, executable: str, camera_args: list[str], model: str) -> Path:
    """Replay a JSON recipe and take one corrected still for another script."""
    try:
        fiducial = _fiducial_module()
    except (ImportError, RuntimeError) as exc:
        raise RuntimeError("--apply-calibration requires numpy and opencv-python") from exc

    recipe = fiducial.load_calibration(args.apply_calibration)
    if recipe["camera_model"] != model:
        raise RuntimeError(f"Calibration was made for {recipe['camera_model']}, but detected {model}")
    _prepare_manual_camera(executable, camera_args, args)
    focus_near(executable, camera_args, args.max_near_pulses)
    focus = recipe["focus"]
    if "target_position" in focus:
        # New recipes use the camera's reported 0..100 position. The command
        # value is only a relative drive and cannot reproduce an absolute
        # position by itself.
        move_focus_to(executable, camera_args, float(focus["target_position"]), tolerance=0.5)
    else:
        # Keep older recipes usable after changing the search pattern.
        for _ in range(int(focus["steps_from_near"])):
            drive_focus(executable, camera_args, int(focus["step_drive"]))
            wait_focus_settled(executable, camera_args)
    if recipe.get("exposure_control_works", True):
        set_camera_value(executable, camera_args, "exposurecompensation", recipe["exposure_compensation"])
    alignment = fiducial.alignment_from_calibration(recipe)
    rectified = fiducial.apply_alignment(capture_full_bgr(executable, camera_args), alignment)
    destination = output_path(args.output)
    if args.output is None:
        destination = destination.with_suffix(".png")
    _save_rectified(destination, rectified)
    return destination


class Rx0Camera:
    """Importable capture/calibration interface for unattended test scripts.

    Camera focus is positioned once at startup. Subsequent stills reuse the
    same exposure and transform without repeating the slow focus search.
    """

    def __init__(self, *, camera_index: int | None = None, gphoto2: Path | None = None):
        self.executable = find_gphoto2(gphoto2)
        self.model, port = select_camera(self.executable, camera_index)
        self.camera_args = ["--port", port]
        self.recipe = None

    def calibrate(self, output: Path, *, crop_to: int | None = 800, show_progress: bool = False) -> Path:
        """Search the fiducial, set focus/exposure, and retain its JSON recipe."""
        args = parse_args([])
        args.output = output
        args.crop_to = crop_to
        args.no_progress_window = not show_progress
        args.archive_stills = output.parent / "calibration_stills"
        image = find_fiducial_mode(args, self.executable, self.camera_args, self.model)
        self.recipe = _fiducial_module().load_calibration(image.with_suffix(".json"))
        return image

    def use_calibration(self, path: Path) -> None:
        """Load an existing recipe and set the camera once for this session."""
        fiducial = _fiducial_module()
        recipe = fiducial.load_calibration(path)
        if recipe["camera_model"] != self.model:
            raise RuntimeError(f"Calibration camera {recipe['camera_model']} differs from {self.model}")
        args = parse_args([])
        _prepare_manual_camera(self.executable, self.camera_args, args)
        focus_near(self.executable, self.camera_args, args.max_near_pulses)
        focus = recipe["focus"]
        if "target_position" not in focus:
            raise RuntimeError("This test needs a calibration JSON with an absolute target_position")
        move_focus_to(self.executable, self.camera_args, float(focus["target_position"]), tolerance=0.5)
        if recipe.get("exposure_control_works", True):
            set_camera_value(self.executable, self.camera_args, "exposurecompensation", recipe["exposure_compensation"])
        self.recipe = recipe

    def capture_rectified(self, output: Path, *, raw_output: Path | None = None):
        """Save one full still and corrected OLED crop; return corrected BGR."""
        if self.recipe is None:
            raise RuntimeError("Calibrate or load a calibration before capturing")
        fiducial = _fiducial_module()
        raw = capture_full_bgr(self.executable, self.camera_args)
        if raw_output is not None:
            raw_output.parent.mkdir(parents=True, exist_ok=True)
            if not fiducial.cv2.imwrite(str(raw_output), raw):
                raise RuntimeError(f"Could not save full camera still: {raw_output}")
        corrected = fiducial.apply_alignment(raw, fiducial.alignment_from_calibration(self.recipe))
        _save_rectified(output, corrected)
        return corrected


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

        if args.find_fiducial:
            destination = find_fiducial_mode(args, executable, camera_args, model)
            print(destination)
            if args.view:
                view_image(destination)
            return 0
        if args.apply_calibration is not None:
            destination = apply_calibration_mode(args, executable, camera_args, model)
            print(destination)
            if args.view:
                view_image(destination)
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
    except (OSError, RuntimeError, ValueError, KeyError, subprocess.TimeoutExpired) as exc:
        print(f"eyes_rx0.py: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
