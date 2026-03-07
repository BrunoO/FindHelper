# Clang-Tidy Warnings Review - 2026-03-06

## Summary

| Metric | Value |
|--------|-------|
| Total files scanned | 208 |
| Files with warnings | 0 |
| Files clean | 208 |
| Total warnings | 0 |
| Configuration Errors | 14 |

**Summary sentence:** All 208 scanned files are clean (0 warnings), but 14 configuration errors were reported due to an incompatible key in `.clang-tidy`.

## Methodology

- **Script:** `scripts/run_clang_tidy.sh`
- **Config:** `.clang-tidy` (project root)
- **Build:** `build/` (`compile_commands.json`)
- **Filter:** init-statement false positives filtered via `filter_clang_tidy_init_statements.py`.

## Top warning types (if any)

None.

## Top files with warnings (if any)

None.

## Configuration Issues

The analysis encountered 14 instances of the following error:
`error: unknown key 'ExcludeHeaderFilterRegex'`

This indicates that the version of `clang-tidy` installed (18.1.3) does not recognize this key, which was introduced in later versions or is not supported in this specific build. This prevented the intended header filtering but did not stop the analysis of the source files themselves.

## How to refresh

From project root, with `build/compile_commands.json` present:

```bash
./scripts/run_clang_tidy.sh
```
