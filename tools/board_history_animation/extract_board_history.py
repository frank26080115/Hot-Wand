#!/usr/bin/env python3
"""Extract every committed revision of an EAGLE board into numbered files."""

from __future__ import annotations

import argparse
import os
import subprocess
import tempfile
from pathlib import Path


PROJECTS = ("hot-wand", "hot-wand-lite")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Write every committed revision of electrical/<project>.brd to "
            "history-<project>/1.brd, 2.brd, and so on. Existing snapshots "
            "are left untouched so later runs only append new history."
        )
    )
    parser.add_argument(
        "project",
        nargs="?",
        choices=PROJECTS,
        default="hot-wand",
        help="board project to extract (default: hot-wand)",
    )
    return parser.parse_args()


def run_git(repo_root: Path, *arguments: str, text: bool = True) -> str | bytes:
    command = ["git", "-C", str(repo_root), *arguments]
    result = subprocess.run(
        command,
        capture_output=True,
        text=text,
        check=False,
    )
    if result.returncode != 0:
        stderr = result.stderr.strip() if text else result.stderr.decode(errors="replace").strip()
        raise RuntimeError(
            f"Git command failed ({result.returncode}): "
            f"{subprocess.list2cmdline(command)}\n{stderr}"
        )
    return result.stdout


def find_repo_root(script_dir: Path) -> Path:
    candidate = script_dir.parents[1]
    output = run_git(candidate, "rev-parse", "--show-toplevel")
    return Path(str(output).strip()).resolve()


def historical_commits(repo_root: Path, board_path: str) -> list[str]:
    output = run_git(
        repo_root,
        "log",
        "--reverse",
        "--format=%H",
        "HEAD",
        "--",
        board_path,
    )
    return [line for line in str(output).splitlines() if line]


def atomic_write(path: Path, contents: bytes) -> None:
    temporary_path: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w+b",
            dir=path.parent,
            prefix=f".{path.name}-",
            suffix=".tmp",
            delete=False,
        ) as temporary_file:
            temporary_path = Path(temporary_file.name)
            temporary_file.write(contents)
            temporary_file.flush()
            os.fsync(temporary_file.fileno())
        os.replace(temporary_path, path)
        temporary_path = None
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)


def main() -> int:
    args = parse_args()
    script_dir = Path(__file__).resolve().parent
    repo_root = find_repo_root(script_dir)
    board_path = f"electrical/{args.project}.brd"
    output_dir = script_dir / f"history-{args.project}"
    output_dir.mkdir(parents=True, exist_ok=True)

    commits = historical_commits(repo_root, board_path)
    if not commits:
        raise RuntimeError(f"No committed history found for {board_path}")

    created = 0
    skipped = 0
    for index, commit in enumerate(commits, start=1):
        output_path = output_dir / f"{index}.brd"
        if output_path.exists():
            if not output_path.is_file():
                raise RuntimeError(f"Snapshot path is not a file: {output_path}")
            skipped += 1
            continue

        contents = run_git(
            repo_root,
            "show",
            f"{commit}:{board_path}",
            text=False,
        )
        if not contents:
            raise RuntimeError(f"Git returned an empty board for {commit}:{board_path}")
        atomic_write(output_path, contents)
        created += 1
        print(f"Created {output_path.name} from {commit[:12]}")

    print(
        f"History for {args.project}: {len(commits)} revisions, "
        f"{created} created, {skipped} already present in {output_dir}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
