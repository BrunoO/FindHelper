#!/usr/bin/env python3
"""Review documentation files for malicious or unsafe content (2026 best practices).

Scans project documentation (docs/, internal-docs/, specs/, root *.md) for:

1. **Hidden / invisible characters**
   - Zero-width and format characters (can hide instructions from human reviewers)
   - Bidirectional override/isolate (Trojan source / display attacks)
   - Control characters and null bytes
   - See: Unicode TR36, CVE-2021-42574, CVE-2025-32711 (Unicode tag injection)

2. **Poisoning / prompt-injection phrases**
   - Instruction-override patterns that could manipulate AI systems reading the docs
   - Based on OWASP LLM Top 10, MCP06:2025, and 2025–2026 prompt-injection research

Best practices (2025–2026) applied:
- NFKC normalization and stripping of invisible/zero-width characters
- Deny-lists for instruction-like phrases in contextual content
- Per-line reporting with file/line for audit

Usage:
  python scripts/review_documentation_security.py [--root <path>] [--verbose] [--json]

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
# Documentation paths to scan (relative to repo root)
# -----------------------------------------------------------------------------
DOC_DIRS: Tuple[str, ...] = ("docs", "internal-docs", "specs")

# Root-level markdown files (AGENTS.md, CLAUDE.md, README.md, etc.)
ROOT_MD_GLOB: str = "*.md"

# Directories to skip inside DOC_DIRS (e.g. external mirrors)
EXCLUDED_DIR_NAMES: Set[str] = {
    ".git",
    ".idea",
    ".vscode",
    "build",
    "external",
    "third_party",
    "vendor",
    "node_modules",
}

# -----------------------------------------------------------------------------
# Hidden / suspicious character definitions (aligned with detect_hidden_characters.py)
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

# Unicode Tag characters (U+E0000–U+E007F) — supplementary plane; check surrogates
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


# -----------------------------------------------------------------------------
# Poisoning / prompt-injection phrase patterns (case-insensitive)
# Sources: OWASP LLM Top 10, MCP06:2025, prompt-injection defense guides 2025–2026
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
    # Exclude descriptive "hidden instructions in X" (e.g. security docs describing the threat)
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

# Compiled regexes for poisoning (case-insensitive)
POISONING_PATTERNS: List[Tuple[re.Pattern[str], str]] = [
    (re.compile(pat, re.IGNORECASE), desc) for pat, desc in POISONING_PHRASES
]


def is_excluded_dir(dirname: str) -> bool:
    """Return True if directory should be skipped."""
    return dirname in EXCLUDED_DIR_NAMES


def get_suspicious_category(codepoint: int) -> Optional[str]:
    """Return category string for reporting, or None if not suspicious."""
    if codepoint in ZERO_WIDTH_AND_FORMAT:
        return ZERO_WIDTH_AND_FORMAT[codepoint]
    if codepoint in CONTROL_SUSPICIOUS:
        if codepoint == 0x00:
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
    """Rough heuristic: common emoji blocks (Misc Symbols, Dingbats, Emoticons, etc.)."""
    return (
        0x2600 <= codepoint <= 0x26FF
        or 0x2700 <= codepoint <= 0x27BF
        or 0x1F300 <= codepoint <= 0x1F9FF
        or 0x1F600 <= codepoint <= 0x1F64F
        or 0x1F1E0 <= codepoint <= 0x1F1FF
    )


def scan_line_hidden_chars(
    line: str, line_no: int
) -> List[Tuple[int, int, int, str]]:
    """Scan one line for suspicious characters. Returns (line_no, col_1based, cp, category)."""
    findings: List[Tuple[int, int, int, str]] = []
    for col_zero, char in enumerate(line):
        cp = ord(char)
        if cp == 0xFEFF and line_no == 1 and col_zero == 0:
            continue
        # ZWJ (U+200D) is standard in emoji sequences (e.g. 👨‍💻); skip when between emoji
        if cp == 0x200D:
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


def collect_doc_files(root: Path) -> List[Path]:
    """Collect all documentation files under root (docs/, internal-docs/, specs/, root *.md)."""
    collected: List[Path] = []
    for name in DOC_DIRS:
        d = root / name
        if not d.is_dir():
            continue
        for dirpath, dirnames, filenames in os.walk(d):
            dirnames[:] = [x for x in dirnames if not is_excluded_dir(x)]
            for f in filenames:
                if f.endswith(".md"):
                    collected.append(Path(dirpath) / f)
    for p in root.glob(ROOT_MD_GLOB):
        if p.is_file():
            collected.append(p)
    return sorted(set(collected))


def scan_file(
    path: Path,
    root: Path,
    verbose: bool,
) -> List[Dict[str, Any]]:
    """Scan one doc file. Returns list of finding dicts."""
    try:
        raw = path.read_bytes()
    except OSError:
        return []
    if not raw:
        return []
    # Skip if too many nulls (binary)
    if raw.count(0x00) / max(1, len(raw)) > 0.01:
        return []
    try:
        content = raw.decode("utf-8", errors="replace")
    except Exception:
        return []
    lines = content.splitlines()
    rel_path = path.relative_to(root) if root != path else path
    findings: List[Dict[str, Any]] = []

    for i, line in enumerate(lines, start=1):
        for line_no, col, cp, category in scan_line_hidden_chars(line, i):
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


def run_scan(root: Path, verbose: bool) -> List[Dict[str, Any]]:
    """Collect doc files and scan; return all findings."""
    all_findings: List[Dict[str, Any]] = []
    for path in collect_doc_files(root):
        all_findings.extend(scan_file(path, root, verbose))
    return all_findings


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Review documentation for hidden characters and poisoning phrases.",
    )
    parser.add_argument(
        "--root",
        type=Path,
        default=None,
        help="Repo root (default: parent of scripts/ or cwd)",
    )
    parser.add_argument(
        "--verbose",
        "-v",
        action="store_true",
        help="Include snippet in each finding",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Output findings as JSON (one object per line or single JSON array)",
    )
    args = parser.parse_args()

    root = args.root
    if root is None:
        script_dir = Path(__file__).resolve().parent
        root = script_dir.parent if script_dir.name == "scripts" else Path.cwd()
    root = root.resolve()
    if not root.is_dir():
        print(f"Error: root is not a directory: {root}", file=sys.stderr)
        return 2

    findings = run_scan(root, args.verbose)

    if args.json:
        print(json.dumps(findings, indent=2))
        return 1 if findings else 0

    if not findings:
        print("No documentation security findings.")
        return 0

    by_file: Dict[str, List[Dict[str, Any]]] = {}
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
    sys.exit(main())
