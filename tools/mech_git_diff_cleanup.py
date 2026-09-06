#!/usr/bin/env python3
"""Restore mechanical exports whose Git diff contains only generated metadata."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path


MECHANICAL_DIRECTORY = "mechanical"

_STEP_FILE_NAME_TIMESTAMP = re.compile(
    rb"(FILE_NAME\s*\(\s*'(?:[^']|'')*'\s*,\s*)"
    rb"'\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:Z|[+-]\d{2}:\d{2})?'",
    re.IGNORECASE,
)

_DXF_DATE_VARIABLES = {
    b"$TDCREATE",
    b"$TDUCREATE",
    b"$TDUPDATE",
    b"$TDUUPDATE",
}
_DXF_GUID_VARIABLES = {
    b"$FINGERPRINTGUID",
    b"$VERSIONGUID",
}
_DXF_JULIAN_DATE = re.compile(rb"[+-]?(?:\d+(?:\.\d*)?|\.\d+)")
_DXF_GUID = re.compile(
    rb"\{?[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-"
    rb"[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}\}?"
)
_DXF_EZDXF_TIMESTAMP = re.compile(
    rb"\d+(?:\.\d+)+\s+@\s+"
    rb"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:Z|[+-]\d{2}:\d{2})?"
)


def _git(repo_root: Path, *arguments: str) -> subprocess.CompletedProcess[bytes]:
    return subprocess.run(
        ["git", "-C", str(repo_root), *arguments],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


def _find_repo_root() -> Path:
    result = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    return Path(result.stdout.strip()).resolve()


def _normalize_line_endings(data: bytes) -> bytes:
    return data.replace(b"\r\n", b"\n").replace(b"\r", b"\n")


def _normalize_step(data: bytes) -> bytes:
    normalized = _normalize_line_endings(data)
    return _STEP_FILE_NAME_TIMESTAMP.sub(rb"\1'<STEP-FILE-NAME-TIMESTAMP>'", normalized)


def _normalize_dxf(data: bytes) -> bytes:
    normalized = _normalize_line_endings(data)
    has_final_newline = normalized.endswith(b"\n")
    lines = normalized.splitlines()

    # A text DXF is a sequence of two-line group-code/value records. Refuse to
    # normalize malformed input so it can never be mistaken for metadata-only.
    if len(lines) % 2 != 0:
        return normalized

    pending_header_variable: bytes | None = None
    in_dictionary_variable = False

    for index in range(0, len(lines), 2):
        group_code = lines[index].strip()
        value = lines[index + 1].strip()

        if group_code == b"0":
            in_dictionary_variable = value.upper() == b"DICTIONARYVAR"

        if pending_header_variable is not None:
            if (
                pending_header_variable in _DXF_DATE_VARIABLES
                and group_code == b"40"
                and _DXF_JULIAN_DATE.fullmatch(value)
            ):
                lines[index + 1] = b"<DXF-HEADER-DATE>"
            elif (
                pending_header_variable in _DXF_GUID_VARIABLES
                and group_code == b"2"
                and _DXF_GUID.fullmatch(value)
            ):
                lines[index + 1] = b"<DXF-HEADER-GUID>"
            pending_header_variable = None

        if group_code == b"9":
            pending_header_variable = value.upper()
        elif (
            in_dictionary_variable
            and group_code == b"1"
            and _DXF_EZDXF_TIMESTAMP.fullmatch(value)
        ):
            lines[index + 1] = b"<EZDXF-WRITER-TIMESTAMP>"

    result = b"\n".join(lines)
    if has_final_newline:
        result += b"\n"
    return result


def _changed_mechanical_files(repo_root: Path) -> list[str]:
    # Compare the worktree to the index, just like a plain `git diff`. This
    # deliberately preserves staged content and ignores untracked files.
    result = _git(
        repo_root,
        "diff",
        "--name-only",
        "--diff-filter=M",
        "-z",
        "--",
        MECHANICAL_DIRECTORY,
    )
    return [
        raw_path.decode("utf-8", errors="surrogateescape")
        for raw_path in result.stdout.split(b"\0")
        if raw_path
    ]


def _index_contents(repo_root: Path, git_path: str) -> bytes:
    return _git(repo_root, "show", f":{git_path}").stdout


def _restore_from_index(repo_root: Path, git_path: str) -> None:
    _git(repo_root, "restore", "--worktree", "--", git_path)


def cleanup(repo_root: Path, dry_run: bool) -> int:
    restored_count = 0
    retained_count = 0
    skipped_stl_count = 0

    for git_path in _changed_mechanical_files(repo_root):
        extension = Path(git_path).suffix.lower()

        if extension == ".stl":
            print(f"SKIP STL: {git_path}")
            skipped_stl_count += 1
            continue

        if extension == ".step" or extension == ".stp":
            normalizer = _normalize_step
            format_name = "STEP"
        elif extension == ".dxf":
            normalizer = _normalize_dxf
            format_name = "DXF"
        else:
            continue

        worktree_path = repo_root / Path(git_path)
        if not worktree_path.is_file():
            print(f"KEEP {format_name}: {git_path} (not a regular file)")
            retained_count += 1
            continue

        index_data = _index_contents(repo_root, git_path)
        worktree_data = worktree_path.read_bytes()
        if normalizer(index_data) != normalizer(worktree_data):
            print(f"KEEP {format_name}: {git_path} (content changed)")
            retained_count += 1
            continue

        action = "WOULD RESTORE" if dry_run else "RESTORE"
        print(f"{action} {format_name}: {git_path} (metadata only)")
        if not dry_run:
            _restore_from_index(repo_root, git_path)
        restored_count += 1

    verb = "would restore" if dry_run else "restored"
    print(
        f"Mechanical cleanup: {verb} {restored_count}, "
        f"kept {retained_count}, skipped {skipped_stl_count} STL file(s)."
    )
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Restore metadata-only STEP and DXF worktree changes under mechanical/."
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="report files that would be restored without changing them",
    )
    arguments = parser.parse_args()

    try:
        return cleanup(_find_repo_root(), arguments.dry_run)
    except (OSError, subprocess.CalledProcessError) as error:
        print(f"ERROR: mechanical Git diff cleanup failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
