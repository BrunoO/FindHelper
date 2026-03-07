# Streaming Search Results – Design

**Date:** 2026-02-01  
**Status:** Implemented (merged to main)  
**Version:** 1.0

---

## Purpose and Responsibilities

Streaming search results allow the UI to show partial results incrementally while a search is still running, instead of waiting for the full result set. The implementation follows a **producer → collector → consumer** pipeline: the search worker pushes results into a collector in batches, and the UI thread pulls pending batches once per frame.

### Primary Responsibilities

1. **Incremental display** – Table and status bar show results as they arrive (e.g. "Displayed: N (no filters, no sort yet)").
2. **Batching** – Results are flushed by size (e.g. 500 items) or time (e.g. 50 ms) to avoid per-result UI updates.
3. **Final handoff** – When the search completes, partial results are replaced by the full set; sort and filters then apply to the final set.
4. **Error handling** – If the search throws, the collector stores an error message; the UI keeps partial results and the error is logged (and can be shown in UI in a follow-up).

---

## Architecture Overview

```
┌─────────────────┐     AddResult()      ┌──────────────────────────┐     GetAllPendingBatches()     ┌─────────────────┐
│  SearchWorker   │ ──────────────────► │ StreamingResultsCollector│ ◄───────────────────────────── │ SearchController│
│  (worker thread)│     MarkSearchComplete│  (mutex + atomics)       │     (once per frame, UI thread)│  (UI thread)     │
└─────────────────┘     SetError()      └──────────────────────────┘                                └────────┬────────┘
        │                              all_results_ / current_batch_ / pending_batches_                        │
        │                              HasNewBatch(), IsSearchComplete(), HasError()                           │
        │                                                                                                     ▼
        │                                                                                    GuiState: partialResults,
        │                                                                                    resultsComplete, showingPartialResults
        │                                                                                                     │
        │                                                                                                     ▼
        │                                                                                    GetDisplayResults() → ResultsTable, StatusBar
        └─────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

- **Producer:** `SearchWorker` (single background thread) runs the search, consumes futures in completion order, and for each chunk calls `ConvertSearchResultData` then `collector.AddResult(...)`. On exception it calls `collector.SetError(...)` and drains remaining futures; it always calls `collector.MarkSearchComplete()` before returning.
- **Collector:** `StreamingResultsCollector` holds `all_results_`, `current_batch_`, and `pending_batches_` under a mutex; flushes to pending by size or interval; exposes `GetAllPendingBatches()` for the UI and `GetAllResults()` for the final handoff.
- **Consumer:** `SearchController::PollResults` (UI thread, once per frame) checks `GetStreamingCollector()`; if present, calls `GetAllPendingBatches()`, appends to `state.partialResults`, and on `IsSearchComplete()` calls `FinalizeStreamingSearchComplete` which uses `collector.GetAllResults()` (not `partialResults`) as the canonical source, then replaces `state.searchResults` and discards the collector.

---

## Components and Code Locations

| Component | File(s) | Role |
|-----------|---------|------|
| **StreamingResultsCollector** | `src/search/StreamingResultsCollector.h`, `.cpp` | Batches results; producer calls `AddResult` / `MarkSearchComplete` / `SetError`; consumer calls `HasNewBatch`, `GetAllPendingBatches`, `IsSearchComplete`, `HasError`, `GetAllResults`. |
| **SearchWorker** | `src/search/SearchWorker.h`, `.cpp` | When `stream_partial_results_` is true, creates a `StreamingResultsCollector`, runs `ProcessStreamingSearchFutures` (completion-order loop), pushes to collector; exposes `GetStreamingCollector()`. |
| **SearchController** | `src/search/SearchController.cpp` | `PollResults()`: if collector non-null, handles error path (copy error, move partial → searchResults, clear state), consumes `GetAllPendingBatches()` into `partialResults`, on `IsSearchComplete()` calls `UpdateSearchResults` or clears. `TriggerManualSearch` / debounced / auto-refresh call `ClearSearchResults` and `SetStreamPartialResults(settings.streamPartialResults)`. |
| **GuiState** | `src/gui/GuiState.h`, `.cpp` | Holds `partialResults`, `resultsComplete`, `resultsBatchNumber`, `showingPartialResults`, `searchError`; reset in `ClearInputs` and at start of each search. |
| **SearchResultsService** | `src/search/SearchResultsService.cpp` | `GetDisplayResults(state)`: if `!resultsComplete && showingPartialResults` returns `&state.partialResults`; otherwise returns filter caches or `&state.searchResults`. |
| **StatusBar** | `src/ui/StatusBar.cpp` | When streaming, shows "Displayed: N" (orange) and "(no filters, no sort yet)". |
| **ResultsTable** | `src/ui/ResultsTable.cpp` | Uses `GetDisplayResults(state)` (after `HandleTableSorting`); renders whatever vector is returned. |
| **AppSettings** | `src/core/Settings.h`, `.cpp` | `streamPartialResults` (default true); persisted in JSON. |

---

## Data Flow (Streaming Path)

1. **Search start**  
   User triggers search (manual, debounced, or auto-refresh). `SearchController` calls `ClearSearchResults` (clears `searchResults`, `partialResults`, invalidates filter caches, sets `resultsComplete = false`, `showingPartialResults = false`, `sortDataReady = false`), then `search_worker.SetStreamPartialResults(settings.streamPartialResults)` and `StartSearch(...)`.

2. **Worker**  
   `SearchWorker::WorkerThread` creates a `StreamingResultsCollector` when `stream_partial_results_` is true, then runs `RunFilteredSearchPath`, which calls `ProcessStreamingSearchFutures`: for each ready future, `.get()`, convert chunk to `SearchResult`, `collector.AddResult(...)`. On exception: `collector.SetError(...)`, cancel, drain remaining futures. Always `collector.MarkSearchComplete()` at exit.

3. **Poll (every frame)**  
   `SearchController::PollResults` sees `GetStreamingCollector()` non-null. If `HasError()`, copy error to `state.searchError`, move `partialResults` → `searchResults` (or clear), invalidate caches, set `resultsComplete`, `showingPartialResults = false`, `searchActive = false`, discard collector when worker idle, return. Otherwise call `GetAllPendingBatches()`; if non-empty, append to `state.partialResults`, set `showingPartialResults = true`, `resultsComplete = false`. If `IsSearchComplete()`, call `FinalizeStreamingSearchComplete` which uses `collector.GetAllResults()` (authoritative; `partialResults` can miss batches due to timing), replaces `state.searchResults`, sets `resultsComplete`, `showingPartialResults = false`, `searchActive = false`, and discards the collector when worker is idle.

4. **Display**  
   `GetDisplayResults(state)` returns `&state.partialResults` when `!resultsComplete && showingPartialResults`; otherwise returns filtered or raw `searchResults`. ResultsTable and StatusBar use that pointer; during streaming they show partial count and "(no filters, no sort yet)".

5. **Completion**  
   After replace, `HandleTableSorting` and filter cache rebuilds run as in the non-streaming path; sort and filters apply to the final `searchResults`.

---

## State Invariants (Debug Assertions)

The following invariants are asserted in Debug builds (see implementation):

- **StreamingResultsCollector:** `AddResult` is never called after `search_complete_` is true (no add after MarkSearchComplete/SetError). After `MarkSearchComplete` or `SetError`, `search_complete_` is true; after `SetError`, `has_error_` is true. After `FlushCurrentBatch` (non-empty), `pending_batches_` is non-empty.
- **SearchWorker:** On exit from `ProcessStreamingSearchFutures`, `collector.IsSearchComplete()` is true (MarkSearchComplete always called).
- **SearchController (streaming branch):** On entry, `!(state.showingPartialResults && state.resultsComplete)`. After handling error: `state.resultsComplete && !state.showingPartialResults && !state.searchActive && !state.searchError.empty()`. After handling complete: `state.resultsComplete && !state.showingPartialResults && !state.searchActive`.
- **GetDisplayResults:** When returning `&state.partialResults`, `!state.resultsComplete && state.showingPartialResults`.

---

## Threading

- **Producer:** Only the SearchWorker thread calls `AddResult`, `MarkSearchComplete`, `SetError` on the collector.
- **Consumer:** Only the UI thread (from `PollResults`) calls `HasNewBatch`, `GetAllPendingBatches`, `IsSearchComplete`, `HasError`, `GetError`, `GetAllResults`. `GetDisplayResults` and table/status bar rendering run on the UI thread.
- **Collector:** Synchronization is via `mutex_` for shared vectors and atomics (`has_new_batch_`, `search_complete_`, `has_error_`) for lock-free reads where appropriate.

---

## Settings and Feature Flag

- **AppSettings::streamPartialResults** (default `true`): When true, the next search uses the streaming path (collector created, completion-order loop, PollResults consumes batches). When false, the worker uses the traditional path (process all futures in order, then set results once).
- The flag is applied at search start; changing it mid-search does not affect the current search.

---

## Error Handling and Logging

- **Collector error:** Set via `SetError(message)` when a search future throws (or `ConvertSearchResultData` throws). Message is stored and `HasError()` / `GetError()` expose it. `PollResults` copies it to `state.searchError`; partial results are kept and search state is cleared.
- **Logging:** When an exception is caught in `ProcessStreamingSearchFutures`, `LOG_ERROR("Search future threw exception: " + message)` or `LOG_ERROR("Search future threw unknown exception")` is emitted. So failures are logged at the source; `searchError` is available for future UI display.

---

## Functions Reference

Functions involved in the streaming pipeline, with file, purpose, callers, and invariants.

### StreamingResultsCollector (`src/search/StreamingResultsCollector.h`, `.cpp`)

| Function | Purpose | Caller / thread |
|----------|---------|-----------------|
| **Constructor** `StreamingResultsCollector(size_t batch_size, uint32_t notification_interval_ms)` | Creates collector with batch size (default 500) and flush interval in ms (default 50). | SearchWorker (worker thread), when starting a streaming search. |
| **AddResult**(`SearchResult&& result`) | Appends one result to `all_results_` and `current_batch_`; flushes batch when size or interval reached. **Precondition:** must not be called after `MarkSearchComplete` or `SetError` (assert in Debug). | SearchWorker only, from `ProcessStreamingSearchFutures` or the non-streaming handoff path. |
| **MarkSearchComplete**() | Marks search finished: flushes current batch, sets `search_complete_` true. Must be called exactly once per search (normal or error exit). | SearchWorker, at end of `ProcessStreamingSearchFutures` or equivalent path. |
| **SetError**(`std::string_view error_message`) | Stores error message, sets `has_error_` and `search_complete_` true, flushes current batch. Used when a search future throws. | SearchWorker, in `ProcessStreamingSearchFutures` catch block. |
| **HasNewBatch**() | Returns true if there is at least one pending batch for the UI. Lock-free (atomic). | SearchController::PollResults (UI thread). |
| **GetAllPendingBatches**() | Moves all pending batches into a single vector, clears `pending_batches_`, clears `has_new_batch_`. Call once per frame from UI. | SearchController::PollResults (UI thread). |
| **GetAllResults**() | Returns const reference to `all_results_` (every result collected). Used for final handoff when search completes. | SearchController (when finalizing streaming complete). |
| **IsSearchComplete**() | True after `MarkSearchComplete` or `SetError`. | SearchController::PollResults (UI thread). |
| **HasError**() / **GetError**() | True after `SetError`; returns stored error message. | SearchController::PollResults (UI thread). |
| **FlushCurrentBatch**() (private) | Moves `current_batch_` into `pending_batches_`, clears `current_batch_`, sets `has_new_batch_` true. Called by AddResult (when batch full or interval elapsed), MarkSearchComplete, SetError. | Internal only. |

### SearchWorker (`src/search/SearchWorker.h`, `.cpp`)

| Function | Purpose | Caller / thread |
|----------|---------|-----------------|
| **GetStreamingCollector**() | Returns pointer to the current `StreamingResultsCollector` if streaming is active, else `nullptr`. | SearchController::PollResults (UI thread), once per frame. |
| **DiscardResults**() | Clears the collector and result buffer. Call when user clears (Clear All) so PollResults does not re-apply. Only call when `!IsBusy()` to avoid use-after-free. | SearchController::Update (when clearResultsRequested and worker idle). |
| **CancelSearch**() | Sets `cancel_current_search_` so the worker exits early. Call when Clear All is clicked during active search to finish faster before discard. | SearchController::Update (when clearResultsRequested and worker busy). |
| **SetStreamPartialResults**(bool) | Sets `stream_partial_results_` for the next search. When true, worker creates a collector and uses completion-order streaming. | SearchController (Update, TriggerManualSearch, HandleAutoRefresh) at search start. |
| **ProcessStreamingSearchFutures**(`searchFutures`, `collector`, `searchTimer`, `searchTimeMs`) | Completion-order loop: waits for any future (e.g. `wait_any`), gets chunk, converts via `ConvertSearchResultData`, calls `collector.AddResult(...)`. On exception: `collector.SetError(...)`, cancel and drain remaining futures. **Always** calls `collector.MarkSearchComplete()` before return (assert in Debug). | Worker thread, from `RunFilteredSearchPath` when streaming. |
| **WorkerThread**() | Main loop: on search request, creates `StreamingResultsCollector` if `stream_partial_results_`, runs `RunFilteredSearchPath` (which calls `ProcessStreamingSearchFutures`) or traditional path, then notifies completion. | Own background thread. |

### SearchController (`src/search/SearchController.cpp`)

| Function | Purpose | Caller / thread |
|----------|---------|-----------------|
| **PollResults**(GuiState&, SearchWorker&) | Main polling entry: if `GetStreamingCollector()` non-null, runs streaming branch. **Entry invariant (Debug):** `!(state.showingPartialResults && state.resultsComplete)`. If `collector.HasError()`, calls `ApplyStreamingErrorToState`; else consumes batches via `ConsumePendingStreamingBatches`; if `collector.IsSearchComplete()`, calls `FinalizeStreamingSearchComplete`. Non-streaming path: uses `HasNewResults()` / `GetResults()` and `ApplyNonStreamingSearchResults`. | SearchController::Update (UI thread), once per frame. |
| **ApplyStreamingErrorToState**(state, collector) | Copies `collector.GetError()` to `state.searchError`; if partial results exist, moves them to `state.searchResults`; clears filter caches; sets `resultsComplete`, `showingPartialResults = false`, `searchActive = false`. **Postcondition (Debug):** `resultsComplete && !showingPartialResults && !searchActive && !searchError.empty()`. | PollResults (streaming branch). |
| **ConsumePendingStreamingBatches**(state, collector) | Calls `collector.GetAllPendingBatches()`; if non-empty, appends to `state.partialResults`, sets `showingPartialResults = true`, `resultsComplete = false`, clears `searchError`, increments `resultsBatchNumber`, sets `resultsUpdated`. | PollResults (streaming branch). |
| **FinalizeStreamingSearchComplete**(state, collector, file_index) | Uses `collector.GetAllResults()` as canonical source (not `partialResults`; accumulated partial can miss batches). Replaces `searchResults` via `UpdateSearchResults`, clears filter caches, sets `resultsComplete`, `showingPartialResults = false`, `searchActive = false`, resets sort state. **Postcondition (Debug):** `resultsComplete && !showingPartialResults && !searchActive`. | PollResults (streaming branch). |
| **Update**(state, search_worker, monitor, is_index_building, settings) | Frame entry: may call `ClearSearchResults`, set `partialResults`/`resultsComplete`/`showingPartialResults`, `SetStreamPartialResults(settings.streamPartialResults)`, `StartSearch`; then always calls `PollResults`. | Main loop (UI thread). |
| **ClearSearchResults**(state, reason) | Clears `searchResults`, partialResults (at search start), filter caches, sort state. Used at start of debounced/manual/auto-refresh search. | Update, TriggerManualSearch, HandleAutoRefresh. |

### SearchResultsService (`src/search/SearchResultsService.cpp`)

| Function | Purpose | Caller / thread |
|----------|---------|-----------------|
| **GetDisplayResults**(const GuiState& state) | Returns pointer to the vector to display. **Streaming:** if `!state.resultsComplete && state.showingPartialResults`, returns `&state.partialResults` (assert in Debug). Otherwise returns filtered cache or `&state.searchResults`. | ResultsTable (after HandleTableSorting), StatusBar (UI thread). |

### GuiState fields used by streaming (`src/gui/GuiState.h`)

| Field | Type | Meaning |
|-------|------|--------|
| **partialResults** | `std::vector<SearchResult>` | Incremental results during streaming; appended each frame from `GetAllPendingBatches()`. |
| **resultsComplete** | bool | True when search is finished (streaming or not); false while streaming. |
| **resultsBatchNumber** | uint64_t | Incremented each time new batches are appended to partialResults (optional UI hint). |
| **showingPartialResults** | bool | True when the table is showing `partialResults` (no sort/filter yet). |
| **searchError** | std::string | Set from `collector.GetError()` when streaming search fails; available for UI display. |

Inline comments and parameter descriptions for these functions live in the corresponding headers (e.g. `StreamingResultsCollector.h`, `SearchWorker.h`, `SearchController.h`, `SearchResultsService.h`).

---

## Related Documents

| Document | Description |
|----------|-------------|
| `docs/plans/features/2026-01-31_STREAMING_SEARCH_RESULTS_IMPLEMENTATION_PLAN.md` | Step-by-step implementation plan (phases, compliance, exit criteria). |
| `docs/plans/features/2026-01-31_STREAMING_SEARCH_FEATURE_INTERACTION_REVIEW.md` | Entry points, PollResults behaviour, GetDisplayResults, filter caches, edge cases (sort during streaming, cancel, error, searchError). |
| `docs/plans/features/2026-01-31_STREAMING_IMPLEMENTATION_REVIEW.md` | Implementation review and crash fixes (dangling pointers, filter cache invalidation). |
| `docs/design/PARALLEL_SEARCH_ENGINE_DESIGN.md` | How search futures are produced (`SearchAsyncWithData`). |

---

*Last updated: 2026-02-01*
