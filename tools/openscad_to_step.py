#!/usr/bin/env python3
"""Convert an OpenSCAD model to an analytic STEP model with CadQuery/OCCT.

OpenSCAD first evaluates the input ``.scad`` file and writes its CSG tree.  The
tree is then parsed here and rebuilt with CadQuery primitives and OpenCASCADE
boolean operations before being exported as STEP. OpenSCAD ``color()`` values
are carried into the STEP assembly as per-part presentation colors.

Usage::

    python tools/openscad_to_step.py model.scad
    python tools/openscad_to_step.py model.scad out/model.step
    python tools/openscad_to_step.py model.scad -o out/model.step --compress
    python tools/openscad_to_step.py model.scad --no-overwrite

CadQuery is an optional repository dependency and can be installed with
``python -m pip install cadquery``.
"""

from __future__ import annotations

import argparse
import gzip
import json
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable, Sequence


DEFAULT_OPENSCAD = Path(r"C:\Program Files\OpenSCAD (Nightly)\openscad.exe")
_NUMBER_RE = re.compile(
    r"[+-]?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?"
)
_IDENTITY = (
    (1.0, 0.0, 0.0, 0.0),
    (0.0, 1.0, 0.0, 0.0),
    (0.0, 0.0, 1.0, 0.0),
    (0.0, 0.0, 0.0, 1.0),
)


class CsgError(Exception):
    """Base class for actionable CSG parse/conversion errors."""


class CsgSyntaxError(CsgError):
    """The OpenSCAD-generated CSG text is malformed or cannot be parsed."""


class CsgConversionError(CsgError):
    """A valid CSG construct cannot be represented by this converter."""


@dataclass(frozen=True)
class CsgIdentifier:
    """A bare CSG value which is neither a number nor a boolean."""

    name: str


@dataclass
class CsgNode:
    name: str
    positional: list[Any]
    named: dict[str, Any]
    children: list["CsgNode"]
    line: int
    column: int
    modifiers: str = ""


@dataclass(frozen=True)
class _Token:
    kind: str
    value: Any
    line: int
    column: int


class _Lexer:
    """Small lexer for OpenSCAD's documented CSG interchange grammar."""

    def __init__(self, text: str) -> None:
        self.text = text
        self.index = 0
        self.line = 1
        self.column = 1

    def tokens(self) -> list[_Token]:
        result: list[_Token] = []
        while True:
            self._skip_space_and_comments()
            if self.index >= len(self.text):
                result.append(_Token("EOF", None, self.line, self.column))
                return result

            char = self.text[self.index]
            line, column = self.line, self.column
            if char in "()[]{},;=":
                self._advance(char)
                result.append(_Token(char, char, line, column))
            elif char in "!#%*":
                self._advance(char)
                result.append(_Token("MODIFIER", char, line, column))
            elif char == '"':
                result.append(_Token("STRING", self._string(), line, column))
            else:
                number_match = _NUMBER_RE.match(self.text, self.index)
                if number_match:
                    raw = number_match.group(0)
                    self._advance(raw)
                    value: int | float
                    value = float(raw) if any(c in raw for c in ".eE") else int(raw)
                    result.append(_Token("NUMBER", value, line, column))
                else:
                    name = self._name()
                    if not name:
                        raise CsgSyntaxError(
                            f"line {line}, column {column}: unexpected character {char!r}"
                        )
                    result.append(_Token("NAME", name, line, column))

    def _advance(self, value: str) -> None:
        for char in value:
            if char == "\n":
                self.line += 1
                self.column = 1
            else:
                self.column += 1
        self.index += len(value)

    def _skip_space_and_comments(self) -> None:
        while self.index < len(self.text):
            if self.text[self.index].isspace():
                self._advance(self.text[self.index])
                continue
            if self.text.startswith("//", self.index):
                end = self.text.find("\n", self.index)
                if end < 0:
                    self._advance(self.text[self.index :])
                else:
                    self._advance(self.text[self.index : end])
                continue
            if self.text.startswith("/*", self.index):
                end = self.text.find("*/", self.index + 2)
                if end < 0:
                    raise CsgSyntaxError(
                        f"line {self.line}, column {self.column}: unterminated comment"
                    )
                self._advance(self.text[self.index : end + 2])
                continue
            return

    def _string(self) -> str:
        start_line, start_column = self.line, self.column
        start = self.index
        self._advance('"')
        escaped = False
        while self.index < len(self.text):
            char = self.text[self.index]
            self._advance(char)
            if char == '"' and not escaped:
                raw = self.text[start : self.index]
                try:
                    return json.loads(raw)
                except json.JSONDecodeError as exc:
                    raise CsgSyntaxError(
                        f"line {start_line}, column {start_column}: invalid string: {exc.msg}"
                    ) from exc
            if char == "\\" and not escaped:
                escaped = True
            else:
                escaped = False
        raise CsgSyntaxError(
            f"line {start_line}, column {start_column}: unterminated string"
        )

    def _name(self) -> str:
        start = self.index
        structural = set("()[]{},;=!#%*")
        while self.index < len(self.text):
            char = self.text[self.index]
            if char.isspace() or char in structural or char == '"':
                break
            self._advance(char)
        return self.text[start : self.index]


