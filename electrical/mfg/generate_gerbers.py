#!/usr/bin/env python3
"""Generate Gerbers from a legacy EAGLE 7 CAM job and build the SVG preview."""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import uuid
import zipfile
from dataclasses import dataclass
from datetime import datetime
from decimal import Decimal, InvalidOperation
from pathlib import Path


DEFAULT_EAGLECON_EXE = Path(
    r"C:\ProgramFiles\EAGLE-7.6.0\bin\eaglecon.exe"
)

FLAG_OPTIONS = ("m", "r", "u", "c", "q", "O", "f")
REPORT_SUFFIXES = {".dri", ".gpi"}
LENGTH_PATTERN = re.compile(
    r"^([+-]?(?:\d+(?:\.\d*)?|\.\d+))\s*(mil|mm|inch|in)?$",
    re.IGNORECASE,
)


@dataclass(frozen=True)
class CamSection:
    section_id: str
    name: str
    device: str
    output_template: str
    layers: tuple[str, ...]
    values: dict[str, str]


def parse_args() -> argparse.Namespace:
    env_eaglecon = os.environ.get("EAGLECON_EXE")

    parser = argparse.ArgumentParser(
        description=(
            "Run every section of a legacy EAGLE CAM job, remove CAM reports, "
            "generate the combined SVG preview, and create a timestamped ZIP. "
            "Relative paths are resolved from this script's directory."
        )
    )
    parser.add_argument(
        "--eaglecon",
        type=Path,
        default=Path(env_eaglecon) if env_eaglecon else DEFAULT_EAGLECON_EXE,
        help="path to eaglecon.exe (default: EAGLECON_EXE or EAGLE 7.6)",
    )
    parser.add_argument(
        "--board",
        type=Path,
        default=Path("..") / "hot-wand.brd",
        help="EAGLE board file (default: ../hot-wand.brd)",
    )
    parser.add_argument(
        "--cam-job",
        "--cam",
        dest="cam_job",
        type=Path,
        default=Path("..") / "3rd-party" / "gerber-job-2layer.cam",
        help="legacy CAM job (default: ../3rd-party/gerber-job-2layer.cam)",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("gerbers"),
        help="published output directory (default: gerbers)",
    )
    parser.add_argument(
        "--archive-dir",
        type=Path,
        default=Path("."),
        help="directory for the timestamped Gerber ZIP (default: script directory)",
    )
    parser.add_argument(
        "--preview-script",
        type=Path,
        default=Path("generate_gerbv_preview_svg.py"),
        help="SVG preview generator (default: generate_gerbv_preview_svg.py)",
    )
    parser.add_argument(
        "--gerbv",
        type=Path,
        help="optional gerbv.exe override forwarded to the preview generator",
    )
    parser.add_argument(
        "--variant",
        help="optional EAGLE assembly variant",
    )
    parser.add_argument(
        "--no-preview",
        action="store_true",
        help="generate only CAM outputs and skip the SVG preview",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="show eaglecon output for successful sections",
    )
    return parser.parse_args()


def resolve_script_relative(path: Path, script_dir: Path) -> Path:
    path = path.expanduser()
    if not path.is_absolute():
        path = script_dir / path
    return path.resolve()


def unquote(value: str) -> str:
    value = value.strip()
    if len(value) >= 2 and value[0] == '"' and value[-1] == '"':
        return value[1:-1]
    return value


