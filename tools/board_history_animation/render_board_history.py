#!/usr/bin/env python3
"""Render numbered EAGLE board history snapshots and build a 2 FPS APNG.

The Gerber-to-SVG rendering code in this file is copied and adapted from
electrical/mfg/generate_gerbv_preview_svg.py.  It is intentionally kept
self-contained so historical rendering does not import or execute that tool.
"""

from __future__ import annotations

import argparse
import io
import math
import os
import re
import shutil
import struct
import subprocess
import tempfile
import zlib
from dataclasses import dataclass
from decimal import Decimal, InvalidOperation
from pathlib import Path


PROJECTS = ("hot-wand", "hot-wand-lite")
DEFAULT_EAGLECON_EXE = Path(r"C:\ProgramFiles\EAGLE-7.6.0\bin\eaglecon.exe")
DEFAULT_GERBV_EXE = Path(
    r"C:\ProgramFiles\gerbv_2023-10-11_ccf6a3_(Windows amd64)\gerbv.exe"
)
DEFAULT_DPI = 200.0
DEFAULT_FPS = 2.0
DEFAULT_ORIGIN = "auto"
DEFAULT_WINDOW = "5x4"
SUPERSAMPLE_FACTOR = 4
OUTLINE_MARGIN_INCHES = 0.05
FINAL_FRAME_EXTRA_REPETITIONS = 6

BACKGROUND_COLOR = "#FFFFFF"
FR4_COLOR = "#064B2F"
COPPER_COLOR = "#2EAD65"
EXPOSED_COPPER_COLOR = "#D4AF37"
SILK_COLOR = "#FFFFFF"
FEATURE_COLOR = "#000000"

EXTENSION_LAYER_MAP: dict[str, tuple[str, str | None]] = {
    ".gml": ("dimensional", None),
    ".gm1": ("dimensional", None),
    ".gko": ("dimensional", None),
    ".xln": ("drills", None),
    ".drl": ("drills", None),
    ".drd": ("drills", None),
    ".do": ("drills", None),
    ".gto": ("top", "silk"),
    ".gtp": ("top", "cream"),
    ".gts": ("top", "mask"),
    ".gtl": ("top", "copper"),
    ".gbo": ("bottom", "silk"),
    ".gbp": ("bottom", "cream"),
    ".gbs": ("bottom", "mask"),
    ".gbl": ("bottom", "copper"),
}

FLAG_OPTIONS = ("m", "r", "u", "c", "q", "O", "f")
LENGTH_PATTERN = re.compile(
    r"^([+-]?(?:\d+(?:\.\d*)?|\.\d+))\s*(mil|mm|inch|in)?$",
    re.IGNORECASE,
)
PAIR_PATTERN = re.compile(
    r"^\s*([0-9]+(?:\.[0-9]+)?)x([0-9]+(?:\.[0-9]+)?)\s*$",
    re.IGNORECASE,
)
GERBER_FORMAT_PATTERN = re.compile(
    r"%FS[^*]*X([0-9])([0-9])Y([0-9])([0-9])\*%",
    re.IGNORECASE,
)
GERBER_COORDINATE_PATTERN = re.compile(r"([XY])([+-]?[0-9]+)", re.IGNORECASE)
NUMBERED_BOARD_PATTERN = re.compile(r"^([1-9][0-9]*)\.brd$")
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


@dataclass(frozen=True)
class CamSection:
    section_id: str
    name: str
    device: str
    output_template: str
    layers: tuple[str, ...]
    values: dict[str, str]


@dataclass(frozen=True)
class LayerFile:
    source_path: Path
    svg_path: Path
    root_group: str
    side_layer: str | None


def parse_args() -> argparse.Namespace:
    env_eaglecon = os.environ.get("EAGLECON_EXE")
    env_gerbv = os.environ.get("GERBV_EXE")
    parser = argparse.ArgumentParser(
        description=(
            "Render missing history-<project>/<n>.png frames from numbered "
            "EAGLE boards, then overwrite history-<project>/<project>-history.png "
            "with an animated PNG."
        )
    )
    parser.add_argument(
        "project",
        nargs="?",
        choices=PROJECTS,
        default="hot-wand",
        help="board project to render (default: hot-wand)",
    )
    parser.add_argument(
        "--eaglecon",
        type=Path,
        default=Path(env_eaglecon) if env_eaglecon else DEFAULT_EAGLECON_EXE,
        help="path to eaglecon.exe (default: EAGLECON_EXE or EAGLE 7.6)",
    )
    parser.add_argument(
        "--gerbv",
        type=Path,
        default=Path(env_gerbv) if env_gerbv else DEFAULT_GERBV_EXE,
        help="path to gerbv.exe (default: GERBV_EXE or configured Windows path)",
    )
    parser.add_argument(
        "--dpi",
        type=float,
        default=DEFAULT_DPI,
        help=f"frame resolution before cropping (default: {DEFAULT_DPI:g} DPI)",
    )
    parser.add_argument(
        "--fps",
        type=float,
        default=DEFAULT_FPS,
        help=f"animated PNG frame rate (default: {DEFAULT_FPS:g} FPS)",
    )
    parser.add_argument(
        "--origin",
        default=DEFAULT_ORIGIN,
        help=(
            "gerbv lower-left origin in inches, or 'auto' to follow the "
            f"milling outline (default: {DEFAULT_ORIGIN})"
        ),
    )
    parser.add_argument(
        "--window",
        default=DEFAULT_WINDOW,
        help=f"gerbv canvas in inches (default: {DEFAULT_WINDOW})",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="show successful eaglecon diagnostics",
    )
    return parser.parse_args()


