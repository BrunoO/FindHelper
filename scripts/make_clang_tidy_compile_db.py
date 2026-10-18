#!/usr/bin/env python3
"""Writes a clang-tidy-safe copy of a CMake compile_commands.json.

Why this exists
---------------
The build emits a precompiled header per target. clang-tidy is a different clang
major version than the one that built it, and rejects that PCH outright:

  error: PCH file '.../cmake_pch.hxx.pch' uses an older format that is no
         longer supported [clang-diagnostic-error]

Every translation unit that references the PCH then fails to analyze. Both
scripts/pre-commit-clang-tidy.sh and scripts/run_clang_tidy.sh filter
`[clang-diagnostic-error]` out of their results (to tolerate Windows/Linux
headers on macOS), so the failure is invisible and the run reports success
while having checked nothing.

Stripping the PCH flag groups from a copy of the database lets clang-tidy
parse each header from source. This costs some time on the first translation
unit of each target and buys real coverage on all of them.

Both toolchains are handled. Apple/GCC spell the flags `-emit-pch`,
`-include-pch` and `-include`; MSVC spells them `/Yc`, `/Yu`, `/Fp` and
`/FI`. An MSVC `.pch` is a different file format that clang cannot read at
all, so on Windows the flags have to go for the same reason.

This is Python rather than the TypeScript it replaced so that the lint
toolchain needs no runtime beyond the one it already requires: python3 drives
scripts/filter_clang_tidy_init_statements.py and scripts/check_cpp_guardrails.py
on every run, and the Windows CI image ships Python but not Bun.

Usage
  python3 scripts/make_clang_tidy_compile_db.py              # writes db, prints its directory
  python3 scripts/make_clang_tidy_compile_db.py --has <src>  # membership test, exit 0/1

Options
  --db <path>   Source compile_commands.json (default: auto-detect)
  --out <dir>   Output directory (default: <repo>/.cache/clang-tidy-db)
  --has <file>  Resolve <file> against the database; print yes/no, exit 0/1

Exit codes: 0 success, 1 failure (message on stderr).
"""

import json
import os
import re
import subprocess
import sys

ARGV = sys.argv[1:]


def flag(name):
    """Value of the first occurrence of `name`, or None when absent/unterminated."""
    if name not in ARGV:
        return None
    i = ARGV.index(name)
    return ARGV[i + 1] if i + 1 < len(ARGV) else None


def flags(name):
    """All occurrences of a repeatable flag, in command-line order."""
    out = []
    for i, arg in enumerate(ARGV):
        if arg == name and i + 1 < len(ARGV):
            out.append(ARGV[i + 1])
    return out


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_git_env = dict(os.environ)
_git_env.pop("GIT_DIR", None)
_git_env.pop("GIT_WORK_TREE", None)
_repo_root = subprocess.run(
    ["git", "-C", SCRIPT_DIR, "rev-parse", "--show-toplevel"],
    capture_output=True,
    text=True,
    env=_git_env,
)
ROOT = (
    _repo_root.stdout.strip()
    if _repo_root.returncode == 0
    else os.path.dirname(SCRIPT_DIR)
)

# Build directories to search, in the order scripts/run_clang_tidy.sh already
# uses. All of them are merged by default: the test build and the app build
# compile overlapping sources with different targets, so neither alone is
# complete. Merging raises the analyzable set from 55 translation units (tests
# only) to 82 (tests + app); what remains unanalyzable is platform-specific
# sources absent from any macOS build.
DB_CANDIDATES = [
    f"{ROOT}/compile_commands.json",
    f"{ROOT}/build/compile_commands.json",
    f"{ROOT}/build_tests/compile_commands.json",
    f"{ROOT}/build_coverage/compile_commands.json",
]

# JavaScript's \s and Python's \s agree on ASCII and on the Unicode spaces, so
# the tokenizer needs no flag here. detokenize does need one: JS \w is ASCII
# only, while Python \w is Unicode-aware and would leave a non-ASCII token
# unquoted where the original quoted it.
_WHITESPACE = re.compile(r"\s")
_PLAIN_TOKEN = re.compile(r"^[\w@%+=:,./-]+$", re.ASCII)


def is_pch_path(token):
    """Marks a PCH artifact regardless of target or language.

    CMake names these `cmake_pch.hxx` for C++ and `cmake_pch.objcxx.hxx` for
    Objective-C++.
    """
    return "cmake_pch" in token


# clang flags whose operand is a PCH path rather than a real flag.
PCH_OPERAND_FLAGS = frozenset(["-include-pch", "-include"])

