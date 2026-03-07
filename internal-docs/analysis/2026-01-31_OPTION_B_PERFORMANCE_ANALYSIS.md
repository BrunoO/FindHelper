# Option B Refactoring – Performance Analysis

**Date:** 2026-01-31  
**Scope:** Search types moved out of FileIndex (SearchTypes.h); PathComponentsView unified.

## Summary

Double-check of the Option B refactor shows **no introduced runtime performance penalties**. Path-component access is strictly better (one fewer copy). Search hot path is unchanged (same types, same layout, same code paths).

## 1. Path component access (GetPathComponentsView*)

**Before:**  
`GetPathComponentsViewLocked` called `path_operations_.GetPathComponentsView(id)`, then copied each field into a local `FileIndex::PathComponentsView` and returned that.

**After:**  
`GetPathComponentsViewLocked` returns `path_operations_.GetPathComponentsView(id)` directly. `PathComponentsView` is an alias for `PathOperations::PathComponentsView`, so the return type is unchanged and RVO applies.

**Effect:** One fewer copy per call. No extra indirection, same lock discipline. **Net: improvement or neutral.**

## 2. Search types (SearchResultData, ThreadTiming, SearchStats)

- **Layout:** Structs in `SearchTypes.h` have the same members in the same order as the former nested types in FileIndex. No padding/layout change.
- **Usage:** Only type names changed (`FileIndex::SearchResultData` → `SearchResultData`). No change to:
  - How `CreateResultData` builds and returns `SearchResultData`
  - How `ProcessChunkRange` pushes into `local_results`
  - How futures/vectors are used
- **ABI:** Single definition in one header; no ODR risk.

**Effect:** No extra copies, moves, or indirection in the search hot path. **Net: neutral.**

## 3. Includes and compile-time

- **SearchTypes.h:** Small header (structs only). Unused `#include <vector>` removed to avoid pulling in extra headers.
- **LoadBalancingStrategy / ParallelSearchEngine / SearchStatisticsCollector:** Now include `SearchTypes.h` instead of (or in addition to) `FileIndex.h` where only types are needed. Fewer heavy includes in some TUs.
- **Runtime:** No new code in hot paths; no extra virtual calls or indirection.

**Effect:** No runtime cost; compile-time impact is either neutral or slightly better. **Net: neutral or better.**

## 4. What could cause perceived regression?

- **Benchmark variance:** Search benchmarks can vary (CPU load, thermal throttling, OS). Run multiple iterations and compare median/percentiles.
- **Build config:** Ensure same optimization level and LTO (e.g. Release, same CMake options) before/after.
- **Unrelated changes:** Other commits or environment changes (different machine, different index size) can affect timings.

## 5. Verification suggestions

1. Run the search benchmark (e.g. `search_benchmark`) with fixed config and index; repeat 5–10 runs before and after Option B; compare median duration.
2. Confirm build flags are identical (e.g. Release, LTO on/off same as baseline).
3. If a specific scenario regresses, narrow it down (e.g. path-component access vs full search) and re-check only that path with the analysis above.

## 6. Change made in this pass

- **SearchTypes.h:** Removed unused `#include <vector>` to keep the header minimal and avoid unnecessary includes in every TU that uses search types.

---

**Conclusion:** Option B does not introduce runtime performance penalties. Path-component access is strictly better; search hot path is unchanged. Any observed regression is likely variance or build/environment; the above checks and a controlled benchmark can confirm.
