#!/usr/bin/env python3
"""Generate a laser-cut thermal-pad SVG from an Autodesk EAGLE board file."""

from __future__ import annotations

import argparse
import math
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Callable, Iterator


SVG_NS = "http://www.w3.org/2000/svg"
INKSCAPE_NS = "http://www.inkscape.org/namespaces/inkscape"
CANVAS_WIDTH_MM = 150.0
CANVAS_HEIGHT_MM = 100.0
RECTANGLE_STROKE_WIDTH_MM = 0.2
MINIMUM_VISIBLE_STROKE_WIDTH_MM = 0.2

SIDE_TOP = "top"
SIDE_BOTTOM = "bottom"
SIDES = (SIDE_TOP, SIDE_BOTTOM)

LAYER_PCB_OUTLINE = "pcb-outline"
LAYER_PLAIN_TOP = "plain-top"
LAYER_PLAIN_BOTTOM = "plain-bottom"
LAYER_THROUGH_HOLE = "through-hole-pad-solder-areas"
LAYER_TOP_MECHANICAL = "top-mechanical"
LAYER_BOTTOM_MECHANICAL = "bottom-mechanical"

OUTPUT_LAYER_SPECS = {
    LAYER_PCB_OUTLINE: ("PCB outline", "#202020"),
    LAYER_PLAIN_TOP: ("stuff from <plain> but top layer only", "#e69f00"),
    LAYER_PLAIN_BOTTOM: ("stuff from <plain> but bottom layer only", "#56b4e9"),
    LAYER_THROUGH_HOLE: ("all through hole pad solder areas", "#009e73"),
    LAYER_TOP_MECHANICAL: ("top mechanical", "#d55e00"),
    LAYER_BOTTOM_MECHANICAL: ("bottom mechanical", "#0072b2"),
}

COMMON_BOUND_LAYERS = {"Dimension", "Milling"}
TOP_BOUND_LAYERS = {"Top", "tStop", "tPlace", "tRestrict", "tKeepout"}
BOTTOM_BOUND_LAYERS = {"Bottom", "bStop", "bPlace", "bRestrict", "bKeepout"}
BOUND_LAYERS = COMMON_BOUND_LAYERS | TOP_BOUND_LAYERS | BOTTOM_BOUND_LAYERS


def number(value: str | None, default: float = 0.0) -> float:
    """Parse an EAGLE numeric attribute."""

    if value is None or value == "":
        return default
    return float(value)


def eagle_length_mm(value: str | None, default: float = 0.0) -> float:
    """Parse an EAGLE design-rule length and return millimetres."""

    if value is None or value == "":
        return default
    text = value.strip().lower()
    unit_factors = {
        "mm": 1.0,
        "mil": 0.0254,
        "inch": 25.4,
        "in": 25.4,
    }
    for suffix, factor in unit_factors.items():
        if text.endswith(suffix):
            return float(text[: -len(suffix)]) * factor
    return float(text)


def svg_number(value: float) -> str:
    """Format millimetre coordinates without unnecessary trailing zeroes."""

    if abs(value) < 0.0000005:
        value = 0.0
    return f"{value:.6f}".rstrip("0").rstrip(".") or "0"


@dataclass(frozen=True)
class Rotation:
    angle_degrees: float = 0.0
    mirrored: bool = False


def parse_rotation(value: str | None) -> Rotation:
    """Parse EAGLE rotations such as R90, MR270, or SMR45."""

    text = (value or "").upper()
    marker = text.rfind("R")
    if marker < 0:
        return Rotation(mirrored="M" in text)
    angle_text = text[marker + 1 :]
    return Rotation(
        angle_degrees=number(angle_text, 0.0),
        mirrored="M" in text[:marker],
    )


def rotate_point(x: float, y: float, angle_degrees: float) -> tuple[float, float]:
    """Rotate a point counter-clockwise in EAGLE's Y-up coordinate system."""

    angle = math.radians(angle_degrees)
    cosine = math.cos(angle)
    sine = math.sin(angle)
    return x * cosine - y * sine, x * sine + y * cosine


def transform_point(
    x: float,
    y: float,
    origin_x: float,
    origin_y: float,
    rotation: Rotation,
) -> tuple[float, float]:
    """Transform package-local coordinates into board coordinates.

    EAGLE applies the element rotation first, then mirrors the result across
    the Y axis.
    """

    x, y = rotate_point(x, y, rotation.angle_degrees)
    if rotation.mirrored:
        x = -x
    return x + origin_x, y + origin_y


