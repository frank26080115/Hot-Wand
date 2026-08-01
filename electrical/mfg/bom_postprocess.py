#!/usr/bin/env python3
"""Replace BOM comments with their corresponding LCSC part numbers."""

from __future__ import annotations

import argparse
import csv
import os
import sys
import tempfile
from pathlib import Path


REQUIRED_COLUMNS = ("Comment", "Designator", "Footprint", "LCSC Part #")
REPLACE_FOOTPRINTS_BY_DEFAULT = True


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Copy an EAGLE/JLCPCB BOM and replace every Comment with its "
            "corresponding LCSC Part #. Relative paths are resolved from this "
            "script's directory."
        )
    )
    parser.add_argument(
        "--input",
        type=Path,
        default=Path("hot-wand-bom.csv"),
        help="source BOM (default: hot-wand-bom.csv)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("hot-wand-bom-postprocessed.csv"),
        help="processed BOM (default: hot-wand-bom-postprocessed.csv)",
    )
    footprint_options = parser.add_mutually_exclusive_group()
    footprint_options.add_argument(
        "--replace-footprints",
        dest="replace_footprints",
        action="store_true",
        help="replace every Footprint with its processed LCSC Part # (default)",
    )
    footprint_options.add_argument(
        "--keep-footprints",
        dest="replace_footprints",
        action="store_false",
        help="preserve the original Footprint values",
    )
    parser.set_defaults(replace_footprints=REPLACE_FOOTPRINTS_BY_DEFAULT)
    return parser.parse_args()


def resolve_script_relative(path: Path, script_dir: Path) -> Path:
    path = path.expanduser()
    if not path.is_absolute():
        path = script_dir / path
    return path.resolve()


def select_lcsc_part_number(value: str) -> str:
    if ";" in value:
        first_chunk = value.split(";", 1)[0].strip()
        if first_chunk:
            return first_chunk
    return value


def read_bom(input_path: Path) -> tuple[list[str], list[dict[str, str]]]:
    with input_path.open("r", encoding="utf-8-sig", newline="") as source:
        reader = csv.DictReader(source)
        if reader.fieldnames is None:
            raise ValueError(f"BOM has no header row: {input_path}")

        fieldnames = list(reader.fieldnames)
        if len(fieldnames) != len(set(fieldnames)):
            raise ValueError(f"BOM contains duplicate column names: {input_path}")

        missing_columns = [
            column for column in REQUIRED_COLUMNS if column not in fieldnames
        ]
        if missing_columns:
            raise ValueError(
                "BOM is missing required columns: " + ", ".join(missing_columns)
            )

        rows: list[dict[str, str]] = []
        for line_number, row in enumerate(reader, start=2):
            if None in row:
                raise ValueError(
                    f"BOM row {line_number} contains more fields than the header"
                )
            if any(value is None for value in row.values()):
                raise ValueError(
                    f"BOM row {line_number} contains fewer fields than the header"
                )
            rows.append({key: value for key, value in row.items() if key is not None})

    return fieldnames, rows


def write_bom_atomic(
    output_path: Path,
    fieldnames: list[str],
    rows: list[dict[str, str]],
) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    temporary_path: Path | None = None

    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            newline="",
            dir=output_path.parent,
            prefix=f".{output_path.name}-",
            suffix=".tmp",
            delete=False,
        ) as temporary_file:
            temporary_path = Path(temporary_file.name)
            writer = csv.DictWriter(
                temporary_file,
                fieldnames=fieldnames,
                extrasaction="raise",
                lineterminator="\r\n",
            )
            writer.writeheader()
            writer.writerows(rows)

        os.replace(temporary_path, output_path)
        temporary_path = None
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)


def main() -> int:
    args = parse_args()
    script_dir = Path(__file__).resolve().parent
    input_path = resolve_script_relative(args.input, script_dir)
    output_path = resolve_script_relative(args.output, script_dir)

    if not input_path.is_file():
        raise FileNotFoundError(f"input BOM not found: {input_path}")
    if input_path == output_path:
        raise ValueError("input and output BOM paths must be different")

    fieldnames, rows = read_bom(input_path)
    for row in rows:
        part_number = select_lcsc_part_number(row["LCSC Part #"])
        row["Comment"] = part_number
        if args.replace_footprints:
            row["Footprint"] = part_number

    write_bom_atomic(output_path, fieldnames, rows)
    footprint_status = "replaced" if args.replace_footprints else "preserved"
    print(
        f"Processed {len(rows)} BOM rows; Footprint values {footprint_status}: "
        f"{output_path}"
    )
    return 0


def cli() -> int:
    try:
        return main()
    except KeyboardInterrupt:
        print("\nCanceled.", file=sys.stderr)
        return 130
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(cli())
