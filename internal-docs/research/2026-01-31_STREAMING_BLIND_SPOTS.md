# Results Streaming – Blind Spots and Open Questions

**Date:** 2026-01-31  
**Purpose:** Identify gaps, risks, and under-specified areas in the results streaming design  
**Reference:** `docs/research/PARTIAL_RESULTS_STREAMING_ANALYSIS.md`

---

## Summary

The streaming design is well scoped for core flow, load balancing, and optional full-result-set behaviour. The items below are **blind spots**: areas that are under-specified, not yet coordinated with other plans, or need explicit decisions before or during implementation.

---

## 1. Cancellation + completion-order consumption

**Gap:** The design says "Search Cancelled → Collector stopped receiving results, last batch marked as final, UI shows partial results received so far." It does not specify:

- **How** the completion-order loop (in SearchWorker) observes cancellation: check `cancel_current_search_` each time we take a future, or only between futures?
- **When** we stop: do we stop feeding the collector immediately and mark complete, or drain already-ready futures first?
- **What the UI shows:** keep partial results already delivered, or clear and show "Search cancelled"?

**Recommendation:** Define cancellation semantics (e.g. "stop loop on next check; mark collector complete; keep partial results in UI with a 'Cancelled' state") and document in the analysis doc.

---

## 2. Result limiting (max_results) and streaming

**Gap:** `LARGE_RESULT_SET_DISPLAY_PLAN.md` proposes a `max_results` (or similar) cap in SearchParams. The streaming doc does not describe:

- Behaviour when the cap is hit **mid-stream**: stop accepting new results into the collector? Stop launching or processing further work?
- Whether workers should be told to stop after N results (cooperative) or whether the collector simply stops pushing to the UI after N (and still accumulates in the background).

**Recommendation:** When/if result limiting is added, define: (a) whether the cap is enforced in the collector, in the workers, or both, and (b) how streaming UI behaves (e.g. "Showing first N of M (limit reached)").

---

## 3. SearchController::PollResults and streaming API

**Gap:** Today `PollResults()` does: `HasNewResults()` → `GetResults()` (one shot). With streaming the model is: multiple "new batch" events until "complete."

- **Polling model:** Does the UI call `HasNewBatch()` / `GetNewBatch()` every frame? Multiple times per frame? Is there a single "has new results" that is true for "any new batch or complete"?
- **Coalescing:** If two batches become available between frames, do we deliver one batch or two? Does the collector coalesce into one "new batch" or expose each?
- **Attribute loading:** Current code does `WaitForAllAttributeLoadingFutures(state)` before replacing results. With incremental updates, do we wait for attribute futures before each partial update, or use a different rule?

**Recommendation:** Specify the exact polling contract (e.g. "once per frame, get all pending batches and append to partialResults; on complete, replace with final set") and how it interacts with `WaitForAllAttributeLoadingFutures` and sort/display logic.

---

## 4. Toggling "stream partial results" during a search

**Gap:** Behaviour is unspecified when the user turns streaming **off** (or on) while a search is in progress.

- Do we switch mode mid-search (e.g. stop pushing partial updates and buffer until complete)?
- Or do we apply the new setting only to the **next** search?

**Recommendation:** Decide and document: either "setting applies only to next search" (simplest) or define mid-search behaviour explicitly.

---

## 5. Default for streamPartialResults

**Gap:** The doc says the default "can be true or false" but does not recommend one.

- **true:** Better perceived performance; first results sooner; more moving parts.
- **false:** Matches current behaviour; simpler; no partial UI.

**Recommendation:** Choose a default (e.g. `true` for GUI, `false` for CLI/headless if we ever detect that) and document the rationale.

---

## 6. Peak memory and streaming

**Gap:** The doc lists "Memory spike – All results held in memory before display" as a current issue. With streaming we still accumulate the full result set in the collector until the search completes; only **when** results are visible changes, not peak memory.

- The doc does not state clearly that streaming improves **time to first result**, not **peak memory**.
- If result limiting is added later, peak memory can be capped; streaming and limiting are complementary.