def parse_cam_job(path: Path) -> list[CamSection]:
    section_order: list[str] = []
    section_values: dict[str, dict[str, str]] = {}
    discovered_sections: list[str] = []
    current_section: str | None = None

    for line_number, raw_line in enumerate(
        path.read_text(encoding="utf-8-sig", errors="replace").splitlines(),
        start=1,
    ):
        line = raw_line.strip()
        if not line or line.startswith((";", "#")):
            continue

        if line.startswith("[") and line.endswith("]"):
            current_section = line[1:-1].strip()
            section_values.setdefault(current_section, {})
            if current_section.casefold() != "cam processor job":
                discovered_sections.append(current_section)
            continue

        if current_section is None or "=" not in line:
            raise ValueError(f"Invalid CAM syntax at {path}:{line_number}: {raw_line}")

        key, value = line.split("=", 1)
        key = key.strip().casefold()
        value = unquote(value)
        if current_section.casefold() == "cam processor job" and key == "section":
            section_order.append(value)
        else:
            section_values[current_section][key] = value

    if not section_order:
        section_order = discovered_sections
    if not section_order:
        raise ValueError(f"CAM job contains no output sections: {path}")

    folded_names = {name.casefold(): name for name in section_values}
    sections: list[CamSection] = []
    seen_ids: set[str] = set()

    for requested_id in section_order:
        folded_id = requested_id.casefold()
        if folded_id in seen_ids:
            raise ValueError(f"CAM job lists section {requested_id!r} more than once")
        seen_ids.add(folded_id)

        actual_id = folded_names.get(folded_id)
        if actual_id is None:
            raise ValueError(f"CAM job references missing section {requested_id!r}")
        values = section_values[actual_id]

        missing = [key for key in ("device", "output", "layers") if not values.get(key)]
        if missing:
            raise ValueError(
                f"CAM section {actual_id!r} is missing: {', '.join(missing)}"
            )

        layers = tuple(values["layers"].split())
        if not layers:
            raise ValueError(f"CAM section {actual_id!r} has no selected layers")

        name = values.get("name[en]", values.get("name", actual_id))
        sections.append(
            CamSection(
                section_id=actual_id,
                name=name,
                device=values["device"],
                output_template=values["output"],
                layers=layers,
                values=values,
            )
        )

    return sections


def decimal_text(value: Decimal) -> str:
    text = format(value, "f")
    if "." in text:
        text = text.rstrip("0").rstrip(".")
    return text or "0"


def parse_decimal(value: str, description: str) -> Decimal:
    try:
        return Decimal(value)
    except InvalidOperation as exc:
        raise ValueError(f"Invalid {description}: {value!r}") from exc


def parse_length(value: str, target_unit: str) -> str:
    match = LENGTH_PATTERN.fullmatch(value.strip())
    if match is None:
        raise ValueError(f"Invalid CAM length: {value!r}")

    amount = parse_decimal(match.group(1), "length")
    source_unit = (match.group(2) or target_unit).casefold()
    if source_unit == "mil":
        inches = amount / Decimal(1000)
    elif source_unit in {"in", "inch"}:
        inches = amount
    elif source_unit == "mm":
        inches = amount / Decimal("25.4")
    else:
        raise ValueError(f"Unsupported CAM length unit: {source_unit!r}")

    if target_unit == "inch":
        return decimal_text(inches)
    if target_unit == "mm":
        return decimal_text(inches * Decimal("25.4"))
    raise ValueError(f"Unsupported target unit: {target_unit!r}")


def bool_value(value: str, description: str) -> bool:
    if value == "0":
        return False
    if value == "1":
        return True
    raise ValueError(f"{description} must be 0 or 1, got {value!r}")


def bool_option(option: str, enabled: bool) -> str:
    return f"-{option}{'+' if enabled else '-'}"


def expand_output_template(template: str, board_stem: str) -> Path:
    percent_literal = "\0PERCENT\0"
    expanded = template.replace("%%", percent_literal).replace("%N", board_stem)
    expanded = expanded.replace(percent_literal, "%")
    if expanded.startswith("."):
        expanded = f"{board_stem}{expanded}"
    if "%" in expanded:
        raise ValueError(f"Unsupported CAM output placeholder in {template!r}")

    relative_path = Path(expanded.replace("\\", os.sep))
    if relative_path.is_absolute() or ".." in relative_path.parts:
        raise ValueError(f"CAM output must remain inside the output directory: {template!r}")
    if not relative_path.name:
        raise ValueError(f"Invalid CAM output path: {template!r}")
    return relative_path


def resolve_auxiliary_file(
    value: str,
    board: Path,
    cam_job: Path,
    build_dir: Path,
) -> Path:
    if value.startswith("."):
        return build_dir / f"{board.stem}{value}"

    requested = Path(value)
    if requested.is_absolute():
        return requested

    candidates = (
        build_dir / requested,
        board.parent / requested,
        cam_job.parent / requested,
    )
    return next((candidate for candidate in candidates if candidate.is_file()), candidates[0])


