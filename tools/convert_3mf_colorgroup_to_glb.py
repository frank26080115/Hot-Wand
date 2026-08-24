#!/usr/bin/env python3
"""Convert a standard color-group 3MF to a self-contained colored GLB.

This intentionally handles the 3MF material/color representation emitted by
OpenSCAD: triangle pid/p1 assignments referencing m:colorgroup resources.
"""

from __future__ import annotations

import argparse
import json
import math
import struct
import zipfile
from collections import defaultdict
from pathlib import Path
from xml.etree import ElementTree as ET


UNIT_TO_METERS = {
    "micron": 1e-6,
    "millimeter": 1e-3,
    "centimeter": 1e-2,
    "inch": 0.0254,
    "foot": 0.3048,
    "meter": 1.0,
}


def local_name(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def srgb_to_linear(value: int) -> float:
    c = value / 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def parse_color(
    value: str,
    *,
    force_opaque: bool = False,
) -> tuple[float, float, float, float]:
    value = value.lstrip("#")
    if len(value) == 6:
        value += "FF"
    if len(value) != 8:
        raise ValueError(f"Unsupported color value: #{value}")
    rgba = [int(value[i : i + 2], 16) for i in range(0, 8, 2)]
    return (
        srgb_to_linear(rgba[0]),
        srgb_to_linear(rgba[1]),
        srgb_to_linear(rgba[2]),
        1.0 if force_opaque else rgba[3] / 255.0,
    )


def parse_transform(value: str | None) -> tuple[float, ...]:
    if not value:
        return (1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0)
    numbers = tuple(float(v) for v in value.split())
    if len(numbers) != 12:
        raise ValueError(f"Expected a 12-number 3MF transform, got {len(numbers)}")
    return numbers


def transform_vertex(
    v: tuple[float, float, float],
    m: tuple[float, ...],
    source_to_meters: float,
) -> tuple[float, float, float]:
    x, y, z = v
    tx = m[0] * x + m[3] * y + m[6] * z + m[9]
    ty = m[1] * x + m[4] * y + m[7] * z + m[10]
    tz = m[2] * x + m[5] * y + m[8] * z + m[11]
    # 3MF is Z-up. glTF is Y-up and defines one coordinate unit as one metre.
    # Rotate without changing handedness, then convert the declared 3MF units
    # to metres so importers such as Blender retain the physical dimensions.
    return (
        tx * source_to_meters,
        tz * source_to_meters,
        -ty * source_to_meters,
    )


def triangle_normal(a: tuple[float, float, float], b: tuple[float, float, float], c: tuple[float, float, float]) -> tuple[float, float, float]:
    ux, uy, uz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
    vx, vy, vz = c[0] - a[0], c[1] - a[1], c[2] - a[2]
    nx = uy * vz - uz * vy
    ny = uz * vx - ux * vz
    nz = ux * vy - uy * vx
    length = math.sqrt(nx * nx + ny * ny + nz * nz)
    if length == 0:
        return 0.0, 1.0, 0.0
    return nx / length, ny / length, nz / length


def align4(data: bytearray, pad: int = 0) -> None:
    while len(data) % 4:
        data.append(pad)


def convert(source: Path, destination: Path) -> dict[str, object]:
    with zipfile.ZipFile(source) as archive:
        model_xml = archive.read("3D/3dmodel.model")
    root = ET.fromstring(model_xml)
    source_unit = root.attrib.get("unit", "millimeter").lower()
    try:
        source_to_meters = UNIT_TO_METERS[source_unit]
    except KeyError as exc:
        supported = ", ".join(sorted(UNIT_TO_METERS))
        raise ValueError(
            f"Unsupported 3MF unit {source_unit!r}; expected one of: {supported}"
        ) from exc
    source_application = next(
        (
            (element.text or "").strip()
            for element in root.iter()
            if local_name(element.tag) == "metadata"
            and element.attrib.get("name") == "Application"
        ),
        "",
    )
    source_is_openscad = source_application.startswith("OpenSCAD")

    property_groups: dict[int, list[tuple[str, tuple[float, float, float, float]]]] = {}
    for element in root.iter():
        kind = local_name(element.tag)
        if kind == "colorgroup":
            group_id = int(element.attrib["id"])
            values = []
            for i, child in enumerate(element):
                if local_name(child.tag) == "color":
                    raw = child.attrib["color"]
                    # The 3MF Materials Extension defines color-group colors
                    # as fully opaque. OpenSCAD/lib3mf may nevertheless emit
                    # an eight-digit value ending in 00; treating that byte as
                    # transparency makes the complete model invisible in GLB.
                    values.append(
                        (raw.upper(), parse_color(raw, force_opaque=True))
                    )
            property_groups[group_id] = values
        elif kind == "basematerials":
            group_id = int(element.attrib["id"])
            values = []
            for i, child in enumerate(element):
                if local_name(child.tag) == "base":
                    raw = child.attrib.get("displaycolor", "#808080FF")
                    # Some OpenSCAD/lib3mf combinations emit every base
                    # material as #RRGGBB00 even when the source color is
                    # visible. Treat that producer-specific zero alpha as an
                    # export artifact. Preserve alpha from other producers and
                    # any nonzero alpha a future OpenSCAD exporter supplies.
                    openscad_zero_alpha = (
                        source_is_openscad
                        and len(raw.lstrip("#")) == 8
                        and raw.upper().endswith("00")
                    )
                    values.append(
                        (
                            raw.upper(),
                            parse_color(raw, force_opaque=openscad_zero_alpha),
                        )
                    )
            property_groups[group_id] = values

    objects = {
        int(element.attrib["id"]): element
        for element in root.iter()
        if local_name(element.tag) == "object" and element.attrib.get("type", "model") == "model"
    }
    build_items = [element for element in root.iter() if local_name(element.tag) == "item"]
    instances = [
        (int(item.attrib["objectid"]), parse_transform(item.attrib.get("transform")))
        for item in build_items
    ] or [(object_id, parse_transform(None)) for object_id in objects]

    grouped: dict[tuple[int, int], list[tuple[tuple[float, float, float], ...]]] = defaultdict(list)
    for object_id, matrix in instances:
        obj = objects.get(object_id)
        if obj is None:
            raise ValueError(f"Build references missing object {object_id}")
        mesh = next((child for child in obj if local_name(child.tag) == "mesh"), None)
        if mesh is None:
            raise ValueError("This converter expects direct mesh objects, not component assemblies")
        vertices_element = next(child for child in mesh if local_name(child.tag) == "vertices")
        triangles_element = next(child for child in mesh if local_name(child.tag) == "triangles")
        vertices = [
            (float(v.attrib["x"]), float(v.attrib["y"]), float(v.attrib["z"]))
            for v in vertices_element
            if local_name(v.tag) == "vertex"
        ]
        object_pid = int(obj.attrib["pid"]) if "pid" in obj.attrib else None
        object_pindex = int(obj.attrib.get("pindex", "0"))
        for tri in triangles_element:
            if local_name(tri.tag) != "triangle":
                continue
            pid = int(tri.attrib["pid"]) if "pid" in tri.attrib else object_pid
            p1 = int(tri.attrib["p1"]) if "p1" in tri.attrib else object_pindex
            p2 = int(tri.attrib.get("p2", str(p1)))
            p3 = int(tri.attrib.get("p3", str(p1)))
            if pid is None or pid not in property_groups:
                raise ValueError(f"Triangle references missing property group {pid}")
            if p1 != p2 or p1 != p3:
                raise ValueError("Per-vertex color gradients are not supported by this converter")
            indices = (int(tri.attrib["v1"]), int(tri.attrib["v2"]), int(tri.attrib["v3"]))
            grouped[(pid, p1)].append(
                tuple(
                    transform_vertex(vertices[i], matrix, source_to_meters)
                    for i in indices
                )
            )

    binary = bytearray()
    buffer_views: list[dict[str, object]] = []
    accessors: list[dict[str, object]] = []
    materials: list[dict[str, object]] = []
    primitives: list[dict[str, object]] = []
    summary: list[dict[str, object]] = []
    model_mins = [float("inf")] * 3
    model_maxs = [float("-inf")] * 3

    for material_index, ((pid, pindex), triangles) in enumerate(sorted(grouped.items())):
        raw_color, factor = property_groups[pid][pindex]
        positions: list[float] = []
        normals: list[float] = []
        mins = [float("inf")] * 3
        maxs = [float("-inf")] * 3
        for a, b, c in triangles:
            normal = triangle_normal(a, b, c)
            for vertex in (a, b, c):
                positions.extend(vertex)
                normals.extend(normal)
                for axis in range(3):
                    mins[axis] = min(mins[axis], vertex[axis])
                    maxs[axis] = max(maxs[axis], vertex[axis])
                    model_mins[axis] = min(model_mins[axis], vertex[axis])
                    model_maxs[axis] = max(model_maxs[axis], vertex[axis])

        position_offset = len(binary)
        binary.extend(struct.pack(f"<{len(positions)}f", *positions))
        align4(binary)
        position_view = len(buffer_views)
        buffer_views.append({"buffer": 0, "byteOffset": position_offset, "byteLength": len(positions) * 4, "target": 34962})
        position_accessor = len(accessors)
        accessors.append({"bufferView": position_view, "componentType": 5126, "count": len(positions) // 3, "type": "VEC3", "min": mins, "max": maxs})

        normal_offset = len(binary)
        binary.extend(struct.pack(f"<{len(normals)}f", *normals))
        align4(binary)
        normal_view = len(buffer_views)
        buffer_views.append({"buffer": 0, "byteOffset": normal_offset, "byteLength": len(normals) * 4, "target": 34962})
        normal_accessor = len(accessors)
        accessors.append({"bufferView": normal_view, "componentType": 5126, "count": len(normals) // 3, "type": "VEC3"})

        material: dict[str, object] = {
            "name": f"Color {pindex}: {raw_color}",
            "pbrMetallicRoughness": {
                "baseColorFactor": list(factor),
                "metallicFactor": 0.0,
                "roughnessFactor": 0.7,
            },
            "doubleSided": False,
            "extras": {"3mfPropertyGroup": pid, "3mfPropertyIndex": pindex, "sourceColor": raw_color},
        }
        if factor[3] < 1.0:
            material["alphaMode"] = "BLEND"
        materials.append(material)
        primitives.append({"attributes": {"POSITION": position_accessor, "NORMAL": normal_accessor}, "material": material_index, "mode": 4})
        summary.append({"propertyIndex": pindex, "color": raw_color, "triangles": len(triangles)})

    document = {
        "asset": {
            "version": "2.0",
            "generator": "OpenSCAD 3MF color-group converter",
            "extras": {
                "sourceFormat": "3MF",
                "sourceApplication": source_application,
                "sourceUnit": source_unit,
                "coordinateUnit": "meter",
                "sourceToMeters": source_to_meters,
            },
        },
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": source.stem}],
        "meshes": [{"name": source.stem, "primitives": primitives}],
        "materials": materials,
        "buffers": [{"byteLength": len(binary)}],
        "bufferViews": buffer_views,
        "accessors": accessors,
    }
    json_bytes = json.dumps(document, separators=(",", ":")).encode("utf-8")
    while len(json_bytes) % 4:
        json_bytes += b" "
    align4(binary)
    total_length = 12 + 8 + len(json_bytes) + 8 + len(binary)
    glb = bytearray(struct.pack("<4sII", b"glTF", 2, total_length))
    glb.extend(struct.pack("<I4s", len(json_bytes), b"JSON"))
    glb.extend(json_bytes)
    glb.extend(struct.pack("<I4s", len(binary), b"BIN\x00"))
    glb.extend(binary)
    destination.write_bytes(glb)
    dimensions_meters = [
        model_maxs[axis] - model_mins[axis]
        for axis in range(3)
    ]
    return {
        "source": str(source),
        "destination": str(destination),
        "sourceApplication": source_application,
        "sourceUnit": source_unit,
        "sourceToMeters": source_to_meters,
        "boundsMeters": {
            "min": model_mins,
            "max": model_maxs,
            "dimensions": dimensions_meters,
        },
        "dimensionsMillimeters": [dimension * 1000 for dimension in dimensions_meters],
        "triangles": sum(item["triangles"] for item in summary),
        "materials": summary,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    report = convert(args.input, args.output)
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