**Recommendation:** Add one sentence to the analysis doc: "Streaming does not reduce peak memory for the full result set; it improves time-to-first-result and responsiveness. Peak memory reduction would come from result limiting or cursor-based approaches."

---

## 7. Completion-order loop efficiency (C++17)

**Gap:** The design suggests polling with `future.wait_for(std::chrono::milliseconds(0))` to see which future is ready. A tight loop can burn CPU if no future is ready.

- No mention of back-off (e.g. `wait_for(10ms)` on a single future or a short sleep) when none are ready.
- No guidance on fairness (e.g. avoiding one slow future delaying others in the loop).

**Recommendation:** Specify a concrete loop: e.g. "if no future is ready, block with wait_for(5ms) on the first pending future, then re-check all; or use a small dedicated thread per future that blocks on .get() and pushes to the collector." Document in the "How It Fits with Load Balancing Strategies" implementation note.

---

## 8. Error handling when a future throws

**Gap:** If we consume futures in completion order and one future throws (e.g. exception in a worker), we may already have pushed other futures' results to the collector.

- Do we mark the search as "complete with error" and show partial results?
- Do we clear partial results and show an error message?
- How do we avoid leaking or double-consuming other futures?

**Recommendation:** Define: (a) that worker exceptions are caught and translated (e.g. to a failed state), (b) whether partial results are kept or cleared on error, and (c) that the completion-order loop uses try/catch around `.get()` and stops cleanly.

---

## 9. Empty first batches

**Gap:** With static or hybrid chunking, the first futures to complete might return 0 results (no matches in those chunks).

- Do we still notify the UI ("Searching... 0 results so far") so the user sees activity?
- Or do we skip notification until the first non-empty batch (simpler, but "time to first result" could be delayed)?

**Recommendation:** Decide and document: e.g. "Notify on every batch (including empty) so status can show 'Searching... N results'; N may be 0 initially."

---

## 10. Saved searches and re-run

**Gap:** No explicit statement that when the user runs a **saved search**, streaming behaves the same as for an ad-hoc search (same setting, same API).

**Recommendation:** One line in the analysis doc: "Streaming applies to all searches started via SearchWorker (ad-hoc and saved) when the option is enabled."

---

## 11. CLI / headless and forcing full result set

**Gap:** The doc says batch/scripting use cases need the full result set. It does not say how we **detect** or **configure** that (e.g. no GUI, or a CLI flag).

- If there is (or will be) a CLI or headless entry point, should streaming be off by default there, or forced off?
- Is the only mechanism the user-visible setting (e.g. in a config file), or do we need a separate "headless" or "export" mode?

**Recommendation:** If the project has or plans a CLI/headless path, document that streaming is disabled (or configurable) there and how (e.g. default `streamPartialResults = false` when no GUI, or an explicit flag).

---

## 12. Metrics and observability for streaming

**Gap:** Success criteria mention "first results in &lt;100ms" but the doc does not specify:

- New metrics: e.g. `time_to_first_result_ms`, `batches_delivered`, `partial_count_at_complete`.
- Where they are updated (SearchWorker, collector, GuiState) and whether they are exposed in the metrics UI or logs.

**Recommendation:** Add a short "Streaming metrics" subsection: which counters/timers to add, and how they support tuning and validation.

---

## 13. Accessibility (screen readers)

**Gap:** No mention of how partial updates are announced (e.g. "Searching... 500 results" then "1,000 results" then "Search complete. 10,000 results").

**Recommendation:** If the project has accessibility requirements, add a note: partial result count and "search complete" should be announced when streaming is on; defer details to UI/accessibility spec.

---

## 14. Lazy loading and index consistency during long searches

**Gap:** Partial results hold references (e.g. IDs) into the FileIndex. During a long search, the index might be updated (e.g. file removed, re-crawl).

- The doc says lazy loading continues unchanged; it does not state that streamed results may reference entries that are still valid for the duration of the search (or define what happens if the index is mutated mid-search).

