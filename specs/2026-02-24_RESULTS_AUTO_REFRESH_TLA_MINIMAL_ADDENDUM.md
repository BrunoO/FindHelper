## TLA+ Model for Auto-Refresh “No Early Clear” Addendum

### 1. Context and Goal

This note complements `2026-02-24_RESULTS_AUTO_REFRESH_DOUBLE_BUFFERING_SPEC.md`.

That spec describes two designs for fixing the “blank table on auto-refresh” issue:

- A **full front/back double-buffering** design (sections 1–8), where `GuiState` owns explicit front and back buffers and promotion semantics.
- A **minimal alternative addendum** (section 9), which keeps today’s single buffer but changes **when** results are cleared for auto-refresh:
  - Manual and debounced searches still call `ClearSearchResults` at search start.
  - Auto-refresh **does not clear** `searchResults` at start; instead, it keeps the old results visible until `PollResults` has new data, and only then replaces the buffer in one step.

This document focuses on the **minimal alternative**. It provides:

- A small TLA+ model capturing the intended behavior (“no early clear”).
- The main invariants derived from that model.
- How those invariants map to C++ assertions in `SearchController`.

The goal is to make the auto-refresh semantics **explicit and machine-checkable** at an abstract level, and then enforce the same properties in the C++ implementation.

---

### 2. Minimal TLA+ Model (Single Buffer, No Early Clear)

The TLA+ module below models:

- A finite set of file IDs (`FileIds`) representing the abstract contents of the index.
- An abstract **index state** (`index`) and **displayed results** (`searchResults`).
- A `searchMode` that distinguishes `"Idle"`, `"Manual"`, and `"AutoRefresh"`.
- A minimal notion of “completion” via `resultsComplete`.

The key focus is that:

- **Manual** and **debounced** searches clear `searchResults` immediately at start.
- **Auto-refresh** does **not** clear `searchResults` at start; it leaves them unchanged until the moment a new full result set is ready, which then replaces `searchResults` in one step.