def board_to_svg(x: float, y: float) -> tuple[float, float]:
    """Map EAGLE's bottom-left, Y-up coordinates to SVG's top-left, Y-down."""

    return x, CANVAS_HEIGHT_MM - y


@dataclass
class Bounds:
    """Axis-aligned bounds which remain empty until geometry is included."""

    left: float | None = None
    bottom: float | None = None
    right: float | None = None
    top: float | None = None

    @property
    def empty(self) -> bool:
        return self.left is None

    def include_point(self, x: float, y: float) -> None:
        if self.empty:
            self.left = self.right = x
            self.bottom = self.top = y
            return
        assert self.left is not None
        assert self.right is not None
        assert self.bottom is not None
        assert self.top is not None
        self.left = min(self.left, x)
        self.right = max(self.right, x)
        self.bottom = min(self.bottom, y)
        self.top = max(self.top, y)

    def include_box(self, left: float, bottom: float, right: float, top: float) -> None:
        self.include_point(min(left, right), min(bottom, top))
        self.include_point(max(left, right), max(bottom, top))

    def include_centered(
        self, center_x: float, center_y: float, width: float, height: float
    ) -> None:
        self.include_box(
            center_x - width / 2.0,
            center_y - height / 2.0,
            center_x + width / 2.0,
            center_y + height / 2.0,
        )

    def corners(self) -> Iterator[tuple[float, float]]:
        if self.empty:
            return
        assert self.left is not None
        assert self.right is not None
        assert self.bottom is not None
        assert self.top is not None
        yield self.left, self.bottom
        yield self.left, self.top
        yield self.right, self.bottom
        yield self.right, self.top


@dataclass
class PadArea:
    name: str
    bounds: Bounds


@dataclass(frozen=True)
class RestringRule:
    ratio: float
    minimum_mm: float
    maximum_mm: float

    def pad_diameter(self, drill_mm: float) -> float:
        annular_ring = max(drill_mm * self.ratio, self.minimum_mm)
        if self.maximum_mm > 0.0:
            annular_ring = min(annular_ring, self.maximum_mm)
        return drill_mm + 2.0 * annular_ring


@dataclass(frozen=True)
class PadDiameterRules:
    top: RestringRule | None
    bottom: RestringRule | None

    def solder_area_diameter(self, drill_mm: float) -> float:
        diameters = [
            rule.pad_diameter(drill_mm)
            for rule in (self.top, self.bottom)
            if rule is not None
        ]
        return max(diameters, default=drill_mm)


@dataclass
class PackageInfo:
    library_name: str
    package_name: str
    element: ET.Element
    bounds: dict[str, Bounds]
    through_hole_pads: list[PadArea]
    mechanically_exists: dict[str, bool]


def layer_side(layer_name: str) -> str | None:
    if layer_name in TOP_BOUND_LAYERS:
        return SIDE_TOP
    if layer_name in BOTTOM_BOUND_LAYERS:
        return SIDE_BOTTOM
    if layer_name == "Top":
        return SIDE_TOP
    if layer_name == "Bottom":
        return SIDE_BOTTOM
    if layer_name.startswith("t"):
        return SIDE_TOP
    if layer_name.startswith("b"):
        return SIDE_BOTTOM
    return None


def opposite_side(side: str) -> str:
    return SIDE_BOTTOM if side == SIDE_TOP else SIDE_TOP


def primitive_sides(primitive: ET.Element, layers: dict[int, str]) -> tuple[str, ...]:
    """Return the package sides affected by a primitive."""

    if primitive.tag in {"pad", "hole"}:
        return SIDES

    layer_number = primitive.get("layer")
    if layer_number is None:
        return ()
    layer_name = layers.get(int(layer_number), f"Layer{layer_number}")
    if layer_name not in BOUND_LAYERS:
        return ()
    if layer_name in COMMON_BOUND_LAYERS:
        return SIDES

    side = layer_side(layer_name)
    if side is None:
        return ()
    if primitive.tag == "smd" and parse_rotation(primitive.get("rot")).mirrored:
        side = opposite_side(side)
    return (side,)


def include_rotated_rectangle(
    bounds: Bounds,
    center_x: float,
    center_y: float,
    width: float,
    height: float,
    angle_degrees: float,
) -> None:
    half_width = width / 2.0
    half_height = height / 2.0
    for local_x, local_y in (
        (-half_width, -half_height),
        (-half_width, half_height),
        (half_width, -half_height),
        (half_width, half_height),
    ):
        rotated_x, rotated_y = rotate_point(local_x, local_y, angle_degrees)
        bounds.include_point(center_x + rotated_x, center_y + rotated_y)