class CsgParser:
    """Parse OpenSCAD CSG text into a simple, dependency-free syntax tree."""

    def __init__(self, text: str) -> None:
        self.tokens = _Lexer(text).tokens()
        self.index = 0

    def parse(self) -> list[CsgNode]:
        nodes: list[CsgNode] = []
        while self.current.kind != "EOF":
            nodes.append(self._node())
        return nodes

    @property
    def current(self) -> _Token:
        return self.tokens[self.index]

    def _accept(self, kind: str) -> _Token | None:
        if self.current.kind != kind:
            return None
        token = self.current
        self.index += 1
        return token

    def _expect(self, kind: str) -> _Token:
        token = self._accept(kind)
        if token is None:
            current = self.current
            raise CsgSyntaxError(
                f"line {current.line}, column {current.column}: expected {kind!r}, "
                f"found {current.kind!r}"
            )
        return token

    def _node(self) -> CsgNode:
        modifiers = ""
        while self.current.kind == "MODIFIER":
            modifiers += str(self._expect("MODIFIER").value)

        name_token = self._expect("NAME")
        self._expect("(")
        positional: list[Any] = []
        named: dict[str, Any] = {}
        if self.current.kind != ")":
            while True:
                if (
                    self.current.kind == "NAME"
                    and self.tokens[self.index + 1].kind == "="
                ):
                    key = str(self._expect("NAME").value)
                    self._expect("=")
                    if key in named:
                        raise CsgSyntaxError(
                            f"line {self.current.line}, column {self.current.column}: "
                            f"duplicate argument {key!r}"
                        )
                    named[key] = self._value()
                else:
                    positional.append(self._value())
                if self._accept(",") is None:
                    break
        self._expect(")")

        children: list[CsgNode] = []
        if self._accept(";") is None:
            self._expect("{")
            while self.current.kind != "}":
                if self.current.kind == "EOF":
                    raise CsgSyntaxError(
                        f"line {self.current.line}, column {self.current.column}: "
                        f"unterminated scope for {name_token.value!r}"
                    )
                children.append(self._node())
            self._expect("}")

        return CsgNode(
            str(name_token.value),
            positional,
            named,
            children,
            name_token.line,
            name_token.column,
            modifiers,
        )

    def _value(self) -> Any:
        token = self.current
        if token.kind == "NUMBER":
            self.index += 1
            return token.value
        if token.kind == "STRING":
            self.index += 1
            return token.value
        if token.kind == "NAME":
            self.index += 1
            if token.value == "true":
                return True
            if token.value == "false":
                return False
            if token.value == "undef":
                return None
            return CsgIdentifier(str(token.value))
        if self._accept("[") is not None:
            values: list[Any] = []
            if self.current.kind != "]":
                while True:
                    values.append(self._value())
                    if self._accept(",") is None:
                        break
            self._expect("]")
            return values
        raise CsgSyntaxError(
            f"line {token.line}, column {token.column}: expected a CSG value, "
            f"found {token.kind!r}"
        )


@dataclass
class Geometry:
    dimension: int | None
    shapes: list[Any] = field(default_factory=list)
    colors: list["ColorValue | None"] = field(default_factory=list)

    def __post_init__(self) -> None:
        if not self.colors:
            self.colors = [None] * len(self.shapes)
        if len(self.colors) != len(self.shapes):
            raise ValueError("each geometry shape must have exactly one color entry")


ColorValue = tuple[float, float, float, float]


@dataclass(frozen=True)
class StepPart:
    shape: Any
    color: ColorValue | None


def parse_csg(text: str) -> list[CsgNode]:
    """Public parser entry point, kept independent of CadQuery for easy testing."""

    return CsgParser(text).parse()


def _matrix_multiply(
    left: Sequence[Sequence[float]], right: Sequence[Sequence[float]]
) -> tuple[tuple[float, ...], ...]:
    return tuple(
        tuple(sum(left[row][k] * right[k][column] for k in range(4)) for column in range(4))
        for row in range(4)
    )


def _transform_point(
    matrix: Sequence[Sequence[float]], point: Sequence[float]
) -> tuple[float, float, float]:
    x, y, z = point
    result = tuple(
        matrix[row][0] * x
        + matrix[row][1] * y
        + matrix[row][2] * z
        + matrix[row][3]
        for row in range(3)
    )
    return result  # type: ignore[return-value]


def _vector_length(vector: Sequence[float]) -> float:
    return math.sqrt(sum(component * component for component in vector))


def _dot(left: Sequence[float], right: Sequence[float]) -> float:
    return sum(a * b for a, b in zip(left, right))


def _uniform_scale_3d(matrix: Sequence[Sequence[float]]) -> float | None:
    columns = [tuple(matrix[row][column] for row in range(3)) for column in range(3)]
    lengths = [_vector_length(column) for column in columns]
    if min(lengths) <= 1e-12:
        return None
    tolerance = max(lengths) * 1e-8
    if max(lengths) - min(lengths) > tolerance:
        return None
    if any(abs(_dot(columns[a], columns[b])) > tolerance for a, b in ((0, 1), (0, 2), (1, 2))):
        return None
    return sum(lengths) / 3.0


def _uniform_scale_2d(matrix: Sequence[Sequence[float]]) -> float | None:
    x_axis = (matrix[0][0], matrix[1][0], matrix[2][0])
    y_axis = (matrix[0][1], matrix[1][1], matrix[2][1])
    x_length, y_length = _vector_length(x_axis), _vector_length(y_axis)
    tolerance = max(x_length, y_length, 1.0) * 1e-8
    if x_length <= 1e-12 or abs(x_length - y_length) > tolerance:
        return None
    if abs(_dot(x_axis, y_axis)) > tolerance:
        return None
    if abs(matrix[2][3]) > tolerance or abs(x_axis[2]) > tolerance or abs(y_axis[2]) > tolerance:
        return None
    return (x_length + y_length) / 2.0


def _convex_hull_2d(points: Iterable[tuple[float, float]]) -> list[tuple[float, float]]:
    unique = sorted(set(points))
    if len(unique) <= 1:
        return unique

    def cross(
        origin: tuple[float, float],
        first: tuple[float, float],
        second: tuple[float, float],
    ) -> float:
        return (first[0] - origin[0]) * (second[1] - origin[1]) - (
            first[1] - origin[1]
        ) * (second[0] - origin[0])

    lower: list[tuple[float, float]] = []
    for point in unique:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], point) <= 0:
            lower.pop()
        lower.append(point)
    upper: list[tuple[float, float]] = []
    for point in reversed(unique):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], point) <= 0:
            upper.pop()
        upper.append(point)
    return lower[:-1] + upper[:-1]