def unquote(value: str) -> str:
    value = value.strip()
    if len(value) >= 2 and value[0] == '"' and value[-1] == '"':
        return value[1:-1]
    return value


def parse_cam_job(path: Path) -> list[CamSection]:
    section_order: list[str] = []
    section_values: dict[str, dict[str, str]] = {}
    discovered_sections: list[str] = []
    current_section: str | None = None

    for line_number, raw_line in enumerate(
        path.read_text(encoding="utf-8-sig", errors="replace").splitlines(),
        start=1,
    ):
        line = raw_line.strip()
        if not line or line.startswith((";", "#")):
            continue
        if line.startswith("[") and line.endswith("]"):
            current_section = line[1:-1].strip()
            section_values.setdefault(current_section, {})
            if current_section.casefold() != "cam processor job":
                discovered_sections.append(current_section)
            continue
        if current_section is None or "=" not in line:
            raise ValueError(f"Invalid CAM syntax at {path}:{line_number}: {raw_line}")
        key, value = line.split("=", 1)
        key = key.strip().casefold()
        value = unquote(value)
        if current_section.casefold() == "cam processor job" and key == "section":
            section_order.append(value)
        else:
            section_values[current_section][key] = value

    if not section_order:
        section_order = discovered_sections
    if not section_order:
        raise ValueError(f"CAM job contains no output sections: {path}")

    folded_names = {name.casefold(): name for name in section_values}
    sections: list[CamSection] = []
    seen_ids: set[str] = set()
    for requested_id in section_order:
        folded_id = requested_id.casefold()
        if folded_id in seen_ids:
            raise ValueError(f"CAM job lists section {requested_id!r} more than once")
        seen_ids.add(folded_id)
        actual_id = folded_names.get(folded_id)
        if actual_id is None:
            raise ValueError(f"CAM job references missing section {requested_id!r}")
        values = section_values[actual_id]
        missing = [key for key in ("device", "output", "layers") if not values.get(key)]
        if missing:
            raise ValueError(
                f"CAM section {actual_id!r} is missing: {', '.join(missing)}"
            )
        layers = tuple(values["layers"].split())
        sections.append(
            CamSection(
                section_id=actual_id,
                name=values.get("name[en]", values.get("name", actual_id)),
                device=values["device"],
                output_template=values["output"],
                layers=layers,
                values=values,
            )
        )
    return sections


def decimal_text(value: Decimal) -> str:
    text = format(value, "f")
    if "." in text:
        text = text.rstrip("0").rstrip(".")
    return text or "0"


def parse_decimal(value: str, description: str) -> Decimal:
    try:
        return Decimal(value)
    except InvalidOperation as exc:
        raise ValueError(f"Invalid {description}: {value!r}") from exc


def parse_length(value: str, target_unit: str) -> str:
    match = LENGTH_PATTERN.fullmatch(value.strip())
    if match is None:
        raise ValueError(f"Invalid CAM length: {value!r}")
    amount = parse_decimal(match.group(1), "length")
    source_unit = (match.group(2) or target_unit).casefold()
    if source_unit == "mil":
        inches = amount / Decimal(1000)
    elif source_unit in {"in", "inch"}:
        inches = amount
    elif source_unit == "mm":
        inches = amount / Decimal("25.4")
    else:
        raise ValueError(f"Unsupported CAM length unit: {source_unit!r}")
    if target_unit == "inch":
        return decimal_text(inches)
    if target_unit == "mm":
        return decimal_text(inches * Decimal("25.4"))
    raise ValueError(f"Unsupported target unit: {target_unit!r}")


def bool_value(value: str, description: str) -> bool:
    if value == "0":
        return False
    if value == "1":
        return True
    raise ValueError(f"{description} must be 0 or 1, got {value!r}")


def bool_option(option: str, enabled: bool) -> str:
    return f"-{option}{'+' if enabled else '-'}"