def include_primitive(bounds: Bounds, primitive: ET.Element) -> None:
    """Grow bounds using one supported EAGLE package primitive."""

    tag = primitive.tag
    if tag == "pad":
        drill = number(primitive.get("drill"))
        diameter = number(primitive.get("diameter"), drill)
        bounds.include_centered(
            number(primitive.get("x")),
            number(primitive.get("y")),
            diameter,
            diameter,
        )
    elif tag == "hole":
        drill = number(primitive.get("drill"))
        bounds.include_centered(
            number(primitive.get("x")),
            number(primitive.get("y")),
            drill,
            drill,
        )
    elif tag == "rectangle":
        x1 = number(primitive.get("x1"))
        y1 = number(primitive.get("y1"))
        x2 = number(primitive.get("x2"))
        y2 = number(primitive.get("y2"))
        rotation = parse_rotation(primitive.get("rot"))
        center_x = (x1 + x2) / 2.0
        center_y = (y1 + y2) / 2.0
        include_rotated_rectangle(
            bounds,
            center_x,
            center_y,
            abs(x2 - x1),
            abs(y2 - y1),
            rotation.angle_degrees,
        )
    elif tag == "circle":
        extent = number(primitive.get("radius")) + number(primitive.get("width")) / 2.0
        bounds.include_centered(
            number(primitive.get("x")),
            number(primitive.get("y")),
            extent * 2.0,
            extent * 2.0,
        )
    elif tag == "wire":
        half_width = number(primitive.get("width")) / 2.0
        bounds.include_box(
            min(number(primitive.get("x1")), number(primitive.get("x2"))) - half_width,
            min(number(primitive.get("y1")), number(primitive.get("y2"))) - half_width,
            max(number(primitive.get("x1")), number(primitive.get("x2"))) + half_width,
            max(number(primitive.get("y1")), number(primitive.get("y2"))) + half_width,
        )
    elif tag == "smd":
        rotation = parse_rotation(primitive.get("rot"))
        include_rotated_rectangle(
            bounds,
            number(primitive.get("x")),
            number(primitive.get("y")),
            number(primitive.get("dx")),
            number(primitive.get("dy")),
            rotation.angle_degrees,
        )
    elif tag == "polygon":
        half_width = number(primitive.get("width")) / 2.0
        vertices = list(primitive.findall("vertex"))
        if not vertices:
            return
        left = min(number(vertex.get("x")) for vertex in vertices) - half_width
        right = max(number(vertex.get("x")) for vertex in vertices) + half_width
        bottom = min(number(vertex.get("y")) for vertex in vertices) - half_width
        top = max(number(vertex.get("y")) for vertex in vertices) + half_width
        bounds.include_box(left, bottom, right, top)


def through_hole_pad_diameter(
    pad: ET.Element, pad_diameter_rules: PadDiameterRules
) -> float:
    drill = number(pad.get("drill"))
    explicit_diameter = number(pad.get("diameter"))
    design_rule_diameter = pad_diameter_rules.solder_area_diameter(drill)
    return max(explicit_diameter, design_rule_diameter)


def through_hole_pad_area(
    pad: ET.Element, pad_diameter_rules: PadDiameterRules
) -> PadArea:
    diameter = through_hole_pad_diameter(pad, pad_diameter_rules)
    bounds = Bounds()
    bounds.include_centered(
        number(pad.get("x")),
        number(pad.get("y")),
        diameter,
        diameter,
    )
    return PadArea(name=pad.get("name", ""), bounds=bounds)


def mechanical_presence_side(
    primitive: ET.Element, layers: dict[int, str]
) -> str | None:
    """Return the side on which a primitive establishes a physical body."""

    layer_number = primitive.get("layer")
    if layer_number is None:
        return None
    layer_name = layers.get(int(layer_number), f"Layer{layer_number}")

    if primitive.tag == "smd":
        side = layer_side(layer_name)
        if side is not None and parse_rotation(primitive.get("rot")).mirrored:
            side = opposite_side(side)
        return side

    if layer_name in {"tPlace", "tKeepout", "tRestrict"}:
        return SIDE_TOP
    if layer_name in {"bPlace", "bKeepout", "bRestrict"}:
        return SIDE_BOTTOM
    return None


