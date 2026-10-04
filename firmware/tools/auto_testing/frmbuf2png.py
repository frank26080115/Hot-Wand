"""Render a Hot-Wand OLED framebuffer text dump as a black-and-white PNG.

The u8g2 full buffer is 512 bytes: four pages of 128 columns. Each byte's
least-significant bit is the top pixel of its eight-pixel page. Firmware uses
U8G2_R1, so the default output undoes that rotation to show the 32x128 OLED
in its logical, upright orientation.

Reusable entry points are parse_hex_bytes(), framebuffer_to_array(), and
save_framebuffer_png(). The array contains uint8 values 0 (off) and 255 (on).
"""

from __future__ import annotations

import argparse
from datetime import datetime
from pathlib import Path
import re
import sys
import tempfile
from typing import Sequence

import numpy as np
from PIL import Image


NATIVE_WIDTH = 128
NATIVE_HEIGHT = 32
FRAMEBUFFER_BYTES = NATIVE_WIDTH * NATIVE_HEIGHT // 8
_SEPARATORS = re.compile(r"[\s,;{}\[\]()]+")
_HEX_TOKEN = re.compile(r"(?:0[xX])?[0-9a-fA-F]+\Z")


def parse_hex_bytes(text: str) -> bytes:
    """Parse separated byte tokens or compact, even-length hex strings.

    Examples: ``00 12 34``, ``0x01, 0x02``, and ``DEADBEEF1234``.
    A one-digit token represents one byte; longer tokens are read in pairs.
    Blank lines and trailing # or // comments are ignored. Invalid text is
    rejected so that a damaged dump cannot silently become a plausible image.
    """
    clean_lines = []
    for line in text.splitlines():
        line = line.split("//", 1)[0].split("#", 1)[0]
        clean_lines.append(line)

    values = bytearray()
    for token in _SEPARATORS.split("\n".join(clean_lines)):
        if not token:
            continue
        if not _HEX_TOKEN.fullmatch(token):
            raise ValueError(f"Invalid hexadecimal token: {token!r}")
        digits = token[2:] if token.lower().startswith("0x") else token
        if len(digits) > 1 and len(digits) % 2:
            raise ValueError(f"Hex token has an odd number of digits: {token!r}")
        for start in range(0, len(digits), 2):
            values.append(int(digits[start : start + 2], 16))
    return bytes(values)


def framebuffer_to_array(
    framebuffer: bytes | bytearray | memoryview | Sequence[int],
    *,
    upright: bool = True,
) -> np.ndarray:
    """Decode 512 buffer bytes to a 0/255 grayscale NumPy pixel array.

    ``upright=True`` returns the firmware's 128-high by 32-wide R1 view.
    ``upright=False`` returns the controller's native 32-high by 128-wide view.
    The input may be a binary blob or a list of byte values, including data
    collected from GDB in a future test harness.
    """
    if isinstance(framebuffer, (bytes, bytearray, memoryview)):
        raw = np.frombuffer(framebuffer, dtype=np.uint8)
    else:
        # Validate before casting; uint8 conversion would wrap bad list values.
        values = list(framebuffer)
        if any(not isinstance(value, (int, np.integer)) or not 0 <= value <= 255 for value in values):
            raise ValueError("Framebuffer list must contain byte values from 0 to 255")
        raw = np.asarray(values, dtype=np.uint8)

    if raw.size != FRAMEBUFFER_BYTES:
        raise ValueError(f"Expected {FRAMEBUFFER_BYTES} framebuffer bytes, got {raw.size}")

    # A page contains 128 byte-columns. Expand each byte vertically, with
    # bit zero above bit one, then join all four pages into the native raster.
    pages = raw.reshape(NATIVE_HEIGHT // 8, NATIVE_WIDTH)
    bits = (pages[:, :, None] >> np.arange(8, dtype=np.uint8)) & 1
    native = bits.transpose(0, 2, 1).reshape(NATIVE_HEIGHT, NATIVE_WIDTH) * 255

    # u8g2 R1 maps logical (x, y) to native (127-y, x). Undo that mapping
    # so the PNG reads in the same orientation as the firmware's coordinates.
    return np.rot90(native, 1).copy() if upright else native.copy()


def save_framebuffer_png(
    framebuffer: bytes | bytearray | memoryview | Sequence[int],
    output: str | Path,
    *,
    upright: bool = True,
    scale: int = 8,
) -> Path:
    """Save decoded pixels using nearest-neighbor enlargement; return full path."""
    if scale < 1:
        raise ValueError("Scale must be a positive integer")
    path = Path(output).expanduser().resolve()
    if path.suffix.lower() != ".png":
        raise ValueError("Output path must end in .png")

    pixels = framebuffer_to_array(framebuffer, upright=upright)
    image = Image.fromarray(pixels, mode="L")
    if scale != 1:
        image = image.resize((image.width * scale, image.height * scale), Image.Resampling.NEAREST)
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path)
    return path


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="Text file containing hexadecimal framebuffer bytes")
    parser.add_argument("output", nargs="?", type=Path, help="Output PNG path (optional)")
    parser.add_argument("-o", "--output", dest="output_option", type=Path, help="Output PNG path")
    parser.add_argument("--scale", type=int, default=8, help="Nearest-neighbor pixel scale (default: 8)")
    parser.add_argument("--native", action="store_true", help="Use controller's horizontal 128x32 orientation")
    args = parser.parse_args(argv)

    if args.output is not None and args.output_option is not None:
        parser.error("Specify output either as a positional path or with --output")
    output = args.output_option or args.output
    if output is None:
        name = datetime.now().strftime("%Y-%m-%d_%H-%M-%S-%f.png")
        output = Path(tempfile.gettempdir()) / name

    try:
        dump = args.input.read_text(encoding="utf-8-sig")
        result = save_framebuffer_png(
            parse_hex_bytes(dump), output, upright=not args.native, scale=args.scale
        )
    except (OSError, ValueError) as exc:
        parser.exit(1, f"{parser.prog}: {exc}\n")

    print(result)
    return 0


if __name__ == "__main__":
    sys.exit(main())
