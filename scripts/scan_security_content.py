#!/usr/bin/env python3
"""Scan project content for security issues: hidden characters and poisoning phrases.

Two modes:

1. **Documentation mode (default)**  
   Scans docs (docs/, internal-docs/, specs/, root *.md) for:
   - Hidden/invisible characters (zero-width, bidirectional override, control chars, null bytes)
   - Poisoning/prompt-injection phrases (instruction override, prompt extraction, etc.)
   See: Unicode TR36, CVE-2021-42574, CVE-2025-32711; OWASP LLM Top 10, MCP06:2025.

2. **File scan mode (--scan-files)**  
   Scans all files by extension under root (default: source/text extensions).  
   Only checks hidden/suspicious characters (no poisoning). Use to audit external/ or full tree.

Usage:
  # Documentation only (hidden chars + poisoning)
  python scripts/scan_security_content.py [--root <path>] [--verbose] [--json]

  # All project files by extension (hidden chars only; excludes external/ by default)
  python scripts/scan_security_content.py --scan-files [--root <path>] [--extensions .py,.cpp,...] [--verbose]

  # Scan external libraries
  python scripts/scan_security_content.py --scan-files --root external [--verbose]

Exit code 0 if no findings; 1 if any finding (suitable for CI).
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import unicodedata
from pathlib import Path
from typing import Any, Dict, List, Optional, Set, Tuple

# -----------------------------------------------------------------------------
# Documentation mode: paths and exclusions
# -----------------------------------------------------------------------------
DOC_DIRS: Tuple[str, ...] = ("docs", "internal-docs", "specs")
ROOT_MD_GLOB: str = "*.md"

# Directories to skip when walking (docs and file scan)
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

# Docs that list the deny-list (intentional examples) — skip in doc mode
EXCLUDED_DOC_FILES: Set[str] = {
    "internal-docs/design/2026-03-07_DOCUMENTATION_SECURITY_REVIEW_BEST_PRACTICES.md",
}

# File scan: default extensions
DEFAULT_EXTENSIONS: Set[str] = {
    ".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".hxx",
    ".py", ".sh", ".ps1", ".cmake", ".txt", ".md", ".yml", ".yaml",
    ".json", ".xml", ".html", ".css", ".js", ".ts", ".bat", ".cmd",
}

# -----------------------------------------------------------------------------
# Hidden / suspicious character definitions
# Unicode TR36, CVE-2021-42574, CVE-2025-32711
# -----------------------------------------------------------------------------
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
    0x202E: "RIGHT-TO-LEFT OVERRIDE",
    0x2060: "WORD JOINER (invisible)",
    0x2066: "LEFT-TO-RIGHT ISOLATE",
    0x2067: "RIGHT-TO-LEFT ISOLATE",
    0x2068: "FIRST STRONG ISOLATE",
    0x2069: "POP DIRECTIONAL ISOLATE",
    0xFEFF: "ZERO WIDTH NO-BREAK SPACE (BOM)",
}
CONTROL_SUSPICIOUS: Set[int] = set(range(0x00, 0x20)) | set(range(0x80, 0xA0))
CONTROL_SUSPICIOUS -= {0x09, 0x0A, 0x0D}
NULL_CODEPOINT: int = 0x00

# -----------------------------------------------------------------------------
# Poisoning / prompt-injection phrase patterns (doc mode only)
# -----------------------------------------------------------------------------
POISONING_PHRASES: List[Tuple[str, str]] = [
    (r"ignore\s+(all\s+)?previous\s+instructions?", "instruction override"),
    (r"disregard\s+(all\s+)?(previous|your)\s+instructions?", "instruction override"),
    (r"override\s+(system|your)\s+instructions?", "system override"),
    (r"system\s+override", "system override"),
    (r"reveal\s+(your\s+)?(system\s+)?prompt", "prompt extraction"),
    (r"show\s+(your|the|system)\s+(system\s+)?instructions?", "prompt extraction"),
    (r"you\s+are\s+now\s+", "role override"),
    (r"new\s+instructions?\s*:", "instruction override"),
    (r"follow\s+these\s+instructions?\s+instead", "instruction override"),
    (r"pretend\s+you\s+are\s+", "role override"),
    (r"act\s+as\s+if\s+you\s+", "role override"),
    (r"\bjailbreak\b", "jailbreak"),
    (r"DAN\s+mode", "jailbreak"),
    (r"maximum\s+mode", "jailbreak"),
    (r"do\s+not\s+follow\s+(your\s+)?instructions?", "instruction override"),
    (r"forget\s+everything\s+(above|before)", "context override"),
    (r"never\s+refuse", "safety bypass"),
    (r"bypass\s+(safety|content\s+policy)", "safety bypass"),
    (r"ignore\s+(safety|content\s+policy)", "safety bypass"),
    (r"hidden\s+instruction(?!s?\s+in\s+)s?", "hidden instruction"),
    (r"do\s+not\s+tell\s+the\s+user", "conceal from user"),
    (r"do\s+not\s+disclose", "conceal from user"),
    (r"keep\s+this\s+secret", "conceal from user"),
    (r"do\s+not\s+reveal\s+", "conceal from user"),
    (r"output\s+only\s+", "output override"),
    (r"only\s+output\s+", "output override"),
    (r"ignore\s+the\s+above\s+", "context override"),
    (r"disregard\s+the\s+above\s+", "context override"),
]
POISONING_PATTERNS: List[Tuple[re.Pattern[str], str]] = [
    (re.compile(pat, re.IGNORECASE), desc) for pat, desc in POISONING_PHRASES
]


def is_excluded_dir(dirname: str) -> bool:
    """Return True if directory should be skipped."""
    if dirname in EXCLUDED_DIR_NAMES:
        return True
    return any(dirname.startswith(p) for p in EXCLUDED_DIR_PREFIXES)


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
    if 0xE0000 <= codepoint <= 0xE007F:
        return "UNICODE TAG (supplementary)"
    return None


def _is_emoji_codepoint(codepoint: int) -> bool:
    """Rough heuristic: common emoji blocks."""
    return (
        0x2600 <= codepoint <= 0x26FF
        or 0x2700 <= codepoint <= 0x27BF
        or 0x1F300 <= codepoint <= 0x1F9FF
        or 0x1F600 <= codepoint <= 0x1F64F
        or 0x1F1E0 <= codepoint <= 0x1F1FF
    )


def _scan_line_hidden_chars(
    line: str, line_no: int, skip_zwj_in_emoji: bool
) -> List[Tuple[int, int, int, str]]:
    """Scan one line for suspicious characters. Returns (line_no, col_1based, cp, category)."""
    findings: List[Tuple[int, int, int, str]] = []
    for col_zero, char in enumerate(line):
        cp = ord(char)
        if cp == 0xFEFF and line_no == 1 and col_zero == 0:
            continue
        if skip_zwj_in_emoji and cp == 0x200D:
            prev_cp = ord(line[col_zero - 1]) if col_zero > 0 else 0
            next_cp = ord(line[col_zero + 1]) if col_zero + 1 < len(line) else 0
            if _is_emoji_codepoint(prev_cp) and (
                next_cp == 0 or _is_emoji_codepoint(next_cp)
            ):
                continue
        category = get_suspicious_category(cp)
        if category:
            findings.append((line_no, col_zero + 1, cp, category))
    return findings


def scan_content_hidden_chars(
    content: str, skip_zwj_in_emoji: bool = True
) -> List[Tuple[int, int, int, str]]:
    """Scan text for suspicious characters. Returns (line_1based, col_1based, cp, category)."""
    findings: List[Tuple[int, int, int, str]] = []
    lines = content.splitlines(keepends=True)
    for line_no, line in enumerate(lines, start=1):
        findings.extend(
            _scan_line_hidden_chars(line, line_no, skip_zwj_in_emoji)
        )
    return findings


def scan_line_poisoning(
    line: str, line_no: int
) -> List[Tuple[int, str, str]]:
    """Scan one line for poisoning phrases. Returns (line_no, description, matched_text)."""
    findings: List[Tuple[int, str, str]] = []
    for pattern, desc in POISONING_PATTERNS:
        match = pattern.search(line)
        if match:
            findings.append((line_no, desc, match.group(0)))
    return findings


def snippet_escape(content: str, max_len: int = 60) -> str:
    """Escape non-printables and truncate for display."""
    out: List[str] = []
    for c in content[:max_len]:
        if c in "\t":
            out.append("\\t")
        elif c == "\n":
            out.append("\\n")
        elif get_suspicious_category(ord(c)) is not None or (
            not c.isprintable() and c not in " \r"
        ):
            out.append(f"<U+{ord(c):04X}>")
        else:
            out.append(c)
    if len(content) > max_len:
        out.append("...")
    return "".join(out)


def snippet_context(content: str, line_no: int, col: int, width: int = 40) -> str:
    """Return a one-line snippet around (line_no, col), escaping non-printables."""
    lines = content.splitlines()
    if line_no < 1 or line_no > len(lines):
        return ""
    line = lines[line_no - 1]
    start = max(0, col - 1 - width // 2)
    end = min(len(line), start + width)
    raw = line[start:end]
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


def is_likely_binary(content: bytes) -> bool:
    """Heuristic: treat as binary if too many nulls or high bytes."""
    if not content:
        return True
    if content.count(0x00) / len(content) > 0.01:
        return True
    try:
        text = content.decode("utf-8", errors="strict")
    except UnicodeDecodeError:
        return True
    non_print = sum(1 for c in text if ord(c) < 0x20 and c not in "\t\n\r")
    if len(text) > 0 and non_print / len(text) > 0.1:
        return True
    return False


# -----------------------------------------------------------------------------
# Documentation mode
# -----------------------------------------------------------------------------
def collect_doc_files(root: Path) -> List[Path]:
    """Collect all documentation files under root."""
    collected: List[Path] = []
    for name in DOC_DIRS:
        d = root / name
        if not d.is_dir():
            continue
        for dirpath, dirnames, filenames in os.walk(d):
            dirnames[:] = [x for x in dirnames if not is_excluded_dir(x)]
            for f in filenames:
                if f.endswith(".md"):
                    full_path = Path(dirpath) / f
                    try:
                        rel_str = str(full_path.relative_to(root))
                    except ValueError:
                        rel_str = str(full_path)
                    if rel_str not in EXCLUDED_DOC_FILES:
                        collected.append(full_path)
    for p in root.glob(ROOT_MD_GLOB):
        if p.is_file():
            try:
                rel_str = str(p.relative_to(root))
            except ValueError:
                rel_str = str(p)
            if rel_str not in EXCLUDED_DOC_FILES:
                collected.append(p)
    return sorted(set(collected))


def scan_doc_file(
    path: Path, root: Path, verbose: bool
) -> List[Dict[str, Any]]:
    """Scan one doc file. Returns list of finding dicts (hidden_char + poisoning_phrase)."""
    try:
        raw = path.read_bytes()
    except OSError:
        return []
    if not raw or raw.count(0x00) / max(1, len(raw)) > 0.01:
        return []
    try:
        content = raw.decode("utf-8", errors="replace")
    except Exception:
        return []
    lines = content.splitlines()
    try:
        rel_path = path.relative_to(root)
    except ValueError:
        rel_path = path
    findings: List[Dict[str, Any]] = []
    for i, line in enumerate(lines, start=1):
        for line_no, col, cp, category in _scan_line_hidden_chars(line, i, True):
            rec: Dict[str, Any] = {
                "file": str(rel_path),
                "line": line_no,
                "type": "hidden_char",
                "detail": f"U+{cp:04X} {category}",
                "column": col,
            }
            if verbose:
                rec["snippet"] = snippet_escape(line)
            findings.append(rec)
        for line_no, desc, matched in scan_line_poisoning(line, i):
            rec = {
                "file": str(rel_path),
                "line": line_no,
                "type": "poisoning_phrase",
                "detail": desc,
                "matched": matched,
            }
            if verbose:
                rec["snippet"] = snippet_escape(line)
            findings.append(rec)
    return findings


def run_doc_scan(root: Path, verbose: bool) -> List[Dict[str, Any]]:
    """Run documentation scan (hidden chars + poisoning)."""
    all_findings: List[Dict[str, Any]] = []
    for path in collect_doc_files(root):
        all_findings.extend(scan_doc_file(path, root, verbose))
    return all_findings


# -----------------------------------------------------------------------------
# File scan mode (by extension; hidden chars only)
# -----------------------------------------------------------------------------
def should_scan_file(path: Path, extensions: Set[str]) -> bool:
    """Return True if file extension is in the allowed set."""
    return path.suffix.lower() in extensions


def scan_file_for_hidden(
    path: Path,
    root: Path,
    extensions: Set[str],
    verbose: bool,
) -> List[Tuple[Path, int, int, int, str, str]]:
    """Scan one file for hidden chars only. Returns (rel_path, line, col, cp, category, context)."""
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
    try:
        rel_path = path.relative_to(root)
    except ValueError:
        rel_path = path
    findings = scan_content_hidden_chars(content, skip_zwj_in_emoji=True)
    results: List[Tuple[Path, int, int, int, str, str]] = []
    for line_no, col, cp, category in findings:
        ctx = snippet_context(content, line_no, col) if verbose else ""
        results.append((rel_path, line_no, col, cp, category, ctx))
    return results


def run_file_scan(
    root: Path,
    extensions: Set[str],
    verbose: bool,
) -> List[Tuple[Path, int, int, int, str, str]]:
    """Walk tree and collect hidden-char findings (respects EXCLUDED_*)."""
    all_findings: List[Tuple[Path, int, int, int, str, str]] = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if not is_excluded_dir(d)]
        for name in filenames:
            path = Path(dirpath) / name
            all_findings.extend(
                scan_file_for_hidden(path, root, extensions, verbose)
            )
    return all_findings


# -----------------------------------------------------------------------------
# CLI
# -----------------------------------------------------------------------------
def _main() -> int:
    parser = argparse.ArgumentParser(
        description="Review documentation and/or files for hidden characters and poisoning phrases.",
    )
    parser.add_argument(
        "--root",
        type=Path,
        default=None,
        help="Root directory (default: parent of scripts/ or cwd)",
    )
    parser.add_argument(
        "--scan-files",
        action="store_true",
        help="Scan by file extension (hidden chars only); default is docs only (hidden + poisoning)",
    )
    parser.add_argument(
        "--extensions",
        type=str,
        default=",".join(sorted(DEFAULT_EXTENSIONS)),
        help="Comma-separated extensions for --scan-files (e.g. .py,.cpp,.md)",
    )
    parser.add_argument(
        "--verbose",
        "-v",
        action="store_true",
        help="Include snippet/context in each finding",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Output findings as JSON (doc mode: array of objects; file mode: array of {file,line,column,...})",
    )
    args = parser.parse_args()

    script_dir = Path(__file__).resolve().parent
    root = args.root
    if root is None:
        root = script_dir.parent if script_dir.name == "scripts" else Path.cwd()
    root = root.resolve()
    if not root.is_dir():
        print(f"Error: root is not a directory: {root}", file=sys.stderr)
        return 2

    if args.scan_files:
        extensions = {e.strip().lower() for e in args.extensions.split(",") if e.strip()}
        if not extensions:
            extensions = DEFAULT_EXTENSIONS
        findings = run_file_scan(root, extensions, args.verbose)
        if args.json:
            out = [
                {
                    "file": str(f[0]),
                    "line": f[1],
                    "column": f[2],
                    "codepoint": f[3],
                    "category": f[4],
                    **({"snippet": f[5]} if f[5] else {}),
                }
                for f in findings
            ]
            print(json.dumps(out, indent=2))
            return 1 if findings else 0
        if not findings:
            print("No hidden or suspicious characters found.")
            return 0
        by_file: Dict[Path, List[Tuple[int, int, int, str, str]]] = {}
        for rel_path, line_no, col, cp, category, ctx in findings:
            by_file.setdefault(rel_path, []).append((line_no, col, cp, category, ctx))
        print(f"Found {len(findings)} suspicious character(s) in {len(by_file)} file(s):\n")
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

    # Documentation mode
    findings = run_doc_scan(root, args.verbose)
    if args.json:
        print(json.dumps(findings, indent=2))
        return 1 if findings else 0
    if not findings:
        print("No documentation security findings.")
        return 0
    by_file = {}
    for f in findings:
        by_file.setdefault(f["file"], []).append(f)
    print(f"Documentation security review: {len(findings)} finding(s) in {len(by_file)} file(s)\n")
    for file_path in sorted(by_file.keys()):
        file_findings = by_file[file_path]
        print(f"  {file_path}")
        for r in sorted(file_findings, key=lambda x: (x["line"], x.get("column", 0))):
            line_str = f"    line {r['line']}: [{r['type']}] {r['detail']}"
            if r.get("matched"):
                line_str += f"  matched: {r['matched']!r}"
            if r.get("snippet"):
                line_str += f"  | {r['snippet']}"
            print(line_str)
        print()
    print(
        "Remove hidden characters (e.g. NFKC normalize and strip invisible). "
        "Review poisoning phrases for legitimate vs malicious use."
    )
    return 1


if __name__ == "__main__":
    sys.exit(_main())
