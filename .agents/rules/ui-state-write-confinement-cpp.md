---
description: Enforce UI-state write confinement and staged async apply
globs: "**/*.cpp,**/*.h,**/*.hpp,**/*.cc,**/*.cxx"
alwaysApply: false
paths:
  - "**/*.cpp"
  - "**/*.h"
  - "**/*.hpp"
  - "**/*.cc"
  - "**/*.cxx"
---

# UI State Write Confinement

- `GuiState` display/result containers are UI-thread owned; worker threads must not mutate them directly.
- Background tasks may only write to dedicated staging buffers/queues with explicit synchronization.
- Apply staged updates on the UI thread in one commit phase.
- Cancellation/reset paths must clear staged state to prevent stale-generation apply.

## GuiState substates and ownership map (god-object decomposition, Phase 0/1)

- `GuiState` is being split into cohesive substate structs; the single-writer rule is
  now codified in the **FIELD OWNERSHIP MAP** at the top of `src/gui/GuiState.h`:
  before adding a write site for any state field, declare it in that map.
- Current substates and their sole writers: `SelectionState` (ResultsTable),
  `SearchCriteria` (input widgets; value type in `SearchCriteria.h`),
  `ResultFilterCaches` (SearchController via SearchResultUtils helpers),
  `SearchPipelineState` (SearchController; documented co-writer:
  `Application::UpdateSearchState` consumes `pending_manual_history_record` via
  `ConsumeManualHistoryRecord()` and clears `search_was_manual`),
  `ExportWorkflowState` (export service + export popup),
  `GeminiWorkflowState` (AI-assisted search panel), `HistoryInteractionState`
  (SearchHistoryWindow + popups), `AsyncSortState` (SearchController + ResultsTable),
  `IndexBuildProgressState` (Application::UpdateIndexBuildState), `ResultPoolOwner`
  (SearchController Clear/BumpBatchNumber protocol), `CloudFileWorkflowState`
  (SearchController/SearchResultUtils enqueue + CleanUpCloudFutures; teardown
  paths use `WaitAndDrain()`).
- New features must not add flat fields to `GuiState`; extend the matching substate
  or propose a new one per the plan
  (`internal-docs/plans/2026-09-11_GUISTATE_GOD_OBJECT_DECOMPOSITION_PLAN.md`).
- `UIThreadOwned<T>` debug asserts enforce UI-thread-only mutation of the result pool;
  render code must use const accessors.

## Pattern

```cpp
// ✅ Worker: write staged values only
{
  std::scoped_lock lock(state.pending_mutex_);
  state.pending_[i] = value;
}

// ✅ UI thread: apply once after completion
ApplyPendingToResults(state, results);
```
