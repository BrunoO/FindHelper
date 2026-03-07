# Approach to Remove llvm-cov "Mismatched Data" Warnings

**Date:** 2026-02-23

## Problem

When generating coverage with `generate_coverage_macos.sh`, we:

1. Merge **all** `.profraw` files into a single `coverage.profdata`.
2. Run `llvm-cov report` / `llvm-cov show` with **multiple** `-object <exe>` (one per test executable, plus FindHelper and optionally `coverage_all_sources`).

This produces:

- **"622 functions have mismatched data"** (or similar): profile counts were recorded by one binary, but the coverage mapping (function hash) in another binary doesn’t match, so llvm-cov can’t attribute those counts.
- **0% (or wrong %) for some files** that are actually executed: e.g. `LoadBalancingStrategyUtils.cpp`, `PathPatternMatcher.cpp`. Those are only run from specific test binaries; when the merged profile is matched against a different binary that also contains that code (with a different mapping), the tool reports no coverage.

### Root cause (short)

- The **same source** is compiled into **multiple** executables (object libs linked into several tests, and/or app).
- Each binary has its own **coverage mapping** (e.g. structural hash per function). In some binaries a symbol may have a “dummy” mapping (hash `0x0`) when that TU didn’t emit the function; in others it has the real mapping.
- **Merging** all `.profraw` mixes counts from every binary. **One** llvm-cov run with multiple `-object` then tries to match every count to every binary; when the same function has different hashes across binaries, you get “mismatched data” and wrong/missing coverage.

So: **multi-binary merge + single llvm-cov run** is the direct cause of mismatches and of 0% for test-only code.

## Approach: Per-binary report, then merge at report level

To remove mismatches and get correct coverage for test-only (and app-only) code:

1. **Do not** merge all `.profraw` and call llvm-cov once with many `-object`.
2. **Per executable**: run llvm-cov with **only that executable** and **only the profile(s) that belong to it**:
   - Unit tests: one `.profraw` per test (e.g. `file_index_search_strategy_tests.profraw` → `file_index_search_strategy_tests`).
   - FindHelper: merge only `FindHelper_*.profraw`, then llvm-cov with FindHelper binary only.
   - `coverage_all_sources`: its own `.profraw` and binary.
3. **Export** each per-binary result to a mergeable format (e.g. **LCOV** `.info` or **llvm-cov export** JSON).
4. **Merge** those reports (e.g. take **max** line count per file/line, or use `lcov --add-tracefile`).
5. **Generate** the final summary and HTML from the merged report only.

Effects:

- **No mismatches**: each llvm-cov run sees a single binary and its own profile.
- **Correct attribution**: code run only in tests (e.g. `LoadBalancingStrategyUtils.cpp`) is reported from the test binary that ran it; app-only code from FindHelper.

## Implementation options

### Option A: LCOV pipeline (recommended)

- **Per binary**:  
  `llvm-cov export -instr-profile=<that>.profdata <that_exe> --format=lcov`  
  (or use a small script that converts llvm-cov export JSON → LCOV if your llvm-cov doesn’t emit lcov directly).
- **Merge**: `lcov --add-tracefile a.info --add-tracefile b.info ... -o merged.info` (lcov takes max per line when merging).
- **Report**: `genhtml merged.info -o report` and/or parse `merged.info` for a summary.

Requires: `llvm-cov` with `-format=lcov`, and `lcov`/`genhtml` (e.g. `brew install lcov` on macOS).

### Option B: Per-binary llvm-cov report, custom merge

- **Per binary**:  
  `llvm-cov export -instr-profile=<that>.profdata <that_exe> -ignore-filename-regex="..."`  
  → JSON per executable.
- **Custom script**: Read all JSONs; for each file/line, keep the **maximum** execution count; write merged JSON or a simple text summary.
- **Report**: Use merged data to drive HTML (e.g. existing script that consumes JSON) or a small generator.

No extra tools, but more scripting.

### Option C: Keep current script, add “split” mode

- **Current behavior** (default): merge all profraw, one llvm-cov with all `-object` (keeps current “single command” workflow; mismatches and 0% for some files remain).
- **New flag** (e.g. `--no-mismatch`):  
  - Build per-binary profdata (each test’s `.profraw` → `<test>.profdata`, FindHelper `*.profraw` → `FindHelper.profdata`).  
  - Run Option A or B (LCOV or custom merge).  
  - Generate summary/HTML from merged result only.

Gives a clear path to “no mismatches” without changing the default until the new path is validated.

## Data mapping (current layout)

- **Unit tests**: `build_tests_macos.sh` runs each test with  
  `LLVM_PROFILE_FILE="${test_name}.profraw"`  
  from the build dir → `<test_name>.profraw` in the build dir, 1:1 with executable `<test_name>`.
- **FindHelper**: `LLVM_PROFILE_FILE="$BUILD_DIR/FindHelper_%p.profraw"` → multiple `FindHelper_*.profraw`; merge these into one profdata for FindHelper.
- **coverage_all_sources**: one run → `coverage_all_sources.profraw` → one profdata.

So the mapping “which profraw(s) → which binary” is already well-defined; the script only needs to use it per binary and then merge at report level.

## Recommended next steps

1. **Implement Option A (LCOV)** in `generate_coverage_macos.sh`:
   - Add a mode (e.g. `--per-binary` or make it the default when lcov is available) that:
     - Merges per-binary profraw into per-binary profdata (and FindHelper’s `FindHelper_*.profraw` → one profdata).
     - Runs `llvm-cov export ... --format=lcov` (or JSON→lcov) for each executable.
     - Runs `lcov --add-tracefile ... -o merged.info`.
     - Generates summary/HTML from `merged.info` only.
   - Keep the current “merge all + multi-object” path under a flag if desired for comparison.
2. **Document** in this file (or in the script) that “no-mismatch” coverage uses the per-binary then LCOV-merge approach.
3. **Verify** that `LoadBalancingStrategyUtils.cpp`, `PathPatternMatcher.cpp`, and other test-only files show non-zero coverage in the merged report after this change.

## References

- LLVM issue [#72786](https://github.com/llvm/llvm-project/issues/72786): “X functions have mismatched data” with multiple binaries / shared libs; workaround suggested: “process profdata into lcov one binary at a time, then aggregate lcov.”
- llvm-dev: [Combined report for multiple executables](https://lists.llvm.org/pipermail/llvm-dev/2018-March/121705.html) — no built-in multi-binary merge; report-level merge is the way.
- Project coverage verification: `docs/analysis/2026-02-23_COVERAGE_UNIT_AND_IMGUI_CUMULATIVE_VERIFICATION.md`.
