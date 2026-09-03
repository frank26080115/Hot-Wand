#!/usr/bin/env python3
"""List the named solid bodies contained in a STEP file using CadQuery."""

from __future__ import annotations

import argparse
import gc
import os
import sys
from collections import Counter
from pathlib import Path

import cadquery as cq
from OCP.IFSelect import IFSelect_RetDone
from OCP.STEPCAFControl import STEPCAFControl_Reader
from OCP.TCollection import TCollection_ExtendedString
from OCP.TDataStd import TDataStd_Name
from OCP.TDF import TDF_Label, TDF_LabelSequence
from OCP.TDocStd import TDocStd_Document
from OCP.XCAFApp import XCAFApp_Application
from OCP.XCAFDoc import XCAFDoc_DocumentTool


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="List named solid bodies and occurrences contained in a STEP file."
    )
    parser.add_argument("step_file", type=Path, help="path to a .step or .stp file")
    parser.add_argument(
        "--name",
        help="only show products whose names contain this text (case-insensitive)",
    )
    return parser.parse_args()


def _label_name(label: TDF_Label) -> str:
    name = TDataStd_Name()
    if label.FindAttribute(TDataStd_Name.GetID_s(), name):
        return str(name.Get().ToExtString())
    return "<unnamed>"


def load_products(step_file: Path) -> list[tuple[str, list[cq.Solid]]]:
    """Load top-level STEP products without discarding their product names."""

    reader = STEPCAFControl_Reader()
    reader.SetNameMode(True)
    if reader.ReadFile(str(step_file)) != IFSelect_RetDone:
        raise ValueError("STEP reader could not read the file")

    application = XCAFApp_Application.GetApplication_s()
    document = TDocStd_Document(TCollection_ExtendedString("step-body-explorer"))
    application.InitDocument(document)
    if not reader.Transfer(document):
        raise ValueError("STEP reader could not transfer the file")

    shape_tool = XCAFDoc_DocumentTool.ShapeTool_s(document.Main())
    labels = TDF_LabelSequence()
    shape_tool.GetFreeShapes(labels)

    products = []
    for label in labels:
        shape = cq.Shape.cast(shape_tool.GetShape_s(label))
        products.append((_label_name(label), shape.Solids()))

    # Explicitly release the XCAF document before interpreter shutdown.  Some
    # Windows OpenCascade builds otherwise destroy these handles out of order.
    del shape_tool, labels
    del document, reader, application
    gc.collect()
    return products


def main() -> int:
    args = parse_args()
    step_file = args.step_file.expanduser()

    if not step_file.is_file():
        print(f"error: STEP file not found: {step_file}", file=sys.stderr)
        return 2

    try:
        products = load_products(step_file)
    except Exception as exc:
        print(f"error: could not import {step_file}: {exc}", file=sys.stderr)
        return 1

    name_counts = Counter(name for name, _ in products)
    selected = products
    if args.name:
        needle = args.name.casefold()
        selected = [(name, solids) for name, solids in products if needle in name.casefold()]

    total_bodies = sum(len(solids) for _, solids in products)
    print(
        f"{step_file}: {len(products)} named product occurrences, "
        f"{total_bodies} solid bodies"
    )
    if args.name:
        print(f"Showing {len(selected)} occurrence(s) matching {args.name!r}")

    seen_counts: Counter[str] = Counter()
    for product_index, (name, solids) in enumerate(selected, start=1):
        seen_counts[name] += 1
        occurrence = (
            f", occurrence {seen_counts[name]}/{name_counts[name]}"
            if name_counts[name] > 1
            else ""
        )
        print(f"Product {product_index}: name={name!r}, solids={len(solids)}{occurrence}")

        for solid_index, body in enumerate(solids, start=1):
            center = body.Center()
            bounds = body.BoundingBox()
            print(
                f"  Solid {solid_index}: "
                f"volume={body.Volume():.6g}, "
                f"center=({center.x:.6g}, {center.y:.6g}, {center.z:.6g}), "
                f"size=({bounds.xlen:.6g}, {bounds.ylen:.6g}, {bounds.zlen:.6g})"
            )

    if not selected:
        print("No matching named products found.")
        return 1

    return 0


if __name__ == "__main__":
    exit_code = main()
    sys.stdout.flush()
    sys.stderr.flush()
    # Avoid a known Windows OCP teardown access violation after CadQuery exits.
    os._exit(exit_code)
