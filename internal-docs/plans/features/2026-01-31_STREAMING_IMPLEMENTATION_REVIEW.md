# Streaming Search Results – Implementation Review

**Date:** 2026-01-31  
**Branch:** `feat-streaming-search-results-5859145357306867867`  
**Reference:** `docs/plans/features/2026-01-31_STREAMING_SEARCH_RESULTS_IMPLEMENTATION_PLAN.md`

---

## Summary

The contributor implemented **Phase 1 (Core Infrastructure)** and **Phase 2 (UI Integration)** of the streaming plan. The design matches the plan (Pipeline, Throttling/Batching, Facade, completion-order loop, GetAllPendingBatches once per frame). Several **bugs**, **compliance gaps**, and **missing behaviors** should be fixed before merge.

---

## What Was Done Well

- **StreamingResultsCollector:** Correct dual storage (all_results_ + batched pending), batching by size and time, `GetAllPendingBatches()` returns one vector and clears pending, `SetError(std::string_view)`, mutex + atomics.
- **SearchWorker:** Completion-order loop with `wait_for(0)` then `wait_for(5ms)`, cancellation drain, reuse of `ConvertSearchResultData` (DRY), `GetStreamingCollector()` and `GetResults()` preserved (Facade).
- **GuiState:** `partialResults`, `resultsComplete`, `resultsBatchNumber`, `showingPartialResults`, `searchError` added as specified.
- **Settings:** `streamPartialResults = true`, persist/load in JSON.
- **SearchController::PollResults:** Calls `GetAllPendingBatches()` once per frame when collector present; appends to `partialResults`; only on completion calls `UpdateSearchResults` (which does `WaitForAllAttributeLoadingFutures` before full replace). No wait on partial append.
- **SearchResultsService::GetDisplayResults:** Returns `partialResults` when `!resultsComplete && showingPartialResults`, else filtered/final results.
- **ResultsTable / StatusBar:** Use `GetDisplayResults(state)` and show “Searching… N” when `!resultsComplete`.
- **Reset on new search:** `partialResults.clear()`, `resultsComplete = false`, `showingPartialResults = false` when starting search (debounced, manual, auto-refresh).
- **Tests:** Batching, time-based flush, MarkSearchComplete, SetError, thread-safety with explicit lambda captures.
- **Windows (std::min)/(std::max):** Used in SearchWorker (e.g. LogPerThreadTiming).

---

## Known Crashes (Streaming Toggle and Quick Filters)

Two crash types have been observed during streaming and filtering. Fixes have been applied to mitigate them, but they are **not yet fully fixed**; this section documents symptoms, root causes identified so far, and what was done.

---

### Crash type 1: Multiple streaming toggle (on/off) + “Search now”

**Symptoms**

- Toggle “Stream search results” on or off, then click “Search now” (possibly after a second toggle).
- **First toggle + search:** May work.
- **Second toggle + search:** App can crash with **EXC_BAD_ACCESS** (e.g. in `ui::ResultsTable::Render` during `memmove`), indicating use-after-free or buffer overrun when iterating/rendering the results table.

**Findings so far**

1. **Display pointer vs. filter cache rebuild**  
   `ResultsTable::Render` used to take `display_results` (pointer to the vector to show) **before** `HandleTableSorting()`. `HandleTableSorting()` can call `UpdateTimeFilterCacheIfNeeded` / `UpdateSizeFilterCacheIfNeeded`, which **rebuild** `state.filteredResults` and `state.sizeFilteredResults`. If `display_results` pointed at one of these caches, the rebuild freed the old buffer and left `display_results` dangling; iterating over it caused the crash.  
   **Fix applied:** Take `GetDisplayResults(state)` **after** `HandleTableSorting()` in ResultsTable, and clamp `state.selectedRow` to the display vector size.

