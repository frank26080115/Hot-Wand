#!/usr/bin/env python3
"""Export a named body from an Onshape STEP file as STEP and STL."""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

import cadquery as cq

from step_body_explorer import load_products


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Export a named body from a STEP file as STEP and STL."
    )
    parser.add_argument("input_step", type=Path, help="path to the source STEP file")
    parser.add_argument("body_name", help="exact Onshape body/product name to export")
    parser.add_argument("output_step", type=Path, help="path for the exported STEP file")
    parser.add_argument(
        "--force", action="store_true", help="overwrite the output file if it exists"
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    input_step = args.input_step.expanduser()
    output_step = args.output_step.expanduser()
    output_stl = output_step.with_suffix(".stl")

    if not input_step.is_file():
        print(f"error: STEP file not found: {input_step}", file=sys.stderr)
        return 2
    if output_step.suffix.casefold() not in {".step", ".stp"}:
        print("error: output path must end in .step or .stp", file=sys.stderr)
        return 2
    if input_step.resolve() == output_step.resolve():
        print("error: input and output paths must be different", file=sys.stderr)
        return 2
    existing_outputs = [path for path in (output_step, output_stl) if path.exists()]
    if existing_outputs and not args.force:
        for path in existing_outputs:
            print(f"error: output file already exists: {path}", file=sys.stderr)
        print("Use --force to overwrite existing output files.", file=sys.stderr)
        return 2
    if not output_step.parent.is_dir():
        print(f"error: output directory not found: {output_step.parent}", file=sys.stderr)
        return 2

    try:
        products = load_products(input_step)
    except Exception as exc:
        print(f"error: could not import {input_step}: {exc}", file=sys.stderr)
        return 1

    matches = [solids for name, solids in products if name == args.body_name]
    solids = [solid for occurrence in matches for solid in occurrence]
    if not matches:
        available = sorted({name for name, _ in products}, key=str.casefold)
        print(f"error: body {args.body_name!r} was not found", file=sys.stderr)
        print("Available names:", file=sys.stderr)
        for name in available:
            print(f"  {name}", file=sys.stderr)
        return 1
    if not solids:
        print(f"error: body {args.body_name!r} contains no solids", file=sys.stderr)
        return 1

    shape: cq.Shape
    if len(solids) == 1:
        shape = solids[0]
    else:
        shape = cq.Compound.makeCompound(solids)

    try:
        cq.exporters.export(shape, str(output_step))
    except Exception as exc:
        print(f"error: could not export {output_step}: {exc}", file=sys.stderr)
        return 1

    try:
        cq.exporters.export(
            shape,
            str(output_stl),
            tolerance=0.02,
            angularTolerance=0.1,
        )
    except Exception as exc:
        print(f"error: could not export {output_stl}: {exc}", file=sys.stderr)
        return 1

    print(
        f"Exported {len(matches)} occurrence(s) of {args.body_name!r} "
        f"({len(solids)} solid(s)) to {output_step} and {output_stl}"
    )
    return 0


if __name__ == "__main__":
    exit_code = main()
    sys.stdout.flush()
    sys.stderr.flush()
    # Avoid a known Windows OCP teardown access violation after CadQuery exits.
    os._exit(exit_code)