```tla
--------------------------- MODULE ResultsAutoRefreshMinimal ---------------------------

EXTENDS Naturals, Sequences

(***************************************************************************)
(* CONSTANTS                                                              *)
(***************************************************************************)

CONSTANT FileIds \* Finite set of file identifiers, e.g. {f1, f2, f3}

(***************************************************************************)
(* STATE VARIABLES                                                        *)
(***************************************************************************)

VARIABLES
    index,          \* Abstract index contents: subset of FileIds
    searchResults,  \* What the UI displays in non-streaming mode
    baselineResults,\* Snapshot of searchResults when auto-refresh starts
    searchMode,     \* "Idle", "Manual", or "AutoRefresh"
    resultsComplete \* TRUE when the current search run has finished

(***************************************************************************)
(* Helper definitions                                                     *)
(***************************************************************************)

SearchMode == {"Idle", "Manual", "AutoRefresh"}

\* In this minimal model, display results are exactly searchResults.
DisplayResults == searchResults

HasBaseline == baselineResults # {}  \* baselineResults is non-empty

(***************************************************************************)
(* Type / shape invariant                                                 *)
(***************************************************************************)

TypeInvariant ==
    /\ index \subseteq FileIds
    /\ searchResults \subseteq FileIds
    /\ baselineResults \subseteq FileIds
    /\ searchMode \in SearchMode
    /\ resultsComplete \in BOOLEAN

(***************************************************************************)
(* Initial state                                                          *)
(***************************************************************************)

Init ==
    /\ index \subseteq FileIds
    /\ searchResults = {}      \* No search run yet
    /\ baselineResults = {}
    /\ searchMode = "Idle"
    /\ resultsComplete = FALSE

(***************************************************************************)
(* Environment action: index changes                                      *)
(***************************************************************************)

EnvChangeIndex ==
    /\ UNCHANGED <<searchResults, baselineResults, searchMode, resultsComplete>>
    /\ index' \subseteq FileIds

(***************************************************************************)
(* Manual search: uses ClearSearchResults at start                        *)
(***************************************************************************)

StartManualSearch ==
    /\ searchMode = "Idle"
    /\ searchMode' = "Manual"
    /\ resultsComplete' = FALSE
    /\ searchResults' = {}         \* Manual search clears immediately
    /\ baselineResults' = {}       \* Not used for manual runs
    /\ UNCHANGED index

ManualComplete ==
    /\ searchMode = "Manual"
    /\ searchMode' = "Idle"
    /\ resultsComplete' = TRUE
    /\ searchResults' \subseteq index    \* Result set derived from index
    /\ baselineResults' = {}
    /\ UNCHANGED index

(***************************************************************************)
(* Auto-refresh search: minimal addendum ("no early clear")               *)
(***************************************************************************)

StartAutoRefresh ==
    /\ searchMode = "Idle"
    /\ searchResults # {}          \* Spec: only trigger after at least one run
    /\ searchMode' = "AutoRefresh"
    /\ resultsComplete' = FALSE
    /\ baselineResults' = searchResults  \* Snapshot old results
    /\ searchResults' = searchResults    \* NOTE: no early clear here
    /\ UNCHANGED index

\* While auto-refresh is in progress but worker has not produced final data.
AutoRefreshNoDataYet ==
    /\ searchMode = "AutoRefresh"
    /\ ~resultsComplete
    /\ UNCHANGED <<index, searchResults, baselineResults, searchMode, resultsComplete>>

\* Worker finishes auto-refresh and replaces front buffer in one step.
AutoRefreshComplete ==
    /\ searchMode = "AutoRefresh"
    /\ searchMode' = "Idle"
    /\ resultsComplete' = TRUE
    /\ searchResults' \subseteq index    \* New results computed from index
    /\ baselineResults' = {}             \* Baseline no longer needed
    /\ UNCHANGED index

(***************************************************************************)
(* Next-state relation                                                    *)
(***************************************************************************)

Next ==
    \/ EnvChangeIndex
    \/ StartManualSearch
    \/ ManualComplete
    \/ StartAutoRefresh
    \/ AutoRefreshNoDataYet
    \/ AutoRefreshComplete

(***************************************************************************)
(* Key invariants for the minimal addendum                                *)
(***************************************************************************)

\* 1) During auto-refresh, before completion, we never clear searchResults:
\*    it must remain equal to the baseline snapshot captured at start.
NoEarlyClearDuringAutoRefresh ==
    /\ searchMode = "AutoRefresh"
    /\ ~resultsComplete
    => searchResults = baselineResults

\* 2) When auto-refresh is active with a baseline, the displayed table is
\*    never blank (no 0-row frame) before completion.
NoBlankDisplayDuringAutoRefresh ==
    /\ searchMode = "AutoRefresh"
    /\ HasBaseline
    /\ ~resultsComplete
    => DisplayResults # {}

\* 3) Outside of auto-refresh, baselineResults must be empty.
BaselineOnlyForAutoRefresh ==
    searchMode # "AutoRefresh" => baselineResults = {}

(***************************************************************************)
(* Specification                                                          *)
(***************************************************************************)

Spec ==
    Init /\ [][Next]_<<index, searchResults, baselineResults, searchMode, resultsComplete>>

Inv ==
    TypeInvariant /\ NoEarlyClearDuringAutoRefresh /\ BaselineOnlyForAutoRefresh

=============================================================================
```

You can load this module into the TLA+ Toolbox and:

- Instantiate `FileIds` with a small finite set (e.g. `{f1, f2, f3}`).
- Ask TLC to check `TypeInvariant`, `NoEarlyClearDuringAutoRefresh`, `NoBlankDisplayDuringAutoRefresh`, and `BaselineOnlyForAutoRefresh`.

---

### 3. Invariants Derived for the C++ Implementation

From this model (and the prose spec addendum) we derived several concrete invariants that are now enforced in the C++ code, primarily in `SearchController`.

At a high level, the new invariants discovered and encoded during this session are:

- **M1 – Manual/debounced searches clear at start**: Whenever a non-auto-refresh search starts, `ClearSearchResults` must leave `GuiState::searchResults` empty before `SearchWorker::StartSearch` is called.
- **A1 – Auto-refresh does not clear at start**: When `HandleAutoRefresh` decides to trigger a refresh (auto-refresh enabled, non-empty `searchResults`, index size changed, worker idle), the call must *not* modify `searchResults`; its size is preserved until `PollResults` later replaces it with new data.
- **A2 – No blank display due to early clear**: For auto-refresh runs that started from a non-empty `searchResults`, the model never enters a state where `searchResults` has been cleared and no new data is yet available, so `GetDisplayResults(state)` does not return an empty vector purely because auto-refresh started.

#### 3.1 Manual/debounced searches clear results at start

**Intent (TLA+ view)**  
`StartManualSearch` sets `searchResults' = {}` and then later `ManualComplete` fills it with a new subset of `index`. This expresses:

- Manual and debounced searches must **start from an empty result buffer**.
- Any visible results during the run are entirely from the new search.

**C++ enforcement**  
Immediately after `ClearSearchResults` in both the debounced and manual paths we now assert that the buffer is actually empty:

```420:435:src/search/SearchController.cpp
  if (ShouldTriggerDebouncedSearch(state, search_worker)) {
    ClearSearchResults(state, "debounced search started");
    assert(state.searchResults.empty());
    state.inputChanged = false;
    state.searchActive = true;
    state.partialResults.clear();
    state.resultsComplete = false;
    state.showingPartialResults = false;
    state.searchWasManual = false; // Instant search is not manual
    search_worker.SetStreamPartialResults(settings.streamPartialResults);
    search_worker.StartSearch(state.BuildCurrentSearchParams(), &settings);
  }
```

```446:465:src/search/SearchController.cpp
  ClearSearchResults(state, "manual search started");
  assert(state.searchResults.empty());
  state.inputChanged = false; // Reset debounce state when manually searching
  state.searchActive = true;
  state.partialResults.clear();
  state.resultsComplete = false;
  state.showingPartialResults = false;
  state.searchWasManual = true; // Mark as manually triggered
  search_worker.SetStreamPartialResults(settings.streamPartialResults);
  search_worker.StartSearch(state.BuildCurrentSearchParams(), &settings);
```

These assertions encode the invariant:

- **Invariant M1**: For non-auto-refresh searches, `ClearSearchResults` must fully clear `searchResults` at search start.

#### 3.2 Auto-refresh does not clear results at start

**Intent (TLA+ view)**  
`StartAutoRefresh` takes a snapshot of the current results into `baselineResults` and explicitly sets:

- `searchResults' = searchResults`  
  (i.e., auto-refresh start does not clear the buffer).

Combined with `AutoRefreshNoDataYet`, we get:

- While in auto-refresh and not complete, `searchResults` stays equal to the baseline snapshot.

**C++ enforcement**  
In `SearchController::HandleAutoRefresh`, once all preconditions are met (auto-refresh enabled, non-empty `searchResults`, index size changed, worker idle), we:

- Remember the size of `searchResults` before starting the new search.
- Assert that the auto-refresh setup does not change that size.