def compute_package_geometry(
    package: ET.Element,
    layers: dict[int, str],
    pad_diameter_rules: PadDiameterRules,
) -> tuple[dict[str, Bounds], list[PadArea], dict[str, bool]]:
    bounds = {SIDE_TOP: Bounds(), SIDE_BOTTOM: Bounds()}
    through_hole_pads: list[PadArea] = []
    mechanically_exists = {SIDE_TOP: False, SIDE_BOTTOM: False}

    for primitive in package:
        if primitive.tag == "text":
            continue
        if primitive.tag == "pad":
            through_hole_pads.append(
                through_hole_pad_area(primitive, pad_diameter_rules)
            )
            continue

        for side in primitive_sides(primitive, layers):
            include_primitive(bounds[side], primitive)

        physical_side = mechanical_presence_side(primitive, layers)
        if physical_side is not None:
            mechanically_exists[physical_side] = True

    return bounds, through_hole_pads, mechanically_exists


def parse_pad_diameter_rules(board: ET.Element) -> PadDiameterRules:
    design_rules = board.find("designrules")
    if design_rules is None:
        return PadDiameterRules(top=None, bottom=None)

    parameters = {
        parameter.get("name", ""): parameter.get("value", "")
        for parameter in design_rules.findall("param")
    }

    def parse_side(side: str) -> RestringRule | None:
        ratio = parameters.get(f"rvPad{side}")
        minimum = parameters.get(f"rlMinPad{side}")
        maximum = parameters.get(f"rlMaxPad{side}")
        if ratio is None or minimum is None or maximum is None:
            return None
        return RestringRule(
            ratio=number(ratio),
            minimum_mm=eagle_length_mm(minimum),
            maximum_mm=eagle_length_mm(maximum),
        )

    return PadDiameterRules(
        top=parse_side("Top"),
        bottom=parse_side("Bottom"),
    )


def parse_board(
    board_path: Path,
) -> tuple[
    ET.Element,
    dict[int, str],
    PadDiameterRules,
    dict[tuple[str, str], PackageInfo],
    dict[str, list[PackageInfo]],
]:
    tree = ET.parse(board_path)
    drawing = tree.getroot().find("drawing")
    if drawing is None:
        raise ValueError(f"{board_path} has no <drawing> section")
    board = drawing.find("board")
    if board is None:
        raise ValueError(f"{board_path} has no <board> section")

    layers_parent = drawing.find("layers")
    if layers_parent is None:
        raise ValueError(f"{board_path} has no <layers> section")
    layers = {
        int(layer.get("number", "0")): layer.get("name", "")
        for layer in layers_parent.findall("layer")
    }
    pad_diameter_rules = parse_pad_diameter_rules(board)

    packages: dict[tuple[str, str], PackageInfo] = {}
    packages_by_name: dict[str, list[PackageInfo]] = {}
    libraries = board.find("libraries")
    if libraries is not None:
        for library in libraries.findall("library"):
            library_name = library.get("name", "")
            packages_parent = library.find("packages")
            if packages_parent is None:
                continue
            for package in packages_parent.findall("package"):
                package_name = package.get("name", "")
                (
                    bounds,
                    through_hole_pads,
                    mechanically_exists,
                ) = compute_package_geometry(package, layers, pad_diameter_rules)
                info = PackageInfo(
                    library_name=library_name,
                    package_name=package_name,
                    element=package,
                    bounds=bounds,
                    through_hole_pads=through_hole_pads,
                    mechanically_exists=mechanically_exists,
                )
                packages[(library_name, package_name)] = info
                packages_by_name.setdefault(package_name, []).append(info)

    return board, layers, pad_diameter_rules, packages, packages_by_name


def make_svg_root() -> ET.Element:
    ET.register_namespace("", SVG_NS)
    ET.register_namespace("inkscape", INKSCAPE_NS)
    return ET.Element(
        f"{{{SVG_NS}}}svg",
        {
            "width": f"{svg_number(CANVAS_WIDTH_MM)}mm",
            "height": f"{svg_number(CANVAS_HEIGHT_MM)}mm",
            "viewBox": (
                f"0 0 {svg_number(CANVAS_WIDTH_MM)} {svg_number(CANVAS_HEIGHT_MM)}"
            ),
        },
    )


def svg_element(parent: ET.Element, tag: str, attributes: dict[str, str]) -> ET.Element:
    return ET.SubElement(parent, f"{{{SVG_NS}}}{tag}", attributes)