def expand_output_template(template: str, board_stem: str) -> Path:
    percent_literal = "\0PERCENT\0"
    expanded = template.replace("%%", percent_literal).replace("%N", board_stem)
    expanded = expanded.replace(percent_literal, "%")
    if expanded.startswith("."):
        expanded = f"{board_stem}{expanded}"
    if "%" in expanded:
        raise ValueError(f"Unsupported CAM output placeholder in {template!r}")
    relative_path = Path(expanded.replace("\\", os.sep))
    if relative_path.is_absolute() or ".." in relative_path.parts:
        raise ValueError(f"CAM output escapes the build directory: {template!r}")
    return relative_path


def resolve_auxiliary_file(
    value: str,
    board: Path,
    cam_job: Path,
    build_dir: Path,
) -> Path:
    if value.startswith("."):
        return build_dir / f"{board.stem}{value}"
    requested = Path(value)
    if requested.is_absolute():
        return requested
    candidates = (
        build_dir / requested,
        board.parent / requested,
        cam_job.parent / requested,
    )
    return next((candidate for candidate in candidates if candidate.is_file()), candidates[0])


def add_flags(command: list[str], section: CamSection) -> None:
    raw_flags = section.values.get("flags")
    if raw_flags:
        flag_values = raw_flags.split()
        if len(flag_values) != len(FLAG_OPTIONS):
            raise ValueError(f"{section.section_id}: Flags must contain seven values")
        for option, value in zip(FLAG_OPTIONS, flag_values):
            command.append(bool_option(option, bool_value(value, f"-{option}")))
    raw_emulate = section.values.get("emulate")
    if raw_emulate:
        emulate_values = raw_emulate.split()
        if len(emulate_values) == 1:
            command.append(bool_option("e", bool_value(emulate_values[0], "emulate")))
        elif len(emulate_values) == 3:
            for option, value in zip(("e", "a", "t"), emulate_values):
                command.append(bool_option(option, bool_value(value, f"-{option}")))
        else:
            raise ValueError(f"{section.section_id}: Emulate must have one or three values")


def add_numeric_options(command: list[str], section: CamSection) -> None:
    values = section.values
    if values.get("scale"):
        command.append(f"-s{decimal_text(parse_decimal(values['scale'], 'scale'))}")
    if values.get("offset"):
        offset = values["offset"].split()
        if len(offset) != 2:
            raise ValueError(f"{section.section_id}: Offset must contain two values")
        command.extend(
            (f"-x{parse_length(offset[0], 'inch')}", f"-y{parse_length(offset[1], 'inch')}")
        )
    if values.get("page"):
        page = values["page"].split()
        if len(page) != 2:
            raise ValueError(f"{section.section_id}: Page must contain two values")
        command.extend(
            (f"-h{parse_length(page[0], 'inch')}", f"-w{parse_length(page[1], 'inch')}")
        )
    if values.get("pen"):
        pen = values["pen"].split()
        if len(pen) != 2:
            raise ValueError(f"{section.section_id}: Pen must contain two values")
        command.extend(
            (
                f"-p{parse_length(pen[0], 'mm')}",
                f"-v{decimal_text(parse_decimal(pen[1], 'pen velocity'))}",
            )
        )
    if values.get("tolerance"):
        tolerances = values["tolerance"].split()
        if len(tolerances) != 6:
            raise ValueError(f"{section.section_id}: Tolerance must contain six values")
        for (option, sign), raw_value in zip(
            (("D", "-"), ("D", "+"), ("F", "-"), ("F", "+"), ("E", "-"), ("E", "+")),
            tolerances,
        ):
            amount = abs(parse_decimal(raw_value, f"-{option} tolerance"))
            if amount:
                command.append(f"-{option}{sign}{decimal_text(amount)}")


def build_eagle_command(
    eaglecon: Path,
    board: Path,
    cam_job: Path,
    build_dir: Path,
    section: CamSection,
    output_path: Path,
) -> list[str]:
    command = [
        str(eaglecon),
        "-X",
        "-N+",
        f"-d{section.device}",
        f"-o{output_path}",
    ]
    add_flags(command, section)
    add_numeric_options(command, section)
    wheel = section.values.get("wheel", "")
    if wheel:
        wheel_path = resolve_auxiliary_file(wheel, board, cam_job, build_dir)
        if wheel_path.is_file():
            command.append(f"-W{wheel_path}")
        elif not section.device.upper().startswith("GERBER_RS274X"):
            raise FileNotFoundError(f"Aperture wheel not found: {wheel_path}")
    rack = section.values.get("rack", "")
    if rack:
        rack_path = resolve_auxiliary_file(rack, board, cam_job, build_dir)
        if not rack_path.is_file():
            raise FileNotFoundError(f"Drill rack not found: {rack_path}")
        command.append(f"-R{rack_path}")
    command.extend((str(board), *section.layers))
    return command


def process_output(result: subprocess.CompletedProcess[str]) -> str:
    return "\n".join(
        part.strip()
        for part in (result.stdout, result.stderr)
        if part and part.strip()
    )


