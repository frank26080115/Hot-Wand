#!/usr/bin/env python3
"""Export the largest planar face of a STEP model as a millimeter DXF."""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

import cadquery as cq
from ezdxf import units


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Export the largest face of a STEP model as a millimeter DXF."
    )
    parser.add_argument("input_step", type=Path, help="path to the source STEP file")
    parser.add_argument("output_dxf", type=Path, help="path for the exported DXF file")
    parser.add_argument(
        "--force", action="store_true", help="overwrite the output file if it exists"
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    input_step = args.input_step.expanduser()
    output_dxf = args.output_dxf.expanduser()

    if not input_step.is_file():
        print(f"error: STEP file not found: {input_step}", file=sys.stderr)
        return 2
    if output_dxf.suffix.casefold() != ".dxf":
        print("error: output path must end in .dxf", file=sys.stderr)
        return 2
    if input_step.resolve() == output_dxf.resolve():
        print("error: input and output paths must be different", file=sys.stderr)
        return 2
    if output_dxf.exists() and not args.force:
        print(
            f"error: output file already exists: {output_dxf} "
            "(use --force to overwrite)",
            file=sys.stderr,
        )
        return 2
    if not output_dxf.parent.is_dir():
        print(f"error: output directory not found: {output_dxf.parent}", file=sys.stderr)
        return 2

    try:
        model = cq.importers.importStep(str(input_step), unit="MM")
        faces = model.faces().vals()
    except Exception as exc:
        print(f"error: could not import {input_step}: {exc}", file=sys.stderr)
        return 1

    if not faces:
        print(f"error: no faces found in {input_step}", file=sys.stderr)
        return 1

    largest_face = max(faces, key=lambda face: face.Area())
    if largest_face.geomType() != "PLANE":
        print(
            "error: the largest face is not planar and cannot be exported "
            "directly as a 2D DXF",
            file=sys.stderr,
        )
        return 1

    # A DXF is two-dimensional. Put the selected face on a local XY workplane;
    # exporting its wires retains both the outside cut and all internal holes.
    face_plane = cq.Plane(
        origin=largest_face.Center(),
        normal=largest_face.normalAt(),
    )
    profile = cq.Workplane(face_plane).newObject(largest_face.Wires())

    try:
        cq.exporters.exportDXF(profile, str(output_dxf), doc_units=units.MM)
    except Exception as exc:
        print(f"error: could not export {output_dxf}: {exc}", file=sys.stderr)
        return 1

    print(
        f"Exported largest face ({largest_face.Area():.6g} mm^2) "
        f"from {input_step} to {output_dxf}"
    )
    return 0


if __name__ == "__main__":
    exit_code = main()
    sys.stdout.flush()
    sys.stderr.flush()
    # Avoid a known Windows OCP teardown access violation after CadQuery exits.
    os._exit(exit_code)
