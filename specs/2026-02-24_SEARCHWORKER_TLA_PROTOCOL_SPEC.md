## SearchWorker Request / Cancel / Discard TLA+ Protocol Spec

### 1. Context

This document captures an abstract protocol model for `SearchWorker` and its interaction with `SearchController` around:

- **Starting searches** (`StartSearch` from manual / debounced / auto-refresh paths).
- **Cancelling in-flight work** (`CancelSearch` when the user clears results while a search is running).
- **Discarding cached results** (`DiscardResults` when the worker is idle and UI wants to ensure `PollResults` will not re-apply stale data).

The real implementation lives primarily in:

- `SearchWorker` (`SearchWorker.h` / `SearchWorker.cpp`).
- `SearchController::Update` and `SearchController::PollResults`.

This TLA+ model is intentionally **small and abstract**. It focuses on:

- **Search generations** (to reason about “old vs. new” requests).
- The lifecycle of **cached non-streaming results** (`HasNewResults` / `GetResultsData`).
- A coarse model of the **streaming collector** state.

It does **not** attempt to model:

- Actual file system contents or the `FileIndex`.
- Concrete result data (paths, IDs, metadata).
- Load-balancing strategies or parallelism internal to `FileIndex`.

Instead, it provides a state machine we can model-check and from which we can derive implementation invariants and debug assertions.

---

### 2. TLA+ Model: Non-Streaming Protocol

The first module models the **non-streaming** protocol: generations, cached results, and discard semantics.

The most important invariants we identified in this session are:

- **SW1 – No superseded results**: When the worker exposes cached non-streaming results (`hasNewResults = TRUE` / `SearchWorker::HasNewResults()`), there must be no newer pending request; i.e., those results always correspond to the most recently completed search, not an older generation.
- **SW2 – No stale results after discard**: After the UI has requested a clear and the worker is idle, calling `DiscardResults` must clear both the collector and any cached non-streaming results so that subsequent `PollResults` calls cannot re-apply stale data.
- **SW3 – Collector activity matches worker activity (streaming)**: When the streaming collector exists and is “active”, the worker must be running; once the worker is idle and the collector has been marked complete or error, `DiscardResults` must reset it so the UI never sees an “active” collector while the worker is idle.