def generate_cam_outputs(
    eaglecon: Path,
    board: Path,
    cam_job: Path,
    build_dir: Path,
    sections: list[CamSection],
    verbose: bool,
) -> None:
    seen_outputs: set[str] = set()
    for section in sections:
        relative_output = expand_output_template(section.output_template, board.stem)
        key = str(relative_output).casefold()
        if key in seen_outputs:
            raise ValueError(f"Duplicate CAM output: {relative_output}")
        seen_outputs.add(key)
        output_path = (build_dir / relative_output).resolve()
        output_path.relative_to(build_dir)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        command = build_eagle_command(
            eaglecon,
            board,
            cam_job,
            build_dir,
            section,
            output_path,
        )
        result = subprocess.run(
            command,
            cwd=build_dir,
            capture_output=True,
            text=True,
            errors="replace",
            check=False,
        )
        diagnostics = process_output(result)
        if verbose and diagnostics:
            print(diagnostics)
        if result.returncode != 0 or not output_path.is_file() or output_path.stat().st_size == 0:
            raise RuntimeError(
                f"eaglecon failed for {section.name} ({result.returncode})\n"
                f"Command: {subprocess.list2cmdline(command)}\n{diagnostics}"
            )


def discover_layers(input_dir: Path, svg_dir: Path) -> list[LayerFile]:
    layers: list[LayerFile] = []
    for source_path in sorted(input_dir.iterdir(), key=lambda path: path.name.casefold()):
        if not source_path.is_file():
            continue
        classification = EXTENSION_LAYER_MAP.get(source_path.suffix.lower())
        if classification is None or source_path.suffix.lower() == ".do":
            continue
        root_group, side_layer = classification
        layers.append(
            LayerFile(
                source_path=source_path,
                svg_path=svg_dir / f"{source_path.name}.svg",
                root_group=root_group,
                side_layer=side_layer,
            )
        )
    if not layers:
        raise RuntimeError(f"No recognized Gerber layers found in {input_dir}")
    return layers


def render_svg(
    gerbv_exe: Path,
    input_path: Path,
    output_path: Path,
    origin: str,
    window: str,
) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.unlink(missing_ok=True)
    command = [
        str(gerbv_exe),
        "-x",
        "svg",
        "-B0",
        f"-O{origin}",
        f"-W{window}",
        "-f#000000",
        "-o",
        str(output_path),
        str(input_path),
    ]
    result = subprocess.run(
        command,
        cwd=gerbv_exe.parent,
        capture_output=True,
        text=True,
        errors="replace",
        check=False,
    )
    if result.returncode != 0 or not output_path.is_file():
        diagnostics = process_output(result) or "gerbv produced no diagnostic output"
        raise RuntimeError(
            f"gerbv failed to render {input_path.name} ({result.returncode})\n{diagnostics}"
        )


def parse_pair(
    value: str,
    description: str,
    *,
    allow_zero: bool = False,
) -> tuple[float, float]:
    match = PAIR_PATTERN.fullmatch(value)
    if match is None:
        raise ValueError(f"Invalid {description}: {value!r}; expected WIDTHxHEIGHT")
    pair = (float(match.group(1)), float(match.group(2)))
    minimum_is_invalid = (
        pair[0] < 0 or pair[1] < 0
        if allow_zero
        else pair[0] <= 0 or pair[1] <= 0
    )
    if minimum_is_invalid:
        comparison = "non-negative" if allow_zero else "greater than zero"
        raise ValueError(f"{description} values must be {comparison}")
    return pair


def gerber_bounds_inches(path: Path) -> tuple[float, float, float, float]:
    contents = path.read_text(encoding="ascii", errors="replace")
    format_match = GERBER_FORMAT_PATTERN.search(contents)
    if format_match is None:
        raise RuntimeError(f"Cannot determine Gerber coordinate format: {path}")
    x_fractional_digits = int(format_match.group(2))
    y_fractional_digits = int(format_match.group(4))

    upper_contents = contents.upper()
    if "%MOIN*%" in upper_contents:
        unit_scale = 1.0
    elif "%MOMM*%" in upper_contents:
        unit_scale = 1.0 / 25.4
    else:
        raise RuntimeError(f"Cannot determine Gerber coordinate units: {path}")

    x_values: list[float] = []
    y_values: list[float] = []
    for line in contents.splitlines():
        # Extended Gerber parameter blocks contain strings such as X25Y25 for
        # the coordinate format itself.  Only ordinary plot commands carry
        # board coordinates.
        if "%" in line:
            continue
        for axis, raw_value in GERBER_COORDINATE_PATTERN.findall(line):
            if axis.upper() == "X":
                x_values.append(
                    int(raw_value) / (10**x_fractional_digits) * unit_scale
                )
            else:
                y_values.append(
                    int(raw_value) / (10**y_fractional_digits) * unit_scale
                )
    if not x_values or not y_values:
        raise RuntimeError(f"Milling outline contains no coordinates: {path}")
    return min(x_values), min(y_values), max(x_values), max(y_values)


