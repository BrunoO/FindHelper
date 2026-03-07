# Clang-Tidy in This Project: Summary of All Approaches

**Date:** 2026-02-19

This document lists every way clang-tidy is run in the project and how they differ, so you can see at a glance which does what.

---

## 1. Full-project report (shell)

| Item | Value |
|------|--------|
| **Script** | `scripts/run_clang_tidy.sh` |
| **When** | Manual: run to get a full report of the repo. |
| **Scope** | All `src/**/*.cpp` and `src/**/*.h` (excludes `external/`, `Embedded*`). |
| **Clang-tidy** | Prefers `scripts/clang-tidy-wrapper.sh` if present, else Homebrew or system `clang-tidy`. |
| **Config** | Auto-discovered `.clang-tidy` (no `--config-file`; clang-tidy finds it). |
| **Compile DB** | Prefers project root if `compile_commands.json` there, else `build/`. |
| **Post-processing** | Pipes output through `filter_clang_tidy_init_statements.py` (removes C++17 init-statement false positives). |
| **Exclusions** | None; every `warning:` and `error:` is counted. |
| **Parallel** | Yes: `xargs -P N -n 1` (default N = cores). `--jobs N` to override. |
| **Output** | Writes `clang-tidy-report-YYYY-MM-DD.txt`; prints summary. |

**Use this for:** Authoritative full-project count and report. Prefer this when you want “how many issues in the whole repo.”

---

## 2. Pre-commit hook

| Item | Value |
|------|--------|
| **Script** | `scripts/pre-commit-clang-tidy.sh` (installed as `.git/hooks/pre-commit`). |
| **When** | Automatically on `git commit` (only if hook is installed). |
| **Scope** | Only **staged** C++ files (excludes `external/`, `Embedded*`, and some platform paths on macOS). |
| **Clang-tidy** | Same as (1): `scripts/clang-tidy-wrapper.sh` when present, else Homebrew/system `clang-tidy`. |
| **Config** | Auto-discovered `.clang-tidy` (same as (1)). |
| **Compile DB** | Same as (1): prefer project root, else `build/`, else `build_coverage/`. |
| **Post-processing** | Same init-statement filter (`filter_clang_tidy_init_statements.py`) when available. |
| **Exclusions** | None for the pass/fail decision; display grep filters to `readability-non-const-parameter|warning:|error:` and drops `llvmlibc-`. |
| **Parallel** | No; one file after another. |
| **Output** | Blocks commit if any warning/error in staged files; prints filtered lines. |

**Use this for:** Catching issues in what you’re about to commit. Same invocation as (1); only difference is scope (staged files only).

---

## 3. Wrapper script (used by 1 and 2)

| Item | Value |
| **When** | When (1) or (2) invoke it (both use the same CLANG_TIDY_CMD logic). |
| **What it does** | Chooses clang-tidy binary (Homebrew or system), then on macOS adds `-I$SDK_PATH/usr/include/c++/v1` and `-I$SDK_PATH/usr/include`. Forwards all other args. |

Both (1) and (2) use the wrapper when it exists, so they get the same results per file.

Both (1) and (2) use the wrapper when it exists, so they get the same results per file.

---

## 4. Init-statement filter (used by 1 and 2)

| Item | Value |
|------|--------|
| **Script** | `scripts/filter_clang_tidy_init_statements.py` |
| **When** | (1) run_clang_tidy.sh pipes clang-tidy output through it; (2) pre-commit does the same. |
| **What it does** | Removes cppcoreguidelines-init-variables warnings that are false positives for C++17 init-statements (e.g. `if (int x = foo(); x > 0)`). |

---

## Quick comparison

| Aspect | run_clang_tidy.sh | pre-commit-clang-tidy.sh |
|--------|-------------------|--------------------------|
| **Scope** | All src (excl. external/Embedded) | Staged files only |
| **Clang-tidy** | Wrapper if present | Same (wrapper if present) |
| **Config** | Auto .clang-tidy | Same |
| **Compile DB** | Prefer root, else build/ | Same |
| **Init-statement filter** | Yes | Yes |
| **Extra exclusions** | None | None (display filtered) |
| **Parallel** | Yes (xargs -P) | No |
| **Output** | Report file + summary | Pass/fail + filtered lines |

---

## Why counts can differ

- **run_clang_tidy.sh** vs **pre-commit**: Only scope differs. Both use the same clang-tidy, compile DB, and filter. (Previously: Different clang-tidy invocation (wrapper vs plain), different compile DB preference, init-statement filter only in (1), and Python excludes several categories → (1) is usually higher and is the one to treat as authoritative for “total repo” count.
- **Pre-commit** vs **run_clang_tidy.sh**: Pre-commit only sees staged files; on macOS it also uses a different invocation (xcrun + isysroot vs wrapper + explicit includes), so for the same file the set of diagnostics can occasionally differ.

---

## What to use when

- **“How many issues in the whole repo? / Full report”** → `run_clang_tidy.sh`
- **“Block commit if staged files have issues”** → pre-commit hook.
- **“Quick filtered summary and top offenders”** 
---

## Recommendation: Keep Only Two (Same Results)

To avoid confusion and get **identical results** for any given file, keep exactly two entry points and make them share the same invocation.

### Keep these two

| Role | Script | Scope |
|------|--------|--------|
| **Full project** | `scripts/run_clang_tidy.sh` | All `src` (excl. external/Embedded). Use for reports and “total repo” count. |
| **Pre-commit** | `scripts/pre-commit-clang-tidy.sh` | Staged C++ files only. Use to block commits when staged code has issues. |

### Make them give the same result per file

- **Same clang-tidy:** Pre-commit should use the same command as the full script: `scripts/clang-tidy-wrapper.sh` when present, else Homebrew/system `clang-tidy` (no separate `xcrun` path).
- **Same compile DB order:** Prefer project root if `compile_commands.json` exists there, else `build/` (pre-commit currently prefers `build/` first — change it to match).
- **Same config:** Both rely on auto-discovered `.clang-tidy` (no `--config-file` when using the wrapper path).
- **Same filter:** Both pipe through `filter_clang_tidy_init_statements.py`. No extra exclusions in either.

Then the **only** difference is scope: full script = all files, pre-commit = staged files. For any file that runs in both, the set of diagnostics is the same.

### Removed

- **`scripts/analyze_clang_tidy_warnings.py`** — Removed. It uses a different clang-tidy (no wrapper), different exclusions, and no init-statement filter, so its count and “top” lists don’t match the other two. For a summary or “top” view, use the report produced by `run_clang_tidy.sh` (it already has totals and top categories); you can add a tiny helper to parse that report for “top N” if needed.

### Summary

- **Two pieces:** (1) `run_clang_tidy.sh` for full project, (2) `pre-commit-clang-tidy.sh` for staged files.
- **One way to run clang-tidy:** wrapper when present, same compile DB order, same filter. Implement that in the pre-commit script so both paths are aligned.
- **One report format:** the text report from `run_clang_tidy.sh`; use it for counts and “top” instead of the Python script.