def add_flags(command: list[str], section: CamSection) -> None:
    raw_flags = section.values.get("flags")
    if raw_flags:
        flag_values = raw_flags.split()
        if len(flag_values) != len(FLAG_OPTIONS):
            raise ValueError(
                f"{section.section_id}: Flags must contain seven values, got {raw_flags!r}"
            )
        for option, value in zip(FLAG_OPTIONS, flag_values):
            command.append(bool_option(option, bool_value(value, f"-{option}")))

    raw_emulate = section.values.get("emulate")
    if raw_emulate:
        emulate_values = raw_emulate.split()
        if len(emulate_values) == 1:
            command.append(
                bool_option("e", bool_value(emulate_values[0], "aperture emulation"))
            )
        elif len(emulate_values) == 3:
            for option, description, value in zip(
                ("e", "a", "t"),
                ("aperture emulation", "annulus emulation", "thermal emulation"),
                emulate_values,
            ):
                command.append(bool_option(option, bool_value(value, description)))
        else:
            raise ValueError(
                f"{section.section_id}: Emulate must contain one or three values"
            )


def add_numeric_options(command: list[str], section: CamSection) -> None:
    values = section.values

    if values.get("scale"):
        command.append(f"-s{decimal_text(parse_decimal(values['scale'], 'scale'))}")

    if values.get("offset"):
        offset = values["offset"].split()
        if len(offset) != 2:
            raise ValueError(f"{section.section_id}: Offset must contain two values")
        command.extend((f"-x{parse_length(offset[0], 'inch')}", f"-y{parse_length(offset[1], 'inch')}"))

    if values.get("page"):
        page = values["page"].split()
        if len(page) != 2:
            raise ValueError(f"{section.section_id}: Page must contain height and width")
        command.extend((f"-h{parse_length(page[0], 'inch')}", f"-w{parse_length(page[1], 'inch')}"))

    if values.get("pen"):
        pen = values["pen"].split()
        if len(pen) != 2:
            raise ValueError(f"{section.section_id}: Pen must contain diameter and velocity")
        velocity = decimal_text(parse_decimal(pen[1], "pen velocity"))
        command.extend((f"-p{parse_length(pen[0], 'mm')}", f"-v{velocity}"))

    if values.get("tolerance"):
        tolerances = values["tolerance"].split()
        if len(tolerances) != 6:
            raise ValueError(f"{section.section_id}: Tolerance must contain six values")
        tolerance_options = (
            ("D", "-"),
            ("D", "+"),
            ("F", "-"),
            ("F", "+"),
            ("E", "-"),
            ("E", "+"),
        )
        for (option, sign), raw_value in zip(tolerance_options, tolerances):
            amount = abs(parse_decimal(raw_value, f"-{option} tolerance"))
            if amount:
                command.append(f"-{option}{sign}{decimal_text(amount)}")


def build_eagle_command(
    eaglecon: Path,
    board: Path,
    cam_job: Path,
    build_dir: Path,
    section: CamSection,
    output_path: Path,
    variant: str | None,
) -> list[str]:
    command = [
        str(eaglecon),
        "-X",
        "-N+",
        f"-d{section.device}",
        f"-o{output_path}",
    ]
    if variant:
        command.append(f"-A{variant}")

    add_flags(command, section)
    add_numeric_options(command, section)

    wheel = section.values.get("wheel", "")
    if wheel:
        wheel_path = resolve_auxiliary_file(wheel, board, cam_job, build_dir)
        if wheel_path.is_file():
            command.append(f"-W{wheel_path}")
        elif not section.device.upper().startswith("GERBER_RS274X"):
            raise FileNotFoundError(
                f"{section.section_id}: aperture wheel not found: {wheel_path}"
            )

    rack = section.values.get("rack", "")
    if rack:
        rack_path = resolve_auxiliary_file(rack, board, cam_job, build_dir)
        if not rack_path.is_file():
            raise FileNotFoundError(
                f"{section.section_id}: drill rack not found: {rack_path}"
            )
        command.append(f"-R{rack_path}")

    command.extend((str(board), *section.layers))
    return command


def process_output(result: subprocess.CompletedProcess[str]) -> str:
    return "\n".join(
        part.strip()
        for part in (result.stdout, result.stderr)
        if part and part.strip()
    )


