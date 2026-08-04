#!/usr/bin/env python3
"""Report components, vias, and bare holes grouped by EAGLE drill size."""

from __future__ import annotations

import argparse
import re
import sys
import xml.etree.ElementTree as ET
from collections import defaultdict
from decimal import Decimal, InvalidOperation
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_BOARD = REPO_ROOT / "electrical" / "hot-wand.brd"

PackageKey = tuple[str, str]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Read an EAGLE .brd file and print component designators grouped "
            "by the drill diameters used in their packages, including via "
            "and bare mechanical-hole counts."
        )
    )
    parser.add_argument(
        "board",
        nargs="?",
        type=Path,
        default=DEFAULT_BOARD,
        help="input EAGLE .brd file (default: electrical/hot-wand.brd)",
    )
    return parser.parse_args()


def parse_board(path: Path) -> ET.Element:
    if path.suffix.lower() != ".brd":
        raise ValueError(f"Board must have a .brd extension: {path}")
    if not path.is_file():
        raise FileNotFoundError(f"Board not found: {path}")

    try:
        return ET.parse(path).getroot()
    except ET.ParseError as exc:
        raise ValueError(f"Invalid EAGLE board XML in {path}: {exc}") from exc


def parse_drill_size(value: str, context: str) -> Decimal:
    try:
        drill_size = Decimal(value)
    except InvalidOperation as exc:
        raise ValueError(f"Invalid drill size {value!r} in {context}") from exc
    if not drill_size.is_finite() or drill_size <= 0:
        raise ValueError(f"Invalid drill size {value!r} in {context}")
    return drill_size.normalize()


def index_drilled_packages(
    root: ET.Element,
) -> tuple[dict[PackageKey, frozenset[Decimal]], set[PackageKey]]:
    drilled_packages: dict[PackageKey, frozenset[Decimal]] = {}
    known_packages: set[PackageKey] = set()

    libraries = root.find("./drawing/board/libraries")
    if libraries is None:
        raise ValueError("Board does not contain an embedded package library")

    for library in libraries.findall("library"):
        library_name = library.get("name")
        if not library_name:
            raise ValueError("Encountered an embedded library without a name")

        packages = library.find("packages")
        if packages is None:
            continue

        for package in packages.findall("package"):
            package_name = package.get("name")
            if not package_name:
                raise ValueError(
                    f"Encountered an unnamed package in library {library_name!r}"
                )

            key = (library_name, package_name)
            if key in known_packages:
                raise ValueError(
                    f"Duplicate package {library_name!r}/{package_name!r}"
                )
            known_packages.add(key)

            drill_sizes: set[Decimal] = set()
            for element_name in ("pad", "hole"):
                for drilled_element in package.findall(element_name):
                    drill_value = drilled_element.get("drill")
                    if drill_value is None:
                        continue
                    context = (
                        f"{element_name} in package "
                        f"{library_name!r}/{package_name!r}"
                    )
                    drill_sizes.add(parse_drill_size(drill_value, context))

            if drill_sizes:
                drilled_packages[key] = frozenset(drill_sizes)

    return drilled_packages, known_packages


def group_parts_by_drill_size(
    root: ET.Element,
    drilled_packages: dict[PackageKey, frozenset[Decimal]],
    known_packages: set[PackageKey],
) -> dict[Decimal, set[str]]:
    parts_by_drill_size: dict[Decimal, set[str]] = defaultdict(set)
    elements = root.find("./drawing/board/elements")
    if elements is None:
        raise ValueError("Board does not contain an elements section")

    for element in elements.findall("element"):
        part_name = element.get("name")
        library_name = element.get("library")
        package_name = element.get("package")
        if not part_name or not library_name or not package_name:
            raise ValueError(
                "Encountered an element without a name, library, or package"
            )

        key = (library_name, package_name)
        if key not in known_packages:
            raise ValueError(
                f"Element {part_name!r} references missing package "
                f"{library_name!r}/{package_name!r}"
            )

        for drill_size in drilled_packages.get(key, ()):
            parts_by_drill_size[drill_size].add(part_name)

    return dict(parts_by_drill_size)


def add_vias_by_drill_size(
    root: ET.Element,
    parts_by_drill_size: dict[Decimal, set[str]],
) -> None:
    via_counts: dict[Decimal, int] = defaultdict(int)
    signals = root.find("./drawing/board/signals")
    if signals is None:
        raise ValueError("Board does not contain a signals section")

    for signal in signals.findall("signal"):
        signal_name = signal.get("name", "<unnamed>")
        for via in signal.findall("via"):
            drill_value = via.get("drill")
            if drill_value is None:
                raise ValueError(
                    f"Encountered a via without a drill size in signal "
                    f"{signal_name!r}"
                )
            context = f"via in signal {signal_name!r}"
            drill_size = parse_drill_size(drill_value, context)
            via_counts[drill_size] += 1

    for drill_size, count in via_counts.items():
        parts_by_drill_size.setdefault(drill_size, set()).add(f"VIA[{count}]")


def add_bare_holes_by_drill_size(
    root: ET.Element,
    parts_by_drill_size: dict[Decimal, set[str]],
) -> None:
    hole_counts: dict[Decimal, int] = defaultdict(int)
    plain = root.find("./drawing/board/plain")
    if plain is None:
        raise ValueError("Board does not contain a plain section")

    for hole in plain.findall("hole"):
        drill_value = hole.get("drill")
        if drill_value is None:
            raise ValueError("Encountered a bare mechanical hole without a drill size")
        drill_size = parse_drill_size(drill_value, "bare mechanical hole")
        hole_counts[drill_size] += 1

    for drill_size, count in hole_counts.items():
        parts_by_drill_size.setdefault(drill_size, set()).add(f"HOLE[{count}]")


def natural_name_key(name: str) -> tuple[tuple[int, int | str], ...]:
    return tuple(
        (0, int(part)) if part.isdigit() else (1, part.casefold())
        for part in re.split(r"(\d+)", name)
        if part
    )


def format_decimal(value: Decimal) -> str:
    return format(value, "f")


def print_report(parts_by_drill_size: dict[Decimal, set[str]]) -> None:
    drill_values = [
        (drill_size, format_decimal(drill_size))
        for drill_size in sorted(parts_by_drill_size)
    ]
    heading = "Drill (mm)"
    column_width = max(
        len(heading),
        *(len(formatted) for _drill_size, formatted in drill_values),
    )

    print(f"{heading:<{column_width}}  Parts")
    print(f"{'-' * len(heading):<{column_width}}  -----")
    for drill_size, formatted in drill_values:
        part_names = sorted(
            parts_by_drill_size[drill_size],
            key=natural_name_key,
        )
        print(f"{formatted:<{column_width}}  {', '.join(part_names)}")


def main() -> int:
    args = parse_args()
    try:
        board_path = args.board.expanduser().resolve()
        root = parse_board(board_path)
        drilled_packages, known_packages = index_drilled_packages(root)
        parts_by_drill_size = group_parts_by_drill_size(
            root,
            drilled_packages,
            known_packages,
        )
        add_vias_by_drill_size(root, parts_by_drill_size)
        add_bare_holes_by_drill_size(root, parts_by_drill_size)
        print_report(parts_by_drill_size)
        return 0
    except (OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
