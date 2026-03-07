# Clang-Tidy Warnings Review - 2026-02-19

## Summary

| Metric | Value |
|--------|-------|
| Total files scanned | 199 |
| Files with warnings | N/A |
| Files clean | N/A |
| Total warnings | N/A |

**Summary sentence:** Analysis could not be completed due to `clang-tidy` (v18.1.3) segmentation faults when parsing project headers in the current environment.

## Methodology

- **Script:** `scripts/run_clang_tidy.sh`
- **Config:** `.clang-tidy` (project root)
- **Build:** `build/` (`compile_commands.json`)
- **Status**: Failed (Tool crash).

## Top warning types (if any)

*None (Analysis failed)*

## Top files with warnings (if any)

*None (Analysis failed)*

## How to refresh

From project root, with `build/compile_commands.json` present:

```bash
./scripts/run_clang_tidy.sh
```

## Note on Tool Stability
`clang-tidy` v18.1.3 encountered a segmentation fault during the parsing phase of multiple source files (including `ResultsTable.cpp` and `StringUtils_linux.cpp`). This appears to be an environment-specific issue with the interplay between Clang 18 and the system headers or specific C++17 constructs used in the project. Previous successful runs (e.g., 2026-02-14) utilized `scripts/analyze_clang_tidy_warnings.py` which may have used a different clang-tidy version or environment.