```468:514:src/search/SearchController.cpp
void SearchController::HandleAutoRefresh(GuiState &state,
                                         SearchWorker &search_worker,  // NOLINT(readability-identifier-naming) - Parameter name follows project convention
                                         size_t current_index_size,
                                         const AppSettings& settings) const {
  // Auto-refresh logic (triggered by index changes, not user input).
  // NOTE:
  // - We no longer require state.searchActive to be true here.
  //   Previously, searchActive was cleared as soon as a search completed,
  //   which prevented auto-refresh from ever triggering after the first search.
  // - Instead, we require that we have existing search results. This ensures
  //   auto-refresh is only active after at least one successful search, and
  //   uses the last search parameters for refreshes.
  if (!state.autoRefresh) {
    return;
  }

  // Require an existing search result set so we know what to auto-refresh.
  if (state.searchResults.empty()) {
    return;
  }

  // Only trigger when the indexed file count has changed since the last
  // auto-refresh baseline. This baseline is updated each time we trigger.
  if (current_index_size == state.lastIndexSize) {
    return;
  }

  // Only trigger auto-refresh if the worker is not busy
  if (search_worker.IsBusy()) {
    return;
  }

  const size_t previous_results_size = state.searchResults.size();

  state.lastIndexSize = current_index_size;
  // NOTE: Do not clear results up front for auto-refresh. Keeping existing searchResults
  // visible until new data arrives avoids a transient 0-row frame (see
  // 2026-02-24_RESULTS_AUTO_REFRESH_DOUBLE_BUFFERING_SPEC.md §9).
  state.inputChanged = false;      // Reset debounce state for auto-refresh
  state.searchActive = true;
  // Keep previous completed results in searchResults to avoid a 0-row frame,
  // but clear any stale streaming partials so a new stream starts from a clean slate.
  state.partialResults.clear();
  state.resultsComplete = false;
  state.showingPartialResults = false;
  state.searchWasManual = false;   // Auto-refresh is not manual
  search_worker.SetStreamPartialResults(settings.streamPartialResults);
  search_worker.StartSearch(state.BuildCurrentSearchParams(), &settings);
  assert(state.searchResults.size() == previous_results_size);
}
```

This assertion approximates the TLA+ invariant:

- **Invariant A1 (NoEarlyClearDuringAutoRefresh)**:  
  After `HandleAutoRefresh` returns, and before any `PollResults` call with new data, the **visible buffer** (`searchResults`) must remain unchanged.

Note that the actual replacement of `searchResults` still happens later in `PollResults`, when `HasNewResults()` is true and new data has been pulled from `SearchWorker`—matching the “replace in one step when data is ready” from `AutoRefreshComplete`.

#### 3.3 No blank display during auto-refresh (conceptual)

The TLA+ invariant:

- **NoBlankDisplayDuringAutoRefresh**:
  - When in `"AutoRefresh"` mode with a non-empty baseline and not complete, `DisplayResults # {}`.

In the current C++ design:

- `GetDisplayResults` always falls back to `searchResults` unless:
  - Streaming partials are active, or
  - Valid filter caches exist and are selected instead.
- Under the minimal addendum:
  - `HandleAutoRefresh` no longer calls `ClearSearchResults`.
  - The non-streaming `PollResults` path only clears and replaces `searchResults` in the same frame when `HasNewResults()` is true and data has been pulled.

Together with the `HandleAutoRefresh` assertion on `searchResults.size()`, this ensures:

- For auto-refresh runs where there was a prior non-empty result set, **the model is never put into a “cleared but no new data yet” state**, which was the root cause of the 0-row frame.

We did not add a direct C++ assertion on `GetDisplayResults(state).empty()` here (because it intertwines controller logic with UI concerns), but the structural changes plus the invariants above enforce the necessary precondition:

- **Invariant A2 (structural)**:  
  Auto-refresh never causes `searchResults` to be emptied before new data is available, so `GetDisplayResults` will not return an empty vector due to early clear.

---

### 4. How to Extend This Model

If needed, the TLA+ model can be refined later to:

- Distinguish streaming vs. non-streaming behavior (partial results vs. complete results).
- Add an explicit `DisplayResults` function mirroring the real `SearchResultsService::GetDisplayResults`, including filter-cache validity and `deferFilterCacheRebuild`.
- Model `resultsActive` / `resultsComplete` flags more precisely and relate them to the UI’s notion of “searching vs. idle”.

For now, this minimal model plus the implemented assertions capture the core guarantees of the **no-early-clear auto-refresh** addendum:

- Manual / debounced searches still clear up front.
- Auto-refresh does not clear up front.
- Replacement of results happens only when new data is ready, avoiding the transient blank table frame.


