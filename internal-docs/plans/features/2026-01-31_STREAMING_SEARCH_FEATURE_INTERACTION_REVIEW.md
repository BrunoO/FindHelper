# Streaming Search: Feature Interaction Review

**Date:** 2026-01-31  
**Purpose:** Review how search entry points, filters, sorting, and UI interact on the streaming path to anticipate bugs.  
**Context:** After fixing "diminishing count when filter active + repeated Search now" (clear results on new search) and "Displayed:N wrong when filter active after search completes" (show filtered count whenever filter active).

---

## 1. Search Entry Points and What They Clear

| Entry point | When | Clears searchResults + filter caches? | Notes |
|-------------|------|---------------------------------------|--------|
| **TriggerManualSearch** ("Search now") | User clicks button / F5 | **Yes** (ClearSearchResults) | Prevents showing previous filtered count then "drop" when first batch arrives. |
| **Debounced search** (search-as-you-type) | Input changed + debounce elapsed + worker not busy | **Yes** (ClearSearchResults) | Same as manual. |
| **HandleAutoRefresh** | Index size changed + worker not busy + auto-refresh on | **Yes** (ClearSearchResults) | Same as manual. |

**Invariant:** Any path that starts a new search (manual, debounced, auto-refresh) now clears previous results and invalidates filter caches so the UI does not show stale data.

---

## 2. PollResults: Streaming vs Non-Streaming

- **Streaming path** (collector non-null): Check error → consume pending batches (append to `partialResults`) → if complete, move `partialResults` → `searchResults` and invalidate caches, or if complete with no partial (0 results) clear `searchResults` and caches.
- **Non-streaming path:** HasNewResults() → GetResults() → UpdateSearchResults or ClearSearchResults per existing logic (empty→populated, complete+0, should_update, etc.).

**Important:** When streaming, filter caches are only built from `searchResults`. During streaming we show `partialResults` (unfiltered) in the table and do not build filter caches from `partialResults`.

---

## 3. GetDisplayResults and Filter Caches

- **When** `!resultsComplete && showingPartialResults`: returns `&state.partialResults` (unfiltered).
- **Otherwise:** if filter active and cache valid → `sizeFilteredResults` or `filteredResults`; else → `searchResults`.
- Filter caches are built only from `searchResults` (and for size filter, from `filteredResults` when time filter is on). They are invalidated whenever `searchResults` is replaced.

**Implication:** While streaming, the table shows unfiltered partial results. The status bar (when a filter is active) uses filter caches built from `searchResults`. After we clear on new search, `searchResults` is empty during streaming, so filter cache helpers hit the "empty" branch and show 0.

---

## 4. Anticipated Bugs / Edge Cases

### 4.1 Status bar "Displayed: 0" during streaming with filter (minor UX)

- **Scenario:** Filter active (e.g. Tiny), user clicks Search now. We clear `searchResults`. While streaming, `GetDisplayResults` returns `partialResults` (table shows e.g. 100 rows), but StatusBar is in the filter branch and calls UpdateSizeFilterCacheIfNeeded with empty `searchResults` → empty filter cache → "Displayed: 0 (filtered from 0)".
- **Effect:** Table shows N rows, status shows "Displayed: 0 (filtered from 0)" until search completes.
- **Mitigation options:** (1) When `showingPartialResults` and a filter is active, show `partialResults.size()` in the status bar (and e.g. "(filtered count when complete)") instead of filtered count; or (2) accept one-frame/transient "0" until completion. Document and/or fix in a follow-up.

### 4.2 Sort during streaming

- **Scenario:** User clicks column header while streaming. `HandleTableSorting` runs and sorts `state.searchResults` (which is empty after we clear on new search). Filter caches invalidated. Table still shows `partialResults` (unsorted).
- **Effect:** Sort has no visible effect until search completes; then next frame re-sort uses full `searchResults`. No crash, but user might expect immediate sort of current partial list.
- **Mitigation:** Either disable sort UI while `showingPartialResults`, or document that sort applies to final result set. Optional: sort `partialResults` in-place for streaming display (could be expensive and reorder as more batches arrive).