**Recommendation:** Rely on existing guarantees (e.g. shared_lock during search prevents index mutation during that search). If not already stated, add: "Search holds a shared lock (or equivalent) so the index is not modified until the search run completes; streamed result IDs remain valid for that run."

---

## 15. Coordination with LARGE_RESULT_SET_DISPLAY_PLAN

**Gap:** That plan has Phase 1 (result limiting), Phase 2 (virtual scrolling enhancements), Phase 3 (cursor-based). The streaming doc does not reference it.

- **Result limiting:** See §2 above.
- **Virtual scrolling:** Doc assumes current virtual scrolling works with partial results; no explicit dependency or ordering (streaming Phase 2 vs large-result-set Phase 2).
- **Cursor-based:** Long-term, cursor-based results might change how "partial" is defined (e.g. by ID range or windows); streaming would need to align.

**Recommendation:** Add a "Related plans" note in the streaming doc: reference LARGE_RESULT_SET_DISPLAY_PLAN, state that streaming is compatible with result limiting (once defined) and current virtual scrolling, and that cursor-based work may require a later alignment pass.

---

## What is needed to make decisions about the high-priority items

For each high-priority blind spot, the table below states: (1) the **decision** to make, (2) the **inputs** needed to make it, and (3) **how to get** those inputs.

---

### §3 PollResults and streaming API

| What to decide | Inputs needed | How to get them |
|----------------|----------------|------------------|
| **Polling model:** Once per frame vs multiple times per frame; single "has new" vs "has new batch or complete". | (a) ImGui frame rate and whether one batch per frame is enough for smooth UX. (b) Whether `GetNewBatch()` returns one batch or "all pending" (collector API choice). | (a) Product/UX: "first results within 1–2 frames" acceptable? (b) Design: decide collector API (e.g. `GetNewBatch()` returns one batch; caller can loop until no more, or collector exposes `GetAllPendingBatches()`). |
| **Coalescing:** If multiple batches are ready between frames, deliver one combined update or one batch per call? | (a) Impact on "time to first result" (coalesce = one update, maybe slightly later). (b) Impact on UI jank (many small updates vs one larger update per frame). | (a) Prototype or design: collector either merges batches into one "pending" buffer, or exposes batches one-by-one. (b) Rule of thumb: one update per frame is simpler and avoids jank; coalesce in collector or in PollResults. |
| **Attribute loading:** Wait for all attribute futures before each partial update, or allow partial update and let attribute loading catch up? | (a) Current semantics: `WaitForAllAttributeLoadingFutures` is used before **replacing** results because futures hold references to old `SearchResult` objects. (b) With streaming we **append** partial results; existing attribute futures refer to previous `state.searchResults`. | (a) Code: see `SearchController::PollResults` and `SearchResultUtils` – attribute futures are for **sort-driven** attribute loading, not for the initial result set. (b) Decision: if we only **append** to `state.searchResults` (or `partialResults`), we don't replace the vector, so we may not need to wait for attribute futures before **each** append. We still need to wait before a **full replace** (e.g. when switching to final results or clearing). (c) Clarify: do we ever replace the whole result set on a partial update, or only append? If only append, rule: "Wait for attribute futures only before a full replace (final results or clear)." |

**Concrete decisions to lock in:**

1. **Collector API:** `GetNewBatch()` returns exactly one batch (and caller can loop), or `GetAllPendingBatches()` returns a vector of all batches since last call?  
   → **Input:** Prefer simplicity (one batch per call, loop in PollResults) vs fewer round-trips (return all pending).  
2. **PollResults with streaming:** Once per frame: "while (HasNewBatch()) { append GetNewBatch() to partialResults }" then if complete replace with final.  
   → **Input:** Confirm one update per frame is acceptable for "smooth" progress.  
3. **Attribute loading:** Wait for attribute futures only when doing a **full replace** of results (final or clear), not when appending partial batches.  
   → **Input:** Confirm that attribute futures in GuiState are only for the current `searchResults`/display set and that appending doesn't invalidate them (or that we don't start attribute loading until we have a stable set for the visible window).

---

