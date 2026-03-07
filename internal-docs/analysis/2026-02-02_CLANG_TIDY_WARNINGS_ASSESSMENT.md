# Clang-Tidy Warnings – Current State Assessment

**Date:** 2026-02-02  
**Methodology:** `python3 scripts/analyze_clang_tidy_warnings.py` from project root (uses `-p build` when `compile_commands.json` exists).

**Latest status:** Run the script and update this document with current totals. Example (2026-02-14): 204 files scanned, 12 with warnings, 192 clean, 126 total warnings.

---

## Current snapshot (2026-02-02)

| Metric | Value |
|--------|-------|
| Total files scanned | 198 |
| Files with warnings | 59 |
| Files clean | 139 |
| **Total warnings** | **370** |

---

## Progress vs last documented baseline (2026-02-02)

| Metric | 2026-02-02 (doc) | 2026-02-02 (post-Settings.h) | Change |
|--------|------------------|------------------------------|--------|
| Files scanned | 198 | 198 | — |
| Files with warnings | 61 | 59 | −2 |
| Files clean | 137 | 139 | +2 |
| Total warnings | 465 | **370** | **−95** |

**Summary:** 95 fewer warnings and 2 more files at 0 warnings since last run. Settings.h: unnamed namespace replaced with `settings_defaults` + `inline constexpr` (cert-dcl59-cpp, google-build-namespaces); redundant `{}` initializers removed (readability-redundant-member-init). Settings.h dropped from 30 to 2 (remaining 2 are likely analyzer artifacts when header is processed in isolation).

**Cumulative vs 2026-02-01:** 380 fewer warnings (750 → 370) and 35 more clean files (104 → 139). Recent: Settings.h (named namespace, inline constexpr, redundant init). Earlier: EmbeddedFont_*, ParallelSearchEngine, MftMetadataReader, SearchTypes.h, StringUtils.h, SearchPatternUtils.h, etc. Additional reduction from disabling checks for unused libraries in `.clang-tidy`.

---

## Top warning types (current)

