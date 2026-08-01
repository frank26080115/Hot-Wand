#!/usr/bin/env python3
"""Build combined SVG/SVGZ and side-specific PNG previews from Gerber layers."""

from __future__ import annotations

import argparse
import copy
import gzip
import os
import re
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path


DEFAULT_GERBV_EXE = Path(
    r"C:\ProgramFiles\gerbv_2023-10-11_ccf6a3_(Windows amd64)\gerbv.exe"
)

SVG_NAMESPACE = "http://www.w3.org/2000/svg"
INKSCAPE_NAMESPACE = "http://www.inkscape.org/namespaces/inkscape"
XLINK_NAMESPACE = "http://www.w3.org/1999/xlink"
SODIPODI_NAMESPACE = "http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd"

# Inkscape lists the last SVG child first in its Layers panel, so serialize
# these in reverse of the desired visible order.
ROOT_GROUP_ORDER = ("bottom", "top", "drills", "dimensional")
SIDE_LAYER_ORDER = ("copper", "mask", "cream", "silk")

# The extension alone determines where a file is placed in the merged SVG.
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

LAYER_COLORS: dict[tuple[str, str | None], str] = {
    ("dimensional", None): "#000000",
    ("drills", None): "#000000",
    ("top", "silk"): "#BFBFBF",
    ("top", "cream"): "#33FF00",  # Hue 108 degrees
    ("top", "mask"): "#FFE600",  # Hue 54 degrees
    ("top", "copper"): "#FF0000",  # Hue 0 degrees
    ("bottom", "silk"): "#BFBFBF",
    ("bottom", "cream"): "#0066FF",  # Hue 216 degrees
    ("bottom", "mask"): "#00FFB3",  # Hue 162 degrees
    ("bottom", "copper"): "#8000FF",  # Hue 270 degrees
}

LAYER_OPACITIES: dict[tuple[str, str | None], str] = {
    classification: (
        "0.8" if classification[1] in {"cream", "mask", "copper"} else "1"
    )
    for classification in LAYER_COLORS
}

INHERITED_PAINT_PROPERTIES = {
    "color",
    "fill-opacity",
    "stroke-opacity",
}

SVG_GRAPHICAL_ELEMENTS = {
    "circle",
    "ellipse",
    "foreignObject",
    "image",
    "line",
    "mesh",
    "path",
    "polygon",
    "polyline",
    "rect",
    "text",
    "textPath",
    "tspan",
    "use",
}


@dataclass(frozen=True)
class LayerFile:
    source_path: Path
    svg_path: Path
    root_group: str
    side_layer: str | None
    color: str
    opacity: str


def parse_args() -> argparse.Namespace:
    env_gerbv = os.environ.get("GERBV_EXE")

    parser = argparse.ArgumentParser(
        description=(
            "Render every recognized manufacturing layer with gerbv and combine "
            "the results into structured SVG, SVGZ, and side-specific PNG "
            "previews. Relative paths are resolved from this script's directory."
        )
    )
    parser.add_argument(
        "--gerbv",
        type=Path,
        default=Path(env_gerbv) if env_gerbv else DEFAULT_GERBV_EXE,
        help="path to gerbv.exe (default: GERBV_EXE or the configured Windows path)",
    )
    parser.add_argument(
        "--input-dir",
        type=Path,
        default=Path("gerbers"),
        help="directory containing the Gerber files (default: gerbers)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("gerbers") / "preview.svg",
        help="combined SVG output path (default: gerbers/preview.svg)",
    )
    parser.add_argument(
        "--svgz-output",
        type=Path,
        help="compressed output path (default: --output with an .svgz suffix)",
    )
    parser.add_argument(
        "--png-dpi",
        type=float,
        default=500.0,
        help="resolution for top and bottom PNG previews (default: 500 DPI)",
    )
    parser.add_argument(
        "--origin",
        default="0x0",
        help="lower-left render origin in inches as XxY (default: 0x0)",
    )
    parser.add_argument(
        "--window",
        default="5x4",
        help="shared render window in inches as WIDTHxHEIGHT (default: 5x4)",
    )
    do_file_options = parser.add_mutually_exclusive_group()
    do_file_options.add_argument(
        "--ignore-do",
        dest="ignore_do",
        action="store_true",
        default=True,
        help="ignore .do Gerber drill files (default)",
    )
    do_file_options.add_argument(
        "--include-do",
        dest="ignore_do",
        action="store_false",
        help="render and merge .do Gerber drill files",
    )
    return parser.parse_args()


