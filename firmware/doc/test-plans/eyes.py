"""Capture one webcam image for reviewing the Hot-Wand OLED.

Requires OpenCV: ``py -m pip install opencv-python``
Run ``py eyes.py --help`` for camera and exposure options.
"""

from __future__ import annotations

import argparse
from datetime import datetime
import os
from pathlib import Path
import platform
import subprocess
import sys
import tempfile
import time

try:
    import cv2
except ImportError as exc:
    raise SystemExit("OpenCV is required: py -m pip install opencv-python") from exc


# OpenCV cannot enumerate every video mode on every backend. Probe a short list
# of common high-resolution modes; each mode switch can be slow on USB cameras.
# The oversized request lets drivers that clamp to their maximum advertise it.
RESOLUTIONS = (
    (8192, 8192),
    (3840, 2160),
    (3264, 2448),
    (2592, 1944),
    (2560, 1440),
    (1920, 1080),
    (1280, 720),
)

# Camera drivers differ in which controls they implement and in their numeric
# ranges. Passing raw numbers makes it possible to experiment with the values
# reported by the user's particular webcam driver.
CONTROLS = {
    "exposure": "CAP_PROP_EXPOSURE",
    "gain": "CAP_PROP_GAIN",
    "brightness": "CAP_PROP_BRIGHTNESS",
    "contrast": "CAP_PROP_CONTRAST",
    "gamma": "CAP_PROP_GAMMA",
    "backlight": "CAP_PROP_BACKLIGHT",
    "white_balance": "CAP_PROP_WB_TEMPERATURE",
    "saturation": "CAP_PROP_SATURATION",
    "sharpness": "CAP_PROP_SHARPNESS",
    "hue": "CAP_PROP_HUE",
    "focus": "CAP_PROP_FOCUS",
    "zoom": "CAP_PROP_ZOOM",
    "iso": "CAP_PROP_ISO_SPEED",
    "aperture": "CAP_PROP_APERTURE",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Take one maximum-resolution webcam snapshot, optionally cropped around the OLED.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("--camera-index", type=int, help="Camera number; omit to open the first available camera")
    parser.add_argument(
        "--output",
        type=Path,
        help="Output image path; omit for a timestamped PNG in a new temporary directory",
    )
    parser.add_argument(
        "--crop-to",
        type=int,
        metavar="PIXELS",
        help="Save a centered PIXELS-by-PIXELS square from the full-resolution frame",
    )
    parser.add_argument("--view", action="store_true", help="Open the saved image in the default viewer")
    parser.add_argument(
        "--backend",
        choices=("auto", "default", "dshow", "msmf", "v4l2"),
        default="auto",
        help="OpenCV video backend; auto tries DirectShow first on Windows",
    )
    parser.add_argument(
        "--pixel-format",
        choices=("native", "mjpg"),
        default="native",
        help="Use the camera's native stream, or request MJPEG for high-resolution USB modes",
    )
    parser.add_argument(
        "--camera-settings",
        action="store_true",
        help="Open the camera driver's settings dialog before the snapshot (if supported)",
    )
    parser.add_argument(
        "--auto-exposure",
        choices=("auto", "manual"),
        help="Exposure mode; use manual with --exposure to reduce OLED blooming",
    )
    parser.add_argument(
        "--auto-white-balance",
        choices=("auto", "manual"),
        help="White-balance mode; use manual with --white-balance",
    )
    parser.add_argument(
        "--auto-focus", choices=("auto", "manual"), help="Focus mode; use manual with --focus"
    )
    for name in CONTROLS:
        parser.add_argument(
            "--" + name.replace("_", "-"),
            type=float,
            metavar="VALUE",
            help=f"Raw camera {name.replace('_', ' ')} value (driver-dependent)",
        )
    parser.add_argument(
        "--settle-seconds",
        type=float,
        default=1.5,
        metavar="SECONDS",
        help="Time to discard frames after selecting resolution and controls",
    )
    args = parser.parse_args()
    if args.camera_index is not None and args.camera_index < 0:
        parser.error("--camera-index must be nonnegative")
    if args.crop_to is not None and args.crop_to <= 0:
        parser.error("--crop-to must be positive")
    if args.settle_seconds < 0:
        parser.error("--settle-seconds must be nonnegative")
    if args.output is not None and args.output.suffix.lower() not in (".png", ".jpg", ".jpeg", ".bmp", ".tiff", ".webp"):
        parser.error("--output must end in .png, .jpg, .jpeg, .bmp, .tiff, or .webp")
    return args


def open_camera(args: argparse.Namespace) -> tuple[cv2.VideoCapture, int]:
    backends = {
        "default": cv2.CAP_ANY,
        "dshow": cv2.CAP_DSHOW,
        "msmf": cv2.CAP_MSMF,
        "v4l2": cv2.CAP_V4L2,
    }
    if args.backend == "auto":
        backend_names = ("dshow", "msmf", "default") if platform.system() == "Windows" else ("default",)
    else:
        backend_names = (args.backend,)
    indices = (args.camera_index,) if args.camera_index is not None else range(10)
    for index in indices:
        for backend_name in backend_names:
            camera = cv2.VideoCapture(index, backends[backend_name])
            if camera.isOpened():
                return camera, index
            camera.release()
    raise RuntimeError("No available camera found; try --camera-index or another --backend")


def select_resolution(camera: cv2.VideoCapture, pixel_format: str) -> tuple[tuple[int, int], tuple[int, int]]:
    # Forcing MJPEG can return black frames on some driver/backend combinations,
    # so leave the camera's native stream alone unless explicitly requested.
    if pixel_format == "mjpg":
        if not camera.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG")):
            print("Warning: camera rejected MJPEG; using its current stream format", file=sys.stderr)
    initial = (round(camera.get(cv2.CAP_PROP_FRAME_WIDTH)), round(camera.get(cv2.CAP_PROP_FRAME_HEIGHT)))
    best = initial
    best_request = initial
    for width, height in RESOLUTIONS:
        camera.set(cv2.CAP_PROP_FRAME_WIDTH, width)
        camera.set(cv2.CAP_PROP_FRAME_HEIGHT, height)
        # Query the negotiated size instead of reading a frame at every mode.
        # Reading at each setting was the main source of the long startup time.
        actual = (round(camera.get(cv2.CAP_PROP_FRAME_WIDTH)), round(camera.get(cv2.CAP_PROP_FRAME_HEIGHT)))
        if actual[0] > 0 and actual[1] > 0 and actual[0] * actual[1] > best[0] * best[1]:
            best, best_request = actual, (width, height)
    if best == (0, 0):
        raise RuntimeError("Camera did not report any usable frame size")

    camera.set(cv2.CAP_PROP_FRAME_WIDTH, best_request[0])
    camera.set(cv2.CAP_PROP_FRAME_HEIGHT, best_request[1])
    print(f"Largest reported camera mode: {best[0]} x {best[1]}", file=sys.stderr)
    return best, initial


def apply_controls(camera: cv2.VideoCapture, args: argparse.Namespace) -> None:
    # Set automatic modes first so explicit exposure/gain/white-balance/focus
    # values have the best chance of taking effect afterward.
    auto_exposure = args.auto_exposure
    if auto_exposure is None and args.exposure is not None:
        auto_exposure = "manual"
    if auto_exposure is not None:
        # DirectShow and V4L2 use different OpenCV auto-exposure conventions.
        value = (
            (3 if auto_exposure == "auto" else 1)
            if camera.getBackendName() == "V4L2"
            else (0.75 if auto_exposure == "auto" else 0.25)
        )
        set_control(camera, "auto exposure", cv2.CAP_PROP_AUTO_EXPOSURE, value)
    if args.auto_white_balance is not None:
        set_control(camera, "auto white balance", cv2.CAP_PROP_AUTO_WB, 1 if args.auto_white_balance == "auto" else 0)
    if args.auto_focus is not None:
        set_control(camera, "auto focus", cv2.CAP_PROP_AUTOFOCUS, 1 if args.auto_focus == "auto" else 0)
    for name, property_name in CONTROLS.items():
        value = getattr(args, name)
        if value is not None:
            property_id = getattr(cv2, property_name, None)
            if property_id is None:
                print(f"Warning: this OpenCV build does not expose {name}", file=sys.stderr)
                continue
            set_control(camera, name.replace("_", " "), property_id, value)


def set_control(camera: cv2.VideoCapture, name: str, property_id: int, value: float) -> None:
    supported = camera.set(property_id, value)
    actual = camera.get(property_id)
    if supported:
        print(f"{name}: requested {value:g}, reported {actual:g}", file=sys.stderr)
    else:
        print(f"Warning: camera/backend rejected {name}={value:g} (reported {actual:g})", file=sys.stderr)


def capture(camera: cv2.VideoCapture, settle_seconds: float):
    # Keep reading during the settling interval; sleeping alone may leave an
    # old buffered image in the driver after exposure or resolution changes.
    deadline = time.monotonic() + settle_seconds
    hard_deadline = deadline + 5
    frame = None
    frames_read = 0
    while True:
        ok, candidate = camera.read()
        if ok and candidate is not None:
            frames_read += 1
            # Prefer the latest nonblank frame over a black startup frame.
            if frame is None or candidate.max() >= 5:
                frame = candidate
        now = time.monotonic()
        if (now >= deadline and frames_read >= 3) or now >= hard_deadline:
            break
    if frame is None:
        raise RuntimeError("Camera stopped returning frames")
    return frame


def output_path(args: argparse.Namespace) -> Path:
    if args.output is not None:
        return args.output.expanduser().resolve()
    directory = Path(tempfile.mkdtemp(prefix="hot-wand-eyes-"))
    timestamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S-%f")
    return directory / f"{timestamp}.png"


def view_image(path: Path) -> None:
    if platform.system() == "Windows":
        os.startfile(path)
    elif platform.system() == "Darwin":
        subprocess.Popen(["open", str(path)])
    else:
        subprocess.Popen(["xdg-open", str(path)])


def main() -> int:
    args = parse_args()
    camera = None
    started = time.monotonic()
    try:
        camera, index = open_camera(args)
        print(f"Camera index: {index}; backend: {camera.getBackendName()}", file=sys.stderr)
        selected, initial = select_resolution(camera, args.pixel_format)
        print(f"Camera setup: {time.monotonic() - started:.2f} s", file=sys.stderr)
        if args.camera_settings:
            set_control(camera, "camera settings dialog", cv2.CAP_PROP_SETTINGS, 1)
        apply_controls(camera, args)
        try:
            frame = capture(camera, args.settle_seconds)
        except RuntimeError:
            if selected == initial:
                raise
            frame = None
        if selected != initial and (frame is None or frame.max() < 5):
            # Some drivers accept a resolution change but only deliver blank
            # frames at that mode. Retry the camera's original working mode.
            print("Selected mode gave a black frame; retrying the original mode", file=sys.stderr)
            camera.set(cv2.CAP_PROP_FRAME_WIDTH, initial[0])
            camera.set(cv2.CAP_PROP_FRAME_HEIGHT, initial[1])
            frame = capture(camera, args.settle_seconds)
        height, width = frame.shape[:2]
        print(f"Captured frame: {width} x {height}", file=sys.stderr)
        print(f"Capture ready: {time.monotonic() - started:.2f} s total", file=sys.stderr)
        if frame.max() < 5:
            print(
                "Warning: frame is nearly black. Try --backend msmf, remove manual exposure/gain, "
                "or try --pixel-format mjpg.",
                file=sys.stderr,
            )
        if args.crop_to is not None:
            size = args.crop_to
            if size > min(width, height):
                raise ValueError(f"--crop-to {size} exceeds captured frame {width} x {height}")
            left = (width - size) // 2
            top = (height - size) // 2
            frame = frame[top : top + size, left : left + size]

        path = output_path(args)
        path.parent.mkdir(parents=True, exist_ok=True)
        encoded_ok, encoded = cv2.imencode(path.suffix.lower(), frame)
        if not encoded_ok:
            raise RuntimeError(f"Could not encode image as {path.suffix}")
        path.write_bytes(encoded.tobytes())
        print(path)
        if args.view:
            view_image(path)
        return 0
    except (OSError, RuntimeError, ValueError, cv2.error) as exc:
        print(f"eyes.py: {exc}", file=sys.stderr)
        return 1
    finally:
        if camera is not None:
            camera.release()


if __name__ == "__main__":
    raise SystemExit(main())
