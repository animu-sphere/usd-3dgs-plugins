#!/usr/bin/env python3
"""Check or apply the repository's clang-format policy."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import subprocess
import sys


SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}
IGNORED_PARTS = {".strata", "build", "dist", "third_party"}


def git_files(*args: str) -> list[Path]:
    result = subprocess.run(
        ["git", *args], check=True, capture_output=True, text=False
    )
    return [Path(name) for name in result.stdout.decode().split("\0") if name]


def is_source(path: Path) -> bool:
    return path.suffix.lower() in SOURCE_SUFFIXES and not any(
        part in IGNORED_PARTS for part in path.parts
    )


def changed_ranges() -> dict[Path, list[tuple[int, int]]]:
    base_ref = os.environ.get("GITHUB_BASE_REF")
    revision = f"origin/{base_ref}...HEAD" if base_ref else "HEAD"
    result = subprocess.run(
        [
            "git",
            "diff",
            "--diff-filter=ACMR",
            "--no-color",
            "--unified=0",
            revision,
        ],
        check=True,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    ranges: dict[Path, list[tuple[int, int]]] = {}
    current: Path | None = None
    for line in result.stdout.splitlines():
        if line.startswith("+++ b/"):
            current = Path(line[6:])
            ranges.setdefault(current, [])
            continue
        if current is None or not line.startswith("@@"):
            continue
        match = re.search(r"\+(\d+)(?:,(\d+))?", line)
        if not match:
            continue
        start = int(match.group(1))
        count = int(match.group(2) or "1")
        if count:
            ranges[current].append((start, start + count - 1))
    return ranges


def format_file(
    clang_format: str,
    path: Path,
    write: bool,
    ranges: list[tuple[int, int]] | None = None,
) -> bool:
    command = [clang_format, "--style=file"]
    if ranges:
        for start, end in ranges:
            command.extend(["--lines", f"{start}:{end}"])
    if write:
        command.append("--i")
    else:
        command.extend(["--dry-run", "--Werror"])
    command.append(str(path))
    try:
        result = subprocess.run(command)
    except FileNotFoundError:
        print(
            f"clang-format executable not found: {clang_format}. "
            "Install LLVM's clang-format and retry.",
            file=sys.stderr,
        )
        return False
    return result.returncode == 0


def main() -> int:
    parser = argparse.ArgumentParser()
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument(
        "--all", action="store_true", help="check every tracked C/C++ source"
    )
    selection.add_argument(
        "--changed", action="store_true", help="check only changed C/C++ sources"
    )
    parser.add_argument(
        "--write", action="store_true", help="rewrite selected files in place"
    )
    parser.add_argument(
        "--clang-format", default="clang-format", help="clang-format executable"
    )
    args = parser.parse_args()

    ranges = changed_ranges() if args.changed else {}
    paths = list(ranges) if args.changed else git_files("ls-files", "-z")
    paths = sorted(
        path
        for path in paths
        if is_source(path) and (not args.changed or ranges.get(path))
    )
    if not paths:
        print("clang-format: no matching C/C++ files")
        return 0

    failed = [
        path
        for path in paths
        if not format_file(
            args.clang_format, path, args.write, ranges.get(path)
        )
    ]
    if failed:
        print("clang-format: files need formatting:")
        for path in failed:
            print(f"  {path}")
        return 1
    action = "formatted" if args.write else "checked"
    print(f"clang-format: {action} {len(paths)} file(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