def resolve_script_relative(path: Path, script_dir: Path) -> Path:
    if not path.is_absolute():
        path = script_dir / path
    return path.expanduser().resolve()


def discover_layers(
    input_dir: Path,
    svg_preview_dir: Path,
    ignore_do: bool,
) -> list[LayerFile]:
    layers: list[LayerFile] = []

    for source_path in sorted(input_dir.iterdir(), key=lambda path: path.name.casefold()):
        if not source_path.is_file():
            continue

        extension = source_path.suffix.lower()
        classification = EXTENSION_LAYER_MAP.get(extension)
        if classification is None:
            continue
        if extension == ".do" and ignore_do:
            (svg_preview_dir / f"{source_path.name}.svg").unlink(missing_ok=True)
            continue

        root_group, side_layer = classification
        layers.append(
            LayerFile(
                source_path=source_path.resolve(),
                svg_path=svg_preview_dir / f"{source_path.name}.svg",
                root_group=root_group,
                side_layer=side_layer,
                color=LAYER_COLORS[(root_group, side_layer)],
                opacity=LAYER_OPACITIES[(root_group, side_layer)],
            )
        )

    if not layers:
        extensions = ", ".join(sorted(EXTENSION_LAYER_MAP))
        raise RuntimeError(
            f"No recognized manufacturing files found in {input_dir}. "
            f"Accepted extensions: {extensions}"
        )

    return layers


def render_svg(
    gerbv_exe: Path,
    input_path: Path,
    output_path: Path,
    origin: str,
    window: str,
    color: str,
) -> None:
    output_path.unlink(missing_ok=True)
    command = [
        str(gerbv_exe),
        "-x",
        "svg",
        "-B0",
        f"-O{origin}",
        f"-W{window}",
        f"-f{color}",
        "-o",
        str(output_path),
        str(input_path),
    ]
    result = subprocess.run(
        command,
        cwd=gerbv_exe.parent,
        capture_output=True,
        text=True,
        check=False,
    )

    if result.returncode != 0 or not output_path.is_file():
        details = "\n".join(
            part.strip()
            for part in (result.stdout, result.stderr)
            if part and part.strip()
        )
        if not details:
            details = "gerbv produced no diagnostic output"
        raise RuntimeError(
            f"gerbv failed to render {input_path.name} "
            f"(exit code {result.returncode}):\n{details}"
        )


def prefix_svg_ids(element: ET.Element, prefix: str) -> None:
    id_map: dict[str, str] = {}
    for child in element.iter():
        old_id = child.get("id")
        if old_id:
            new_id = f"{prefix}-{old_id}"
            id_map[old_id] = new_id
            child.set("id", new_id)

    if not id_map:
        return

    reference_pattern = re.compile(
        r"#(" + "|".join(re.escape(old_id) for old_id in id_map) + r")\b"
    )

    def replace_reference(match: re.Match[str]) -> str:
        return f"#{id_map[match.group(1)]}"

    for child in element.iter():
        for name, value in child.attrib.items():
            child.set(name, reference_pattern.sub(replace_reference, value))
        if child.text:
            child.text = reference_pattern.sub(replace_reference, child.text)


