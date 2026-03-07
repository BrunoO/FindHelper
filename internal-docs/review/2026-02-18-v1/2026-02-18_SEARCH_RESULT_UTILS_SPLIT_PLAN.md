# 2026-02-18 Plan: SearchResultUtils.cpp split (Utility grab-bag)

## Is it relevant?

**Yes, but low priority.** The finding is valid: one ~1063-line file mixes sorting, total size computation, time/size filter caches, and metadata (display string) loading. Cohesion is low. Refactoring would improve SRP and navigability. Given **Low** severity and **4h** effort, treat as **optional**; do after higher-priority items (e.g. ResultsTable decomposition, ARCHITECTURE_REVIEW High/Medium) or when you need to extend this area.

---

## Relevance summary

| Criterion        | Assessment |
|-----------------|------------|
| **YAGNI**       | Split is not required for current features; it reduces risk when adding new filter/sort/total-size behavior. |
| **SOLID**       | Single Responsibility is violated; splitting would improve it. |
| **Effort vs benefit** | 4h for clearer modules and easier testing; no immediate user-facing gain. |
| **Risk**        | Medium: many call sites (StatusBar, ResultsTable, SearchController, SearchResultsService, ExportSearchResultsService); API must remain stable. |

**Recommendation:** Defer until a change in this area justifies the refactor, or schedule as a dedicated “cleanup” task after higher-impact refactors.

---

## If you proceed: suggested split

Keep **one public header** (`SearchResultUtils.h`) so existing call sites do not change. Move implementations into three source files that each `#include "search/SearchResultUtils.h"` and implement the declared functions.

### 1. SearchResultMetadata.cpp (metadata + filter caches)

**Responsibility:** Lazy loading and formatting of result attributes; time and size filter cache updates.

**Move here:**
- `EnsureDisplayStringsPopulated`, `EnsureModTimeLoaded`
- Helpers used only by these or by filter: `IsSentinelTime`, `IsFailedTime`, `FormatFileSize`, `FormatFileTime` (if in this file), time-filter helpers (`ApplyTimeFilter`, `CalculateCutoffTime`, `MatchesTimeFilter`, `HandleCloudFileTimeLoading`, `InitializeFilteredResults`), size-filter logic used by `UpdateSizeFilterCacheIfNeeded`
- `UpdateTimeFilterCacheIfNeeded`, `UpdateSizeFilterCacheIfNeeded`
- Shared helper used by both total-size and sort: **`EnsureSizeLoaded`** (promote from `static` to namespace-level or to a small internal header so TotalSize and Sorter can call it; or keep in Metadata and have TotalSize/Sorter call into Metadata for “load size only” / “load for sort”)

**Dependencies:** `SearchResult`, `FileIndex`, `GuiState`, `ThreadPool`, filter types, `FileTimeTypes`.

### 2. SearchResultTotalSize.cpp (displayed total size)

**Responsibility:** Compute and cache the sum of file sizes for the currently displayed results.

**Move here:**
- `UpdateDisplayedTotalSizeIfNeeded`
- Constants and progress state used only here (e.g. `kDisplayedTotalSizeLoadsPerFrame`, `kDisplayedTotalSizeScansPerFrame`, and any `ResetDisplayedTotalSizeProgress` usage that is local to this logic)

**Dependencies:** `GuiState`, `FileIndex`, `SearchResult`. Must call metadata layer for loading size when needed (e.g. `EnsureSizeLoaded` or a Metadata function that only loads size).

### 3. SearchResultSorter.cpp (sorting + async attribute loading for sort)

**Responsibility:** Column comparison, sort execution, folder-stats for FolderFileCount/FolderTotalSize columns, and async attribute loading used for sort (Size/LastModified).

**Move here:**
- `SortSearchResults`, `StartSortAttributeLoading`, `CheckSortAttributeLoadingAndSort`, `CleanupAttributeLoadingFutures`
- `CompareByColumn`, `CreateSearchResultComparator` (currently in header; can move to .cpp with explicit instantiation or keep in header for inlining — document trade-off)
- Folder-stats: `FolderStatsForSort`, `BuildFolderStatsForSort`, `GetFolderSortMetric`, `AccumulateFolderStatsForResult`
- Future/async helpers: `AreAllFuturesComplete`, `WaitBrieflyForCancelledTasks`, `CheckAndHandleSortDataReady`, `ValidateIndexAndCancellation`, `PrefetchAndFormatSortDataBlocking`, and any sort-specific formatting

**Dependencies:** `GuiState`, `FileIndex`, `SearchResult`, `ThreadPool`, `ResultColumn`, `ImGuiSortDirection`. Pre-fetch for sort uses display-string population (metadata); call into Metadata or shared `EnsureDisplayStringsPopulated` / size load.

---

## Shared pieces and dependencies

- **EnsureSizeLoaded**: Used by both total-size and sort pre-fetch. Options: (a) Implement once in `SearchResultMetadata.cpp` and declare in a small internal header (e.g. `SearchResultUtilsInternal.h`) that only the three .cpp files include, or (b) keep it in one of the three and have the other call through a thin Metadata API (“ensure size loaded for result”). Prefer (a) to avoid circular dependency.
- **Filter cache templates** (e.g. `UpdateFilterCacheForEmptyState`, `UpdateFilterCacheForNoFilter`): Used by time and size filter updates; keep in `SearchResultMetadata.cpp` (or one shared implementation file).
- **Public API**: All existing declarations stay in `SearchResultUtils.h`. No change to call sites (StatusBar, ResultsTable, SearchController, SearchResultsService, ExportSearchResultsService).

---

## Steps (if you implement)

1. **Add internal header** (optional): `SearchResultUtilsInternal.h` with `EnsureSizeLoaded` and any other shared helpers needed by more than one of the three .cpp files. Include it only from the three implementation files.
2. **Create SearchResultMetadata.cpp**: Move metadata and filter-cache implementations; build and run tests.
3. **Create SearchResultTotalSize.cpp**: Move total-size logic; ensure it uses Metadata (or internal header) for size loading; build and run tests.
4. **Create SearchResultSorter.cpp**: Move sort and async attribute-loading logic; ensure it uses Metadata for pre-fetch; build and run tests.
5. **Remove moved code from SearchResultUtils.cpp**: Delete the moved blocks; if anything remains (e.g. shared glue), keep it or move to the appropriate file. Consider deleting `SearchResultUtils.cpp` if it becomes empty and all declarations are implemented in the new files.
6. **CMakeLists.txt**: Add the three new source files to the same target(s) that currently build `SearchResultUtils.cpp` (e.g. `find_helper`, and any test/benchmark that links it).
7. **Tests**: Run full test suite (including `search_result_sort_tests`, `total_size_computation_tests`, and any test that uses time/size filter or SearchResultsService). No behavior change; only layout.

---

## Testing

- **Existing tests:** `search_result_sort_tests`, `total_size_computation_tests`; GUI and search tests that go through `SearchResultsService` or filter/sort.
- **Sanity:** Run `scripts/build_tests_macos.sh` (or Windows equivalent); manual smoke test: run app, run search, change sort column, change time/size filter, confirm total size and sort order.

---

## Effort and priority

- **Effort:** ~4 hours (matches review).
- **Priority:** Low. Prefer after ResultsTable decomposition and other High/Medium architecture items. Optionally do when adding a new filter type, new sort column, or new “displayed total” variant, so the refactor pays off immediately.