| Count | Warning type |
|------:|--------------|
| 149 | readability-identifier-naming |
| 27 | cppcoreguidelines-pro-type-vararg, hicpp-vararg |
| 25 | readability-magic-numbers |
| 19 | cppcoreguidelines-pro-type-member-init, hicpp-member-init |
| 16 | readability-implicit-bool-conversion |
| 15 | cppcoreguidelines-avoid-const-or-ref-data-members |
| 15 | misc-non-private-member-variables-in-classes |
| 13 | google-readability-braces-around-statements, hicpp-braces-around-statements, readability-braces-around-statements |
| 12 | bugprone-branch-clone |
| 10 | cppcoreguidelines-init-variables |
| 10 | misc-const-correctness |
| 8 | bugprone-empty-catch |
| 6 | llvm-include-order |
| 4 | [nodiscard |
| 4 | cppcoreguidelines-avoid-non-const-global-variables |
| 4 | hicpp-named-parameter, readability-named-parameter |
| 4 | readability-use-std-min-max |
| 4 | llvm-else-after-return, readability-else-after-return |
| 3 | cppcoreguidelines-rvalue-reference-param-not-moved |
| 2 | cppcoreguidelines-missing-std-forward |

---

## Top 25 files by warning count (current)

| Count | File |
|------:|------|
| 39 | src/ui/SearchInputs.cpp |
| 28 | src/core/Application.h |
| 27 | src/platform/windows/DragDropUtils.cpp |
| 25 | src/ui/ResultsTable.cpp |
| 24 | src/utils/SimpleRegex.h |
| 22 | src/index/FileIndex.h |
| 22 | src/search/SearchWorker.cpp |
| 20 | src/core/Settings.cpp |
| 20 | src/index/FileIndexStorage.h |
| 20 | src/index/LazyAttributeLoader.cpp |
| 18 | src/search/SearchWorker.h |
| 18 | src/utils/StdRegexUtils.h |
| 16 | src/search/SearchController.cpp |
| 3 | src/platform/macos/ThreadUtils_mac.cpp |
| 3 | src/search/SearchInputField.h |
| 3 | src/search/SearchTypes.h |
| 3 | src/search/StreamingResultsCollector.cpp |
| 3 | src/ui/Popups.cpp |
| 3 | src/ui/StoppingState.cpp |
| 3 | src/utils/CpuFeatures.cpp |
| 2 | src/core/Settings.h |
| 2 | src/crawler/FolderCrawler.cpp |
| 2 | src/core/IndexBuilder.h |
| 2 | src/index/FileIndex.cpp |
| 2 | src/path/PathUtils.cpp |
| 2 | src/platform/linux/AppBootstrap_linux.cpp |

**Note:** Settings.h reduced from 30 to 2 (remaining 2 are analyzer artifacts when header is processed in isolation). ParallelSearchEngine.h, MftMetadataReader.cpp are no longer in top 25. FileTimeTypes.h, IndexOperations.h/cpp, EmbeddedFont_Cousine are clean (0 warnings).

---

## Trend from earliest baseline (2026-01-31)

| Snapshot | Files w/ warnings | Clean | Total warnings |
|----------|-------------------|-------|----------------|
| 2026-01-31 | 102 | 95 | 1,068 |
| Post–Logger.h | 101 | 96 | 1,008 |
| Post–4-files | 101 | 96 | 933 |
| Post–refactoring (2026-02-01) | 93 | 104 | 750 |
| Post–StringUtils/SearchPatternUtils (2026-02-02) | 89 | 108 | 690 |
| 2026-02-02 (post–config, doc) | 87 | 110 | 673 |
| 2026-02-02 (mid-day doc) | 86 | 112 | 610 |
| 2026-02-02 (doc) | 61 | 137 | 465 |
| **Current (2026-02-02, post-Settings.h)** | **59** | **139** | **370** |

**Cumulative:** 698 fewer warnings and 44 more clean files since 2026-01-31.

---

## Config changes (2026-02-02)

Checks for libraries/frameworks the project does not use were disabled in `.clang-tidy` to avoid irrelevant suggestions and NOLINT clutter:

- **abseil-\*** – Project does not use Google Abseil; removed abseil NOLINTs.
- **android-\*** – Desktop app (Windows/macOS/Linux), not Android.
- **boost-use-ranges**, **boost-use-to-string** – No Boost in application code; removed boost NOLINTs.
- **GTest / googletest** – Project uses doctest.
- **clang-analyzer-optin.mpi**, **clang-analyzer-webkit.\***, **linuxkernel-\*** – Not used.

See “Disabled Checks Rationale” in `.clang-tidy` for full list and rationale.

---

## Recommendations

1. **readability-identifier-naming (149)** – Still the largest category. Per-file NOLINT for project conventions or targeted renames; see `docs/analysis/2026-01-31_FILES_READABILITY_IDENTIFIER_NAMING.md`.
2. **readability-magic-numbers (25)** – Replace with named constants where it helps; suppress where intentional (e.g. 0, 1, dimensions).
3. **High-impact checks** – Continue with `misc-const-correctness`, `cppcoreguidelines-pro-type-member-init`, `[[nodiscard]]`, `llvm-include-order`, and brace/else-after-return as in `docs/analysis/2026-01-22_CLANG_TIDY_REDUCTION_PLAN.md`.
4. **Largest files** – SearchInputs.cpp (39), Application.h (28), DragDropUtils.cpp (27), ResultsTable.cpp (25), SimpleRegex.h (24) offer the biggest per-file gains if cleaned next.
5. **Quick wins (2–3 warnings)** – Settings.h (2), FolderCrawler.cpp (2), IndexBuilder.h (2), FileIndex.cpp (2), PathUtils.cpp (2), AppBootstrap_linux.cpp (2), and several 3-warning files are good next targets; see `docs/prompts/2026-02-02_NEXT_CLANG_TIDY_FILE_TASK.md`.

---

## How to refresh

From project root:

```bash
python3 scripts/analyze_clang_tidy_warnings.py -p build
```

Update this document with new totals and top types/files after major cleanups.