def comparable_root_attributes(root: ET.Element) -> dict[str, str]:
    return {
        name: value
        for name, value in root.attrib.items()
        if name in {"viewBox", "width", "height", "preserveAspectRatio"}
    }


def load_svg(path: Path) -> ET.Element:
    parser = ET.XMLParser(target=ET.TreeBuilder(insert_comments=True))
    return ET.parse(path, parser=parser).getroot()


def points_to_mm(value: str) -> str:
    match = re.fullmatch(r"([0-9]+(?:\.[0-9]+)?)pt", value)
    if match is None:
        raise RuntimeError(f"Expected an SVG dimension in points, got {value!r}")
    millimetres = float(match.group(1)) * 25.4 / 72.0
    formatted = f"{millimetres:.6f}".rstrip("0").rstrip(".")
    return f"{formatted}mm"


def set_standalone_svg_properties(path: Path, opacity: str) -> None:
    root = load_svg(path)
    root.set("width", points_to_mm(root.attrib["width"]))
    root.set("height", points_to_mm(root.attrib["height"]))
    if opacity != "1":
        root.set("opacity", opacity)

    ET.register_namespace("", SVG_NAMESPACE)
    ET.register_namespace("inkscape", INKSCAPE_NAMESPACE)
    ET.register_namespace("xlink", XLINK_NAMESPACE)
    ET.register_namespace("sodipodi", SODIPODI_NAMESPACE)
    ET.SubElement(
        root,
        f"{{{SODIPODI_NAMESPACE}}}namedview",
        {f"{{{INKSCAPE_NAMESPACE}}}document-units": "mm"},
    )
    ET.ElementTree(root).write(path, encoding="utf-8", xml_declaration=True)


def svg_id(text: str) -> str:
    normalized = re.sub(r"[^A-Za-z0-9_.-]+", "-", text).strip("-.")
    return f"file-{normalized or 'layer'}"


def layer_css_class(root_group: str, side_layer: str | None) -> str:
    if side_layer is None:
        return root_group
    return f"{root_group}-{side_layer}"


def merged_svg_stylesheet() -> str:
    rules: dict[tuple[str, str], list[str]] = {}
    for classification, color in LAYER_COLORS.items():
        opacity = LAYER_OPACITIES[classification]
        rules.setdefault((color, opacity), []).append(
            f".{layer_css_class(*classification)}"
        )

    blocks: list[str] = []
    for (color, opacity), selectors in rules.items():
        blocks.append(
            ",\n".join(selectors)
            + " {\n"
            + f"  fill: {color};\n"
            + f"  stroke: {color};\n"
            + f"  opacity: {opacity};\n"
            + "}"
        )
    return "\n\n".join(blocks)


def remove_inline_layer_paint(element: ET.Element) -> None:
    """Let the layer group's embedded CSS provide color and opacity."""
    for child in element.iter():
        for attribute in INHERITED_PAINT_PROPERTIES:
            child.attrib.pop(attribute, None)

        for attribute in ("fill", "stroke"):
            value = child.get(attribute)
            if value is not None and value.strip().casefold() != "none":
                child.attrib.pop(attribute)

        style = child.get("style")
        if style is None:
            continue

        retained: list[str] = []
        for declaration in style.split(";"):
            declaration = declaration.strip()
            if not declaration:
                continue
            if ":" not in declaration:
                retained.append(declaration)
                continue

            property_name, value = declaration.split(":", 1)
            property_name = property_name.strip().casefold()
            if property_name in INHERITED_PAINT_PROPERTIES:
                continue
            if (
                property_name in {"fill", "stroke"}
                and value.strip().casefold() != "none"
            ):
                continue
            retained.append(declaration)

        if retained:
            child.set("style", ";".join(retained) + ";")
        else:
            child.attrib.pop("style", None)


def local_tag_name(element: ET.Element) -> str:
    if not isinstance(element.tag, str):
        return ""
    return element.tag.rsplit("}", 1)[-1]