### §7 Completion-order loop efficiency (C++17)

| What to decide | Inputs needed | How to get them |
|----------------|----------------|------------------|
| **Blocking strategy when no future is ready:** Pure poll (wait_for(0)) vs block on one future (e.g. wait_for(5ms) on first) vs one thread per future. | (a) CPU impact of a tight poll loop during search (e.g. 1–10 s). (b) Typical number of futures (e.g. thread count, often &lt; 32). (c) Willingness to add a dedicated "collector" thread. | (a) Micro-benchmark or prototype: run a loop that does wait_for(0) on N futures until one is ready; measure CPU %. (b) Code: thread count from `DetermineThreadCount` / settings. (c) Design: one thread that runs "while more futures: if any ready, get(); else wait_for(5ms) on first pending" avoids busy-loop and doesn't require N threads. |
| **Fairness:** If we block on "first" future, a slow future could delay processing of ready ones. | Whether any strategy produces seriously unfair ordering (e.g. one future never consumed until the end). | In practice, blocking on "first pending" with a short timeout (5–10 ms) and then re-checking **all** futures gives reasonable fairness; no future is stuck forever. |

**Concrete decisions to lock in:**

1. **Loop shape:** Use a single thread that: (a) checks all futures with `wait_for(0)`; (b) if any ready, `.get()` and push to collector; (c) if none ready, `wait_for(5ms)` on the first pending future, then go to (a).  
   → **Input:** Accept 5 ms as max extra latency when no future is ready; or measure and pick 1–10 ms.  
2. **Alternative:** One dedicated thread per future (each blocks on `.get()`, then pushes to collector).  
   → **Input:** Reject if we want to avoid N extra threads; accept if thread count is small and simplicity is preferred.

---

### §8 Error handling when a future throws

| What to decide | Inputs needed | How to get them |
|----------------|----------------|------------------|
| **Semantics:** On first exception from a future, do we mark "complete with error", keep partial results, or clear and show error? | (a) Product: should the user see partial results when a worker fails, or only an error message? (b) Consistency: do we today propagate worker exceptions to the UI or swallow them? | (a) Product/UX decision. (b) Code: check whether worker lambdas in `LoadBalancingStrategy.cpp` can throw and how `ProcessSearchFutures` (current path) handles it – today we call `.get()` in a loop; if one throws, we don't consume the rest (and may leak or leave futures unconsumed). |
| **Consumption:** Ensure we don't double-.get() or skip .get() on remaining futures when one throws. | C++17: after a future throws, that future is invalid; remaining futures must still be consumed (or abandoned). If we don't call .get() on them, the corresponding thread may block in the destructor of the promise. | Standard: either consume all futures (e.g. in a finally-style cleanup, or catch, mark error, then loop over remaining and .get() to drain), or document that we abandon the run (and accept that worker threads may block until the process exits). |

**Concrete decisions to lock in:**

1. **On exception:** Mark search as "complete with error"; keep partial results already in the collector; show an error indicator in the UI (e.g. "Search failed: …").  
   → **Input:** Product: "show partial + error" vs "clear and show error only".  
