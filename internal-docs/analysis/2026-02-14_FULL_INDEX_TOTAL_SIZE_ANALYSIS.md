# Full-Index Total Size in Status Bar – Analysis

**Date:** 2026-02-14

## Problem

Total size does not appear in the status bar when displaying full-index content (e.g. `rs:.*` or `pp:`), while partial searches work correctly.

## Root Cause Hypotheses (from reviewer comments)

### 1. Starvation in UpdateTimeFilterCacheIfNeeded deferral

**Original behavior:** Frame-based deferral (`kMaxDeferFrames = 2000` ≈ 33s at 60 FPS) delayed cloud cleanup and time-filter cache rebuild while progressive total-size was running.

**Status:** **FIXED** – Replaced with time-based budget (250 ms). Cleanup runs at least every 250 ms regardless of frame rate.

### 2. Reset/invalidations causing perpetual restart

**Mechanism:** Frequent cache rebuilds or invalidations reset progressive computation before it completes.

**Status:** **PARTIALLY ADDRESSED** – `InvalidateDisplayedTotalSize()` is now called before mutating `filteredResults` to avoid transient `idx > results.size()`. However, invalidations still occur from:

- `HandleTableSorting` – on user sort or `resultsUpdated` re-sort (lines 46, 66, 92 in SearchResultsService.cpp)
- `CleanUpCloudFutures` – when a cloud file future completes (only when `timeFilter != None`)
- `UpdateTimeFilterCacheIfNeeded` – when time-filter cache is rebuilt (only when `timeFilter != None`)

## Flow for rs:.* / pp: (no filters)

| Step | Behavior |
|------|----------|
| `UpdateTimeFilterCacheIfNeeded` | `timeFilter == None` → returns early (line 384), no rebuild, no invalidation |
| `UpdateSizeFilterCacheIfNeeded` | `sizeFilter == None` → returns early (line 530), no rebuild |
| `UpdateDisplayedTotalSizeIfNeeded` | `display_results = &searchResults`, progressive sum at 100 loads/frame |

For 100k results: 100k / 100 = 1000 frames ≈ 17 s at 60 FPS. **Issue persists despite this.**

## Remaining Risk Factors

1. **Per-frame budget too low:** 50 loads/frame may be too conservative for very large result sets. Increasing to 100–200 for large sets would speed convergence.

2. **Sort-triggered invalidations:** When the user sorts (or `resultsUpdated` triggers re-sort), we always invalidate. The total size is invariant under reordering; we could avoid invalidation for sort-only changes, but that would require tracking whether the underlying set changed.

3. **Cloud files with time filter:** If `timeFilter != None` and there are many cloud files, `CleanUpCloudFutures` invalidates on each completion. The 250 ms starvation guard limits how often we run cleanup, but each run can still invalidate. The design intentionally defers cleanup during progressive computation; when we do run it, invalidation is required because the filter set may change.

## Recommended Next Steps

### Option A: Increase per-frame budget (low risk)

- Increase `kDisplayedTotalSizeLoadsPerFrame` from 50 to 100 or 150.
- Or scale by result set size (e.g. 50 + min(100, results.size() / 1000)).

### Option B: Add diagnostic logging (for verification)

Temporary counters to confirm behavior:

- Number of `InvalidateDisplayedTotalSize` calls during a single `rs:.*` search
- Frames of progressive accumulation before reset
- How often `UpdateTimeFilterCacheIfNeeded` returns early due to deferral

### Option C: Avoid invalidation on sort-only changes (higher complexity)

- Track whether the displayed set changed (e.g. filter rebuild) vs. only sort order.
- Skip `InvalidateDisplayedTotalSize` when only sort changed; total size is unchanged.

## Summary

The time-based starvation guard (250 ms), reset-before-mutate ordering, and increased per-frame budget (100) are in place. **The issue persists.** A likely remaining cause is sort-triggered invalidations: the table's default sort (e.g. Size column) may trigger async sort on first frame; when sort completes, `CheckAndCompleteAsyncSort` calls `InvalidateDisplayedTotalSize`, resetting progressive accumulation. If sort completes before the total-size computation finishes, we never reach `displayedTotalSizeValid = true`.

**Agent task:** See `docs/prompts/2026-02-14_FULL_INDEX_TOTAL_SIZE_ROOT_CAUSE_TASK.md` for instructions to diagnose and fix the root cause.