def make_output_layers(root: ET.Element) -> dict[str, ET.Element]:
    output_layers: dict[str, ET.Element] = {}
    for layer_id, (label, color) in OUTPUT_LAYER_SPECS.items():
        output_layers[layer_id] = svg_element(
            root,
            "g",
            {
                "id": layer_id,
                f"{{{INKSCAPE_NS}}}groupmode": "layer",
                f"{{{INKSCAPE_NS}}}label": label,
                "fill": color,
                "stroke": color,
                "opacity": "0.6",
            },
        )
    return output_layers


def layer_metadata(primitive: ET.Element, layers: dict[int, str]) -> dict[str, str]:
    layer_number = primitive.get("layer")
    if layer_number is None:
        return {}
    return {
        "data-layer-number": layer_number,
        "data-layer": layers.get(int(layer_number), f"Layer{layer_number}"),
    }


def arc_path_data(
    start: tuple[float, float],
    end: tuple[float, float],
    curve_degrees: float,
    mirrored: bool,
) -> str:
    chord = math.dist(start, end)
    sine = math.sin(math.radians(abs(curve_degrees)) / 2.0)
    if chord == 0.0 or abs(sine) < 0.0000001:
        return (
            f"M {svg_number(start[0])} {svg_number(start[1])} "
            f"L {svg_number(end[0])} {svg_number(end[1])}"
        )
    radius = chord / (2.0 * abs(sine))
    large_arc = "1" if abs(curve_degrees) > 180.0 else "0"
    effective_curve = -curve_degrees if mirrored else curve_degrees
    sweep = "0" if effective_curve > 0.0 else "1"
    return (
        f"M {svg_number(start[0])} {svg_number(start[1])} "
        f"A {svg_number(radius)} {svg_number(radius)} 0 "
        f"{large_arc} {sweep} {svg_number(end[0])} {svg_number(end[1])}"
    )


def output_wire(
    parent: ET.Element,
    primitive: ET.Element,
    layers: dict[int, str],
    point_transform: Callable[[float, float], tuple[float, float]],
    mirrored: bool,
    metadata: dict[str, str],
) -> None:
    start = point_transform(number(primitive.get("x1")), number(primitive.get("y1")))
    end = point_transform(number(primitive.get("x2")), number(primitive.get("y2")))
    curve = number(primitive.get("curve"))
    if curve:
        path_data = arc_path_data(start, end, curve, mirrored)
    else:
        path_data = (
            f"M {svg_number(start[0])} {svg_number(start[1])} "
            f"L {svg_number(end[0])} {svg_number(end[1])}"
        )
    attributes = {
        "d": path_data,
        "fill": "none",
        "stroke-width": svg_number(
            max(number(primitive.get("width")), MINIMUM_VISIBLE_STROKE_WIDTH_MM)
        ),
        "stroke-linecap": "round",
    }
    attributes.update(layer_metadata(primitive, layers))
    attributes.update(metadata)
    svg_element(parent, "path", attributes)


def output_polygon(
    parent: ET.Element,
    primitive: ET.Element,
    layers: dict[int, str],
    point_transform: Callable[[float, float], tuple[float, float]],
    metadata: dict[str, str],
) -> None:
    points = [
        point_transform(number(vertex.get("x")), number(vertex.get("y")))
        for vertex in primitive.findall("vertex")
    ]
    if not points:
        return
    path_data = (
        f"M {svg_number(points[0][0])} {svg_number(points[0][1])} "
        + " ".join(
            f"L {svg_number(point[0])} {svg_number(point[1])}" for point in points[1:]
        )
        + " Z"
    )
    attributes = {
        "d": path_data,
        "stroke-width": svg_number(
            max(number(primitive.get("width")), MINIMUM_VISIBLE_STROKE_WIDTH_MM)
        ),
        "stroke-linejoin": "round",
    }
    attributes.update(layer_metadata(primitive, layers))
    attributes.update(metadata)
    svg_element(parent, "path", attributes)


def output_circle(
    parent: ET.Element,
    primitive: ET.Element,
    layers: dict[int, str],
    point_transform: Callable[[float, float], tuple[float, float]],
    metadata: dict[str, str],
) -> None:
    center_x, center_y = point_transform(
        number(primitive.get("x")), number(primitive.get("y"))
    )
    attributes = {
        "cx": svg_number(center_x),
        "cy": svg_number(center_y),
        "r": svg_number(number(primitive.get("radius"))),
        "stroke-width": svg_number(
            max(number(primitive.get("width")), MINIMUM_VISIBLE_STROKE_WIDTH_MM)
        ),
    }
    attributes.update(layer_metadata(primitive, layers))
    attributes.update(metadata)
    svg_element(parent, "circle", attributes)