### 4.3 Cancel search (new search started while streaming)

- **Scenario:** Search 1 streaming. User clicks Search now → ClearSearchResults + StartSearch. Worker eventually sees cancel, completes search 1 (possibly with partial results), creates new collector for search 2. PollResults: collector is for search 2; when search 2 completes we replace with its results.
- **Effect:** Old partial results from search 1 are discarded (we already cleared `partialResults` in TriggerManualSearch). Correct.
- **Note:** If we ever allowed "cancel and keep partial" without starting a new search, we would need a path that moves partial to searchResults and invalidates caches without calling StartSearch. Currently not needed.

### 4.4 Defer filter cache rebuild (first frame after complete)

- **Scenario:** Streaming completes → UpdateSearchResults(state, std::move(partialResults), true) → searchResults set, caches invalidated, deferFilterCacheRebuild = true. Next frame GetDisplayResults returns searchResults (unfiltered) because cache invalid/deferred. Following frame caches rebuilt, we show filtered.
- **Effect:** One frame of unfiltered count/list, then filtered. Brief flash possible; acceptable per current design (avoids blocking UI with heavy filter work on first frame).

### 4.5 Error path (streaming)

- **Scenario:** Collector has error. We move `partialResults` → `searchResults`, clear partial, invalidate caches, set resultsComplete/showingPartialResults/searchActive. GetDisplayResults then returns `searchResults` (the partial we kept). Status bar: no filter branch uses searchResults.size() or filter caches; filter caches were cleared so we show searchResults.
- **Effect:** Partial results and error state are consistent. **Gap:** `state.searchError` is set but **not displayed** anywhere in the UI (StatusBar, table, or popup). Users do not see the error message when a streaming search fails. See implementation plan "Gaps and possible omissions" — recommend showing searchError in status bar or a small banner when non-empty.

### 4.6 Delete row (pendingDeletions)

- **Scenario:** User deletes a row while streaming. We add path to `pendingDeletions`; row is still in `display_results` (partialResults) but rendered as deleted (strikethrough/hidden per existing logic). When search completes we replace with searchResults; deleted file may still be in searchResults until index/journal updates.
- **Effect:** Pending delete is visual only; actual removal from index is external. No change needed for streaming.

### 4.7 "(filtered from N)" during streaming with filter

- **Scenario:** Filter active, we cleared on new search. Status bar filter branch: we show "Displayed: 0" and "(filtered from 0)" because searchResults.size() is 0.
- **Effect:** Same as 4.1; "(filtered from 0)" is consistent with empty searchResults but confusing while table shows partialResults. Same mitigation options as 4.1.

---

## 5. State Transitions (Summary)

- **searchActive:** true when any search is in progress (manual, debounced, auto-refresh); set false when PollResults sees complete or error.
- **resultsComplete:** false until search completes (or error); then true.
- **showingPartialResults:** true once first streaming batch has been merged into partialResults; false after complete or when we clear for new search.
- **Filter caches:** Built from searchResults (and filteredResults for size filter). Valid only after rebuild; invalidated on replace, sort, and when starting a new search (ClearSearchResults).

---

## 6. Recommendations

