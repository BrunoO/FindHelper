# Clang-Tidy Warnings Review - 2026-02-18

## Summary

| Metric | Value |
|--------|-------|
| Total files scanned | 199 |
| Files with warnings | 8 |
| Files clean | 191 |
| Total warnings | 54 |

**Summary sentence:** 8 files have warnings (54 total); 191 files clean.

## Methodology

- **Script:** `scripts/analyze_clang_tidy_warnings.py`
- **Config:** `.clang-tidy` (project root)
- **Build:** `build/` (`compile_commands.json`)
- **Excluded patterns:** clang-diagnostic-error, cppcoreguidelines-avoid-magic-numbers, llvmlibc-*, fuchsia, readability-uppercase-literal-suffix.

## Top warning types (if any)

| Count | Warning type |
|------:|--------------|
| 26 | readability-identifier-naming |
| 6 | misc-no-recursion |
| 4 | cppcoreguidelines-pro-type-member-init,hicpp-member-init |
| 4 | cppcoreguidelines-prefer-member-initializer |
| 3 | readability-implicit-bool-conversion |
| 2 | cppcoreguidelines-avoid-non-const-global-variables |
| 2 | cppcoreguidelines-init-variables |
| 2 | google-explicit-constructor,hicpp-explicit-conversions |
| 1 | cppcoreguidelines-pro-type-const-cast |
| 1 | cppcoreguidelines-pro-bounds-pointer-arithmetic |
| 1 | cert-dcl58-cpp |
| 1 | hicpp-use-equals-default,modernize-use-equals-default |
| 1 | misc-const-correctness |

## Top files with warnings (if any)

| Count | File |
|------:|------|
| 19 | /app/src/ui/Theme.cpp |
| 11 | /app/src/utils/LightweightCallable.h |
| 7 | /app/src/utils/SimpleRegex.h |
| 5 | /app/src/platform/windows/DragDropUtils.cpp |
| 5 | /app/src/utils/HashMapAliases.h |
| 3 | /app/src/ui/UiStyleGuards.h |
| 2 | /app/src/index/mft/MftMetadataReader.h |
| 2 | /app/src/ui/StoppingState.cpp |

## How to refresh

From project root, with `build/compile_commands.json` present:

```bash
python3 scripts/analyze_clang_tidy_warnings.py
```