def automatic_render_origin(layers: list[LayerFile]) -> str:
    outline_files = [
        layer.source_path
        for layer in layers
        if layer.root_group == "dimensional"
    ]
    if not outline_files:
        raise RuntimeError("Cannot select an automatic origin without a milling outline")
    bounds = [gerber_bounds_inches(path) for path in outline_files]
    minimum_x = min(item[0] for item in bounds)
    minimum_y = min(item[1] for item in bounds)
    origin_x = max(0.0, minimum_x - OUTLINE_MARGIN_INCHES)
    origin_y = max(0.0, minimum_y - OUTLINE_MARGIN_INCHES)
    return f"{origin_x:.6f}x{origin_y:.6f}"


def render_svg_alpha(svg_path: Path, output_size: tuple[int, int]) -> object:
    try:
        import cairosvg
        from PIL import Image
    except ImportError as exc:
        raise RuntimeError(
            "CairoSVG and Pillow are required; install them with "
            "'python -m pip install CairoSVG Pillow'"
        ) from exc
    png_bytes = cairosvg.svg2png(
        url=str(svg_path),
        output_width=output_size[0],
        output_height=output_size[1],
    )
    with Image.open(io.BytesIO(png_bytes)) as image:
        image.load()
        return image.getchannel("A").copy()


def combined_layer_alpha(
    layers: list[LayerFile],
    root_group: str,
    side_layer: str | None,
    output_size: tuple[int, int],
) -> object:
    from PIL import Image, ImageChops

    selected = [
        layer
        for layer in layers
        if layer.root_group == root_group and layer.side_layer == side_layer
    ]
    combined = Image.new("L", output_size, 0)
    for layer in selected:
        alpha = render_svg_alpha(layer.svg_path, output_size)
        try:
            updated = ImageChops.lighter(combined, alpha)
        finally:
            alpha.close()
            combined.close()
        combined = updated
    return combined


def detect_board_masks(
    layers: list[LayerFile],
    final_size: tuple[int, int],
) -> tuple[object, object, object] | None:
    from PIL import ImageChops, ImageDraw

    outline_alpha = combined_layer_alpha(layers, "dimensional", None, final_size)
    outline_binary = outline_alpha.point(lambda value: 255 if value else 0, mode="1")
    outline_alpha.close()
    if outline_binary.getbbox() is None:
        outline_binary.close()
        return None

    flood_map = outline_binary.convert("L")
    flood_corner = next(
        (
            corner
            for corner in (
                (0, 0),
                (final_size[0] - 1, 0),
                (0, final_size[1] - 1),
                (final_size[0] - 1, final_size[1] - 1),
            )
            if flood_map.getpixel(corner) == 0
        ),
        None,
    )
    if flood_corner is None:
        flood_map.close()
        outline_binary.close()
        return None
    ImageDraw.floodfill(flood_map, flood_corner, 128, thresh=0)
    exterior_binary = flood_map.point(lambda value: 255 if value == 128 else 0, mode="1")
    flood_map.close()
    board_binary = ImageChops.invert(exterior_binary)
    exterior_binary.close()
    board_alpha = board_binary.convert("L")
    if board_binary.getbbox() is None:
        board_alpha.close()
        board_binary.close()
        outline_binary.close()
        return None
    return outline_binary, board_binary, board_alpha


def compose_side(
    layers: list[LayerFile],
    side: str,
    final_size: tuple[int, int],
    board_masks: tuple[object, object, object] | None,
) -> object:
    from PIL import Image, ImageChops

    render_size = tuple(value * SUPERSAMPLE_FACTOR for value in final_size)
    copper_alpha = combined_layer_alpha(layers, side, "copper", render_size)
    mask_alpha = combined_layer_alpha(layers, side, "mask", render_size)
    copper_binary = copper_alpha.point(lambda value: 255 if value else 0, mode="1")
    mask_binary = mask_alpha.point(lambda value: 255 if value else 0, mode="1")
    exposed_alpha = ImageChops.logical_and(copper_binary, mask_binary)
    illustration = Image.new("RGBA", render_size, (0, 0, 0, 0))
    try:
        illustration.paste(COPPER_COLOR, (0, 0, *render_size), copper_alpha)
        illustration.paste(EXPOSED_COPPER_COLOR, (0, 0, *render_size), exposed_alpha)
        for root_group, side_layer, color in (
            (side, "silk", SILK_COLOR),
            ("drills", None, FEATURE_COLOR),
        ):
            feature_alpha = combined_layer_alpha(
                layers,
                root_group,
                side_layer,
                render_size,
            )
            try:
                illustration.paste(color, (0, 0, *render_size), feature_alpha)
            finally:
                feature_alpha.close()
        rendered_features = illustration.resize(final_size, Image.Resampling.LANCZOS)
    finally:
        illustration.close()
        copper_alpha.close()
        mask_alpha.close()
        copper_binary.close()
        mask_binary.close()
        exposed_alpha.close()

    final_image = Image.new("RGBA", final_size, BACKGROUND_COLOR)
    try:
        if board_masks is not None:
            outline_binary, board_binary, board_alpha = board_masks
            feature_alpha = rendered_features.getchannel("A")
            clipped_alpha = ImageChops.multiply(feature_alpha, board_alpha)
            rendered_features.putalpha(clipped_alpha)
            final_image.paste(FR4_COLOR, (0, 0, *final_size), board_binary)
            final_image.alpha_composite(rendered_features)
            final_image.paste(FEATURE_COLOR, (0, 0, *final_size), outline_binary)
            feature_alpha.close()
            clipped_alpha.close()
        else:
            final_image.alpha_composite(rendered_features)
    finally:
        rendered_features.close()
    return final_image