class CsgConverter:
    """Build a CadQuery/OpenCASCADE model from parsed CSG nodes."""

    _PASSTHROUGH = {"group", "color", "render"}

    def __init__(self, cadquery_module: Any, source_directory: Path) -> None:
        self.cq = cadquery_module
        self.source_directory = source_directory
        self.warnings: list[str] = []
        self.step_parts: list[StepPart] = []

    def convert(self, nodes: Sequence[CsgNode]) -> Any:
        geometry = self._evaluate_children(nodes)
        if not geometry.shapes:
            raise CsgConversionError("the evaluated CSG tree contains no geometry")
        if geometry.dimension != 3:
            raise CsgConversionError(
                "the evaluated CSG tree is two-dimensional; STEP output requires a 3D model"
            )
        self.step_parts = [
            StepPart(solid, color)
            for shape, color in zip(geometry.shapes, geometry.colors)
            for solid in shape.Solids()
        ]
        if not self.step_parts:
            raise CsgConversionError("the evaluated CSG tree contains no solid geometry")
        if len(self.step_parts) == 1:
            return self.step_parts[0].shape
        return self.cq.Compound.makeCompound(
            [part.shape for part in self.step_parts]
        )

    def evaluate(self, node: CsgNode) -> Geometry:
        if "*" in node.modifiers or "%" in node.modifiers:
            return Geometry(None)

        method = getattr(self, f"_node_{node.name}", None)
        if method is not None:
            try:
                return method(node)
            except CsgError:
                raise
            except Exception as exc:
                raise self._error(node, f"OpenCASCADE operation failed: {exc}") from exc
        if node.name in self._PASSTHROUGH:
            return self._evaluate_children(node.children)
        raise self._error(
            node,
            f"unsupported CSG element {node.name!r}; no geometry was omitted",
        )

    def _error(self, node: CsgNode, message: str) -> CsgConversionError:
        return CsgConversionError(f"line {node.line}, column {node.column}: {message}")

    def _warn(self, node: CsgNode, message: str) -> None:
        warning = f"line {node.line}, column {node.column}: {message}"
        if warning not in self.warnings:
            self.warnings.append(warning)

    def _arg(self, node: CsgNode, name: str, index: int, default: Any = None) -> Any:
        if name in node.named:
            return node.named[name]
        if index < len(node.positional):
            return node.positional[index]
        return default

    def _number(self, node: CsgNode, value: Any, label: str) -> float:
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise self._error(node, f"{label} must be a number")
        if not math.isfinite(float(value)):
            raise self._error(node, f"{label} must be finite")
        return float(value)

    def _boolean(self, node: CsgNode, value: Any, label: str) -> bool:
        if not isinstance(value, bool):
            raise self._error(node, f"{label} must be true or false")
        return value

    def _vector(
        self, node: CsgNode, value: Any, length: int, label: str
    ) -> tuple[float, ...]:
        if not isinstance(value, list) or len(value) != length:
            raise self._error(node, f"{label} must be a {length}-element vector")
        return tuple(self._number(node, item, label) for item in value)

    def _evaluate_children(self, nodes: Sequence[CsgNode]) -> Geometry:
        geometries = [self.evaluate(child) for child in nodes]
        nonempty = [geometry for geometry in geometries if geometry.shapes]
        if not nonempty:
            known_dimensions = {
                geometry.dimension for geometry in geometries if geometry.dimension is not None
            }
            dimension = known_dimensions.pop() if len(known_dimensions) == 1 else None
            return Geometry(dimension)
        dimensions = {geometry.dimension for geometry in nonempty}
        if len(dimensions) != 1:
            raise CsgConversionError("a CSG scope mixes 2D and 3D geometry")
        return Geometry(
            nonempty[0].dimension,
            [shape for geometry in nonempty for shape in geometry.shapes],
            [color for geometry in nonempty for color in geometry.colors],
        )

    def _one_shape(self, geometry: Geometry) -> Any:
        if len(geometry.shapes) == 1:
            return geometry.shapes[0]
        return self.cq.Compound.makeCompound(geometry.shapes)

    def _common_color(self, geometries: Sequence[Geometry]) -> ColorValue | None:
        colors = [color for geometry in geometries for color in geometry.colors]
        if not colors:
            return None
        first = colors[0]
        return first if all(color == first for color in colors[1:]) else None

    def _boolean_children(self, node: CsgNode) -> list[Geometry]:
        children = [self.evaluate(child) for child in node.children]
        nonempty = [child for child in children if child.shapes]
        if not nonempty:
            return []
        dimensions = {child.dimension for child in nonempty}
        if len(dimensions) != 1:
            raise self._error(node, "boolean operation mixes 2D and 3D geometry")
        return nonempty

    def _node_group(self, node: CsgNode) -> Geometry:
        return self._evaluate_children(node.children)

    def _node_color(self, node: CsgNode) -> Geometry:
        geometry = self._evaluate_children(node.children)
        raw_color = self._arg(node, "c", 0, [1, 1, 0, 1])
        if not isinstance(raw_color, list) or len(raw_color) not in (3, 4):
            raise self._error(node, "color must be a three- or four-element vector")
        components = [
            self._number(node, component, "color component")
            for component in raw_color
        ]
        raw_alpha = self._arg(node, "alpha", 1, None)
        if raw_alpha is not None:
            alpha = self._number(node, raw_alpha, "color alpha")
        elif len(components) == 4:
            alpha = components[3]
        else:
            alpha = 1.0
        rgba = (*components[:3], alpha)
        clamped = tuple(max(0.0, min(1.0, component)) for component in rgba)
        if clamped != rgba:
            self._warn(node, "color components outside 0..1 were clamped")
        return Geometry(
            geometry.dimension,
            geometry.shapes,
            [clamped] * len(geometry.shapes),
        )

    def _node_render(self, node: CsgNode) -> Geometry:
        return self._evaluate_children(node.children)

    def _node_union(self, node: CsgNode) -> Geometry:
        children = self._boolean_children(node)
        if not children:
            return Geometry(None)
        shapes = [self._one_shape(child) for child in children]
        return Geometry(
            children[0].dimension,
            [shapes[0].fuse(*shapes[1:])],
            [self._common_color(children)],
        )

    def _node_difference(self, node: CsgNode) -> Geometry:
        if not node.children:
            return Geometry(None)
        first = self.evaluate(node.children[0])
        if not first.shapes:
            return first
        rest = [self.evaluate(child) for child in node.children[1:]]
        cutters = [shape for geometry in rest for shape in geometry.shapes]
        if any(geometry.dimension not in (None, first.dimension) for geometry in rest):
            raise self._error(node, "difference mixes 2D and 3D geometry")
        base = self._one_shape(first)
        return Geometry(
            first.dimension,
            [base.cut(*cutters)] if cutters else [base],
            [self._common_color([first])],
        )

    def _node_intersection(self, node: CsgNode) -> Geometry:
        children = self._boolean_children(node)
        if not children:
            return Geometry(None)
        shapes = [self._one_shape(child) for child in children]
        return Geometry(
            children[0].dimension,
            [shapes[0].intersect(*shapes[1:])],
            [self._common_color(children)],
        )

    def _node_cube(self, node: CsgNode) -> Geometry:
        raw_size = self._arg(node, "size", 0, [1, 1, 1])
        if isinstance(raw_size, (int, float)) and not isinstance(raw_size, bool):
            size = (float(raw_size),) * 3
        else:
            size = self._vector(node, raw_size, 3, "cube size")
        center = self._boolean(node, self._arg(node, "center", 1, False), "cube center")
        if any(component <= 0 for component in size):
            return Geometry(3)
        origin = tuple(-component / 2 for component in size) if center else (0, 0, 0)
        return Geometry(3, [self.cq.Solid.makeBox(*size, origin)])

    def _node_sphere(self, node: CsgNode) -> Geometry:
        radius = self._number(node, self._arg(node, "r", 0, 1), "sphere radius")
        if radius <= 0:
            return Geometry(3)
        shape = self.cq.Solid.makeSphere(
            radius, (0, 0, 0), (0, 0, 1), -90, 90, 360
        )
        return Geometry(3, [shape])

    def _node_cylinder(self, node: CsgNode) -> Geometry:
        height = self._number(node, self._arg(node, "h", 0, 1), "cylinder height")
        common_radius = self._arg(node, "r", 1, None)
        radius1 = self._number(
            node,
            common_radius if common_radius is not None else self._arg(node, "r1", 1, 1),
            "cylinder bottom radius",
        )
        radius2 = self._number(
            node,
            common_radius if common_radius is not None else self._arg(node, "r2", 2, 1),
            "cylinder top radius",
        )
        center = self._boolean(node, self._arg(node, "center", 3, False), "cylinder center")
        if height <= 0 or max(radius1, radius2) <= 0:
            return Geometry(3)
        if min(radius1, radius2) < 0:
            raise self._error(node, "cylinder radii cannot be negative")
        origin = (0, 0, -height / 2 if center else 0)
        if math.isclose(radius1, radius2):
            shape = self.cq.Solid.makeCylinder(radius1, height, origin, (0, 0, 1))
        else:
            shape = self.cq.Solid.makeCone(radius1, radius2, height, origin, (0, 0, 1))
        return Geometry(3, [shape])

    def _circle_face(self, radius: float, center: tuple[float, float] = (0, 0)) -> Any:
        wire = self.cq.Wire.makeCircle(radius, (center[0], center[1], 0), (0, 0, 1))
        return self.cq.Face.makeFromWires(wire)

    def _node_circle(self, node: CsgNode) -> Geometry:
        radius = self._number(node, self._arg(node, "r", 0, 1), "circle radius")
        return Geometry(2, [self._circle_face(radius)]) if radius > 0 else Geometry(2)

    def _node_square(self, node: CsgNode) -> Geometry:
        raw_size = self._arg(node, "size", 0, [1, 1])
        if isinstance(raw_size, (int, float)) and not isinstance(raw_size, bool):
            size = (float(raw_size),) * 2
        else:
            size = self._vector(node, raw_size, 2, "square size")
        center = self._boolean(node, self._arg(node, "center", 1, False), "square center")
        if any(component <= 0 for component in size):
            return Geometry(2)
        x0, y0 = (-size[0] / 2, -size[1] / 2) if center else (0, 0)
        points = [
            (x0, y0, 0),
            (x0 + size[0], y0, 0),
            (x0 + size[0], y0 + size[1], 0),
            (x0, y0 + size[1], 0),
        ]
        wire = self.cq.Wire.makePolygon(points, close=True)
        return Geometry(2, [self.cq.Face.makeFromWires(wire)])

    def _node_polygon(self, node: CsgNode) -> Geometry:
        raw_points = self._arg(node, "points", 0, None)
        if not isinstance(raw_points, list):
            raise self._error(node, "polygon points must be an array")
        points = [self._vector(node, point, 2, "polygon point") for point in raw_points]
        raw_paths = self._arg(node, "paths", 1, None)
        paths = [list(range(len(points)))] if raw_paths in (None, []) else raw_paths
        if not isinstance(paths, list):
            raise self._error(node, "polygon paths must be an array")
        wires: list[Any] = []
        for raw_path in paths:
            if not isinstance(raw_path, list) or len(raw_path) < 3:
                raise self._error(
                    node,
                    "each polygon path must contain at least three point indices",
                )
            try:
                vertices = [(*points[int(index)], 0) for index in raw_path]
            except (IndexError, TypeError, ValueError) as exc:
                raise self._error(node, "polygon path contains an invalid point index") from exc
            wires.append(self.cq.Wire.makePolygon(vertices, close=True))
        if not wires:
            return Geometry(2)
        # OpenSCAD writes the outer contour first and any hole contours after it.
        face = self.cq.Face.makeFromWires(wires[0], wires[1:])
        return Geometry(2, list(face.Faces()) or [face])

    def _node_polyhedron(self, node: CsgNode) -> Geometry:
        raw_points = self._arg(node, "points", 0, None)
        raw_faces = self._arg(node, "faces", 1, self._arg(node, "triangles", 1, None))
        if not isinstance(raw_points, list) or not isinstance(raw_faces, list):
            raise self._error(node, "polyhedron requires points and faces arrays")
        points = [self._vector(node, point, 3, "polyhedron point") for point in raw_points]
        faces: list[Any] = []
        for raw_face in raw_faces:
            if not isinstance(raw_face, list) or len(raw_face) < 3:
                raise self._error(node, "each polyhedron face needs at least three indices")
            try:
                vertices = [points[int(index)] for index in raw_face]
            except (IndexError, TypeError, ValueError) as exc:
                raise self._error(node, "polyhedron face contains an invalid point index") from exc
            try:
                wire = self.cq.Wire.makePolygon(vertices, close=True)
                faces.append(self.cq.Face.makeFromWires(wire))
            except Exception:
                # OpenSCAD accepts non-planar n-gons and triangulates them.
                for index in range(1, len(vertices) - 1):
                    wire = self.cq.Wire.makePolygon(
                        [vertices[0], vertices[index], vertices[index + 1]], close=True
                    )
                    faces.append(self.cq.Face.makeFromWires(wire))
        shell = self.cq.Shell.makeShell(faces)
        return Geometry(3, [self.cq.Solid.makeSolid(shell).fix()])

    def _node_text(self, node: CsgNode) -> Geometry:
        text = self._arg(node, "text", 0, "")
        if not isinstance(text, str):
            raise self._error(node, "text value must be a string")
        if not text:
            return Geometry(2)
        size = self._number(node, self._arg(node, "size", 1, 10), "text size")
        spacing = self._number(node, self._arg(node, "spacing", 2, 1), "text spacing")
        if not math.isclose(spacing, 1.0):
            self._warn(node, "CadQuery does not support OpenSCAD text spacing; spacing=1 was used")
        direction = self._arg(node, "direction", 4, "ltr")
        if direction != "ltr":
            raise self._error(node, f"text direction {direction!r} is not supported")
        halign = self._arg(node, "halign", 7, "left")
        valign = self._arg(node, "valign", 8, "baseline")
        if halign not in {"left", "center", "right"}:
            raise self._error(node, f"text horizontal alignment {halign!r} is not supported")
        if valign == "baseline":
            valign = "bottom"
            self._warn(node, "CadQuery has no baseline text alignment; bottom alignment was used")
        if valign not in {"bottom", "center", "top"}:
            raise self._error(node, f"text vertical alignment {valign!r} is not supported")

        raw_font = self._arg(node, "font", 3, "")
        if not isinstance(raw_font, str):
            raise self._error(node, "text font must be a string")
        font, _, style = raw_font.partition(":style=")
        font = font or "Arial"
        style_lower = style.casefold()
        if "italic" in style_lower:
            kind = "italic"
        elif "bold" in style_lower:
            kind = "bold"
        else:
            kind = "regular"
        if "bold" in style_lower and "italic" in style_lower:
            self._warn(node, "CadQuery cannot combine bold and italic text; italic was used")
        shape = self.cq.Compound.makeText(
            text,
            size,
            0,
            font=font,
            kind=kind,
            halign=halign,
            valign=valign,
        )
        return Geometry(2, [shape])

    def _matrix(self, node: CsgNode) -> tuple[tuple[float, ...], ...]:
        raw = self._arg(node, "m", 0, None)
        if not isinstance(raw, list) or len(raw) not in (3, 4):
            raise self._error(node, "multmatrix m must be a 3x4 or 4x4 matrix")
        rows: list[tuple[float, ...]] = []
        for row in raw:
            rows.append(self._vector(node, row, 4, "multmatrix row"))
        if len(rows) == 3:
            rows.append((0.0, 0.0, 0.0, 1.0))
        is_affine = all(
            math.isclose(rows[3][index], expected, abs_tol=1e-10)
            for index, expected in enumerate((0, 0, 0, 1))
        )
        if not is_affine:
            raise self._error(node, "multmatrix must be affine (last row [0, 0, 0, 1])")
        return tuple(rows)

    def _apply_matrix(self, shape: Any, matrix: Sequence[Sequence[float]]) -> Any:
        cq_matrix = self.cq.Matrix([list(row) for row in matrix])
        scale = _uniform_scale_3d(matrix)
        if scale is not None and math.isclose(scale, 1.0, rel_tol=1e-8, abs_tol=1e-10):
            try:
                return shape.transformShape(cq_matrix)
            except Exception:
                pass
        return shape.transformGeometry(cq_matrix)

    def _node_multmatrix(self, node: CsgNode) -> Geometry:
        matrix = self._matrix(node)
        geometry = self._evaluate_children(node.children)
        return Geometry(
            geometry.dimension,
            [self._apply_matrix(shape, matrix) for shape in geometry.shapes],
            list(geometry.colors),
        )

    def _profile_faces(
        self, node: CsgNode, geometry: Geometry
    ) -> list[tuple[Any, ColorValue | None]]:
        if geometry.dimension != 2:
            raise self._error(node, f"{node.name} requires 2D child geometry")
        return [
            (face, color)
            for shape, color in zip(geometry.shapes, geometry.colors)
            for face in shape.Faces()
        ]

    def _node_linear_extrude(self, node: CsgNode) -> Geometry:
        profile = self._evaluate_children(node.children)
        faces = self._profile_faces(node, profile)
        if not faces:
            return Geometry(3)
        height = self._number(node, self._arg(node, "height", 0, 1), "extrusion height")
        center = self._boolean(node, self._arg(node, "center", 1, False), "extrusion center")
        twist = self._number(node, self._arg(node, "twist", 2, 0), "extrusion twist")
        raw_scale = self._arg(node, "scale", 4, [1, 1])
        if isinstance(raw_scale, (int, float)) and not isinstance(raw_scale, bool):
            scale = (float(raw_scale), float(raw_scale))
        else:
            scale = self._vector(node, raw_scale, 2, "extrusion scale")
        if height <= 0 or min(scale) < 0:
            if min(scale) < 0:
                raise self._error(node, "extrusion scale cannot be negative")
            return Geometry(3)

        shapes: list[Any] = []
        colors: list[ColorValue | None] = []
        for face, color in faces:
            if all(math.isclose(value, 1.0) for value in scale):
                if math.isclose(twist, 0.0):
                    solid = self.cq.Solid.extrudeLinear(face, (0, 0, height))
                else:
                    solid = self.cq.Solid.extrudeLinearWithRotation(
                        face, (0, 0, 0), (0, 0, height), twist
                    )
            else:
                default_slices = max(1, math.ceil(abs(twist) / 15))
                slices = int(
                    self._number(
                        node,
                        self._arg(node, "slices", 3, default_slices),
                        "extrusion slices",
                    )
                )
                solid = self._loft_scaled_face(
                    node, face, height, twist, scale, max(1, slices)
                )
            if center:
                solid = solid.translate((0, 0, -height / 2))
            shapes.append(solid)
            colors.append(color)
        return Geometry(3, shapes, colors)

    def _loft_scaled_face(
        self,
        node: CsgNode,
        face: Any,
        height: float,
        twist: float,
        scale: tuple[float, float],
        slices: int,
    ) -> Any:
        def transformed(wire: Any, fraction: float) -> Any:
            angle = math.radians(twist * fraction)
            sx = 1 + (scale[0] - 1) * fraction
            sy = 1 + (scale[1] - 1) * fraction
            if sx <= 0 or sy <= 0:
                raise self._error(node, "zero extrusion scale is not supported by the B-Rep loft")
            cosine, sine = math.cos(angle), math.sin(angle)
            matrix = (
                (cosine * sx, -sine * sy, 0.0, 0.0),
                (sine * sx, cosine * sy, 0.0, 0.0),
                (0.0, 0.0, 1.0, height * fraction),
                (0.0, 0.0, 0.0, 1.0),
            )
            return wire.transformGeometry(self.cq.Matrix([list(row) for row in matrix]))

        fractions = [index / slices for index in range(slices + 1)]
        outer = self.cq.Solid.makeLoft(
            [transformed(face.outerWire(), fraction) for fraction in fractions],
            ruled=True,
        )
        holes = []
        for inner in face.innerWires():
            holes.append(
                self.cq.Solid.makeLoft(
                    [transformed(inner, fraction) for fraction in fractions], ruled=True
                )
            )
        return outer.cut(*holes) if holes else outer

    def _node_rotate_extrude(self, node: CsgNode) -> Geometry:
        profile = self._evaluate_children(node.children)
        faces = self._profile_faces(node, profile)
        angle = self._number(node, self._arg(node, "angle", 0, 360), "rotation angle")
        start = self._number(node, self._arg(node, "start", 1, 0), "rotation start")
        if angle <= 0 or not faces:
            return Geometry(3)
        if angle > 360:
            raise self._error(node, "rotation angle cannot exceed 360 degrees")
        shapes = []
        colors: list[ColorValue | None] = []
        for face, color in faces:
            # OpenSCAD's profile x/y coordinates become radius/z.  Rotate the
            # XY face into XZ before revolving it about the global Z axis.
            profile_xz = face.rotate((0, 0, 0), (1, 0, 0), 90)
            if not math.isclose(start, 0.0):
                profile_xz = profile_xz.rotate((0, 0, 0), (0, 0, 1), start)
            shapes.append(
                self.cq.Solid.revolve(
                    profile_xz, angle, (0, 0, 0), (0, 0, 1)
                )
            )
            colors.append(color)
        return Geometry(3, shapes, colors)

    def _simple_round_primitives(
        self,
        node: CsgNode,
        wanted: str,
        matrix: Sequence[Sequence[float]] = _IDENTITY,
    ) -> list[tuple[tuple[float, float, float], float]] | None:
        if "*" in node.modifiers or "%" in node.modifiers:
            return []
        if node.name in self._PASSTHROUGH:
            collected: list[tuple[tuple[float, float, float], float]] = []
            for child in node.children:
                values = self._simple_round_primitives(child, wanted, matrix)
                if values is None:
                    return None
                collected.extend(values)
            return collected
        if node.name == "multmatrix":
            combined = _matrix_multiply(matrix, self._matrix(node))
            collected = []
            for child in node.children:
                values = self._simple_round_primitives(child, wanted, combined)
                if values is None:
                    return None
                collected.extend(values)
            return collected
        if node.name != wanted or node.children:
            return None
        radius = self._number(node, self._arg(node, "r", 0, 1), f"{wanted} radius")
        scale = _uniform_scale_3d(matrix) if wanted == "sphere" else _uniform_scale_2d(matrix)
        if scale is None:
            return None
        return [(_transform_point(matrix, (0, 0, 0)), radius * scale)]

    def _node_hull(self, node: CsgNode) -> Geometry:
        sphere_values: list[tuple[tuple[float, float, float], float]] = []
        spheres_supported = True
        for child in node.children:
            values = self._simple_round_primitives(child, "sphere")
            if values is None:
                spheres_supported = False
                break
            sphere_values.extend(values)
        if spheres_supported and sphere_values:
            return self._hull_spheres(node, sphere_values)

        circle_values: list[tuple[tuple[float, float, float], float]] = []
        circles_supported = True
        for child in node.children:
            values = self._simple_round_primitives(child, "circle")
            if values is None:
                circles_supported = False
                break
            circle_values.extend(values)
        if circles_supported and circle_values:
            return self._hull_circles(node, circle_values)

        raise self._error(
            node,
            "hull is currently supported for two equal-radius spheres or any number "
            "of coplanar equal-radius circles",
        )

    def _hull_spheres(
        self,
        node: CsgNode,
        values: list[tuple[tuple[float, float, float], float]],
    ) -> Geometry:
        if len(values) == 1:
            center, radius = values[0]
            sphere = self.cq.Solid.makeSphere(
                radius, center, (0, 0, 1), -90, 90, 360
            )
            return Geometry(3, [sphere])
        equal_radii = len(values) == 2 and math.isclose(
            values[0][1], values[1][1], rel_tol=1e-8, abs_tol=1e-10
        )
        if not equal_radii:
            raise self._error(node, "3D hull requires exactly two equal-radius spheres")
        first, second = values[0][0], values[1][0]
        radius = values[0][1]
        direction = tuple(second[index] - first[index] for index in range(3))
        distance = _vector_length(direction)
        sphere1 = self.cq.Solid.makeSphere(radius, first, (0, 0, 1), -90, 90, 360)
        if distance <= 1e-12:
            return Geometry(3, [sphere1])
        sphere2 = self.cq.Solid.makeSphere(radius, second, (0, 0, 1), -90, 90, 360)
        cylinder = self.cq.Solid.makeCylinder(radius, distance, first, direction)
        return Geometry(3, [sphere1.fuse(cylinder, sphere2)])

    def _hull_circles(
        self,
        node: CsgNode,
        values: list[tuple[tuple[float, float, float], float]],
    ) -> Geometry:
        radii = [value[1] for value in values]
        if not all(
            math.isclose(radius, radii[0], rel_tol=1e-8, abs_tol=1e-10)
            for radius in radii[1:]
        ):
            raise self._error(node, "2D hull requires equal-radius circles")
        centers = [(value[0][0], value[0][1]) for value in values]
        hull = _convex_hull_2d(centers)
        radius = radii[0]
        if len(hull) == 1:
            return Geometry(2, [self._circle_face(radius, hull[0])])
        if len(hull) == 2:
            first, second = hull
            dx, dy = second[0] - first[0], second[1] - first[1]
            distance = math.hypot(dx, dy)
            ux, uy = dx / distance, dy / distance
            px, py = -uy * radius, ux * radius
            a_plus = (first[0] + px, first[1] + py, 0)
            b_plus = (second[0] + px, second[1] + py, 0)
            b_minus = (second[0] - px, second[1] - py, 0)
            a_minus = (first[0] - px, first[1] - py, 0)
            edges = [
                self.cq.Edge.makeLine(a_plus, b_plus),
                self.cq.Edge.makeThreePointArc(
                    b_plus,
                    (second[0] + ux * radius, second[1] + uy * radius, 0),
                    b_minus,
                ),
                self.cq.Edge.makeLine(b_minus, a_minus),
                self.cq.Edge.makeThreePointArc(
                    a_minus,
                    (first[0] - ux * radius, first[1] - uy * radius, 0),
                    a_plus,
                ),
            ]
            wire = self.cq.Wire.assembleEdges(edges)
            return Geometry(2, [self.cq.Face.makeFromWires(wire)])
        center_wire = self.cq.Wire.makePolygon([(*point, 0) for point in hull], close=True)
        offset_wires = center_wire.offset2D(radius, "arc")
        if not offset_wires:
            raise self._error(node, "OpenCASCADE could not construct the circle hull")
        return Geometry(2, [self.cq.Face.makeFromWires(offset_wires[0])])

    def _node_offset(self, node: CsgNode) -> Geometry:
        geometry = self._evaluate_children(node.children)
        faces = self._profile_faces(node, geometry)
        radius = self._arg(node, "r", 0, None)
        delta = self._arg(node, "delta", 0, None)
        if radius is not None and delta is not None:
            raise self._error(node, "offset cannot specify both r and delta")
        raw_amount = radius if radius is not None else delta if delta is not None else 1
        amount = self._number(node, raw_amount, "offset distance")
        chamfer = self._boolean(
            node, self._arg(node, "chamfer", 1, False), "offset chamfer"
        )
        if chamfer:
            raise self._error(node, "offset(chamfer=true) has no exact CadQuery equivalent")
        kind = "arc" if radius is not None else "intersection"
        output_faces: list[Any] = []
        output_colors: list[ColorValue | None] = []
        for face, color in faces:
            outers = face.outerWire().offset2D(amount, kind)
            holes = [wire for inner in face.innerWires() for wire in inner.offset2D(-amount, kind)]
            for outer in outers:
                output_faces.append(self.cq.Face.makeFromWires(outer, holes))
                output_colors.append(color)
        return Geometry(2, output_faces, output_colors)

    def _node_import(self, node: CsgNode) -> Geometry:
        filename = self._arg(node, "file", 0, None)
        if not isinstance(filename, str) or not filename:
            raise self._error(node, "import requires a file path")
        path = Path(filename)
        if not path.is_absolute():
            path = self.source_directory / path
        if path.suffix.casefold() != ".dxf":
            raise self._error(
                node,
                f"import of {path.suffix or 'extensionless'} files is not supported; "
                "only 2D DXF imports have a CadQuery equivalent",
            )
        if not path.is_file():
            raise self._error(node, f"import file not found: {path}")
        workplane = self.cq.importers.importDXF(str(path))
        return Geometry(2, list(workplane.vals()))