```tla
--------------------------- MODULE SearchWorkerProtocol ---------------------------

EXTENDS Naturals

(***************************************************************************)
(* STATE VARIABLES                                                        *)
(***************************************************************************)

VARIABLES
    workerState,      \* "Idle" or "Running"
    pendingReq,       \* UI has requested a new search the worker hasn't started yet
    currentGen,       \* Generation tag of the search currently running (or 0 when none)
    nextGen,          \* Next generation number to assign when a new search starts
    hasNewResults,    \* Worker has produced results for some completed search
    resultsGen,       \* Generation tag associated with those cached results
    uiResultsGen,     \* Generation currently displayed by the UI
    uiClearRequested  \* UI has requested that results be cleared (Clear All / Escape)

Gen == Nat \ {0}  \* Positive integers used as search generations

(***************************************************************************)
(* Type / shape invariant                                                 *)
(***************************************************************************)

TypeInvariant ==
    /\ workerState \in {"Idle", "Running"}
    /\ pendingReq \in BOOLEAN
    /\ currentGen \in Gen \cup {0}
    /\ nextGen \in Gen
    /\ hasNewResults \in BOOLEAN
    /\ resultsGen \in Gen \cup {0}
    /\ uiResultsGen \in Gen \cup {0}
    /\ uiClearRequested \in BOOLEAN

(***************************************************************************)
(* Initial state                                                          *)
(***************************************************************************)

Init ==
    /\ workerState = "Idle"
    /\ pendingReq = FALSE
    /\ currentGen = 0
    /\ nextGen = 1
    /\ hasNewResults = FALSE
    /\ resultsGen = 0
    /\ uiResultsGen = 0
    /\ uiClearRequested = FALSE

(***************************************************************************)
(* UI actions                                                             *)
(***************************************************************************)

\* UI requests a new search (manual, debounced, or auto-refresh).
RequestSearch ==
    /\ workerState = "Idle"
    /\ ~pendingReq
    /\ pendingReq' = TRUE
    /\ nextGen' = nextGen
    /\ UNCHANGED <<workerState, currentGen, hasNewResults, resultsGen, uiResultsGen, uiClearRequested>>

\* UI asks to clear current results while a search may be running.
UIClearWhileRunningOrIdle ==
    /\ uiClearRequested = FALSE
    /\ uiClearRequested' = TRUE
    /\ UNCHANGED <<workerState, pendingReq, currentGen, nextGen, hasNewResults, resultsGen, uiResultsGen>>

\* UI cancels a running search when clear is requested (modeled separately from worker exit).
UICancelIfRunning ==
    /\ uiClearRequested
    /\ workerState = "Running"
    /\ workerState' = "Running"   \* Cancellation request is a flag; worker will observe it later
    /\ UNCHANGED <<pendingReq, currentGen, nextGen, hasNewResults, resultsGen, uiResultsGen, uiClearRequested>>

\* UI polls and applies cached results (non-streaming path in PollResults).
UIPollApplyResults ==
    /\ hasNewResults
    /\ uiClearRequested = FALSE
    /\ uiResultsGen' = resultsGen
    /\ hasNewResults' = FALSE
    /\ UNCHANGED <<workerState, pendingReq, currentGen, nextGen, resultsGen, uiClearRequested>>

(***************************************************************************)
(* Worker actions                                                         *)
(***************************************************************************)

\* Worker takes a pending request and starts a new search with a fresh generation.
WorkerStartSearch ==
    /\ workerState = "Idle"
    /\ pendingReq
    /\ workerState' = "Running"
    /\ pendingReq' = FALSE
    /\ currentGen' = nextGen
    /\ nextGen' = nextGen + 1
    /\ UNCHANGED <<hasNewResults, resultsGen, uiResultsGen, uiClearRequested>>

\* Worker completes search normally (no cancel) and publishes results.
WorkerCompleteSearch ==
    /\ workerState = "Running"
    /\ ~pendingReq                 \* No newer request arrived mid-run
    /\ workerState' = "Idle"
    /\ hasNewResults' = TRUE
    /\ resultsGen' = currentGen    \* Results are tagged with the gen that just ran
    /\ UNCHANGED <<pendingReq, currentGen, nextGen, uiResultsGen, uiClearRequested>>

\* Worker exits early due to cancellation / superseded request; no new results are published.
WorkerCancelExit ==
    /\ workerState = "Running"
    /\ (pendingReq \/ uiClearRequested)
    /\ workerState' = "Idle"
    /\ hasNewResults' = hasNewResults  \* No new results produced
    /\ UNCHANGED <<pendingReq, currentGen, nextGen, resultsGen, uiResultsGen, uiClearRequested>>

\* When worker is idle and a clear request is pending, UI can safely discard cached results.
UIDiscardWhenIdle ==
    /\ workerState = "Idle"
    /\ uiClearRequested
    /\ hasNewResults' = FALSE
    /\ resultsGen' = 0
    /\ uiResultsGen' = 0
    /\ uiClearRequested' = FALSE
    /\ UNCHANGED <<workerState, pendingReq, currentGen, nextGen>>

(***************************************************************************)
(* Next-state relation                                                    *)
(***************************************************************************)

Next ==
    \/ RequestSearch
    \/ UIClearWhileRunningOrIdle
    \/ UICancelIfRunning
    \/ UIPollApplyResults
    \/ WorkerStartSearch
    \/ WorkerCompleteSearch
    \/ WorkerCancelExit
    \/ UIDiscardWhenIdle

(***************************************************************************)
(* Key invariants                                                         *)
(***************************************************************************)

\* I1: Cached results are never from a superseded generation.
\*     If worker publishes results, there must be no newer pending request.
NoSupersededResults ==
    hasNewResults => ~pendingReq

\* I2: Cached results, when present, are always tagged with the generation
\*     that most recently completed (currentGen at completion time).
\*     (In this abstraction, WorkerCompleteSearch sets resultsGen = currentGen.)
ResultsTagConsistent ==
    hasNewResults => resultsGen # 0

\* I3: Once the UI has discarded results while the worker is idle, no subsequent
\*     PollResults can apply stale cached data (hasNewResults is cleared).
NoStaleAfterDiscard ==
    /\ workerState = "Idle"
    /\ ~hasNewResults
    => resultsGen = 0

Inv ==
    TypeInvariant /\ NoSupersededResults /\ ResultsTagConsistent /\ NoStaleAfterDiscard

Spec ==
    Init /\ [][Next]_<<workerState, pendingReq, currentGen, nextGen, hasNewResults, resultsGen, uiResultsGen, uiClearRequested>>

=============================================================================
```

