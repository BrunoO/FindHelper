#!/usr/bin/env python3
"""Detect hidden or suspicious characters in project files (potential attack vectors).

Scans text and source files for:
- Zero-width and format characters (invisible, can hide malicious code)
- Bidirectional override/isolate characters (Trojan source / display attacks)
- Null bytes and other control characters
- BOM (U+FEFF) in unexpected positions

Such characters can be used for:
- Trojan source attacks (right-to-left override changing perceived code)
- Invisible identifiers or strings
- Path or command injection via null bytes

Usage:
  python scripts/detect_hidden_characters.py [--root <path>] [--extensions .py,.cpp,...] [--verbose]

Runs from the repo root by default. Exit code 1 if any finding; 0 otherwise.
"""

from __future__ import annotations

import argparse
import os
import sys
import unicodedata
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple

# -----------------------------------------------------------------------------
# Directories to exclude from scanning (same style as other project scripts)
# -----------------------------------------------------------------------------
EXCLUDED_DIR_NAMES: Set[str] = {
    ".git",
    ".idea",
    ".vscode",
    "build",
    "dist",
    "external",
    "third_party",
    "vendor",
    "node_modules",
    "windirstat",
    "coverage",
    "build_tests",
    "build_coverage",
    "build_test_linux",
    "build_test_linux_check",
    "build_test_linux_check2",
}

EXCLUDED_DIR_PREFIXES: Tuple[str, ...] = ("cmake-build-",)

# Default: scan common text/source/config extensions
DEFAULT_EXTENSIONS: Set[str] = {
    ".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".hxx",
    ".py", ".sh", ".ps1", ".cmake", ".txt", ".md", ".yml", ".yaml",
    ".json", ".xml", ".html", ".css", ".js", ".ts", ".bat", ".cmd",
}

# -----------------------------------------------------------------------------
# Suspicious character definitions (codepoint -> short description)
# See: https://www.unicode.org/reports/tr36/ (Unicode Security), CVE-2021-42574
# -----------------------------------------------------------------------------

# Zero-width and invisible format characters
ZERO_WIDTH_AND_FORMAT: Dict[int, str] = {
    0x200B: "ZERO WIDTH SPACE",
    0x200C: "ZERO WIDTH NON-JOINER",
    0x200D: "ZERO WIDTH JOINER",
    0x200E: "LEFT-TO-RIGHT MARK",
    0x200F: "RIGHT-TO-LEFT MARK",
    0x202A: "LEFT-TO-RIGHT EMBEDDING",
    0x202B: "RIGHT-TO-LEFT EMBEDDING",
    0x202C: "POP DIRECTIONAL FORMATTING",
    0x202D: "LEFT-TO-RIGHT OVERRIDE",
    0x202E: "RIGHT-TO-LEFT OVERRIDE",  # Classic Trojan source
    0x2060: "WORD JOINER (invisible)",
    0x2066: "LEFT-TO-RIGHT ISOLATE",
    0x2067: "RIGHT-TO-LEFT ISOLATE",
    0x2068: "FIRST STRONG ISOLATE",
    0x2069: "POP DIRECTIONAL ISOLATE",
    0xFEFF: "ZERO WIDTH NO-BREAK SPACE (BOM)",
}

# C0 and C1 control characters (except common benign: tab 0x09, LF 0x0A, CR 0x0D)
CONTROL_SUSPICIOUS: Set[int] = set(range(0x00, 0x20)) | set(range(0x80, 0xA0))
CONTROL_SUSPICIOUS -= {0x09, 0x0A, 0x0D}  # tab, LF, CR allowed

# Null byte explicitly (path/argument injection)
NULL_CODEPOINT: int = 0x00


def is_excluded_dir(dirname: str) -> bool:
    """Return True if directory should be skipped."""
    if dirname in EXCLUDED_DIR_NAMES:
        return True
    return any(dirname.startswith(p) for p in EXCLUDED_DIR_PREFIXES)


def should_scan_file(path: Path, extensions: Set[str]) -> bool:
    """Return True if file extension is in the allowed set."""
    return path.suffix.lower() in extensions


def is_likely_binary(content: bytes) -> bool:
    """Heuristic: treat as binary if too many nulls or high bytes."""
    if not content:
        return True
    null_ratio = content.count(0x00) / len(content)
    if null_ratio > 0.01:
        return True
    # High proportion of non-printable non-whitespace
    try:
        text = content.decode("utf-8", errors="strict")
    except UnicodeDecodeError:
        return True
    non_print = sum(1 for c in text if ord(c) < 0x20 and c not in "\t\n\r")
    if len(text) > 0 and non_print / len(text) > 0.1:
        return True
    return False


def get_suspicious_category(codepoint: int) -> Optional[str]:
    """Return category string for reporting, or None if not suspicious."""
    if codepoint in ZERO_WIDTH_AND_FORMAT:
        return ZERO_WIDTH_AND_FORMAT[codepoint]
    if codepoint in CONTROL_SUSPICIOUS:
        if codepoint == NULL_CODEPOINT:
            return "NULL BYTE (U+0000)"
        try:
            name = unicodedata.name(chr(codepoint))
        except ValueError:
            name = "CONTROL"
        return f"CONTROL: {name}"
    return None