def derive_output_paths(input_scad: Path, output_step: Path | None) -> tuple[Path, Path]:
    """Return the STEP and retained intermediate CSG paths."""

    step = output_step if output_step is not None else input_scad.with_suffix(".step")
    return step, step.with_suffix(".csg")


def compressed_output_path(output_step: Path) -> Path:
    """Return the gzip path corresponding to a STEP output path."""

    return output_step.with_name(f"{output_step.name}.gz")


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert an OpenSCAD .scad model to an analytic .step B-Rep."
    )
    parser.add_argument("input_scad", type=Path, help="source .scad file")
    parser.add_argument(
        "output_step",
        nargs="?",
        type=Path,
        help="output .step path (default: input path with a .step extension)",
    )
    parser.add_argument(
        "-o",
        "--output",
        dest="output_option",
        type=Path,
        help="alternative way to specify the output .step path",
    )
    parser.add_argument(
        "--openscad",
        type=Path,
        default=DEFAULT_OPENSCAD,
        help=f"OpenSCAD executable (default: {DEFAULT_OPENSCAD})",
    )
    parser.add_argument(
        "--no-overwrite",
        action="store_true",
        help="fail if any requested output file already exists",
    )
    parser.add_argument(
        "--compress",
        action="store_true",
        help="also write a gzip-compressed copy to OUTPUT.gz",
    )
    args = parser.parse_args(argv)
    if args.output_step is not None and args.output_option is not None:
        parser.error("specify the output path either positionally or with --output, not both")
    if args.output_option is not None:
        args.output_step = args.output_option
    return args


