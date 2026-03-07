# Clang-Tidy Warnings Review - 2026-02-20

## Summary

| Metric | Value |
|--------|-------|
| Total files scanned | 201 |
| Files with warnings | 0 |
| Files clean | 201 |
| Total warnings | 0 |

**Summary sentence:** All 201 scanned files are clean (0 warnings).

## Methodology

- **Script:** `scripts/run_clang_tidy.sh`
- **Config:** `.clang-tidy` (project root)
- **Build:** `build/` (`compile_commands.json`)
- **Filter:** init-statement false positives filtered via `filter_clang_tidy_init_statements.py`.
- **Note:** The `ExcludeHeaderFilterRegex` key was temporarily commented out in `.clang-tidy` as it was reported as an "unknown key" by clang-tidy version 18.1.3, preventing the analysis from running.

## Top warning types (if any)
None.

## Top files with warnings (if any)
None.

## How to refresh

From project root, with `build/compile_commands.json` present:

```bash
./scripts/run_clang_tidy.sh
```