def output_rectangle_path(
    parent: ET.Element,
    primitive: ET.Element,
    layers: dict[int, str],
    point_transform: Callable[[float, float], tuple[float, float]],
    metadata: dict[str, str],
) -> None:
    x1 = number(primitive.get("x1"))
    y1 = number(primitive.get("y1"))
    x2 = number(primitive.get("x2"))
    y2 = number(primitive.get("y2"))
    center_x = (x1 + x2) / 2.0
    center_y = (y1 + y2) / 2.0
    half_width = abs(x2 - x1) / 2.0
    half_height = abs(y2 - y1) / 2.0
    primitive_rotation = parse_rotation(primitive.get("rot"))

    points = []
    for local_x, local_y in (
        (-half_width, -half_height),
        (half_width, -half_height),
        (half_width, half_height),
        (-half_width, half_height),
    ):
        package_x, package_y = transform_point(
            local_x,
            local_y,
            center_x,
            center_y,
            primitive_rotation,
        )
        points.append(point_transform(package_x, package_y))

    path_data = (
        f"M {svg_number(points[0][0])} {svg_number(points[0][1])} "
        + " ".join(
            f"L {svg_number(point[0])} {svg_number(point[1])}" for point in points[1:]
        )
        + " Z"
    )
    attributes = {
        "d": path_data,
        "stroke-width": svg_number(RECTANGLE_STROKE_WIDTH_MM),
        "stroke-linejoin": "round",
        "data-source-shape": "rectangle",
    }
    attributes.update(layer_metadata(primitive, layers))
    attributes.update(metadata)
    svg_element(parent, "path", attributes)


def output_hole_or_pad(
    parent: ET.Element,
    primitive: ET.Element,
    layers: dict[int, str],
    pad_diameter_rules: PadDiameterRules,
    point_transform: Callable[[float, float], tuple[float, float]],
    metadata: dict[str, str],
) -> None:
    center_x, center_y = point_transform(
        number(primitive.get("x")), number(primitive.get("y"))
    )
    if primitive.tag == "hole":
        diameter = number(primitive.get("drill"))
        shape = "circle"
    else:
        diameter = through_hole_pad_diameter(primitive, pad_diameter_rules)
        shape = "square"

    attributes = {
        "stroke-width": svg_number(RECTANGLE_STROKE_WIDTH_MM),
    }
    attributes.update(layer_metadata(primitive, layers))
    attributes.update(metadata)

    if shape == "square":
        attributes.update(
            {
                "x": svg_number(center_x - diameter / 2.0),
                "y": svg_number(center_y - diameter / 2.0),
                "width": svg_number(diameter),
                "height": svg_number(diameter),
            }
        )
        svg_element(parent, "rect", attributes)
    else:
        attributes.update(
            {
                "cx": svg_number(center_x),
                "cy": svg_number(center_y),
                "r": svg_number(diameter / 2.0),
            }
        )
        svg_element(parent, "circle", attributes)


def output_plain_like_geometry(
    output_layers: dict[str, ET.Element],
    container: ET.Element,
    layers: dict[int, str],
    pad_diameter_rules: PadDiameterRules,
    origin_x: float = 0.0,
    origin_y: float = 0.0,
    rotation: Rotation = Rotation(),
    metadata: dict[str, str] | None = None,
) -> dict[str, int]:
    """Output the supported primitives from <plain> or PCB-OUTLINE."""

    extra_metadata = metadata or {}

    def point_transform(x: float, y: float) -> tuple[float, float]:
        board_x, board_y = transform_point(x, y, origin_x, origin_y, rotation)
        return board_to_svg(board_x, board_y)

    counts = {layer_id: 0 for layer_id in OUTPUT_LAYER_SPECS}
    for primitive in container:
        if primitive.tag == "pad":
            output_layer = LAYER_THROUGH_HOLE
        elif primitive.tag == "hole":
            output_layer = LAYER_PCB_OUTLINE
        else:
            layer_number = primitive.get("layer")
            if layer_number is None:
                continue
            layer_name = layers.get(int(layer_number), f"Layer{layer_number}")
            if layer_name in COMMON_BOUND_LAYERS:
                output_layer = LAYER_PCB_OUTLINE
            else:
                side = layer_side(layer_name)
                if side is None:
                    continue
                if rotation.mirrored:
                    side = opposite_side(side)
                output_layer = (
                    LAYER_PLAIN_TOP if side == SIDE_TOP else LAYER_PLAIN_BOTTOM
                )

        parent = output_layers[output_layer]
        if primitive.tag == "wire":
            output_wire(
                parent,
                primitive,
                layers,
                point_transform,
                rotation.mirrored,
                extra_metadata,
            )
            counts[output_layer] += 1
        elif primitive.tag == "polygon":
            output_polygon(parent, primitive, layers, point_transform, extra_metadata)
            counts[output_layer] += 1
        elif primitive.tag == "circle":
            output_circle(parent, primitive, layers, point_transform, extra_metadata)
            counts[output_layer] += 1
        elif primitive.tag == "rectangle":
            output_rectangle_path(
                parent, primitive, layers, point_transform, extra_metadata
            )
            counts[output_layer] += 1
        elif primitive.tag in {"hole", "pad"}:
            output_hole_or_pad(
                parent,
                primitive,
                layers,
                pad_diameter_rules,
                point_transform,
                extra_metadata,
            )
            counts[output_layer] += 1
    return counts