def _run_openscad(openscad: Path, input_scad: Path, output_csg: Path) -> None:
    with tempfile.TemporaryDirectory(
        prefix="openscad-to-step-", dir=output_csg.parent
    ) as temporary:
        temporary_csg = Path(temporary) / output_csg.name
        result = subprocess.run(
            [str(openscad), "-o", str(temporary_csg.resolve()), str(input_scad.resolve())],
            cwd=input_scad.parent,
            text=True,
            capture_output=True,
            check=False,
        )
        if result.stdout:
            print(result.stdout.rstrip(), file=sys.stderr)
        if result.stderr:
            print(result.stderr.rstrip(), file=sys.stderr)
        if result.returncode != 0:
            raise CsgError(f"OpenSCAD exited with status {result.returncode}")
        if not temporary_csg.is_file() or temporary_csg.stat().st_size == 0:
            raise CsgError("OpenSCAD did not create a non-empty CSG file")
        temporary_csg.replace(output_csg)


def _named_step_assembly(
    cq: Any, parts: Sequence[StepPart], assembly_name: str
) -> Any:
    if not parts:
        raise CsgError("the converted model contains no solids to export")

    assembly = cq.Assembly(name=assembly_name)
    for index, part in enumerate(parts, start=1):
        color = cq.Color(*part.color) if part.color is not None else None
        assembly.add(part.shape, name=f"Part {index}", color=color)
    return assembly


