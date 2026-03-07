# Agent instructions: clang-tidy warnings review report

**Date:** 2026-02-14  
**Audience:** AI agents and maintainers  
**Goal:** Run the clang-tidy analysis and **create a new report file** with the current warning status for this review.

---

## Canonical locations

| What | Path |
|------|------|
| **Report output** (create this) | `docs/CLANG_TIDY_WARNINGS_REVIEW_{YYYY-MM-DD}.md` |
| **Analysis script** (run this) | `scripts/run_clang_tidy.sh` |
| **Config** | `.clang-tidy` (project root) |
| **Build** | `build/` or `build_coverage/` must contain `compile_commands.json` |

---

## Prerequisites

1. **Compile commands:** From project root, ensure a build directory has `compile_commands.json`:
   - `build/compile_commands.json`, or
   - `build_coverage/compile_commands.json`  
   If missing, run: `cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` (or use the project's usual build script).
2. **Python 3** and **clang-tidy** available on the path.

---

## Step-by-step: create the report file

### 1. Run the analysis script

From the **project root**:

```bash
./scripts/run_clang_tidy.sh
```

Capture the full output. You need:

- **Total files scanned**
- **Files with warnings**
- **Files clean**
- **Total warnings**
- **TOP 20 WARNING TYPES** (if any)
- **TOP 25 FILES WITH MOST WARNINGS** (if any)

The script uses `.clang-tidy` and `-p build` (or `-p build_coverage`). It pipes output through `filter_clang_tidy_init_statements.py` to exclude C++17 init-statement false positives. Warnings in the output count toward "files with warnings" and "total warnings".

### 2. Create the report file

Create a **new** file:

**Path:** `docs/CLANG_TIDY_WARNINGS_REVIEW_{YYYY-MM-DD}.md`  
Use the **date of the review run** (e.g. `docs/CLANG_TIDY_WARNINGS_REVIEW_2026-02-14.md`).

### 3. Report file structure and content

Write the report using **only** data from the script output. Use this structure:

```markdown
# Clang-Tidy Warnings Review - {YYYY-MM-DD}

## Summary

| Metric | Value |
|--------|-------|
| Total files scanned | N |
| Files with warnings | N |
| Files clean | N |
| Total warnings | N |

**Summary sentence:** e.g. "All N scanned files are clean (0 warnings)." or "X files have warnings (Y total); Z files clean."

## Methodology

- **Script:** `scripts/run_clang_tidy.sh`
- **Config:** `.clang-tidy` (project root)
- **Build:** `build/` or `build_coverage/` (`compile_commands.json`)
- **Filter:** init-statement false positives filtered via `filter_clang_tidy_init_statements.py`.

## Top warning types (if any)

| Count | Warning type |
|------:|--------------|
| ... | ... |

(If total warnings is 0, write "None" or omit this section.)

## Top files with warnings (if any)

| Count | File |
|------:|------|
| ... | ... |

(If no files with warnings, write "None" or omit this section.)

## How to refresh

From project root, with `build/compile_commands.json` present:

```bash
./scripts/run_clang_tidy.sh
```
```

When filling the template: use the **same** run for all numbers, use the run date in the filename and title, and do not invent data—only use script output.

### 4. Optional: update the living status document

Optionally, you may also update the project's living clang-tidy status document (if present) with a new "Status update (YYYY-MM-DD)" section and "Current status" using the same run. The **required** deliverable for this review phase is the new report file; any status doc update is optional.

---

## Consistency rules

- **One run, one report:** The report file must reflect a single run of `run_clang_tidy.sh`.
- **Date:** Use the date of that run in the report filename and title.
- **No invented data:** Only paste or summarize numbers and lists that appear in the script output.

---

## Reference: what the script does

- Scans all `.cpp`, `.h`, `.hpp`, `.cxx`, `.cc` under `src/` (excluding `external/`).
- For each file: runs `clang-tidy <file> --config-file=.clang-tidy -p <build_dir> --quiet`.
- Pipes clang-tidy output through `filter_clang_tidy_init_statements.py`; counts remaining lines with `warning:` or `error:`.
- Aggregates totals and prints top warning types and top files by count.

The **pre-commit hook** (`scripts/pre-commit-clang-tidy.sh`) runs clang-tidy on **staged** files only and does **not** use the same exclusions; the report reflects the **analysis script** results only.