# MSVC drives the same four PCH operations with /Yc, /Yu, /Fp and /FI,
# spelling the operand either appended to the flag (/Yucmake_pch.hxx) or as
# the following token.
#
# These were unhandled, and the generic is_pch_path catch-all below did not
# cover them: it drops any token naming cmake_pch, which removes the operand
# while leaving the flag beside it. An MSVC database therefore came out of this
# script carrying orphaned /Yu, /Fp and /FI flags for clang-tidy to choke on,
# plus the /Yc entry that generates the PCH. Measured, not predicted: exit
# status was 0 and a database was written. ENABLE_PCH defaults to ON, so this is
# every Windows run, not an edge case.
MSVC_PCH_CREATE_FLAG = "/yc"
MSVC_PCH_ONLY_FLAGS = (MSVC_PCH_CREATE_FLAG, "/yu", "/fp")
MSVC_FORCE_INCLUDE_FLAG = "/fi"


def split_msvc_pch_flag(token):
    """Splits an MSVC PCH flag into its lowercased flag and attached operand.

    Returns None when the token is not one. Case-insensitive because cl accepts
    /Yu and /yu alike and CMake is not contracted to emit one spelling.
    """
    lower = token.lower()
    for candidate in (*MSVC_PCH_ONLY_FLAGS, MSVC_FORCE_INCLUDE_FLAG):
        if lower == candidate:
            return candidate, None
        # No MSVC flag extends these four, so a longer match is flag + operand.
        if lower.startswith(candidate) and len(lower) > len(candidate):
            return candidate, token[len(candidate) :]
    return None


def is_pch_generation_entry(entry):
    """True for the entry that *generates* a PCH.

    That is `-emit-pch`, `-x objective-c++-header`, or MSVC `/Yc`. clang-tidy
    must never run these: they have no translation unit to analyze and they
    reference the output PCH as their input.
    """
    if isinstance(entry.get("arguments"), list):
        tokens = entry["arguments"]
    else:
        tokens = tokenize(entry.get("command") or "")
    if "-emit-pch" in tokens or "objective-c++-header" in tokens:
        return True
    return any(
        (split_msvc_pch_flag(t) or (None, None))[0] == MSVC_PCH_CREATE_FLAG
        for t in tokens
    )


def strip_pch_flags(tokens):
    """Removes every PCH reference.

    Clang/GCC forms:
      -Xclang -include-pch -Xclang <pch>      (C++ targets)
      -include-pch <pch>                      (Objective-C++ targets)
      -Xclang -include -Xclang <pch.hxx>      (precompiled header text)
      -include <pch.hxx>
    MSVC forms:
      /Yc<header>  /Yu<header>  /Fp<pch>      (create, use, output path)
      /FI<header>                              (forced include of the PCH text)

    `-Xclang <other-flag>` and `<flag> <non-PCH operand>` pairs are preserved by
    dropping only the wrapper, so `-Xclang -emit-pch` survives intact.

    /FI is the one asymmetric case: it is a general force-include, so it is
    removed only when it names a PCH header. A hand-written /FI of an ordinary
    header is a real compile instruction and stripping it would analyze a
    translation unit the build never compiles. The other three exist solely to
    drive the PCH, so they always go.
    """
    kept = []
    i = 0
    while i < len(tokens):
        token = tokens[i]
        nxt = tokens[i + 1] if i + 1 < len(tokens) else None

        if token == "-Xclang":
            is_pch_group = (
                nxt in PCH_OPERAND_FLAGS
                and i + 2 < len(tokens)
                and tokens[i + 2] == "-Xclang"
                and i + 3 < len(tokens)
                and is_pch_path(tokens[i + 3])
            )
            if is_pch_group:
                i += 4
                continue
            i += 1  # keep the inner flag, discard the wrapper
            continue

        # Bare `-include-pch <pch>` / `-include <pch.hxx>`.
        if token in PCH_OPERAND_FLAGS and nxt is not None and is_pch_path(nxt):
            i += 2
            continue

        msvc = split_msvc_pch_flag(token)
        if msvc is not None:
            msvc_flag, attached = msvc
            operand = attached if attached is not None else nxt
            is_pch_reference = (
                is_pch_path(operand or "")
                if msvc_flag == MSVC_FORCE_INCLUDE_FLAG
                else True
            )
            if is_pch_reference:
                # Attached operand lives in this token; a separated one is next.
                i += 1 if (attached is not None or nxt is None) else 2
                continue

        if is_pch_path(token):
            i += 1
            continue

        kept.append(token)
        i += 1
    return kept


def tokenize(command):
    """Splits a shell command string into tokens.

    Honours single quotes, double quotes, and backslash escapes. CMake emits
    `command` as a single string, but clang-tidy needs the flag list back, so the
    quoting has to round-trip.

    Known imprecision, preserved deliberately from the TypeScript original so
    this port stays a pure language change: there is no flush at a quote
    boundary, so an operand attached to its flag merges into one token
    (`/Yu"p"` -> `/Yup`) while a space-separated one does not (`/yu "p"` -> two
    tokens). Correcting it would change output that is currently correct on the
    Apple path, so it is a separate change with its own verification.
    """
    tokens = []
    current = ""
    started = False
    quote = None

    i = 0
    while i < len(command):
        ch = command[i]

        if quote == "'":
            if ch == "'":
                quote = None
            else:
                current += ch
            i += 1
            continue

        if quote == '"':
            if ch == "\\" and i + 1 < len(command):
                i += 1
                current += command[i]
            elif ch == '"':
                quote = None
            else:
                current += ch
            i += 1
            continue

        if ch == "\\" and i + 1 < len(command):
            i += 1
            current += command[i]
            started = True
        elif ch in ("'", '"'):
            quote = ch
            started = True
        elif _WHITESPACE.search(ch):
            if started:
                tokens.append(current)
                current = ""
                started = False
        else:
            current += ch
            started = True
        i += 1

    if started:
        tokens.append(current)
    return tokens