2. **Loop structure:** Wrap `.get()` in try/catch; on catch: set error state, mark collector complete (with error), then **drain remaining futures** in a separate loop (try/catch each .get() so one bad future doesn't prevent draining).  
   → **Input:** Confirm that draining is required (to avoid blocking worker threads); code review of current `.get()` loop to see if today we have the same issue.  
3. **Worker exceptions:** Decide whether workers are allowed to throw or must catch and return an error result. If they must not throw, we can still defend in the consumer with try/catch for robustness.

---

## Recommendations for high-priority decisions (with pros/cons)

Below are concrete recommendations for §3, §7, and §8, with pros and cons. These can be adopted as the default design and overridden only if there is a strong reason.

---

### §3 PollResults and streaming API – Recommendation

**Recommendation:**

1. **Collector API:** Expose **`GetAllPendingBatches()`** that returns a single vector of all results from batches that have arrived since the last call (or since search start). Optionally also expose `HasNewBatch()` / `GetNewBatch()` for a one-batch-at-a-time API; the implementation can use a single internal "pending" buffer that is cleared when the caller asks for pending data.
2. **PollResults with streaming:** Once per frame, when streaming is enabled: if `HasNewResults()` (or equivalent "has new batch or complete"), call **`GetAllPendingBatches()`** once and append those results to `state.partialResults`. If search is complete, replace `state.searchResults` with the final set (from collector or from accumulated partialResults), clear partialResults, set `resultsComplete = true`, then run existing sort/display logic.
3. **Attribute loading:** **Wait for attribute futures only when doing a full replace** (when applying final results or clearing). Do **not** wait before appending partial batches. When appending, attribute loading for the existing display set continues; new rows get attributes lazily when they enter the visible window (existing lazy-load behaviour).

**Pros:**

- **One call per frame:** PollResults stays simple: one "get pending" call, one append. No loop in PollResults; coalescing is natural (collector or worker merges batches into one pending buffer).
- **Smooth UX:** One update per frame avoids multiple layout/scroll updates per frame and keeps frame time predictable.
- **Attribute loading safe:** We only replace the whole result set when we have final results (or clear). Appending doesn't invalidate existing `SearchResult` references held by attribute-loading futures, so we don't need to wait before each append.
- **Fits existing PollResults shape:** Same "if has new → get; then update state" pattern; streaming just changes "get" to "get all pending" and "update" to "append or replace".

**Cons:**

- **Slightly later first paint for fast bursts:** If three batches arrive in one frame, we show all three at once next frame instead of one batch per frame for three frames. Acceptable: user still sees results within one frame (~16 ms at 60 FPS).
- **Collector must buffer:** Collector (or SearchWorker) must merge "ready" batches into one pending buffer between frames. Small extra logic; keeps PollResults dumb.

**Alternative considered:** One batch per call, loop in PollResults. Rejected because it complicates PollResults (loop, multiple appends per frame) and can cause multiple layout passes in one frame; one "get all pending" is simpler.

---

### §7 Completion-order loop efficiency – Recommendation

**Recommendation:**

Use a **single consumer thread** (the same SearchWorker thread that today runs `ProcessSearchFutures`) with a **back-off loop**:

1. Maintain a list (or vector) of indices of futures not yet consumed.
2. Loop: For each pending future, call `wait_for(std::chrono::milliseconds(0))`. If any returns `ready`, call `.get()` on that future, push the vector of results to `StreamingResultsCollector`, remove that future from the pending list, and **break** (re-check all from the top so we don't favour one index).
3. If **none** are ready, call `wait_for(std::chrono::milliseconds(5))` on the **first** pending future (arbitrary choice; or rotate to spread fairness). When it returns (ready or timeout), go to step 2.
4. When the pending list is empty, call `MarkSearchComplete()` on the collector and exit.
5. **Cancellation:** At the start of each iteration, check `cancel_current_search_`; if set, remove all pending futures from the list (drain them with .get() in a loop to avoid blocking destructors, or document that we abandon and accept blocking on process exit), call `MarkSearchComplete()` on the collector, and exit.

**Pros:**

- **No busy-loop:** When no future is ready, we block for up to 5 ms instead of spinning. CPU usage during search stays low.
- **No extra threads:** Reuses the existing SearchWorker background thread. No N-thread-per-future model.
- **Reasonable fairness:** Re-checking all futures after each consumption avoids starvation; 5 ms timeout is short enough that we don't delay ready futures by much.
- **C++17-only:** Uses only `std::future::wait_for`; no external libs or platform APIs.

**Cons:**

- **Up to 5 ms extra latency** when no future is ready: the first future that completes might have to wait until the next 5 ms timeout. Acceptable for "first results in &lt;100 ms" target.
- **Drain on cancel:** If we cancel, we should still drain remaining futures (call .get()) to avoid blocking in destructors; that adds a small "shutdown" path. Alternative: document that on cancel we don't drain (worker threads may block until process exit); acceptable for a desktop app if cancel is rare.

**Alternative considered:** One thread per future, each blocking on `.get()` then pushing to collector. Rejected because it adds N threads (N = thread count, often 8–32) and complicates lifecycle; single-thread consumer with back-off is simpler and sufficient.

---

### §8 Error handling when a future throws – Recommendation

**Recommendation:**

1. **Worker contract:** Keep **workers allowed to throw** (e.g. out-of-memory, bug). The **consumer** (completion-order loop) defends with try/catch so one bad future doesn't crash the process or leak other futures.
2. **On first exception in the loop:** (a) **Catch** the exception, (b) **store** an error state (e.g. `search_error_` string or enum in SearchWorker; collector can have `SetError(std::string)`), (c) **mark** the collector complete with error (e.g. `MarkSearchComplete()` plus `SetError(...)`), (d) **drain** all remaining futures in a loop: for each pending future, try/catch `.get()` (ignore or log secondary exceptions), (e) **exit** the loop. UI then sees "complete with error" and partial results (whatever was already pushed to the collector).
3. **UI behaviour:** **Keep partial results** and show an **error indicator** (e.g. status bar: "Search failed: &lt;message&gt;" or "Partial results (search failed)"). Do not clear partial results on error; the user can still see what was found before the failure.

**Pros:**

- **Robust:** One failing worker doesn't crash the app; remaining futures are drained so worker threads don't block in destructors.
- **Useful UX:** Partial results are often still valuable (e.g. 80% of chunks succeeded); user sees what was found and can retry or refine.
- **Clear contract:** Workers can throw; consumer always catches and translates to error state. No hidden "must not throw" requirement that could be violated by third-party or future code.

**Cons:**

- **Partial + error is a new state:** UI must handle "resultsComplete = true but error != null" (show partial list + error message). One extra branch in status/display logic.
- **Drain loop on error:** We must iterate over remaining futures and .get() them; if a second future also throws, we catch and continue (or log). Slightly more code than "abort and leave futures unconsumed".

**Alternative considered:** Clear partial results and show only "Search failed". Rejected because it discards useful information and forces the user to re-run to see any results.

---

## Suggested order of resolution

| Priority | Item | When |
|----------|------|------|
| High | §3 PollResults / streaming API | Before Phase 1 implementation |
| High | §7 Completion-order loop efficiency | Before Phase 1 implementation |
| High | §8 Error handling (future throws) | Before Phase 1 implementation |
| Medium | §1 Cancellation semantics | Phase 1 |
| Medium | §4 Toggle during search | Phase 2 (UI) |
| Medium | §5 Default for streamPartialResults | Phase 2 |
| Medium | §12 Streaming metrics | Phase 2 |
| Lower | §2 Result limiting interaction | When result limiting is designed |
| Lower | §6 Peak memory clarification | Doc-only, anytime |
| Lower | §9 Empty first batches | Phase 1 or 2 |
| Lower | §10 Saved searches | One-line doc |
| Lower | §11 CLI/headless | When CLI exists or is planned |
| Lower | §13 Accessibility | If required by project |
| Lower | §14 Index consistency | Doc-only if not already guaranteed |
| Lower | §15 LARGE_RESULT_SET_DISPLAY_PLAN | Doc-only, anytime |

---

**Next step:** Review this list with the team; decide which items to resolve in the design doc before implementation and which to leave as "resolve during implementation."

---

## Pre-implementation: Clarifications and best practices

Before starting implementation of the streaming feature, two things are needed: (1) **remaining clarifications** that would block or confuse coding, and (2) **best practices** the project already expects for major features but that are not yet explicitly applied to this change. Addressing both reduces risk and rework.

---

### 1. Clarifications to lock in before coding

These should be decided or documented so implementation can proceed without guesswork.

| # | Clarification | Why it blocks / confuses | Suggested resolution |
|---|----------------|---------------------------|------------------------|
| **C1** | **Adopt high-priority recommendations** | §3, §7, §8 recommendations are written but not yet "approved" as the design. Implementers need a single source of truth. | **Decision:** Adopt the recommendations in this doc (§3 GetAllPendingBatches + one update per frame + wait for attribute futures only on full replace; §7 single consumer thread with 5 ms back-off; §8 keep partial + error, drain futures, workers may throw). Update `PARTIAL_RESULTS_STREAMING_ANALYSIS.md` with a short "Implementation decisions" subsection that references this doc and states these choices. |
| **C2** | **Cancellation semantics (medium priority)** | Completion-order loop must check cancel and drain/exit; UI must show "Cancelled" vs "Complete". Without a rule, behaviour will be inconsistent. | **Decision:** "On cancel: stop loop on next iteration; drain remaining futures (try/catch .get()); MarkSearchComplete(); keep partial results in UI with a 'Cancelled' indicator." Add to analysis doc. |
| **C3** | **Default for streamPartialResults** | Settings UI and persistence need a default; tests and docs need a single value. | **Decision:** Recommend **default true** for GUI (better perceived performance); document in analysis doc. If CLI/headless is added later, default false there. |
| **C4** | **Empty batches** | First batches may have 0 results; UI and collector must behave consistently. | **Decision:** "Notify on every batch (including empty); status shows 'Searching... N results' with N possibly 0." One line in analysis doc. |
| **C5** | **Exact API and type names** | Implementers need: collector method names (`GetAllPendingBatches`, `HasNewBatch`, etc.), GuiState field names (`partialResults`, `resultsComplete`, `streaming_config`, etc.), and whether `streamPartialResults` lives in AppSettings or only in a runtime struct. | **Action:** Add a short "Phase 1 API contract" subsection to the analysis doc (or this doc): method signatures, GuiState fields, and setting name/location so code and tests use the same names. |
| **C6** | **Toggle during search** | If user turns streaming off mid-search, behaviour is unspecified. | **Decision:** "Setting applies only to the **next** search; in-flight search continues with current mode." Simplest; add one sentence to analysis doc. |

**Summary:** C1–C4 and C6 are **decisions** (adopt recommendations, cancellation, default, empty batches, toggle). C5 is an **artifact** (API contract / type names) to add to the design doc so implementation and tests are aligned.

---

### 2. Best practices we should follow before / during this big change

The project's **Production Readiness Checklist** (`docs/plans/production/PRODUCTION_READINESS_CHECKLIST.md`) and the **streaming analysis doc** call out several practices for major features. Below: what applies to streaming, what we're **not** yet doing, and what to do.

| Practice | Source | Status / gap | Action before or during implementation |
|----------|--------|--------------|----------------------------------------|
| **Design review** | Analysis doc ("Design review by threading expert"); Summary ("Review & Approve – Design review with team") | Not done: no formal sign-off that the design (including recommendations and blind-spot resolutions) is approved. | **Before:** Run a short design review (internal or with a reviewer): confirm architecture, §3/§7/§8 recommendations, cancellation, and API contract. Record outcome (e.g. "Design approved for Phase 1, 2026-01-31"). |
| **Comprehensive checklist for major feature** | Production Readiness Checklist – "Use Comprehensive Checklist for major features/releases" | Not done: streaming is a major feature but the Comprehensive Checklist has not been explicitly applied. | **Before or at start:** Walk the Comprehensive Checklist (Phases 1–8) with streaming in mind: compilation (new files, includes, Windows), thread safety (futures, collector, cancel), exception handling, future cleanup, memory. Use it as a pre-implementation checklist and again before merge. |
| **Test plan before implementation** | Good practice; Analysis doc has "Testing Strategy" per phase but no single "test plan" that defines scope and order. | Partially done: unit tests for collector/notifier and integration with SearchWorker are mentioned; no explicit "test plan" doc or acceptance tests. | **Before Phase 1:** Write a short **test plan** (1–2 pages): unit tests (StreamingResultsCollector, completion-order loop with mock futures, error/cancel paths), integration tests (SearchWorker + collector, PollResults with streaming on/off), and one or two manual/acceptance scenarios (first results &lt;100 ms, cancel shows partial). Add to docs/research or docs/plans/features. |
| **Thread sanitizer / stress tests** | Analysis doc ("Add thread sanitizer to CI", "Stress tests with 100+ threads"); Production checklist (thread safety, memory). | Not done: no commitment to run TSAN on streaming paths or to add a stress test for the completion-order loop. | **During/after Phase 1:** Run thread sanitizer on a build that exercises streaming (search with streaming on); add (or tag) a test that runs the completion-order loop with many futures. Document in test plan. |
| **Future cleanup and RAII** | Production checklist (Phase 6 & 8): "All std::future objects must be properly cleaned up"; wait/valid/get/reset pattern. | Design addresses drain-on-cancel and drain-on-error; not yet checked against the exact checklist pattern (valid(), wait, get(), reset). | **During implementation:** Ensure completion-order loop and SearchWorker follow the checklist: no valid future is abandoned; after .get(), future is not reused; on cancel/error, remaining futures are drained (get in try/catch). Add a short "Future cleanup" bullet to the implementation notes in the analysis doc. |
| **Feature flag / incremental rollout** | Analysis doc: streaming is optional (streamPartialResults); can be disabled. | Design supports it; default and UI for the setting are not yet decided (C3 above). | **Before Phase 2:** Lock default (C3); ensure setting is persisted and exposed in Settings UI so we can turn streaming off without code change. |
| **Documentation and index update** | Production checklist: "Update DOCUMENTATION_INDEX.md when moving/adding documents"; Analysis doc has many sections. | New docs (blind spots, recommendations) exist; analysis doc will gain "Implementation decisions" and possibly "Phase 1 API contract". Index may need an entry. | **Before or after implementation:** Add or update an entry in `docs/DOCUMENTATION_INDEX.md` for streaming (analysis, summary, blind spots, test plan). Keep analysis doc as the single place for "Implementation decisions" and API contract. |
| **Acceptance criteria signed off** | Analysis doc has "Acceptance Criteria" and "Phase 1 Complete" checkboxes. | Criteria exist but are not explicitly "signed off" by product or tech lead before implementation. | **Before Phase 1:** Confirm with stakeholder that Phase 1 acceptance criteria (collector + notifier + SearchWorker integration, no regression, cross-platform build) are the right exit criteria. Optional: add "Accepted by: ___" and date. |
| **Windows build and PGO** | AGENTS document / Production checklist: Windows build, PGO flags for new code. | New source files (e.g. StreamingResultsCollector, maybe ResultUpdateNotifier) will be added; tests may share sources with main target. | **During implementation:** Follow AGENTS document: new sources in correct CMake targets; if a test uses the same sources as the main executable, give the test target the same PGO compiler flags as the main target (see AGENTS document "Modifying CMakeLists.txt Safely"). Verify Windows build after adding files. |

**Summary – not yet following:**

1. **Formal design review** and written approval/sign-off.  
2. **Explicit pass** of the Comprehensive Production Readiness Checklist for streaming.  
3. **Written test plan** (scope, unit/integration/acceptance, TSAN/stress) before coding.  
4. **API contract** (method and type names) in the design doc.  
5. **Acceptance criteria** explicitly agreed (and optionally signed off).  
6. **Future cleanup** and **Windows/PGO** called out in implementation notes so implementers don’t miss them.

---

### 3. Recommended order of actions before Phase 1

1. **Lock decisions (C1–C4, C6)** and **add API contract (C5)** to the analysis doc (or this doc).  
2. **Design review:** Review design + recommendations + blind-spot resolutions; record approval or changes.  
3. **Test plan:** Write the short test plan (unit, integration, TSAN, acceptance).  
4. **Acceptance criteria:** Confirm Phase 1 acceptance criteria with stakeholder.  
5. **Checklist pass:** Walk the Comprehensive Checklist with streaming in mind; note any gaps and fix in design or plan.  
6. **Start Phase 1** with implementation notes that reference: future cleanup pattern, Windows/PGO, and the test plan.

Doing 1–5 before coding reduces ambiguity and avoids rework; 6 keeps implementation aligned with project standards.