---

### 3. Streaming Collector Extension (Sketch)

The real implementation also has a **streaming** path:

- `SearchWorker` may allocate a `StreamingResultsCollector` when `stream_partial_results_` is enabled.
- `ProcessStreamingSearchFutures` feeds batches into that collector and marks it complete.
- `SearchController::PollResults` streaming branch uses `GetStreamingCollector`, handles partial batches, completion, errors, and then calls `DiscardResults` to drop the collector when appropriate.

We can extend the model with a coarse **collector state**:

```tla
--------------------------- MODULE SearchWorkerStreaming ---------------------------

EXTENDS SearchWorkerProtocol

(***************************************************************************)
(* Additional STATE VARIABLES                                             *)
(***************************************************************************)

VARIABLES
    collectorState,   \* "None", "Active", "Complete", or "Error"
    streamEnabled     \* Whether streaming is enabled for the current search

CollectorStates == {"None", "Active", "Complete", "Error"}

StreamingTypeInvariant ==
    /\ collectorState \in CollectorStates
    /\ streamEnabled \in BOOLEAN

(***************************************************************************)
(* Overrides / extensions of actions                                      *)
(***************************************************************************)

\* When worker starts a search, collectorState depends on streamEnabled.
WorkerStartSearchStreaming ==
    /\ WorkerStartSearch
    /\ IF streamEnabled
       THEN collectorState' = "Active"
       ELSE collectorState' = "None"
    /\ UNCHANGED streamEnabled

\* Worker finishes streaming search successfully.
WorkerStreamingComplete ==
    /\ workerState = "Running"
    /\ collectorState = "Active"
    /\ workerState' = "Idle"
    /\ collectorState' = "Complete"
    /\ UNCHANGED <<pendingReq, currentGen, nextGen, hasNewResults, resultsGen, uiResultsGen, uiClearRequested, streamEnabled>>

\* Worker hits an error in streaming path.
WorkerStreamingError ==
    /\ workerState = "Running"
    /\ collectorState = "Active"
    /\ workerState' = "Idle"
    /\ collectorState' = "Error"
    /\ UNCHANGED <<pendingReq, currentGen, nextGen, hasNewResults, resultsGen, uiResultsGen, uiClearRequested, streamEnabled>>

\* UI (via PollResults) discards collector when worker is idle and collector is done.
UIDiscardCollectorWhenIdle ==
    /\ workerState = "Idle"
    /\ collectorState \in {"Complete", "Error"}
    /\ collectorState' = "None"
    /\ UNCHANGED <<pendingReq, currentGen, nextGen, hasNewResults, resultsGen, uiResultsGen, uiClearRequested, streamEnabled>>

(***************************************************************************)
(* Extended Next relation (sketch)                                        *)
(***************************************************************************)

NextStreaming ==
    \/ RequestSearch
    \/ UIClearWhileRunningOrIdle
    \/ UICancelIfRunning
    \/ UIPollApplyResults
    \/ WorkerStartSearchStreaming
    \/ WorkerCompleteSearch
    \/ WorkerCancelExit
    \/ UIDiscardWhenIdle
    \/ WorkerStreamingComplete
    \/ WorkerStreamingError
    \/ UIDiscardCollectorWhenIdle

StreamingInv ==
    StreamingTypeInvariant
    /\ (collectorState = "Active" => workerState = "Running")
    /\ (workerState = "Idle" /\ collectorState = "Active" => FALSE)

StreamingSpec ==
    Init /\ [][NextStreaming]_<<workerState, pendingReq, currentGen, nextGen, hasNewResults, resultsGen, uiResultsGen, uiClearRequested, collectorState, streamEnabled>>

=============================================================================
```

This extension encodes two important streaming invariants:

- **S1 – Collector only active while worker is running**  
  `collectorState = "Active" ⇒ workerState = "Running"`.

- **S2 – Worker cannot be idle with an “Active” collector**  
  `(workerState = "Idle" ∧ collectorState = "Active")` is unreachable in valid behaviors.

