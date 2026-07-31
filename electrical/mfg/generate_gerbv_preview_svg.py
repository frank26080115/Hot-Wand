#!/usr/bin/env python3
"""Render each Gerber layer to SVG, then combine them into one preview."""

from __future__ import annotations

import argparse
import os
import re
import subprocess
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
            "the results into a structured SVG preview. Relative paths are "
            "resolved from this script's directory."
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
    file_group = create_layer_group(parent, file_group_id, layer.source_path.name)
    if layer.opacity != "1":
        file_group.set("opacity", layer.opacity)
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
                for layer, source_root in loaded_layers:
                    if (
                        layer.root_group == root_group_name
                        and layer.side_layer == side_layer_name
                    ):
                        append_file_svg(side_group, layer, source_root)
        else:
            for layer, source_root in loaded_layers:
                if layer.root_group == root_group_name:
                    append_file_svg(root_group, layer, source_root)

    if hasattr(ET, "indent"):
        ET.indent(merged_root, space="  ")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    ET.ElementTree(merged_root).write(
        output_path,
        encoding="utf-8",
        xml_declaration=True,
    )


def main() -> int:
    args = parse_args()
    script_dir = Path(__file__).resolve().parent
    gerbv_exe = resolve_script_relative(args.gerbv, script_dir)
    input_dir = resolve_script_relative(args.input_dir, script_dir)
    output_path = resolve_script_relative(args.output, script_dir)
    svg_preview_dir = input_dir / "svg_preview"

    if not gerbv_exe.is_file():
        raise FileNotFoundError(f"gerbv executable not found: {gerbv_exe}")
    if not input_dir.is_dir():
        raise FileNotFoundError(f"Gerber input directory not found: {input_dir}")

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
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