def detokenize(tokens):
    """Inverse of :func:`tokenize`, so the emitted `command` stays a valid string."""
    return " ".join(
        t if _PLAIN_TOKEN.match(t) else "'" + t.replace("'", "'\"'\"'") + "'"
        for t in tokens
    )


def strip_entry(entry):
    """Rewrites one entry with PCH flags removed, preserving its key order."""
    nxt = dict(entry)

    if isinstance(entry.get("arguments"), list):
        nxt["arguments"] = strip_pch_flags(entry["arguments"])
    elif isinstance(entry.get("command"), str):
        nxt["command"] = detokenize(strip_pch_flags(tokenize(entry["command"])))

    return nxt


def absolutize(file):
    """Resolves a possibly repo-relative path the way the compile database stores it."""
    return file if file.startswith("/") else f"{ROOT}/{file}"


def load_databases():
    """Merges every candidate database, keeping the first entry per (directory, file)."""
    explicit = flags("--db")
    candidates = explicit if explicit else DB_CANDIDATES
    existing = [c for c in candidates if os.path.exists(c) and os.path.getsize(c) > 0]

    if not existing:
        print("make_clang_tidy_compile_db: no compile_commands.json found.", file=sys.stderr)
        print("Looked in:", file=sys.stderr)
        for c in candidates:
            print(f"  {c}", file=sys.stderr)
        print("\nGenerate one with:", file=sys.stderr)
        print("  cmake -B build_tests -DCMAKE_EXPORT_COMPILE_COMMANDS=ON", file=sys.stderr)
        sys.exit(1)

    merged = []
    seen = set()
    for path in existing:
        with open(path, encoding="utf-8") as handle:
            parsed = json.load(handle)
        for entry in parsed:
            # A file can appear in several builds with different flags; keep the
            # first so the merged database stays deterministic.
            key = f"{entry['directory']}\x00{entry['file']}"
            if key in seen:
                continue
            seen.add(key)
            merged.append(entry)

    return existing, merged


def main():
    db_paths, entries = load_databases()

    # --- membership test ----------------------------------------------------
    probe = flag("--has")
    if probe is not None:
        target = absolutize(probe)
        hit = any(absolutize(e["file"]) == target for e in entries)
        print("yes" if hit else "no")
        sys.exit(0 if hit else 1)

    # --- write filtered copy ------------------------------------------------
    out_dir = flag("--out") or f"{ROOT}/.cache/clang-tidy-db"
    os.makedirs(out_dir, exist_ok=True)

    analyzable = [e for e in entries if not is_pch_generation_entry(e)]
    filtered = [strip_entry(e) for e in analyzable]
    # ensure_ascii=False matches JSON.stringify, which emits raw UTF-8 rather
    # than \uXXXX escapes; the default would silently rewrite every entry.
    with open(
        f"{out_dir}/compile_commands.json", "w", encoding="utf-8"
    ) as handle:
        handle.write(json.dumps(filtered, indent=2, ensure_ascii=False) + "\n")

    pch_entries = len(entries) - len(analyzable)
    pch_refs = sum(
        1 for e in entries if is_pch_path(e.get("command") or " ".join(e.get("arguments") or []))
    )

    leftovers = [
        e for e in filtered if is_pch_path(e.get("command") or " ".join(e.get("arguments") or []))
    ]
    if leftovers:
        plural = "y" if len(leftovers) == 1 else "ies"
        print(
            f"make_clang_tidy_compile_db: {len(leftovers)} entr{plural} still reference a PCH, e.g.:",
            file=sys.stderr,
        )
        print(f"  {leftovers[0]['file']}", file=sys.stderr)
        sys.exit(1)

    # stdout carries only the database directory so callers can consume it
    # directly (`CLANG_TIDY_DB=$(python3 ...)`). Progress detail goes to stderr.
    plural = "y" if pch_entries == 1 else "ies"
    print(
        f"make_clang_tidy_compile_db: {len(filtered)} analyzable entries from "
        f"{len(db_paths)} database(s); {pch_entries} PCH-generating entr{plural} "
        f"dropped, {pch_refs} PCH reference(s) stripped",
        file=sys.stderr,
    )
    for p in db_paths:
        print(f"  {p}", file=sys.stderr)
    print(out_dir)


if __name__ == "__main__":
    main()