def crop_to_content(image: object, padding: int = 10) -> object:
    from PIL import Image, ImageChops, ImageOps

    white = Image.new("RGBA", image.size, BACKGROUND_COLOR)
    try:
        difference = ImageChops.difference(image, white).convert("RGB")
        try:
            bounds = difference.getbbox()
        finally:
            difference.close()
    finally:
        white.close()
    if bounds is None:
        raise RuntimeError("Cannot crop an empty board rendering")
    cropped = image.crop(bounds)
    try:
        return ImageOps.expand(cropped, border=padding, fill=BACKGROUND_COLOR)
    finally:
        cropped.close()


def concatenate_sides(top: object, bottom_mirrored: object) -> object:
    from PIL import Image

    width = max(top.width, bottom_mirrored.width)
    output = Image.new("RGB", (width, top.height + bottom_mirrored.height), BACKGROUND_COLOR)
    top_rgb = top.convert("RGB")
    bottom_rgb = bottom_mirrored.convert("RGB")
    try:
        output.paste(top_rgb, ((width - top.width) // 2, 0))
        output.paste(
            bottom_rgb,
            ((width - bottom_mirrored.width) // 2, top.height),
        )
    finally:
        top_rgb.close()
        bottom_rgb.close()
    return output


def save_png_atomic(image: object, output_path: Path, dpi: float) -> None:
    temporary_path: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w+b",
            dir=output_path.parent,
            prefix=f".{output_path.name}-",
            suffix=".tmp.png",
            delete=False,
        ) as temporary_file:
            temporary_path = Path(temporary_file.name)
        image.save(
            temporary_path,
            format="PNG",
            dpi=(dpi, dpi),
            compress_level=9,
        )
        if temporary_path.stat().st_size == 0:
            raise RuntimeError(f"Pillow created an empty image: {output_path}")
        os.replace(temporary_path, output_path)
        temporary_path = None
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)


def render_board_frame(
    board_path: Path,
    output_path: Path,
    cam_job: Path,
    sections: list[CamSection],
    eaglecon: Path,
    gerbv: Path,
    origin: str,
    window: str,
    dpi: float,
    verbose: bool,
) -> None:
    from PIL import ImageOps

    window_inches = parse_pair(window, "window")
    final_size = (
        max(1, round(window_inches[0] * dpi)),
        max(1, round(window_inches[1] * dpi)),
    )
    with tempfile.TemporaryDirectory(prefix=f"board-history-{board_path.stem}-") as temp_name:
        build_dir = Path(temp_name).resolve()
        temporary_board = build_dir / board_path.name
        shutil.copy2(board_path, temporary_board)
        generate_cam_outputs(
            eaglecon,
            temporary_board,
            cam_job,
            build_dir,
            sections,
            verbose,
        )
        svg_dir = build_dir / "svg"
        layers = discover_layers(build_dir, svg_dir)
        render_origin = (
            automatic_render_origin(layers)
            if origin.casefold() == "auto"
            else origin
        )
        if verbose:
            print(f"Using gerbv origin {render_origin} for {board_path.name}")
        for layer in layers:
            render_svg(
                gerbv,
                layer.source_path,
                layer.svg_path,
                render_origin,
                window,
            )

        board_masks = detect_board_masks(layers, final_size)
        top_uncropped = compose_side(layers, "top", final_size, board_masks)
        bottom_uncropped = compose_side(layers, "bottom", final_size, board_masks)
        bottom_mirrored_uncropped = ImageOps.mirror(bottom_uncropped)
        try:
            top = crop_to_content(top_uncropped)
            bottom_mirrored = crop_to_content(bottom_mirrored_uncropped)
            try:
                frame = concatenate_sides(top, bottom_mirrored)
                try:
                    save_png_atomic(frame, output_path, dpi)
                finally:
                    frame.close()
            finally:
                top.close()
                bottom_mirrored.close()
        finally:
            top_uncropped.close()
            bottom_uncropped.close()
            bottom_mirrored_uncropped.close()
            if board_masks is not None:
                for mask in board_masks:
                    mask.close()


def numbered_boards(history_dir: Path) -> list[tuple[int, Path]]:
    boards: list[tuple[int, Path]] = []
    for path in history_dir.iterdir():
        match = NUMBERED_BOARD_PATTERN.fullmatch(path.name)
        if match and path.is_file():
            boards.append((int(match.group(1)), path))
    boards.sort(key=lambda item: item[0])
    if not boards:
        raise RuntimeError(
            f"No numbered .brd files found in {history_dir}; run extract_board_history.py first"
        )
    return boards


def write_png_chunk(stream: object, chunk_type: bytes, payload: bytes) -> None:
    stream.write(struct.pack(">I", len(payload)))
    stream.write(chunk_type)
    stream.write(payload)
    checksum = zlib.crc32(payload, zlib.crc32(chunk_type)) & 0xFFFFFFFF
    stream.write(struct.pack(">I", checksum))


def png_idat_payloads(contents: bytes, expected_size: tuple[int, int]) -> list[bytes]:
    if not contents.startswith(PNG_SIGNATURE):
        raise RuntimeError("Pillow produced an invalid PNG frame")
    position = len(PNG_SIGNATURE)
    image_size: tuple[int, int] | None = None
    payloads: list[bytes] = []
    while position < len(contents):
        if position + 12 > len(contents):
            raise RuntimeError("Pillow produced a truncated PNG frame")
        payload_size = struct.unpack(">I", contents[position : position + 4])[0]
        chunk_type = contents[position + 4 : position + 8]
        payload_start = position + 8
        payload_end = payload_start + payload_size
        if payload_end + 4 > len(contents):
            raise RuntimeError("Pillow produced a truncated PNG chunk")
        payload = contents[payload_start:payload_end]
        if chunk_type == b"IHDR":
            image_size = struct.unpack(">II", payload[:8])
            if payload[8:] != b"\x08\x02\x00\x00\x00":
                raise RuntimeError("Expected an 8-bit RGB PNG frame")
        elif chunk_type == b"IDAT":
            payloads.append(payload)
        elif chunk_type == b"IEND":
            break
        position = payload_end + 4
    if image_size != expected_size or not payloads:
        raise RuntimeError(
            f"Invalid PNG frame data: size={image_size}, IDAT chunks={len(payloads)}"
        )
    return payloads


def normalized_frame(path: Path, canvas_size: tuple[int, int]) -> object:
    from PIL import Image

    with Image.open(path) as source:
        source.load()
        frame = Image.new("RGB", canvas_size, BACKGROUND_COLOR)
        converted = source.convert("RGB")
        try:
            frame.paste(
                converted,
                (
                    (canvas_size[0] - converted.width) // 2,
                    (canvas_size[1] - converted.height) // 2,
                ),
            )
        finally:
            converted.close()
    return frame


def distinct_successive_frames(
    frame_paths: list[Path],
    canvas_size: tuple[int, int],
) -> list[Path]:
    from PIL import ImageChops

    previous_frame = None
    previous_path: Path | None = None
    distinct_paths: list[Path] = []
    try:
        for path in frame_paths:
            frame = normalized_frame(path, canvas_size)
            if previous_frame is not None and previous_path is not None:
                difference = ImageChops.difference(previous_frame, frame)
                try:
                    identical = difference.getbbox() is None
                finally:
                    difference.close()
                if identical:
                    print(
                        f"Skipping {path.name}; visually identical to "
                        f"{previous_path.name}"
                    )
                    frame.close()
                    continue
                previous_frame.close()
            previous_frame = frame
            previous_path = path
            distinct_paths.append(path)
    finally:
        if previous_frame is not None:
            previous_frame.close()
    return distinct_paths


def create_apng(
    frame_paths: list[Path],
    output_path: Path,
    fps: float,
) -> tuple[int, int]:
    from PIL import Image

    if not frame_paths:
        raise ValueError("Cannot create an APNG without frames")
    duration_ms = max(1, round(1000.0 / fps))
    common_divisor = math.gcd(duration_ms, 1000)
    delay_numerator = duration_ms // common_divisor
    delay_denominator = 1000 // common_divisor
    if delay_numerator > 65535 or delay_denominator > 65535:
        raise ValueError(f"Frame duration cannot be represented in APNG: {duration_ms} ms")

    sizes: list[tuple[int, int]] = []
    for path in frame_paths:
        with Image.open(path) as frame:
            sizes.append(frame.size)
    canvas_size = (
        max(size[0] for size in sizes),
        max(size[1] for size in sizes),
    )
    distinct_frame_paths = distinct_successive_frames(frame_paths, canvas_size)
    animation_frame_paths = [
        *distinct_frame_paths,
        *([distinct_frame_paths[-1]] * FINAL_FRAME_EXTRA_REPETITIONS),
    ]

    temporary_path: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w+b",
            dir=output_path.parent,
            prefix=f".{output_path.name}-",
            suffix=".tmp.png",
            delete=False,
        ) as temporary_file:
            temporary_path = Path(temporary_file.name)
            temporary_file.write(PNG_SIGNATURE)
            write_png_chunk(
                temporary_file,
                b"IHDR",
                struct.pack(">IIBBBBB", *canvas_size, 8, 2, 0, 0, 0),
            )
            write_png_chunk(
                temporary_file,
                b"acTL",
                struct.pack(">II", len(animation_frame_paths), 0),
            )

            sequence_number = 0
            for frame_index, path in enumerate(animation_frame_paths):
                frame = normalized_frame(path, canvas_size)
                try:
                    encoded_frame = io.BytesIO()
                    frame.save(
                        encoded_frame,
                        format="PNG",
                        compress_level=9,
                    )
                    idat_payloads = png_idat_payloads(
                        encoded_frame.getvalue(),
                        canvas_size,
                    )
                finally:
                    frame.close()

                write_png_chunk(
                    temporary_file,
                    b"fcTL",
                    struct.pack(
                        ">IIIIIHHBB",
                        sequence_number,
                        *canvas_size,
                        0,
                        0,
                        delay_numerator,
                        delay_denominator,
                        0,
                        0,
                    ),
                )
                sequence_number += 1
                for payload in idat_payloads:
                    if frame_index == 0:
                        write_png_chunk(temporary_file, b"IDAT", payload)
                    else:
                        write_png_chunk(
                            temporary_file,
                            b"fdAT",
                            struct.pack(">I", sequence_number) + payload,
                        )
                        sequence_number += 1

            write_png_chunk(temporary_file, b"IEND", b"")
            temporary_file.flush()
            os.fsync(temporary_file.fileno())

        if temporary_path.stat().st_size == 0:
            raise RuntimeError("Created an empty animated PNG")
        os.replace(temporary_path, output_path)
        temporary_path = None
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)
    return len(distinct_frame_paths), len(animation_frame_paths)