def resolve_package(
    element: ET.Element,
    packages: dict[tuple[str, str], PackageInfo],
    packages_by_name: dict[str, list[PackageInfo]],
) -> PackageInfo:
    library_name = element.get("library", "")
    package_name = element.get("package", "")
    exact = packages.get((library_name, package_name))
    if exact is not None:
        return exact

    candidates = packages_by_name.get(package_name, [])
    if len(candidates) == 1:
        return candidates[0]
    if not candidates:
        raise KeyError(
            f"Element {element.get('name', '?')} references missing package "
            f"{library_name}:{package_name}"
        )
    libraries = ", ".join(candidate.library_name for candidate in candidates)
    raise KeyError(
        f"Element {element.get('name', '?')} has ambiguous package "
        f"{package_name!r}; found it in libraries: {libraries}"
    )


def element_svg_matrix(origin_x: float, origin_y: float, rotation: Rotation) -> str:
    """Return an SVG affine matrix for EAGLE package-local coordinates."""

    angle = math.radians(rotation.angle_degrees)
    cosine = math.cos(angle)
    sine = math.sin(angle)
    mirror_factor = -1.0 if rotation.mirrored else 1.0
    return (
        "matrix("
        f"{svg_number(mirror_factor * cosine)} "
        f"{svg_number(-sine)} "
        f"{svg_number(-mirror_factor * sine)} "
        f"{svg_number(-cosine)} "
        f"{svg_number(origin_x)} "
        f"{svg_number(CANVAS_HEIGHT_MM - origin_y)}"
        ")"
    )


def output_transformed_rectangle(
    parent: ET.Element,
    element: ET.Element,
    bounds: Bounds,
    origin_x: float,
    origin_y: float,
    rotation: Rotation,
    metadata: dict[str, str],
) -> None:
    if bounds.empty:
        return
    assert bounds.left is not None
    assert bounds.right is not None
    assert bounds.bottom is not None
    assert bounds.top is not None

    left = bounds.left
    right = bounds.right
    bottom = bounds.bottom
    top = bounds.top
    attributes = {
        "x": svg_number(left),
        "y": svg_number(bottom),
        "width": svg_number(right - left),
        "height": svg_number(top - bottom),
        "transform": element_svg_matrix(origin_x, origin_y, rotation),
        "stroke-width": svg_number(RECTANGLE_STROKE_WIDTH_MM),
        "partref": element.get("name", ""),
        "data-library": element.get("library", ""),
        "data-package": element.get("package", ""),
    }
    attributes.update(metadata)
    svg_element(
        parent,
        "rect",
        attributes,
    )