def _export_step(cq: Any, parts: Sequence[StepPart], output_step: Path) -> None:
    assembly = _named_step_assembly(cq, parts, output_step.stem)
    with tempfile.TemporaryDirectory(
        prefix="openscad-to-step-", dir=output_step.parent
    ) as temporary:
        temporary_step = Path(temporary) / output_step.name
        assembly.export(str(temporary_step))
        if not temporary_step.is_file() or temporary_step.stat().st_size == 0:
            raise CsgError("CadQuery did not create a non-empty STEP file")
        temporary_step.replace(output_step)


def _compress_step(input_step: Path, output_gzip: Path) -> None:
    """Create an atomic gzip-compressed copy of a STEP file."""

    with tempfile.TemporaryDirectory(
        prefix="openscad-to-step-", dir=output_gzip.parent
    ) as temporary:
        temporary_gzip = Path(temporary) / output_gzip.name
        with input_step.open("rb") as source, temporary_gzip.open("wb") as destination:
            with gzip.GzipFile(
                filename=input_step.name,
                mode="wb",
                fileobj=destination,
                compresslevel=9,
                mtime=0,
            ) as compressed:
                shutil.copyfileobj(source, compressed)
        if not temporary_gzip.is_file() or temporary_gzip.stat().st_size == 0:
            raise CsgError("gzip compression did not create a non-empty file")
        temporary_gzip.replace(output_gzip)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    input_scad = args.input_scad.expanduser().resolve()
    requested_output = args.output_step.expanduser() if args.output_step is not None else None
    output_step, output_csg = derive_output_paths(input_scad, requested_output)
    output_step = output_step.resolve()
    output_csg = output_csg.resolve()
    output_gzip = compressed_output_path(output_step) if args.compress else None
    openscad = args.openscad.expanduser().resolve()

    if input_scad.suffix.casefold() != ".scad":
        print("error: input path must end in .scad", file=sys.stderr)
        return 2
    if not input_scad.is_file():
        print(f"error: OpenSCAD file not found: {input_scad}", file=sys.stderr)
        return 2
    if output_step.suffix.casefold() not in {".step", ".stp"}:
        print("error: output path must end in .step or .stp", file=sys.stderr)
        return 2
    if not output_step.parent.is_dir():
        print(f"error: output directory not found: {output_step.parent}", file=sys.stderr)
        return 2
    if not openscad.is_file():
        print(f"error: OpenSCAD executable not found: {openscad}", file=sys.stderr)
        return 2
    requested_outputs = [output_csg, output_step]
    if output_gzip is not None:
        requested_outputs.append(output_gzip)
    existing = [path for path in requested_outputs if path.exists()]
    if existing and args.no_overwrite:
        for path in existing:
            print(f"error: output file already exists: {path}", file=sys.stderr)
        return 2

    try:
        import cadquery as cq
    except ImportError:
        print(
            "error: CadQuery is required; install it with "
            f"{sys.executable} -m pip install cadquery",
            file=sys.stderr,
        )
        return 2

    try:
        _run_openscad(openscad, input_scad, output_csg)
        nodes = parse_csg(output_csg.read_text(encoding="utf-8-sig"))
        converter = CsgConverter(cq, input_scad.parent)
        converter.convert(nodes)
        _export_step(cq, converter.step_parts, output_step)
        if output_gzip is not None:
            _compress_step(output_step, output_gzip)
    except (CsgError, OSError, subprocess.SubprocessError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    except Exception as exc:
        print(f"error: CadQuery conversion failed: {exc}", file=sys.stderr)
        return 1

    for warning in converter.warnings:
        print(f"warning: {warning}", file=sys.stderr)
    print(f"Generated CSG:  {output_csg}")
    print(f"Generated STEP: {output_step}")
    if output_gzip is not None:
        print(f"Generated gzip: {output_gzip}")
    return 0


if __name__ == "__main__":
    exit_code = main()
    sys.stdout.flush()
    sys.stderr.flush()
    # Avoid a known Windows OCP teardown access violation after CadQuery exits.
    os._exit(exit_code)
