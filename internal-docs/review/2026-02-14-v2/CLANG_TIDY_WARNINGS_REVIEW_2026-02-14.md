# Clang-Tidy Warnings Review - 2026-02-14

## Summary

| Metric | Value |
|--------|-------|
| Total files scanned | 204 |
| Files with warnings | 6 |
| Files clean | 198 |
| Total warnings | 46 |

**Summary sentence:** 6 files have warnings (46 total); 198 files clean.

## Methodology

- **Script:** `scripts/analyze_clang_tidy_warnings.py`
- **Config:** `.clang-tidy` (project root)
- **Build:** `build/` (`compile_commands.json`)
- **Excluded patterns:** clang-diagnostic-error, cppcoreguidelines-avoid-magic-numbers, llvmlibc-*, fuchsia, readability-uppercase-literal-suffix.

## Top warning types (if any)

| Count | Warning type |
|------:|--------------|
| 23 | readability-identifier-naming |
| 6 | misc-no-recursion |
| 4 | cppcoreguidelines-pro-type-member-init,hicpp-member-init |
| 4 | cppcoreguidelines-prefer-member-initializer |
| 3 | readability-implicit-bool-conversion |
| 2 | cppcoreguidelines-init-variables |
| 1 | cppcoreguidelines-pro-type-const-cast |
| 1 | cppcoreguidelines-pro-bounds-pointer-arithmetic |
| 1 | google-explicit-constructor,hicpp-explicit-conversions |
| 1 | misc-const-correctness |

## Top files with warnings (if any)

| Count | File |
|------:|------|
| 0 | (all previously listed files are now clean as of 2026-02-16; see "How to refresh" below to regenerate from the latest clang-tidy run) |

## How to refresh

From project root, with `build/compile_commands.json` present:

```bash
python3 scripts/analyze_clang_tidy_warnings.py
```