def scan_content(content: str) -> List[Tuple[int, int, int, str]]:
    """Scan decoded text for suspicious characters.

    Returns list of (line_1based, col_1based, codepoint, category)."""
    findings: List[Tuple[int, int, int, str]] = []
    lines = content.splitlines(keepends=True)
    for line_no, line in enumerate(lines, start=1):
        for col_zero, char in enumerate(line):
            cp = ord(char)
            # BOM (U+FEFF) at start of file is conventional; only flag elsewhere
            if cp == 0xFEFF and line_no == 1 and col_zero == 0:
                continue
            category = get_suspicious_category(cp)
            if category:
                # Column 1-based; col_zero is 0-based
                findings.append((line_no, col_zero + 1, cp, category))
    return findings


def snippet_context(content: str, line_no: int, col: int, width: int = 40) -> str:
    """Return a one-line snippet around (line_no, col), escaping non-printables."""
    lines = content.splitlines()
    if line_no < 1 or line_no > len(lines):
        return ""
    line = lines[line_no - 1]
    start = max(0, col - 1 - width // 2)
    end = min(len(line), start + width)
    raw = line[start:end]
    # Replace tabs/newlines for display; show suspicious as <U+XXXX>
    out: List[str] = []
    for c in raw:
        if c in "\t":
            out.append("\\t")
        elif c == "\n":
            out.append("\\n")
        elif ord(c) in ZERO_WIDTH_AND_FORMAT or ord(c) in CONTROL_SUSPICIOUS:
            out.append(f"<U+{ord(c):04X}>")
        elif c.isprintable() or c in " \n\r":
            out.append(c)
        else:
            out.append(f"<U+{ord(c):04X}>")
    return "".join(out)


def scan_file(
    path: Path,
    root: Path,
    extensions: Set[str],
    verbose: bool,
) -> List[Tuple[Path, int, int, int, str, str]]:
    """Scan one file. Returns list of (path, line, col, codepoint, category, context)."""
    if not should_scan_file(path, extensions):
        return []
    try:
        raw = path.read_bytes()
    except OSError:
        return []
    if is_likely_binary(raw):
        return []
    try:
        content = raw.decode("utf-8", errors="replace")
    except Exception:
        return []
    findings = scan_content(content)
    rel_path = path.relative_to(root) if root != path else path
    results: List[Tuple[Path, int, int, int, str, str]] = []
    for line_no, col, cp, category in findings:
        ctx = snippet_context(content, line_no, col) if verbose else ""
        results.append((rel_path, line_no, col, cp, category, ctx))
    return results


def scan_directory(
    root: Path,
    extensions: Set[str],
    verbose: bool,
) -> List[Tuple[Path, int, int, int, str, str]]:
    """Walk tree and collect all findings."""
    all_findings: List[Tuple[Path, int, int, int, str, str]] = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if not is_excluded_dir(d)]
        for name in filenames:
            path = Path(dirpath) / name
            findings = scan_file(path, root, extensions, verbose)
            all_findings.extend(findings)
    return all_findings


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Detect hidden/suspicious characters that could indicate an attack vector.",
    )
    parser.add_argument(
        "--root",
        type=Path,
        default=None,
        help="Root directory to scan (default: repo root / current dir)",
    )
    parser.add_argument(
        "--extensions",
        type=str,
        default=",".join(sorted(DEFAULT_EXTENSIONS)),
        help="Comma-separated file extensions including dot (default: common source/text)",
    )
    parser.add_argument(
        "--verbose",
        "-v",
        action="store_true",
        help="Print context snippet for each finding",
    )
    args = parser.parse_args()

    root = args.root
    if root is None:
        # Default: parent of scripts/ if we're in scripts/, else cwd
        script_dir = Path(__file__).resolve().parent
        if script_dir.name == "scripts":
            root = script_dir.parent
        else:
            root = Path.cwd()
    root = root.resolve()
    if not root.is_dir():
        print(f"Error: root is not a directory: {root}", file=sys.stderr)
        return 2

    extensions = {e.strip().lower() for e in args.extensions.split(",") if e.strip()}
    if not extensions:
        extensions = DEFAULT_EXTENSIONS

    findings = scan_directory(root, extensions, args.verbose)

    if not findings:
        print("No hidden or suspicious characters found.")
        return 0

    print(f"Found {len(findings)} suspicious character(s) in {len({f[0] for f in findings})} file(s):\n")
    # Group by file for readable output
    by_file: Dict[Path, List[Tuple[int, int, int, str, str]]] = {}
    for rel_path, line_no, col, cp, category, ctx in findings:
        by_file.setdefault(rel_path, []).append((line_no, col, cp, category, ctx))

    for rel_path in sorted(by_file.keys()):
        file_findings = by_file[rel_path]
        print(f"  {rel_path}")
        for line_no, col, cp, category, ctx in sorted(file_findings):
            line = f"    line {line_no}, col {col}: U+{cp:04X} {category}"
            if ctx:
                line += f"  | {ctx}"
            print(line)
        print()

    print("Consider removing these characters; they can be used for Trojan source or injection.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
