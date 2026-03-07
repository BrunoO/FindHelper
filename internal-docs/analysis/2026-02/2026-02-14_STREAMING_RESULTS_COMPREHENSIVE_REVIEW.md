# Streaming Search Results – Comprehensive Review

**Date:** 2026-02-14  
**Context:** Post-fix review following Clear All and Sort bugs discovered today

---

## Executive Summary

Two critical bugs were found and fixed today. This review examines the streaming implementation for additional bugs, inconsistencies, regressions, and performance issues. **One additional bug and one potential race condition** were identified and should be addressed.

---

## 1. Bugs Fixed Today

### 1.1 Clear All in Streaming Mode (Fixed)

**Symptom:** Results remained visible after Clear All; placeholder did not show.

**Root cause:** The streaming collector persisted after Clear All. `PollResults` re-applied results from `collector->GetAllResults()` every frame.

**Fix:** `DiscardResults()` when `clearResultsRequested` is set (Clear All or Escape). Also added `searchResultPathPool.clear()` to `ClearInputs()`.

### 1.2 Sort Not Persisting in Streaming Mode (Fixed)

**Symptom:** Column sort did not work after search completed in streaming mode.

**Root cause:** Same as above – `FinalizeStreamingSearchComplete` ran every frame, overwriting `searchResults` and resetting `lastSortColumn` / `sortDataReady`.

**Fix:** Call `DiscardResults()` after `FinalizeStreamingSearchComplete` so the collector is cleared and we stop re-applying.

---

## 2. Additional Bug: Error Path Does Not Discard Collector

**Location:** `SearchController::PollResults` – `ApplyStreamingErrorToState` path

**Issue:** When a streaming search fails (`HasError()`), we apply the error state but **do not call `DiscardResults()`**. The collector persists, so every frame we re-enter the error path and re-apply the same state.

**Impact:** Low – we overwrite with identical state. No user-visible bug, but wasteful and inconsistent with the completion path.

**Recommendation:** Call `search_worker.DiscardResults()` after `ApplyStreamingErrorToState` for consistency.

---

## 3. Potential Race: DiscardResults During Active Search (Fixed – 2026-02-14)

**Scenario:** User clicks Clear All while a streaming search is still running.

**Issue:** `DiscardResults()` resets `collector_` (destroys the `StreamingResultsCollector`). The worker thread may still be in `ProcessStreamingSearchFutures` (calling `collector.AddResult()`, `collector.SetError()` on exception, or `collector_->GetAllResults()`). Destroying the collector while the worker uses it causes use-after-free and SIGABRT crash.

**Fix:** Switched `collector_` from `unique_ptr` to `shared_ptr`. In `RunFilteredSearchPath`, the worker holds a local `shared_ptr` copy for the duration of `ProcessStreamingSearchFutures` and `GetAllResults()`. Even if `DiscardResults()` or `StartSearch` resets the main `collector_`, the worker’s reference keeps the collector alive until processing completes.

**Likelihood:** Low – users typically clear after viewing results. But it is a real race.

**Recommendation:** Only call `DiscardResults()` when `!search_worker.IsBusy()`. If busy, defer discard to the next frame. For Clear All during active search, consider adding `CancelSearch()` to properly stop the worker before discarding.

---

## 4. Potential Race: DiscardResults Before Worker Finishes GetAllResults

**Scenario:** When streaming completes, `IsSearchComplete()` becomes true as soon as the worker calls `MarkSearchComplete()`. The worker then does `results = collector_->GetAllResults()` before setting `is_searching_ = false`. The UI may call `DiscardResults()` in that window.

**Issue:** Same use-after-free if we destroy the collector while the worker is in `GetAllResults()`.

**Mitigation:** Only call `DiscardResults()` when `!search_worker.IsBusy()`. The worker sets `is_searching_ = false` only after it has finished with the collector, so this avoids the race.

**Action:** Add the `IsBusy()` check before `DiscardResults()` in the completion path (and error path).

---

## 5. Design Doc vs Implementation

**Mismatch:** The design doc (`STREAMING_SEARCH_RESULTS_DESIGN.md`) states that `FinalizeStreamingSearchComplete` "moves partialResults → searchResults". The implementation uses `collector.GetAllResults()` as the canonical source, not `partialResults`.

**Rationale (from code comment):** "Accumulated partialResults can miss batches due to timing; GetAllResults is authoritative."

**Status:** Implementation is correct; design doc is outdated. Update the design doc to reflect `GetAllResults()` as the source.

