"""Propagate schematic part attributes to matching EAGLE board elements.

Example:
    python tools/propagate_eagle_attributes.py \
        R9:R11,R24,R27 \
        C8:C41,C58
"""

from __future__ import annotations

import argparse
from html import escape
from pathlib import Path
import re
import xml.etree.ElementTree as ET


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SCHEMATIC = REPO_ROOT / "electrical" / "hot-wand.sch"
DEFAULT_BOARD = REPO_ROOT / "electrical" / "hot-wand.brd"
DISPLAY_ATTRIBUTES = {"NAME", "VALUE"}


def parse_groups(arguments: list[str]) -> dict[str, list[str]]:
    groups: dict[str, list[str]] = {}
    all_targets: set[str] = set()

    for argument in arguments:
        source, separator, targets_text = argument.partition(":")
        targets = [target for target in targets_text.split(",") if target]

        if not separator or not source or not targets:
            raise ValueError(
                f"Invalid group {argument!r}; expected SOURCE:TARGET1,TARGET2"
            )
        if source in groups:
            raise ValueError(f"Source {source!r} was specified more than once")

        duplicates = all_targets.intersection(targets)
        if duplicates:
            raise ValueError(f"Targets specified more than once: {sorted(duplicates)}")
        if source in all_targets or source in targets:
            raise ValueError(f"Source {source!r} must not also be a target")

        groups[source] = targets
        all_targets.update(targets)

    return groups


def logical_attributes(node: ET.Element) -> list[tuple[str, str]]:
    return [
        (attribute.attrib["name"], attribute.attrib.get("value", ""))
        for attribute in node.findall("attribute")
        if attribute.attrib["name"] not in DISPLAY_ATTRIBUTES
    ]


def load_propagation(
    schematic_root: ET.Element,
    board_root: ET.Element,
    groups: dict[str, list[str]],
) -> dict[str, list[tuple[str, str]]]:
    schematic_parts = {
        part.attrib["name"]: part
        for part in schematic_root.findall("./drawing/schematic/parts/part")
    }
    board_elements = {
        element.attrib["name"]: element
        for element in board_root.findall("./drawing/board/elements/element")
    }
    propagation: dict[str, list[tuple[str, str]]] = {}

    for source, targets in groups.items():
        if source not in schematic_parts or source not in board_elements:
            raise ValueError(f"Source {source!r} is missing from the schematic or board")

        attributes = logical_attributes(schematic_parts[source])
        if not attributes:
            raise ValueError(f"Source {source!r} has no logical attributes to propagate")

        board_attributes = dict(logical_attributes(board_elements[source]))
        for name, value in attributes:
            if board_attributes.get(name) != value:
                raise ValueError(
                    f"Source {source!r} attribute {name!r} differs between "
                    "the schematic and board"
                )

        for target in targets:
            if target not in schematic_parts or target not in board_elements:
                raise ValueError(
                    f"Target {target!r} is missing from the schematic or board"
                )
            propagation[target] = attributes

    return propagation