def run_section(
    command: list[str],
    section: CamSection,
    output_path: Path,
    working_dir: Path,
    verbose: bool,
) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.unlink(missing_ok=True)

    result = subprocess.run(
        command,
        cwd=working_dir,
        capture_output=True,
        text=True,
        errors="replace",
        check=False,
    )
    diagnostics = process_output(result)
    if verbose and diagnostics:
        print(diagnostics)

    if result.returncode != 0:
        raise RuntimeError(
            f"eaglecon failed for {section.name} (exit code {result.returncode})\n"
            f"Command: {subprocess.list2cmdline(command)}\n{diagnostics}"
        )
    if not output_path.is_file() or output_path.stat().st_size == 0:
        raise RuntimeError(
            f"eaglecon reported success but did not create {output_path.name}\n"
            f"Command: {subprocess.list2cmdline(command)}\n{diagnostics}"
        )


def remove_reports(directory: Path) -> list[Path]:
    removed: list[Path] = []
    if not directory.exists():
        return removed

    for path in directory.rglob("*"):
        if path.is_file() and path.suffix.casefold() in REPORT_SUFFIXES:
            path.unlink()
            removed.append(path)
    return removed


def run_preview(
    preview_script: Path,
    build_dir: Path,
    gerbv: Path | None,
) -> None:
    command = [
        sys.executable,
        "-B",
        str(preview_script),
        "--input-dir",
        str(build_dir),
        "--output",
        str(build_dir / "preview.svg"),
    ]
    if gerbv is not None:
        command.extend(("--gerbv", str(gerbv)))

    result = subprocess.run(
        command,
        cwd=preview_script.parent,
        capture_output=True,
        text=True,
        errors="replace",
        check=False,
    )
    diagnostics = process_output(result)
    if diagnostics:
        print(diagnostics)
    missing_outputs = [
        path.name
        for path in (
            build_dir / "preview.svg",
            build_dir / "preview.svgz",
            build_dir / "preview-top.png",
            build_dir / "preview-bottom.png",
            build_dir / "preview-bottom-mirrored.png",
        )
        if not path.is_file()
    ]
    if result.returncode != 0 or missing_outputs:
        missing_message = (
            f"\nMissing preview outputs: {', '.join(missing_outputs)}"
            if missing_outputs
            else ""
        )
        raise RuntimeError(
            f"SVG preview generation failed (exit code {result.returncode})\n"
            f"Command: {subprocess.list2cmdline(command)}"
            f"{missing_message}"
        )


def publish_build(build_dir: Path, output_dir: Path) -> None:
    backup_dir = output_dir.with_name(
        f".{output_dir.name}-backup-{uuid.uuid4().hex}"
    )
    had_previous_output = output_dir.exists()

    if had_previous_output:
        if output_dir.is_symlink() or not output_dir.is_dir():
            raise RuntimeError(f"Output path is not a normal directory: {output_dir}")
        os.replace(output_dir, backup_dir)

    try:
        os.replace(build_dir, output_dir)
    except Exception:
        if had_previous_output and backup_dir.exists() and not output_dir.exists():
            os.replace(backup_dir, output_dir)
        raise

    if backup_dir.exists():
        try:
            shutil.rmtree(backup_dir)
        except OSError as exc:
            print(f"Warning: could not remove old output at {backup_dir}: {exc}")


def create_gerber_archive(
    output_dir: Path,
    relative_outputs: list[Path],
    archive_dir: Path,
    board_stem: str,
) -> Path:
    archive_dir.mkdir(parents=True, exist_ok=True)
    timestamp = datetime.now().strftime("%Y%m%d%H%M")
    archive_path = archive_dir / f"gerbers-{board_stem}-{timestamp}.zip"
    temporary_path = archive_dir / f".{archive_path.name}.{uuid.uuid4().hex}.tmp"

    try:
        with zipfile.ZipFile(
            temporary_path,
            mode="w",
            compression=zipfile.ZIP_DEFLATED,
            compresslevel=9,
        ) as archive:
            for relative_output in relative_outputs:
                source = output_dir / relative_output
                if not source.is_file():
                    raise FileNotFoundError(
                        f"Cannot add missing Gerber output to ZIP: {source}"
                    )
                archive.write(source, arcname=relative_output.as_posix())

        os.replace(temporary_path, archive_path)
        return archive_path
    finally:
        temporary_path.unlink(missing_ok=True)