def generate_svg(board_path: Path, output_path: Path) -> dict[str, int]:
    (
        board,
        layers,
        pad_diameter_rules,
        packages,
        packages_by_name,
    ) = parse_board(board_path)
    root = make_svg_root()
    root.append(
        ET.Comment(" Board origin (0,0) maps to SVG (0,100); all dimensions are mm. ")
    )
    output_layers = make_output_layers(root)

    counts = {
        "layers": len(layers),
        "packages": len(packages),
        "plain_top_objects": 0,
        "plain_bottom_objects": 0,
        "outline_objects": 0,
        "through_hole_rectangles": 0,
        "top_mechanical_rectangles": 0,
        "bottom_mechanical_rectangles": 0,
        "elements": 0,
    }

    plain = board.find("plain")
    if plain is not None:
        plain_counts = output_plain_like_geometry(
            output_layers,
            plain,
            layers,
            pad_diameter_rules,
            metadata={"data-source": "plain"},
        )
        counts["plain_top_objects"] += plain_counts[LAYER_PLAIN_TOP]
        counts["plain_bottom_objects"] += plain_counts[LAYER_PLAIN_BOTTOM]
        counts["outline_objects"] += plain_counts[LAYER_PCB_OUTLINE]
        counts["through_hole_rectangles"] += plain_counts[LAYER_THROUGH_HOLE]

    elements_parent = board.find("elements")
    if elements_parent is None:
        raise ValueError(f"{board_path} has no <elements> section")

    for element in elements_parent.findall("element"):
        counts["elements"] += 1
        package = resolve_package(element, packages, packages_by_name)
        rotation = parse_rotation(element.get("rot"))
        origin_x = number(element.get("x"))
        origin_y = number(element.get("y"))

        if element.get("package") == "PCB-OUTLINE":
            outline_counts = output_plain_like_geometry(
                output_layers,
                package.element,
                layers,
                pad_diameter_rules,
                origin_x=origin_x,
                origin_y=origin_y,
                rotation=rotation,
                metadata={
                    "data-source": "PCB-OUTLINE",
                    "partref": element.get("name", ""),
                },
            )
            counts["outline_objects"] += outline_counts[LAYER_PCB_OUTLINE]
            counts["plain_top_objects"] += outline_counts[LAYER_PLAIN_TOP]
            counts["plain_bottom_objects"] += outline_counts[LAYER_PLAIN_BOTTOM]
            counts["through_hole_rectangles"] += outline_counts[LAYER_THROUGH_HOLE]
            continue

        for pad in package.through_hole_pads:
            output_transformed_rectangle(
                output_layers[LAYER_THROUGH_HOLE],
                element,
                pad.bounds,
                origin_x,
                origin_y,
                rotation,
                {
                    "data-kind": "through-hole-pad",
                    "data-pad": pad.name,
                },
            )
            counts["through_hole_rectangles"] += 1

        for source_side in SIDES:
            if not package.mechanically_exists[source_side]:
                continue
            local_bounds = package.bounds[source_side]
            if local_bounds.empty:
                continue
            final_side = (
                opposite_side(source_side) if rotation.mirrored else source_side
            )
            if final_side == SIDE_TOP:
                layer_id = LAYER_TOP_MECHANICAL
                count_key = "top_mechanical_rectangles"
            else:
                layer_id = LAYER_BOTTOM_MECHANICAL
                count_key = "bottom_mechanical_rectangles"
            output_transformed_rectangle(
                output_layers[layer_id],
                element,
                local_bounds,
                origin_x,
                origin_y,
                rotation,
                {
                    "data-kind": "mechanical-bounds",
                    "data-source-side": source_side,
                    "data-final-side": final_side,
                },
            )
            counts[count_key] += 1

    ET.indent(root, space="  ")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    ET.ElementTree(root).write(output_path, encoding="utf-8", xml_declaration=True)
    return counts


def default_paths() -> tuple[Path, Path]:
    repository_root = Path(__file__).resolve().parents[2]
    board_path = repository_root / "electrical" / "hot-wand.brd"
    timestamp = datetime.now().strftime("%Y%m%d%H%M%S")
    output_path = repository_root / "mechanical" / f"thermal-pad-{timestamp}.svg"
    return board_path, output_path


def parse_arguments() -> argparse.Namespace:
    default_board, default_output = default_paths()
    parser = argparse.ArgumentParser(
        description="Generate a laser-cut thermal pad SVG from an EAGLE board."
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
    counts = generate_svg(board_path, output_path)
    print(f"Generated {output_path}")
    print(
        "Parsed "
        f"{counts['layers']} layers, "
        f"{counts['packages']} packages, and "
        f"{counts['elements']} elements."
    )
    print(
        "Wrote "
        f"{counts['outline_objects']} PCB-outline objects, "
        f"{counts['plain_top_objects']} top-plain objects, "
        f"{counts['plain_bottom_objects']} bottom-plain objects, "
        f"{counts['through_hole_rectangles']} through-hole pad rectangles, "
        f"{counts['top_mechanical_rectangles']} top-mechanical rectangles, and "
        f"{counts['bottom_mechanical_rectangles']} bottom-mechanical rectangles."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