These match the intent in `SearchWorker::WaitForSearchRequest` (collector constructed when a new search starts and `stream_partial_results_` is true) and in `ProcessStreamingSearchFutures` / `DrainRemainingSearchFutures`, which always call `collector.MarkSearchComplete()` (or set an error) before returning.

---

### 4. Mapping Invariants Back to C++ Code

#### 4.1 Non-Streaming Invariants

- **NoSupersededResults** (`hasNewResults ⇒ ¬pendingReq`)  

  In C++:

  ```c++
  // SearchWorker::ExecuteSearch
  {
    const std::scoped_lock lock(mutex_);
    if (!search_requested_) {
      results_data_ = std::move(results);
      has_new_results_ = true;
      is_searching_.store(false, std::memory_order_release);
      search_complete_.store(true, std::memory_order_release);
    }
  }
  ```

  `search_requested_` is the concrete analogue of `pendingReq`. The worker publishes results only when `!search_requested_`, preventing superseded results from being cached.

  **Implementation note:** We do *not* assert `hasNewResults ⇒ ¬pendingReq` inside `HasNewResults()`, because the UI can call `StartSearch()` (e.g. debounced or auto-refresh) after the worker completed but before `PollResults()` runs in the same frame, so both flags can be true at observation time. The “no superseded results” guarantee is enforced by: (1) the worker only setting `has_new_results_` when `!search_requested_` at completion time, and (2) the controller skipping the non-streaming apply when `search_worker.IsBusy()` (i.e. do not consume/apply if a new search was already requested).

- **NoStaleAfterDiscard** (after idle discard, no stale results)  

  In C++:

  ```c++
  // SearchWorker::DiscardResults
  void SearchWorker::DiscardResults() {
    const std::scoped_lock lock(mutex_);
    collector_.reset();
    results_data_.clear();
    has_new_results_ = false;
  }
  ```

  Combined with `SearchController::Update`:

  ```c++
  if (state.clearResultsRequested) {
    if (!search_worker.IsBusy()) {
      search_worker.DiscardResults();
      state.clearResultsRequested = false;
    } else {
      search_worker.CancelSearch();
    }
  }
  ```

  This ensures that when the worker is idle and the UI discards, `HasNewResults()` becomes false and `PollResults` cannot re-apply stale data.

#### 4.2 Streaming Invariants

- **S1 / S2 – Collector activity tied to worker state**  

  In `SearchWorker::WaitForSearchRequest`:

  ```c++
  collector_ = stream_enabled ? std::make_shared<StreamingResultsCollector>() : nullptr;
  ```

  Collector is created only when a new search starts (worker transitions to “running”) and `stream_partial_results_` is enabled.

  In `ProcessStreamingSearchFutures`:

  ```c++
  collector.MarkSearchComplete();
  assert(collector.IsSearchComplete() &&
         "ProcessStreamingSearchFutures must call MarkSearchComplete before return");
  ```

  And in `SearchController::PollResults` (streaming branch):

  ```c++
  if (auto* collector = search_worker.GetStreamingCollector()) {
    ...
    if (collector->IsSearchComplete()) {
      FinalizeStreamingSearchComplete(state, *collector, file_index);
      if (!search_worker.IsBusy()) {
        search_worker.DiscardResults();
      }
    }
  }
  ```

  Together, these enforce that:

  - The collector is created when a search begins (Active while worker is running).
  - It is always marked complete (or error) before `ProcessStreamingSearchFutures` returns.
  - Once the worker is idle and `DiscardResults` runs, `collector_` becomes `nullptr` (“None”).

  This matches the TLA+ streaming invariants and prevents the UI from holding on to an “Active” collector when the worker is no longer running.

---

### 5. Potential Future Work

This model is intentionally coarse. It can be extended in several directions if needed:

- Track a **generation tag for the collector** (e.g., `collectorGen`) and assert that `PollResults` never uses a collector from a superseded generation.
- Model **partial batches** and the transition from streaming partials to the final non-streaming result set (`GetAllResults` in `FinalizeStreamingSearchComplete`).
- Add simple liveness properties (e.g., if a search runs to completion without cancellation, its results are eventually either applied or discarded explicitly).

For now, this TLA+ model and the derived invariants provide a solid foundation for reasoning about the `SearchWorker` request / cancel / discard protocol and validating that the existing C++ implementation follows the intended design. 