def validate_unique_outputs(
    sections: list[CamSection],
    board_stem: str,
) -> list[Path]:
    relative_outputs: list[Path] = []
    seen: dict[str, str] = {}

    for section in sections:
        relative_output = expand_output_template(section.output_template, board_stem)
        key = str(relative_output).casefold()
        if key in seen:
            raise ValueError(
                f"CAM sections {seen[key]!r} and {section.section_id!r} "
                f"both output {relative_output}"
            )
        seen[key] = section.section_id
        relative_outputs.append(relative_output)

    return relative_outputs


def main() -> int:
    args = parse_args()
    script_dir = Path(__file__).resolve().parent
    eaglecon = resolve_script_relative(args.eaglecon, script_dir)
    board = resolve_script_relative(args.board, script_dir)
    cam_job = resolve_script_relative(args.cam_job, script_dir)
    output_dir = resolve_script_relative(args.output_dir, script_dir)
    archive_dir = resolve_script_relative(args.archive_dir, script_dir)
    preview_script = resolve_script_relative(args.preview_script, script_dir)
    gerbv = (
        resolve_script_relative(args.gerbv, script_dir)
        if args.gerbv is not None
        else None
    )

    for description, path in (
        ("eaglecon executable", eaglecon),
        ("board file", board),
        ("CAM job", cam_job),
    ):
        if not path.is_file():
            raise FileNotFoundError(f"{description} not found: {path}")
    if not args.no_preview and not preview_script.is_file():
        raise FileNotFoundError(f"preview generator not found: {preview_script}")
    if gerbv is not None and not gerbv.is_file():
        raise FileNotFoundError(f"gerbv executable not found: {gerbv}")
    if output_dir.exists() and not output_dir.is_dir():
        raise RuntimeError(f"Output path is not a directory: {output_dir}")

    sections = parse_cam_job(cam_job)
    relative_outputs = validate_unique_outputs(sections, board.stem)
    output_dir.parent.mkdir(parents=True, exist_ok=True)
    build_dir = Path(
        tempfile.mkdtemp(prefix=f".{output_dir.name}-build-", dir=output_dir.parent)
    ).resolve()

    try:
        print(f"Generating {len(sections)} CAM outputs from {board.name}...")
        for index, (section, relative_output) in enumerate(
            zip(sections, relative_outputs),
            start=1,
        ):
            output_path = (build_dir / relative_output).resolve()
            try:
                output_path.relative_to(build_dir)
            except ValueError as exc:
                raise RuntimeError(
                    f"CAM output escapes the build directory: {relative_output}"
                ) from exc

            print(
                f"[{index}/{len(sections)}] {section.name} -> {relative_output}"
            )
            command = build_eagle_command(
                eaglecon,
                board,
                cam_job,
                build_dir,
                section,
                output_path,
                args.variant,
            )
            run_section(command, section, output_path, board.parent, args.verbose)

        removed_reports = remove_reports(build_dir)
        if removed_reports:
            print(f"Removed {len(removed_reports)} .dri/.gpi report files.")

        missing_outputs = [
            relative_output
            for relative_output in relative_outputs
            if not (build_dir / relative_output).is_file()
        ]
        if missing_outputs:
            raise RuntimeError(
                "Missing generated outputs: "
                + ", ".join(str(path) for path in missing_outputs)
            )

        if not args.no_preview:
            print("Generating SVG preview...")
            run_preview(preview_script, build_dir, gerbv)

        # The whole directory is replaced only after CAM and preview generation pass.
        publish_build(build_dir, output_dir)
        print(f"Gerber package ready: {output_dir}")

        archive_path = create_gerber_archive(
            output_dir,
            relative_outputs,
            archive_dir,
            board.stem,
        )
        print(f"Gerber ZIP ready: {archive_path}")
        return 0
    finally:
        remove_reports(build_dir)
        if build_dir.exists():
            shutil.rmtree(build_dir)


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