def replace_graphical_styles_with_classes(
    root: ET.Element,
    style_element: ET.Element,
) -> dict[str, str]:
    """Move duplicate inline graphical styles into numbered CSS classes."""
    used_class_names = {
        class_name
        for element in root.iter()
        for class_name in element.get("class", "").split()
    }
    unique_styles: list[str] = []
    seen_styles: set[str] = set()

    for element in root.iter():
        if local_tag_name(element) not in SVG_GRAPHICAL_ELEMENTS:
            continue
        style = element.get("style")
        if style is not None and style not in seen_styles:
            seen_styles.add(style)
            unique_styles.append(style)

    style_to_class: dict[str, str] = {}
    next_class_number = 1
    for style in unique_styles:
        while f"class{next_class_number}" in used_class_names:
            next_class_number += 1
        class_name = f"class{next_class_number}"
        next_class_number += 1
        used_class_names.add(class_name)
        style_to_class[style] = class_name

    for element in root.iter():
        if local_tag_name(element) not in SVG_GRAPHICAL_ELEMENTS:
            continue
        style = element.attrib.pop("style", None)
        if style is None:
            continue
        class_name = style_to_class[style]
        existing_classes = element.get("class", "").split()
        existing_classes.append(class_name)
        element.set("class", " ".join(existing_classes))

    if style_to_class:
        numbered_rules = "\n".join(
            f".{class_name}{{{style}}}"
            for style, class_name in style_to_class.items()
        )
        existing_stylesheet = (style_element.text or "").strip()
        style_element.text = (
            f"\n{existing_stylesheet}\n\n{numbered_rules}\n"
        )

    return style_to_class


def create_layer_group(parent: ET.Element, group_id: str, label: str) -> ET.Element:
    return ET.SubElement(
        parent,
        f"{{{SVG_NAMESPACE}}}g",
        {
            "id": group_id,
            f"{{{INKSCAPE_NAMESPACE}}}groupmode": "layer",
            f"{{{INKSCAPE_NAMESPACE}}}label": label,
        },
    )


def append_file_svg(parent: ET.Element, layer: LayerFile, source_root: ET.Element) -> None:
    file_group_id = svg_id(layer.source_path.name)
    prefix_svg_ids(source_root, file_group_id)
    remove_inline_layer_paint(source_root)
    file_group = create_layer_group(parent, file_group_id, layer.source_path.name)
    for child in list(source_root):
        if child.tag == f"{{{SODIPODI_NAMESPACE}}}namedview":
            continue
        file_group.append(child)


def merge_svgs(layers: list[LayerFile], output_path: Path) -> None:
    loaded_layers = [(layer, load_svg(layer.svg_path)) for layer in layers]
    reference_root = loaded_layers[0][1]
    reference_geometry = comparable_root_attributes(reference_root)

    for layer, source_root in loaded_layers[1:]:
        geometry = comparable_root_attributes(source_root)
        if geometry != reference_geometry:
            raise RuntimeError(
                f"The SVG canvas for {layer.source_path.name} does not match:\n"
                f"expected: {reference_geometry}\n"
                f"actual: {geometry}"
            )

    ET.register_namespace("", SVG_NAMESPACE)
    ET.register_namespace("inkscape", INKSCAPE_NAMESPACE)
    ET.register_namespace("xlink", XLINK_NAMESPACE)
    ET.register_namespace("sodipodi", SODIPODI_NAMESPACE)

    merged_attributes = dict(reference_root.attrib)
    merged_attributes.setdefault("version", "1.1")
    merged_root = ET.Element(f"{{{SVG_NAMESPACE}}}svg", merged_attributes)
    ET.SubElement(
        merged_root,
        f"{{{SODIPODI_NAMESPACE}}}namedview",
        {f"{{{INKSCAPE_NAMESPACE}}}document-units": "mm"},
    )
    style = ET.SubElement(
        merged_root,
        f"{{{SVG_NAMESPACE}}}style",
        {"type": "text/css"},
    )
    style.text = "\n" + merged_svg_stylesheet() + "\n"

    for root_group_name in ROOT_GROUP_ORDER:
        root_group = create_layer_group(
            merged_root,
            root_group_name,
            root_group_name.capitalize(),
        )

        if root_group_name in {"top", "bottom"}:
            for side_layer_name in SIDE_LAYER_ORDER:
                side_group = create_layer_group(
                    root_group,
                    f"{root_group_name}-{side_layer_name}",
                    side_layer_name.capitalize(),
                )
                side_group.set(
                    "class",
                    layer_css_class(root_group_name, side_layer_name),
                )
                for layer, source_root in loaded_layers:
                    if (
                        layer.root_group == root_group_name
                        and layer.side_layer == side_layer_name
                    ):
                        append_file_svg(side_group, layer, source_root)
        else:
            root_group.set("class", layer_css_class(root_group_name, None))
            for layer, source_root in loaded_layers:
                if layer.root_group == root_group_name:
                    append_file_svg(root_group, layer, source_root)

    replace_graphical_styles_with_classes(merged_root, style)

    if hasattr(ET, "indent"):
        ET.indent(merged_root, space="  ")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    ET.ElementTree(merged_root).write(
        output_path,
        encoding="utf-8",
        xml_declaration=True,
    )


