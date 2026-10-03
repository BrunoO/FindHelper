#!/usr/bin/env python3
"""Count C++ header/source lines in this project, excluding external dirs.

Counts only:
- .cpp files
- .h files

Exclusions:
- Directories: .git, .idea, .vscode, build, dist, cmake-build-*, external,
  third_party, vendor, node_modules

Usage:
  python count_lines.py

Runs from the repo root by default (directory containing this script).
"""

from __future__ import annotations

import os
import sys
from pathlib import Path
from typing import Dict, List, Tuple

# Directories that are considered "external" or not part of the main source
EXCLUDED_DIR_NAMES = {
    ".git",
    ".idea",
    ".vscode",
    "build",
    "dist",
    "external",
    "third_party",
    "vendor",
    "node_modules",
    "windirstat"
}

EXCLUDED_DIR_PREFIXES = ("cmake-build-", "build")

# File extensions to include (lowercased, including dot)
INCLUDED_FILE_EXTENSIONS = {".cpp", ".h"}


def is_excluded_dir(dirname: str) -> bool:
    """Return True if a directory name should be skipped entirely."""
    if dirname in EXCLUDED_DIR_NAMES:
        return True
    return any(dirname.startswith(prefix) for prefix in EXCLUDED_DIR_PREFIXES)


def should_count_file(path: Path) -> bool:
    """Return True if the file should be included in the line count."""
    if path.name.startswith("EmbeddedFont_"):
        return False
    return path.suffix.lower() in INCLUDED_FILE_EXTENSIONS


def count_lines_in_file(path: Path) -> int:
    """Count lines in a single file, best-effort for text files.

    Files that cannot be decoded as UTF-8 (or have other I/O errors) are
    skipped and treated as having zero lines.
    """
    try:
        with path.open("r", encoding="utf-8", errors="ignore") as f:
            return sum(1 for _ in f)
    except OSError:
        return 0


def count_lines(root: Path) -> None:
    total_lines = 0
    total_files = 0
    lines_by_extension: Dict[str, int] = {}
    per_file: List[Tuple[Path, int]] = []

    for dirpath, dirnames, filenames in os.walk(root):
        # Prune excluded directories in-place so os.walk does not descend
        dirnames[:] = [d for d in dirnames if not is_excluded_dir(d)]

        for filename in filenames:
            file_path = Path(dirpath) / filename
            if not should_count_file(file_path):
                continue

            line_count = count_lines_in_file(file_path)
            if line_count == 0:
                continue

            total_lines += line_count
            total_files += 1
            per_file.append((file_path.relative_to(root), line_count))

            ext = file_path.suffix.lower() or "<no-ext>"
            lines_by_extension[ext] = lines_by_extension.get(ext, 0) + line_count

    print(f"Counted {total_lines} lines across {total_files} .cpp/.h files (excluding external dirs).")

    if lines_by_extension:
        print("\nBreakdown by extension:")
        for ext, lines in sorted(lines_by_extension.items(), key=lambda kv: (-kv[1], kv[0])):
            print(f"  {ext:10s} : {lines:8d} lines")

    if per_file:
        print("\nPer-file breakdown (sorted by line count desc):")
        for path, lines in sorted(per_file, key=lambda kv: (-kv[1], str(kv[0]))):
            print(f"  {lines:8d} lines  {path}")


if __name__ == "__main__":
    repo_root = Path(__file__).resolve().parent
    if len(sys.argv) > 1:
        repo_root = Path(sys.argv[1]).resolve()
    count_lines(repo_root)