---

## 6. Consistency Checklist

| Aspect | Streaming | Non-Streaming | Consistent? |
|--------|-----------|---------------|-------------|
| Clear All shows placeholder | Yes (after fix) | Yes | Yes |
| Sort persists after completion | Yes (after fix) | Yes | Yes |
| Filter caches invalidated on replace | Yes | Yes | Yes |
| Path pool cleared before replace | Yes | Yes | Yes |
| `deferFilterCacheRebuild` on completion | Yes | Yes | Yes |
| Status bar shows partial count during streaming | Yes | N/A | N/A |
| GetDisplayResults returns partialResults when streaming | Yes | N/A | N/A |

---

## 7. Performance Considerations

### 7.1 Path Pool Migration

**Location:** `ConsumePendingStreamingBatches`

When appending would reallocate the pool, we migrate to a new pool and update all `partialResults[].fullPath` string_views. This is O(n) per batch when migration occurs. For large partial result sets, this could cause frame drops.

**Mitigation:** `reserve(pool.size() + pool_bytes_needed)` before append would reduce reallocations. The current logic migrates only when `capacity < size + needed`, which is correct but may migrate frequently if batches are large.

### 7.2 Duplicate Storage

**Collector:** Holds `all_results_` (full set) and `current_batch_` / `pending_batches_` (batched view). Each result is stored in both `all_results_` and a batch. Memory overhead: ~2x during streaming.

**Acceptable:** Batching improves responsiveness; the overhead is temporary.

### 7.3 Filter Cache Rebuild

**Deferral:** `deferFilterCacheRebuild` defers the rebuild by one frame when results first arrive. Prevents blocking the UI on large result sets (especially for cloud files on Windows).

**Status:** Correctly applied in `UpdateSearchResults` for both streaming and non-streaming.

---

## 8. Edge Cases Reviewed

| Edge Case | Handling |
|-----------|----------|
| Search completes with 0 results | `FinalizeStreamingSearchComplete` clears state, sets `resultsComplete` |
| Search fails with partial results | `ApplyStreamingErrorToState` moves partial → searchResults, shows error |
| Search fails with 0 results | `ApplyStreamingErrorToState` clears, sets error |
| Clear All during streaming | `DiscardResults` (potential race – see §3) |
| Auto-refresh during streaming | `HandleAutoRefresh` requires `!IsBusy()`; won't trigger during active search |
| Index building during search | `PollResults` still runs; streaming path unaffected |
| Toggle streamPartialResults mid-search | Setting applied at next search start; current search unchanged |

---

## 9. Recommendations

### High Priority (Implemented 2026-02-14)

1. **Add `IsBusy()` check before `DiscardResults()`** in both completion and error paths to avoid the worker race. ✓
2. **Call `DiscardResults()` after `ApplyStreamingErrorToState`** for consistency and to stop re-applying error state every frame. ✓

### Medium Priority (Implemented 2026-02-14)

3. **Update design doc** – `STREAMING_SEARCH_RESULTS_DESIGN.md` should state that `FinalizeStreamingSearchComplete` uses `GetAllResults()`, not `partialResults`. ✓
4. **CancelSearch for Clear All** – When user clears during active search, call `CancelSearch()` to finish faster; guard `DiscardResults` with `IsBusy()`; skip streaming path in PollResults when `clearResultsRequested`. ✓

### Low Priority (Implemented 2026-02-14)

5. **Path pool reserve strategy** – Reserve 2x current batch size in `ConsumePendingStreamingBatches` when migrating the pool, to reduce migration frequency for large result sets. ✓

---

## 10. Files Touched in Today's Fixes

- `src/search/SearchWorker.h`, `.cpp` – `DiscardResults()`, shared_ptr collector for crash fix
- `src/search/SearchController.cpp` – `clearResultsRequested` handling, `DiscardResults` after finalize, path pool reserve in `ConsumePendingStreamingBatches`
- `src/gui/GuiState.h`, `.cpp` – `clearResultsRequested`, `searchResultPathPool.clear()` in ClearInputs
- `docs/2026-02-14_CLEAR_ALL_STREAMING_MODE_INVESTIGATION.md`

---

## References

- `docs/design/STREAMING_SEARCH_RESULTS_DESIGN.md`
- `docs/design/2026-02-14_PATH_POOL_LIFECYCLE_AND_RISKS.md`
- `docs/2026-02-14_CLEAR_ALL_STREAMING_MODE_INVESTIGATION.md`