2. **Filter caches not invalidated when replacing/clearing results**  
   When `searchResults` is replaced or cleared (e.g. in `UpdateSearchResults`, `ClearSearchResults`, or in the streaming path when moving `partialResults` into `searchResults` or clearing on completion), the filter caches `filteredResults` and `sizeFilteredResults` were only marked invalid via `timeFilterCacheValid = false` and `sizeFilterCacheCacheValid = false`. The **vectors were not cleared** and **GetDisplayResults() did not check the valid flags**. So with a size or time filter active, the UI could still get a pointer to the old filter cache vectors, which contained `SearchResult` elements whose `std::string_view` members pointed into the **freed** `searchResults` buffer → use-after-free, especially visible on the **second** toggle when a new search replaced results again.  
   **Fixes applied:**
   - **GetDisplayResults()** now returns filter cache vectors only when the corresponding cache is valid (`sizeFilterCacheValid` / `timeFilterCacheValid`). When invalid, it returns `&state.searchResults` so the UI never uses stale filter caches.
   - On invalidation (in `UpdateSearchResults`, `ClearSearchResults`, PollResults streaming paths, and `ClearInputs`), we now **clear** `filteredResults` and `sizeFilteredResults` so we do not hold dangling references.

3. **Delete target vs. display**  
   Delete-key handling was updated to use the same `display_results` vector as the table, so the deleted row matches what the user sees (streaming or filtered).

**Status**

- Mitigations above reduce the chance of use-after-free from display pointer timing and from invalid filter caches.
- If crashes still occur on multiple toggles, remaining causes could include: another code path that replaces or clears results without invalidating/clearing filter caches, or a different stale reference (e.g. attribute-loading futures, or another component holding a pointer into old results).

---

### Crash type 2: Selecting quick filters (e.g. “tiny”, “small”)

**Symptoms**

- Select a quick size filter (e.g. “Tiny”, “Small”) from the UI.
- App can crash with **EXC_CRASH (SIGABRT)** during shutdown or frame teardown, often in **MetalManager::ShutdownImGui()** (macOS). The failure is consistent with a **MTLCommandEncoder** being deallocated without **endEncoding** having been called.

**Findings so far**

1. **Metal encoder lifecycle**  
   On macOS, ImGui/Metal creates a render encoder in the frame’s begin path. If the application exits mid-frame (e.g. due to an unhandled exception, or the window closing in a way that skips normal frame end), `EndFrame()` may never run, so the encoder created in `BeginFrame()` is never ended. When the encoder object is later deallocated, Metal aborts if `endEncoding` was not called.

2. **Quick filter as trigger**  
   Applying a quick filter can change which results are shown (e.g. switching to `sizeFilteredResults`), trigger a rebuild of filter caches, or cause other UI/state updates. It is plausible that in some cases this leads to an early exit path (e.g. exception or abnormal teardown) that skips proper ImGui/Metal frame end, leading to the encoder leak and subsequent SIGABRT in `ShutdownImGui()`.

3. **Fix applied**  
   In **MetalManager::ShutdownImGui()**, we now explicitly end the current render encoder if one is active (e.g. call `popDebugGroup` if used, then `endEncoding` on `current_render_encoder_`), so that shutdown does not deallocate an encoder that was never ended. This should prevent the SIGABRT in the shutdown path.

**Status**

- The shutdown fix should remove the **immediate** SIGABRT in `ShutdownImGui()` when an encoder was left active.
- If the **trigger** is an early exit or exception when applying a quick filter, that path may still need to be found and fixed (e.g. ensure all code paths call frame end, or that exceptions do not skip it). Crashes that occur **before** shutdown (e.g. during the frame where the filter is applied) would point to a different bug (e.g. use-after-free in filter/display path).

---

## Bugs and Critical Issues

### 1. **Empty catch when draining futures (AGENTS document / Sonar cpp:S2486)**

**File:** `src/search/SearchWorker.cpp` (drain loop after completion-order loop)

