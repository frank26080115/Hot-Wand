"""Compare rectified RX0 stills with the OLED's actual framebuffer pixels."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import cv2
import numpy as np

try:
    from .frmbuf2png import framebuffer_to_array, save_framebuffer_png
except ImportError:  # Direct execution from this directory.
    from frmbuf2png import framebuffer_to_array, save_framebuffer_png


OLED_WIDTH = 32
OLED_HEIGHT = 128
PIXEL_SCALE = 8


@dataclass(frozen=True)
class ImageComparison:
    mismatch_count: int
    pixel_count: int
    mismatch_fraction: float
    checked_mismatch_count: int
    checked_pixel_count: int
    checked_fraction: float
    threshold: float
    white_level: float
    black_level: float


def compare_framebuffer_to_capture(framebuffer: bytes, captured_bgr: np.ndarray,
                                   artifact_prefix: Path, *, volatile_bottom_rows: int = 0) -> ImageComparison:
    """Classify only each pixel's central 2x2 sample, avoiding OLED bloom.

    The calibration maps the full OLED to 256x1024 (8 camera pixels per OLED
    pixel). Surrounding four pixels are excluded from each square. The
    synthetic PNG, classified camera pixels, and disagreements are retained.
    """
    expected = framebuffer_to_array(framebuffer)
    if expected.shape != (OLED_HEIGHT, OLED_WIDTH):
        raise RuntimeError(f"Unexpected framebuffer raster shape: {expected.shape}")
    if captured_bgr.shape[:2] != (OLED_HEIGHT * PIXEL_SCALE, OLED_WIDTH * PIXEL_SCALE):
        raise RuntimeError(f"Corrected still has wrong size: {captured_bgr.shape[:2]}")
    if not 0 <= volatile_bottom_rows < OLED_HEIGHT:
        raise ValueError("volatile_bottom_rows must leave at least one OLED row")

    artifact_prefix.parent.mkdir(parents=True, exist_ok=True)
    save_framebuffer_png(framebuffer, artifact_prefix.with_name(artifact_prefix.name + "_framebuffer.png"))
    gray = cv2.cvtColor(captured_bgr, cv2.COLOR_BGR2GRAY)
    # Only four camera pixels, arranged 2x2 in the center, contribute to the
    # classification of each physical OLED pixel.
    centers = gray.reshape(OLED_HEIGHT, 8, OLED_WIDTH, 8)[:, 3:5, :, 3:5]
    levels = centers.mean(axis=(1, 3)).astype(np.uint8)
    threshold, _ = cv2.threshold(levels, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
    observed = levels > threshold
    expected_on = expected > 0
    mismatch = observed != expected_on
    checked = mismatch[:OLED_HEIGHT - volatile_bottom_rows]

    observed_large = np.repeat(np.repeat(observed.astype(np.uint8) * 255, 8, axis=0), 8, axis=1)
    mismatch_large = np.repeat(np.repeat(mismatch.astype(np.uint8) * 255, 8, axis=0), 8, axis=1)
    cv2.imwrite(str(artifact_prefix.with_name(artifact_prefix.name + "_classified.png")), observed_large)
    cv2.imwrite(str(artifact_prefix.with_name(artifact_prefix.name + "_difference.png")), mismatch_large)
    whites = levels[expected_on]
    blacks = levels[~expected_on]
    return ImageComparison(int(mismatch.sum()), mismatch.size, float(mismatch.mean()),
                           int(checked.sum()), checked.size, float(checked.mean()), float(threshold),
                           float(whites.mean()) if whites.size else 0.0,
                           float(blacks.mean()) if blacks.size else 0.0)


def stable_frame_signature(framebuffer: bytes, *, voltage_page: bool = False) -> bytes:
    """Use displayed pixels as a second oracle for navigation and value wrap.

    Voltage readout refreshes independently of button events, so compare only
    the upper part of that page when deciding whether two options wrap.
    """
    pixels = framebuffer_to_array(framebuffer)
    return pixels[:100].tobytes() if voltage_page else pixels.tobytes()
