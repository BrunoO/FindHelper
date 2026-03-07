# Clear All Behavior in Streaming Mode – Investigation

**Date:** 2026-02-14  
**Status:** Fixed (Option 1: Discard streaming collector on Clear All)

## Summary

In streaming results mode, clicking "Clear All" does not behave like non-streaming mode: results reappear immediately and the placeholder screen is not shown. This document explains the root cause.

## Observed Behavior

| Mode          | Clear All behavior                                      |
|---------------|----------------------------------------------------------|
| Non-streaming | Results cleared, placeholder shown as expected           |
| Streaming     | Results remain visible; placeholder does not appear      |

## Root Cause

### 1. Clear All does not stop the search or clear the streaming collector

When "Clear All" is clicked (`SearchInputs.cpp:301`), it calls:

- `state.ClearInputs()` – clears inputs, results, and related state
- `state.timeFilter = TimeFilter::None`

`GuiState::ClearInputs()` (`GuiState.cpp:42-118`) clears:

- Input fields, attribute loading futures, cloud file futures, Gemini API future
- `searchResults`, `partialResults`, `filteredResults`, `sizeFilteredResults`
- `searchActive = false`, `resultsComplete = true`, etc.

It does **not**:

- Stop the `SearchWorker`
- Clear or reset the `StreamingResultsCollector`
- Clear `searchResultPathPool` (possible secondary issue)

### 2. Streaming collector keeps results and is never discarded on Clear All

The `StreamingResultsCollector` is owned by `SearchWorker` and is created when a streaming search starts (`SearchWorker.cpp:662`):

```cpp
collector_ = stream_enabled ? std::make_unique<StreamingResultsCollector>() : nullptr;
```

It is only replaced when a **new** search starts. Until then, it keeps all results in `all_results_` and exposes them via `GetAllResults()`.

### 3. PollResults re-applies results from the collector every frame

`PollResults` (`SearchController.cpp:608-651`) runs once per frame from `application_logic::Update()` → `SearchController::Update()`.

**Streaming path** (when `GetStreamingCollector()` is non-null):

1. If `HasError()` → apply error state
2. `ConsumePendingStreamingBatches()` – appends pending batches to `partialResults`
3. If `IsSearchComplete()` → `FinalizeStreamingSearchComplete()` – replaces state with `collector->GetAllResults()`

Because the collector is never cleared on Clear All, it remains non-null and `IsSearchComplete()` stays true. On the next frame:

1. `PollResults` runs
2. Takes the streaming path
3. Calls `FinalizeStreamingSearchComplete()`
4. Replaces `searchResults` with `collector->GetAllResults()`

So the cleared state is overwritten immediately.

### 4. Non-streaming path does not re-apply

In non-streaming mode:

- Results are stored in `SearchWorker::results_data_`
- `GetResultsData()` returns them and clears `has_new_results_`
- After consumption, `HasNewResults()` is false
- `PollResults` returns early and does not re-apply results

There is no persistent collector that keeps results across frames.

## Frame Order

Per-frame order in `Application::ProcessFrame()`:

1. `UpdateIndexBuildState()`
2. `UpdateSearchState()` → `application_logic::Update()` → `SearchController::Update()` → **`PollResults()`**
3. `UIRenderer::RenderMainWindow()` – renders UI, including Clear All button

When the user clicks Clear All:

- **Frame N:** `PollResults` runs (state has results) → render starts → user clicks Clear All → `ClearInputs()` runs → state cleared → content area may render EmptyState briefly
- **Frame N+1:** `PollResults` runs first → re-applies from streaming collector → render shows ResultsTable again

So results reappear on the very next frame.

## Relevant Code Paths

| Component              | File                    | Relevant logic                                                                 |
|------------------------|-------------------------|---------------------------------------------------------------------------------|
| Clear All button       | `SearchInputs.cpp:301`  | Calls `state.ClearInputs()`                                                    |
| ClearInputs            | `GuiState.cpp:42-118`  | Clears results but not worker/collector                                        |
| EmptyState vs table    | `UIRenderer.cpp:113`   | `!state.searchActive && state.searchResults.empty()` → EmptyState               |
| ResultsTable           | `ResultsTable.cpp:688` | Renders if `searchActive \|\| !searchResults.empty() \|\| !partialResults.empty()` |
| PollResults streaming  | `SearchController.cpp:611-626` | Uses collector, `FinalizeStreamingSearchComplete` re-applies results      |
| Collector lifecycle    | `SearchWorker.cpp:662` | Collector created per search, replaced only on next `StartSearch`              |

## Secondary Observation: searchResultPathPool

`ClearInputs()` does not call `searchResultPathPool.clear()`, while `ClearSearchResults()` in `SearchController.cpp` does. This may leave stale path data in the pool after Clear All. It is a separate concern from the main re-population bug.

## Conclusion

The different behavior in streaming mode comes from:

1. **Persistent collector:** The streaming collector keeps all results until the next search.
2. **No Clear All handling:** Clear All does not stop the search or clear the collector.
3. **PollResults re-applies:** Each frame, `PollResults` re-applies results from the collector when `IsSearchComplete()` is true.

## Fix Applied (Option 1: Discard collector)

1. **SearchWorker::DiscardResults()** – Clears the streaming collector and non-streaming buffer so `PollResults` has nothing to re-apply.
2. **GuiState::clearResultsRequested** – Set by `ClearInputs()` (used by both Clear All button and Escape key).
3. **SearchController::Update** – At the start of each frame, if `clearResultsRequested`, calls `search_worker.DiscardResults()` and clears the flag.
4. **ClearInputs** – Also clears `searchResultPathPool` (was previously missing).

---

*Original conclusion: To fix this, Clear All would need to either:*

- Stop the search and clear the collector, or
- Introduce a “results dismissed” flag so `PollResults` does not re-apply when the user has explicitly cleared, or
- Use another mechanism that prevents re-population from the streaming collector after Clear All.