def compress_svg(svg_path: Path, svgz_path: Path) -> None:
    if svg_path.resolve() == svgz_path.resolve():
        raise ValueError("The SVG and SVGZ output paths must be different")

    svgz_path.parent.mkdir(parents=True, exist_ok=True)
    temporary_path: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w+b",
            dir=svgz_path.parent,
            prefix=f".{svgz_path.name}-",
            suffix=".tmp",
            delete=False,
        ) as temporary_file:
            temporary_path = Path(temporary_file.name)
            with svg_path.open("rb") as source:
                with gzip.GzipFile(
                    filename="",
                    mode="wb",
                    compresslevel=9,
                    fileobj=temporary_file,
                    mtime=0,
                ) as compressed:
                    shutil.copyfileobj(source, compressed)

        os.replace(temporary_path, svgz_path)
        temporary_path = None
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)


def side_png_path(svg_path: Path, side: str) -> Path:
    return svg_path.with_name(f"{svg_path.stem}-{side}.png")


def create_mirrored_png(source_path: Path, output_path: Path, dpi: float) -> None:
    try:
        from PIL import Image, ImageOps
    except ImportError as exc:
        raise RuntimeError(
            "Pillow is required for mirrored PNG previews; install it with "
            "'python -m pip install Pillow'"
        ) from exc

    output_path.parent.mkdir(parents=True, exist_ok=True)
    temporary_path: Path | None = None
    mirrored_image = None
    try:
        with Image.open(source_path) as source_image:
            source_image.load()
            mirrored_image = ImageOps.mirror(source_image)

        with tempfile.NamedTemporaryFile(
            mode="w+b",
            dir=output_path.parent,
            prefix=f".{output_path.name}-",
            suffix=".tmp.png",
            delete=False,
        ) as temporary_file:
            temporary_path = Path(temporary_file.name)

        mirrored_image.save(
            temporary_path,
            format="PNG",
            dpi=(dpi, dpi),
            compress_level=9,
        )
        if temporary_path.stat().st_size == 0:
            raise RuntimeError("Pillow created an empty mirrored bottom PNG preview")

        os.replace(temporary_path, output_path)
        temporary_path = None
    finally:
        if mirrored_image is not None:
            mirrored_image.close()
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)