def transform(
    text: str,
    tag: str,
    propagation: dict[str, list[tuple[str, str]]],
    board: bool,
) -> str:
    lines = text.splitlines(keepends=True)
    found: set[str] = set()
    i = 0

    while i < len(lines):
        match = re.match(rf'(\s*)<{tag} name="([^"]+)"', lines[i])
        if match is None or match.group(2) not in propagation:
            i += 1
            continue

        indent = match.group(1)
        target = match.group(2)
        found.add(target)
        opening = lines[i].rstrip("\r\n")

        if opening.endswith("/>"):
            newline = "\r\n" if lines[i].endswith("\r\n") else "\n"
            lines[i] = opening[:-2] + ">" + newline
            lines.insert(i + 1, f"{indent}</{tag}>{newline}")

        end = i + 1
        while end < len(lines) and lines[end].strip() != f"</{tag}>":
            end += 1
        if end == len(lines):
            raise ValueError(f"Unterminated {tag} {target!r}")

        attributes = propagation[target]
        replaced_names = {name for name, _ in attributes}
        if "JLCPARTNUM" in replaced_names:
            replaced_names.add("JLC-DNP")

        kept: list[str] = []
        for line in lines[i + 1 : end]:
            attribute_match = re.match(r'\s*<attribute name="([^"]+)"', line)
            if (
                attribute_match is not None
                and attribute_match.group(1) in replaced_names
            ):
                continue
            kept.append(line)

        newline = "\r\n" if lines[i].endswith("\r\n") else "\n"
        inserted: list[str] = []

        if board:
            x_match = re.search(r'\bx="([^"]+)"', opening)
            y_match = re.search(r'\by="([^"]+)"', opening)
            if x_match is None or y_match is None:
                raise ValueError(f"Board element {target!r} has no coordinates")

            rotation_match = re.search(r'\brot="([^"]+)"', opening)
            rotation = rotation_match.group(1) if rotation_match else ""
            layer = "28" if rotation.startswith("M") else "27"
            rotation_text = f' rot="{rotation}"' if rotation else ""

            for name, value in attributes:
                inserted.append(
                    f'{indent}<attribute name="{escape(name, quote=True)}" '
                    f'value="{escape(value, quote=True)}" '
                    f'x="{x_match.group(1)}" y="{y_match.group(1)}" '
                    f'size="1.778" layer="{layer}"{rotation_text} '
                    f'display="off"/>{newline}'
                )
        else:
            for name, value in attributes:
                inserted.append(
                    f'{indent}<attribute name="{escape(name, quote=True)}" '
                    f'value="{escape(value, quote=True)}"/>{newline}'
                )

        lines[i + 1 : end] = kept + inserted
        i += len(kept) + len(inserted) + 2

    missing = set(propagation) - found
    if missing:
        raise ValueError(f"Missing {tag} targets: {sorted(missing)}")

    return "".join(lines)


def validate_result(
    schematic_text: str,
    board_text: str,
    propagation: dict[str, list[tuple[str, str]]],
) -> None:
    schematic_root = ET.fromstring(schematic_text)
    board_root = ET.fromstring(board_text)
    schematic_parts = {
        part.attrib["name"]: part
        for part in schematic_root.findall("./drawing/schematic/parts/part")
    }
    board_elements = {
        element.attrib["name"]: element
        for element in board_root.findall("./drawing/board/elements/element")
    }

    for target, expected_attributes in propagation.items():
        schematic_attributes = dict(logical_attributes(schematic_parts[target]))
        board_attributes = dict(logical_attributes(board_elements[target]))

        for name, value in expected_attributes:
            if schematic_attributes.get(name) != value:
                raise ValueError(
                    f"Schematic target {target!r} has incorrect {name!r}"
                )
            if board_attributes.get(name) != value:
                raise ValueError(f"Board target {target!r} has incorrect {name!r}")

        if "JLCPARTNUM" in dict(expected_attributes):
            if "JLC-DNP" in schematic_attributes or "JLC-DNP" in board_attributes:
                raise ValueError(
                    f"Target {target!r} has both JLCPARTNUM and JLC-DNP"
                )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "groups",
        nargs="+",
        metavar="SOURCE:TARGET1,TARGET2",
        help="attribute propagation group",
    )
    parser.add_argument("--schematic", type=Path, default=DEFAULT_SCHEMATIC)
    parser.add_argument("--board", type=Path, default=DEFAULT_BOARD)
    args = parser.parse_args()

    groups = parse_groups(args.groups)
    schematic_path = args.schematic.resolve()
    board_path = args.board.resolve()

    for design_path in (schematic_path, board_path):
        lock_path = design_path.parent / f".{design_path.name}.lck"
        if lock_path.exists():
            raise RuntimeError(
                f"Refusing to edit while EAGLE lock exists: {lock_path}"
            )

    schematic_text = schematic_path.read_text(encoding="utf-8")
    board_text = board_path.read_text(encoding="utf-8")
    schematic_root = ET.fromstring(schematic_text)
    board_root = ET.fromstring(board_text)
    propagation = load_propagation(
        schematic_root,
        board_root,
        groups,
    )

    updated_schematic = transform(
        schematic_text,
        "part",
        propagation,
        board=False,
    )
    updated_board = transform(
        board_text,
        "element",
        propagation,
        board=True,
    )
    validate_result(updated_schematic, updated_board, propagation)

    schematic_path.write_text(updated_schematic, encoding="utf-8", newline="")
    board_path.write_text(updated_board, encoding="utf-8", newline="")
    print(
        f"Propagated attributes to {len(propagation)} targets in "
        f"{schematic_path.name} and {board_path.name}."
    )


if __name__ == "__main__":
    main()