def main() -> int:
    args = parse_args()
    if args.dpi <= 0:
        raise ValueError("DPI must be greater than zero")
    if args.fps <= 0:
        raise ValueError("FPS must be greater than zero")
    if args.origin.casefold() != "auto":
        parse_pair(args.origin, "origin", allow_zero=True)
    parse_pair(args.window, "window")

    script_dir = Path(__file__).resolve().parent
    repo_root = script_dir.parents[1]
    history_dir = script_dir / f"history-{args.project}"
    cam_job = repo_root / "electrical" / "3rd-party" / "gerber-job-2layer.cam"
    eaglecon = args.eaglecon.expanduser().resolve()
    gerbv = args.gerbv.expanduser().resolve()
    for description, path in (
        ("history directory", history_dir),
        ("CAM job", cam_job),
        ("eaglecon executable", eaglecon),
        ("gerbv executable", gerbv),
    ):
        if description == "history directory":
            if not path.is_dir():
                raise FileNotFoundError(f"{description} not found: {path}")
        elif not path.is_file():
            raise FileNotFoundError(f"{description} not found: {path}")

    sections = parse_cam_job(cam_job)
    boards = numbered_boards(history_dir)
    created = 0
    skipped = 0
    for position, (number, board_path) in enumerate(boards, start=1):
        output_path = history_dir / f"{number}.png"
        if output_path.exists():
            if not output_path.is_file():
                raise RuntimeError(f"Frame path is not a file: {output_path}")
            skipped += 1
            print(f"[{position}/{len(boards)}] Skipping existing {output_path.name}")
            continue
        print(f"[{position}/{len(boards)}] Rendering {board_path.name}")
        render_board_frame(
            board_path,
            output_path,
            cam_job,
            sections,
            eaglecon,
            gerbv,
            args.origin,
            args.window,
            args.dpi,
            args.verbose,
        )
        created += 1
        print(f"Created {output_path}")

    frame_paths = [history_dir / f"{number}.png" for number, _ in boards]
    missing_frames = [path.name for path in frame_paths if not path.is_file()]
    if missing_frames:
        raise RuntimeError(f"Missing rendered frames: {', '.join(missing_frames)}")
    animation_path = history_dir / f"{args.project}-history.png"
    print(f"Building {animation_path.name} at {args.fps:g} FPS")
    distinct_count, animation_count = create_apng(
        frame_paths,
        animation_path,
        args.fps,
    )
    duplicate_count = len(frame_paths) - distinct_count
    print(
        f"Finished {args.project}: {len(boards)} source frames, {created} rendered, "
        f"{skipped} reused, {duplicate_count} duplicates omitted, "
        f"{animation_count} APNG frames; created {animation_path}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