def render_side_pngs(svg_path: Path, dpi: float) -> list[Path]:
    if dpi <= 0:
        raise ValueError(f"PNG DPI must be greater than zero, got {dpi}")

    try:
        import cairosvg
        from PIL import Image
    except ImportError as exc:
        raise RuntimeError(
            "CairoSVG and Pillow are required for PNG previews; install them with "
            "'python -m pip install CairoSVG'"
        ) from exc

    source_root = load_svg(svg_path)
    generated_paths: list[Path] = []

    for side in ("top", "bottom"):
        side_root = copy.deepcopy(source_root)
        root_groups = {
            child.get("id"): child
            for child in list(side_root)
            if local_tag_name(child) == "g"
            and child.get("id") in {"top", "bottom", "drills", "dimensional"}
        }
        missing_groups = {
            side,
            "drills",
            "dimensional",
        } - root_groups.keys()
        if missing_groups:
            raise RuntimeError(
                "Cannot render side preview; missing SVG layer groups: "
                + ", ".join(sorted(missing_groups))
            )

        opposite_side = "bottom" if side == "top" else "top"
        opposite_group = root_groups.get(opposite_side)
        if opposite_group is not None:
            side_root.remove(opposite_group)

        output_path = side_png_path(svg_path, side)
        output_path.parent.mkdir(parents=True, exist_ok=True)
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
                cairosvg.svg2png(
                    bytestring=ET.tostring(
                        side_root,
                        encoding="utf-8",
                        xml_declaration=True,
                    ),
                    write_to=temporary_file,
                    dpi=dpi,
                )
                temporary_file.flush()

            with Image.open(temporary_path) as rendered_png:
                rendered_png.load()
                png_with_dpi = rendered_png.copy()
            try:
                png_with_dpi.save(
                    temporary_path,
                    format="PNG",
                    dpi=(dpi, dpi),
                    compress_level=9,
                )
            finally:
                png_with_dpi.close()

            if temporary_path.stat().st_size == 0:
                raise RuntimeError(f"CairoSVG created an empty {side} PNG preview")
            os.replace(temporary_path, output_path)
            temporary_path = None
        finally:
            if temporary_path is not None:
                temporary_path.unlink(missing_ok=True)

        generated_paths.append(output_path)
        if side == "bottom":
            mirrored_path = side_png_path(svg_path, "bottom-mirrored")
            create_mirrored_png(output_path, mirrored_path, dpi)
            generated_paths.append(mirrored_path)

    return generated_paths


def main() -> int:
    args = parse_args()
    script_dir = Path(__file__).resolve().parent
    gerbv_exe = resolve_script_relative(args.gerbv, script_dir)
    input_dir = resolve_script_relative(args.input_dir, script_dir)
    output_path = resolve_script_relative(args.output, script_dir)
    svgz_path = (
        resolve_script_relative(args.svgz_output, script_dir)
        if args.svgz_output is not None
        else output_path.with_suffix(".svgz")
    )
    svg_preview_dir = input_dir / "svg_preview"

    if not gerbv_exe.is_file():
        raise FileNotFoundError(f"gerbv executable not found: {gerbv_exe}")
    if not input_dir.is_dir():
        raise FileNotFoundError(f"Gerber input directory not found: {input_dir}")
    if args.png_dpi <= 0:
        raise ValueError(f"PNG DPI must be greater than zero, got {args.png_dpi}")

    svg_preview_dir.mkdir(parents=True, exist_ok=True)
    layers = discover_layers(input_dir, svg_preview_dir, args.ignore_do)

    for layer in layers:
        render_svg(
            gerbv_exe,
            layer.source_path,
            layer.svg_path,
            args.origin,
            args.window,
            layer.color,
        )
        set_standalone_svg_properties(layer.svg_path, layer.opacity)
        print(f"Rendered {layer.source_path.name}")

    merge_svgs(layers, output_path)
    print(f"Created {output_path}")
    compress_svg(output_path, svgz_path)
    print(f"Created {svgz_path}")
    for png_path in render_side_pngs(output_path, args.png_dpi):
        print(f"Created {png_path} at {args.png_dpi:g} DPI")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
