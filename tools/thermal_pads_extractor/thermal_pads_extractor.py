#!/usr/bin/env python3
"""Extract EAGLE cooling-layer polygons into a millimetre-scale SVG."""

from __future__ import annotations

import argparse
import math
import xml.etree.ElementTree as ET
from datetime import datetime
from pathlib import Path


SVG_NAMESPACE = "http://www.w3.org/2000/svg"
COOLING_LAYER_NUMBER = 254
COOLING_LAYER_NAME = "cooling"
CANVAS_WIDTH_MM = 150.0
CANVAS_HEIGHT_MM = 100.0
STROKE_WIDTH_MM = 0.2


def number(value: str | None, default: float = 0.0) -> float:
    """Parse an EAGLE numeric attribute."""

    if value is None or value == "":
        return default
    return float(value)


def svg_number(value: float) -> str:
    """Format a coordinate without unnecessary trailing zeroes."""

    if abs(value) < 0.0000005:
        value = 0.0
    return f"{value:.6f}".rstrip("0").rstrip(".") or "0"


def board_to_svg(x: float, y: float) -> tuple[float, float]:
    """Map EAGLE's bottom-left, Y-up coordinates to SVG's Y-down canvas."""

    return x, CANVAS_HEIGHT_MM - y


def arc_segment(
    start: tuple[float, float],
    end: tuple[float, float],
    curve_degrees: float,
) -> str:
    """Create an SVG arc for an EAGLE polygon edge."""

    chord = math.dist(start, end)
    sine = math.sin(math.radians(abs(curve_degrees)) / 2.0)
    if chord == 0.0 or abs(sine) < 0.0000001:
        return f"L {svg_number(end[0])} {svg_number(end[1])}"

    radius = chord / (2.0 * abs(sine))
    large_arc = "1" if abs(curve_degrees) > 180.0 else "0"
    # Flipping EAGLE's Y-up coordinates reverses the SVG sweep direction.
    sweep = "0" if curve_degrees > 0.0 else "1"
    return (
        f"A {svg_number(radius)} {svg_number(radius)} 0 "
        f"{large_arc} {sweep} {svg_number(end[0])} {svg_number(end[1])}"
    )


def polygon_path_data(polygon: ET.Element) -> str | None:
    """Convert one EAGLE polygon into a closed SVG path."""

    vertices = list(polygon.findall("vertex"))
    if len(vertices) < 3:
        return None

    points = [
        board_to_svg(number(vertex.get("x")), number(vertex.get("y")))
        for vertex in vertices
    ]
    commands = [f"M {svg_number(points[0][0])} {svg_number(points[0][1])}"]

    for index in range(1, len(vertices)):
        curve = number(vertices[index - 1].get("curve"))
        if curve:
            commands.append(arc_segment(points[index - 1], points[index], curve))
        else:
            commands.append(
                f"L {svg_number(points[index][0])} {svg_number(points[index][1])}"
            )

    closing_curve = number(vertices[-1].get("curve"))
    if closing_curve:
        commands.append(arc_segment(points[-1], points[0], closing_curve))
    commands.append("Z")
    return " ".join(commands)


def parse_cooling_polygons(board_path: Path) -> list[ET.Element]:
    """Return direct <plain> polygons assigned to the cooling layer."""

    tree = ET.parse(board_path)
    drawing = tree.getroot().find("drawing")
    if drawing is None:
        raise ValueError(f"{board_path} has no <drawing> section")

    layers = drawing.find("layers")
    if layers is None:
        raise ValueError(f"{board_path} has no <layers> section")
    cooling_layer = next(
        (
            layer
            for layer in layers.findall("layer")
            if int(layer.get("number", "0")) == COOLING_LAYER_NUMBER
        ),
        None,
    )
    if cooling_layer is None:
        raise ValueError(
            f"{board_path} does not define layer {COOLING_LAYER_NUMBER} "
            f"({COOLING_LAYER_NAME})"
        )
    if cooling_layer.get("name") != COOLING_LAYER_NAME:
        raise ValueError(
            f"Layer {COOLING_LAYER_NUMBER} is named "
            f"{cooling_layer.get('name')!r}, not {COOLING_LAYER_NAME!r}"
        )

    board = drawing.find("board")
    if board is None:
        raise ValueError(f"{board_path} has no <board> section")
    plain = board.find("plain")
    if plain is None:
        raise ValueError(f"{board_path} has no <plain> section")

    return [
        child
        for child in plain
        if child.tag == "polygon"
        and int(child.get("layer", "0")) == COOLING_LAYER_NUMBER
    ]


def generate_svg(board_path: Path, output_path: Path) -> int:
    """Extract cooling polygons and write one SVG file."""

    polygons = parse_cooling_polygons(board_path)

    ET.register_namespace("", SVG_NAMESPACE)
    root = ET.Element(
        f"{{{SVG_NAMESPACE}}}svg",
        {
            "width": f"{svg_number(CANVAS_WIDTH_MM)}mm",
            "height": f"{svg_number(CANVAS_HEIGHT_MM)}mm",
            "viewBox": (
                f"0 0 {svg_number(CANVAS_WIDTH_MM)} {svg_number(CANVAS_HEIGHT_MM)}"
            ),
        },
    )
    root.append(
        ET.Comment(" EAGLE board (0,0) maps to SVG (0,100); all dimensions are mm. ")
    )

    emitted_count = 0
    for source_index, polygon in enumerate(polygons, start=1):
        path_data = polygon_path_data(polygon)
        if path_data is None:
            continue
        ET.SubElement(
            root,
            f"{{{SVG_NAMESPACE}}}path",
            {
                "d": path_data,
                "fill": "none",
                "stroke": "#000000",
                "stroke-width": svg_number(STROKE_WIDTH_MM),
                "stroke-linejoin": "round",
                "data-source-layer": str(COOLING_LAYER_NUMBER),
                "data-source-index": str(source_index),
            },
        )
        emitted_count += 1

    ET.indent(root, space="  ")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    ET.ElementTree(root).write(output_path, encoding="utf-8", xml_declaration=True)
    return emitted_count


def default_paths() -> tuple[Path, Path]:
    repository_root = Path(__file__).resolve().parents[2]
    board_path = repository_root / "electrical" / "hot-wand.brd"
    timestamp = datetime.now().strftime("%Y%m%d%H%M%S")
    output_path = repository_root / "mechanical" / f"thermal-pads-{timestamp}.svg"
    return board_path, output_path


def parse_arguments() -> argparse.Namespace:
    default_board, default_output = default_paths()
    parser = argparse.ArgumentParser(
        description="Extract EAGLE layer-254 cooling polygons into an SVG."
    )
    parser.add_argument(
        "--board",
        type=Path,
        default=default_board,
        help=f"input EAGLE board (default: {default_board})",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=default_output,
        help=f"output SVG (default: {default_output})",
    )
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    board_path = arguments.board.resolve()
    output_path = arguments.output.resolve()
    polygon_count = generate_svg(board_path, output_path)
    print(f"Generated {output_path}")
    print(
        f"Extracted {polygon_count} polygons from "
        f"layer {COOLING_LAYER_NUMBER} ({COOLING_LAYER_NAME})."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