```cpp
for (auto idx : pending_indices) {
  if (searchFutures[idx].valid()) {
    try {
      static_cast<void>(searchFutures[idx].get());
    } catch (...) {}  // ❌ Empty catch - violates AGENTS document and cpp:S2486
  }
}
```

**Required:** Log and/or set error state; do not swallow exceptions. Example: log and call `collector.SetError(...)` if not already set, then continue draining.

---

### 2. **Error path: partial results not shown (plan 2.1.4)**

**File:** `src/search/SearchController.cpp` (streaming path when `collector->HasError()`)

**Current:** Sets `state.resultsComplete = true`, `state.showingPartialResults = false`, `state.searchActive = false`, `state.searchError = ...`. Does **not** copy partial results into the display path.

**Plan:** “On error (collector has error state): set resultsComplete, **show partial results and error indicator**; clear searchActive.”

**Issue:** With `showingPartialResults = false`, `GetDisplayResults()` returns `searchResults` (or filtered), so any partial results in `partialResults` are not shown.

**Required:** When handling collector error, make partial results visible. For example:  
`state.searchResults = std::move(state.partialResults);` then clear `state.partialResults`, set `state.resultsComplete = true`, `state.showingPartialResults = false`, `state.searchError = ...`, `state.searchActive = false`. Optionally call `WaitForAllAttributeLoadingFutures(state)` before replacing if attribute futures can reference the old `searchResults`.

---

### 3. **GetAllResults() and GetError() return references to internal state**

**File:** `src/search/StreamingResultsCollector.cpp` / `.h`

- `GetAllResults() const` returns `const std::vector<SearchResult>&` under a lock then unlocks. Callers that keep the reference (e.g. store it) could see dangling data after another thread calls `AddResult` (reallocation).  
  **Current use:** SearchWorker does `results = collector_->GetAllResults()` (immediate copy), so safe in practice.  
  **Recommendation:** Document that the reference must not be stored; or change to return by value (or a snapshot) if the API is used elsewhere.

- `GetError() const` returns `std::string_view` to `error_message_` under lock then unlock. If the caller stores the `string_view`, a later `SetError` could invalidate it.  
  **Current use:** SearchController does `state.searchError = std::string(collector->GetError())` (immediate copy), so safe.  
  **Recommendation:** Document that the view must be copied immediately; or return `std::string` for a safer API.

---

## Compliance Gaps (AGENTS document / Plan)

### 4. **Include order in SearchWorker.h**

**File:** `src/search/SearchWorker.h`

**Rule:** `#pragma once` → system includes → platform includes → project includes → forward declarations.

**Current:** Project includes (`"index/FileIndex.h"`, `"search/SearchTypes.h"`) appear before system includes (`<atomic>`, `<condition_variable>`, etc.).

**Required:** Move system includes (`<atomic>`, `<chrono>`, `<condition_variable>`, `<mutex>`, `<string>`, `<string_view>`, `<thread>`, `<vector>`) above project includes; keep forward declaration `class StreamingResultsCollector;` after includes.

---

### 5. **Catch specificity (AGENTS document / cpp:S1181)**

**File:** `src/search/SearchWorker.cpp` in `ProcessStreamingSearchFutures`

**Current:** `catch (const std::exception& e)` then `catch (...)`. Plan and AGENTS document prefer catching the most specific exception types first.

**Assessment:** Acceptable as-is (std::exception then catch-all), but the inner **drain** loop must not use an empty `catch (...)` (see issue 1).

---

## Plan Coverage and Possible Misunderstandings

### 6. **GetNewBatch() not implemented**

Plan step 1.1.1 listed `GetNewBatch()` alongside `GetAllPendingBatches()`. The implementation only provides `GetAllPendingBatches()` (single vector, clear pending). The plan’s adopted API is “GetAllPendingBatches() once per frame,” so omitting `GetNewBatch()` is **correct**; no change needed. Not a bug.

---

### 7. **ResultUpdateNotifier (plan 1.2) skipped**