1. **4.1 / 4.7 — Addressed:** When streaming (`showingPartialResults`), the status bar now shows partial count and a short label: "Displayed: N (no filters, no sort yet)". The bar matches the table (partial count), and the label explains that filters and sort apply when the search completes.
2. **4.2 — Addressed:** Same label "(no filters, no sort yet)" documents that sort (and filters) are not applied yet during streaming.
3. **Keep current behavior** for cancel, error, defer, delete; no change required for streaming correctness.
4. **Tests (doable):** Add or extend tests that (1) start a new search with filter active and assert results/counts clear then repopulate, (2) stream then complete and assert filter caches and Displayed count match filtered result set.
   - **(1)** In a test (e.g. GuiStateTests or a small integration test): create GuiState with a filter active (e.g. `state.sizeFilter = SizeFilter::Tiny`) and populate `state.searchResults` with dummy `SearchResult`s; create SearchController, SearchWorker (with `TestFileIndexFixture` + ThreadPool), AppSettings; call `TriggerManualSearch(state, search_worker, settings)`; assert `state.searchResults.empty()`, `state.timeFilterCacheValid == false`, `state.sizeFilterCacheValid == false`, `state.sortDataReady == false`. No need to wait for the search to complete—we are testing that the clear happens on start.
   - **(2)** In a test: set `state.searchResults` to a vector of `SearchResult`s whose `fileId`s exist in a test FileIndex (e.g. from `TestFileIndexFixture`); set `state.resultsComplete = true`, `state.sizeFilter` (or time filter); call `SearchResultsService::UpdateFilterCaches(state, file_index, thread_pool)` then `GetDisplayResults(state)`; assert the returned container’s size equals `state.sizeFilteredCount` (or `state.filteredCount` for time filter). The test index from `CreateTestFileIndex` does not populate file sizes; for a full size-filter test the index would need to expose sizes (or test with no filter / time filter only if the test index has mod times).

---

## 7. What `searchError` is and when it appears

**Meaning:** `state.searchError` is the error message from a **streaming search** that failed with an exception. It is set in `SearchController::PollResults` when `collector->HasError()` is true: we copy `collector->GetError()` into `state.searchError`. The collector gets that message when the **SearchWorker** calls `collector.SetError(...)` during the completion-order loop.

**When it gets set (SearchWorker, streaming path only):** In `ProcessStreamingSearchFutures`, for each ready future we call `.get()` and then convert chunk data to `SearchResult` and push to the collector. If either step throws:

1. **`searchFutures[*it].get()` throws**  
   The exception comes from the **search task** that ran in the thread pool (the code behind `FileIndex::SearchAsyncWithData`). Examples:
   - `std::bad_alloc` (out of memory during search)
   - Exceptions from the index or search implementation (e.g. I/O errors, corrupted state)

2. **`ConvertSearchResultData(data, file_index_)` throws**  
   The exception comes from **converting** a chunk result (path lookup, `GetEntry`, etc.). Examples:
   - Path storage or index access throwing (e.g. invalid id, storage error)
   - Any `std::exception` from the conversion logic

We catch `std::exception` and call `collector.SetError(e.what())`, or `catch (...)` and call `collector.SetError("Unknown search error")`. Then we set `cancel_current_search_`, drain remaining futures, and `MarkSearchComplete()`. So the search stops, partial results collected so far are kept, and the error message is stored in the collector. On the next frame, `PollResults` sees `HasError()`, copies the message to `state.searchError`, moves partial results to `searchResults`, and clears search state. **The message is never shown in the UI** (status bar, table, or popup) — that is the gap.

**When it does *not* appear:** Non-streaming search uses `ProcessSearchFutures` (no collector). Exceptions there are not turned into `searchError`; they would typically terminate or be handled elsewhere. So `searchError` is **only** for streaming search failures.

---

## 8. Post-merge (main)

Streaming implementation is merged to main. Status bar shows "Displayed: N (no filters, no sort yet)" during streaming; clear-on-new-search and filter count behaviour are correct. Remaining gap: **searchError is not shown in UI** (see §4.5 and implementation plan Gaps).

---

## 9. References

- `docs/plans/features/2026-01-31_STREAMING_SEARCH_RESULTS_IMPLEMENTATION_PLAN.md`
- `src/search/SearchController.cpp` (PollResults, TriggerManualSearch, ClearSearchResults)
- `src/search/SearchResultsService.cpp` (GetDisplayResults)
- `src/search/SearchResultUtils.cpp` (UpdateTimeFilterCacheIfNeeded, UpdateSizeFilterCacheIfNeeded)
- `src/ui/StatusBar.cpp` (center group Displayed count)