Plan marks ResultUpdateNotifier as optional and allows relying on PollResults polling the collector. The branch does not add ResultUpdateNotifier and uses poll-only. That matches the plan and assessment; **no change required**.

---

### 8. **GuiState reset when starting a new search (plan 1.3.2)**

Plan: “Ensure any GuiState reset/clear logic clears partialResults and sets resultsComplete when starting a new search.”

**Current:** SearchController clears `partialResults`, sets `resultsComplete = false`, `showingPartialResults = false` when triggering debounced, manual, or auto-refresh search. GuiState.cpp `ClearInputs()` clears `partialResults`, `resultsComplete = true`, `showingPartialResults = false`.  
**Verdict:** Correct; no hole.

---

### 9. **WaitForAllAttributeLoadingFutures only on full replace (plan 2.1.1)**

Plan: Call `WaitForAllAttributeLoadingFutures(state)` only **before** the full replace (final results or clear), not before appending partial batches.

**Current:** `UpdateSearchResults()` (used for final replace) calls `WaitForAllAttributeLoadingFutures(state)` at the start. Partial appends only do `state.partialResults.insert(...)` and do not wait for attribute futures.  
**Verdict:** Correct.

---

### 10. **RunShowAllPath and streaming**

When there is no filename/path/extension query, `RunShowAllPath` is used. It pushes to the collector every 500 results and at the end calls `MarkSearchComplete()` and `results = collector_->GetAllResults()`. This is consistent with “show all” not using futures; batching at 500 matches the throttling pattern. No bug.

---

## Minor / Optional

- **StreamingResultsCollector::AddResult:** Stores the same logical result in `all_results_` (copy) and `current_batch_` (move). Intentional for “full set” vs “batched UI”; no bug.
- **Test lambda captures:** Thread-safety test uses `[&collector, t, results_per_thread]` (explicit); OK for AGENTS document.
- **Settings UI:** “Stream search results” checkbox in SettingsWindow; persists via existing JSON; OK.

---

## Recommended Fix Order

1. **Fix empty catch** in SearchWorker drain loop (log and/or set error, then continue).
2. **Fix error path** in SearchController so partial results are shown when collector has error (e.g. move partialResults into searchResults before setting flags).
3. **Fix include order** in SearchWorker.h (system includes before project includes).
4. **Document** that `GetAllResults()` and `GetError()` return references/views that must be copied immediately by the caller (or consider returning by value for safety).

---

## Checklist Before Merge

- [x] Empty catch in ProcessStreamingSearchFutures drain loop removed/handled (log + SetError, then continue).
- [x] On streaming error, partial results visible and error indicator shown (move partialResults → searchResults when non-empty).
- [x] SearchWorker.h include order corrected (system includes before project includes).
- [ ] Run `scripts/build_tests_macos.sh` (macOS) and fix any failures.
- [ ] Run clang-tidy on new/touched files; address new warnings.
- [ ] Optional: TSAN run on streaming path.

---

## Fixes Applied (Reviewer)

1. **SearchWorker.cpp** – Drain loop: replaced empty `catch (...)` with catch of `std::exception` and catch-all; both log and call `collector.SetError(...)` when not already set.
2. **SearchController.cpp** – Error path: when collector has error and `partialResults` is non-empty, call `WaitForAllAttributeLoadingFutures`, then `state.searchResults = std::move(state.partialResults)`, clear `partialResults`, then set flags so partial results are shown with error.
3. **SearchWorker.h** – Include order: system includes (`<atomic>`, `<chrono>`, …) moved above project includes (`"index/FileIndex.h"`, `"search/SearchTypes.h"`).

---

*Review completed 2026-01-31; critical fixes applied.*

**Known crashes (not yet fully fixed):** See section *Known Crashes (Streaming Toggle and Quick Filters)* above for the two crash types (multiple streaming toggle + “Search now”, and selecting quick filters), findings so far, and mitigations applied. If crashes persist, further investigation is needed on remaining code paths and triggers.
