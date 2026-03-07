# Step-by-Step Implementation Plan: Streaming Search Results

**Date:** 2026-01-31  
**Purpose:** Ordered, actionable implementation steps derived from current streaming documentation  
**References:**  
- `docs/research/PARTIAL_RESULTS_STREAMING_ANALYSIS.md`  
- `docs/research/STREAMING_SEARCH_RESULTS_ASSESSMENT.md`  
- `docs/research/2026-01-31_STREAMING_BLIND_SPOTS.md`  
- `docs/plans/features/LARGE_RESULT_SET_DISPLAY_PLAN.md` (coordination)  
- **`AGENTS document`** (mandatory for all steps)  
- **`docs/standards/CXX17_NAMING_CONVENTIONS.md`**

---

## Compliance: AGENTS document, DRY, Sonar, clang-tidy (Every Step)

**Every implementation step must satisfy the following. Verify before marking a step done.**

### AGENTS document (mandatory)

| Rule | Application |
|------|-------------|
| **Windows min/max** | Use `(std::min)` and `(std::max)` with parentheses in any code that may include `Windows.h`. |
| **Include order** | `#pragma once` → system includes → platform includes → project includes → forward declarations. No inline `#` in `.clang-tidy` values. |
| **Forward declarations** | Match `class` vs `struct` to the actual definition (e.g. `struct GuiState;` if GuiState is a struct). Run `python3 scripts/find_class_struct_mismatches.py` if adding forward decls. |
| **Explicit lambda captures** | No `[&]` or `[=]`; use explicit `[&x, y]`. **Critical** for lambdas inside template functions (MSVC). |
| **C++17 init-statement** | Prefer `if (T x = ...; condition)` when the variable is only used inside that `if`. Do not apply when variable is used after the `if` or in ImGui slider/input patterns. |
| **Naming** | Follow `docs/standards/CXX17_NAMING_CONVENTIONS.md`: PascalCase classes/functions, snake_case_ members, kPascalCase constants. |
| **DRY** | No duplication. Reuse existing helpers (e.g. SearchResult conversion from current post-process). Extract shared logic into functions; no block >10 lines duplicated. |
| **Platform blocks** | Do not change code inside `#ifdef _WIN32` / `__APPLE__` / `__linux__` to make it “cross-platform”; use platform-agnostic abstractions if needed. |
| **Safe strings** | Use `strcpy_safe` / `strcat_safe` from `StringUtils.h` for fixed-size buffers; no raw `strncpy`/`strcpy`/`strcat`. |
| **CMake / PGO** | When adding sources: mirror existing patterns in CMakeLists.txt; if a test shares sources with main target, give the test the same PGO compiler flags (see AGENTS document). |
| **RAII** | No `malloc`/`free`/`new`/`delete`; use `std::vector`, `std::unique_ptr`, smart pointers, standard containers. |
| **Assertions** | Add assertions for preconditions, loop invariants, postconditions in debug builds (AGENTS document Core Principles). |
| **Type aliases** | Use `using` for complex or repeated types (e.g. `using ResultVec = std::vector<SearchResult>`) per AGENTS document. |
| **No unused backward compat** | Do not add code "for backward compatibility" that is not used; this project has no external consumers. |

### SonarQube (avoid new issues)

| Rule | Application |
|------|-------------|
| **Nesting (cpp:S134)** | Max 3 levels. Use early returns and extracted functions. |
| **Cognitive complexity (cpp:S3776)** | Keep functions under 25. Extract helpers. |
| **Merge nested ifs (cpp:S1066)** | Combine `if (a) { if (b) { } }` into `if (a && b) { }` when inner has no else. |
| **Limit parameters (cpp:S107)** | Max 7; group into structs if needed. |
| **Const refs (cpp:S5350)** | Use `const T&` (or `std::string_view`) for read-only parameters. |
| **Unused parameters (cpp:S1172)** | Remove or mark `[[maybe_unused]]` or comment. |
| **Exceptions (cpp:S2486, cpp:S1181)** | No empty catch; catch specific types; handle or rethrow. |
| **Avoid void* (cpp:S5008)** | Use concrete types or templates. |
| **Avoid reinterpret_cast (cpp:S3630)** | Prefer `static_cast` or proper design. |
| **Prefer std::array/vector (cpp:S5945)** | No C-style arrays. |
| **Reduce nested breaks (cpp:S924)** | Max 1–2 breaks per loop; extract function or use combined loop condition / flag / goto when breaking from nested loops. |
| **NOLINT/NOSONAR** | Only when justified; place on the **same line** as the finding (per user rule). |

### clang-tidy

| Rule | Application |
|------|-------------|
| **Run before commit** | Use `scripts/pre-commit-clang-tidy.sh` or run clang-tidy on new/modified files. Fix all new warnings. |
| **Float literals** | Use `F` suffix (e.g. `3.14F`), not `f`, to satisfy project rule. |
| **std::string_view** | Prefer `std::string_view` for read-only string parameters where possible. |
| **Const correctness** | Const member functions and const reference parameters for read-only use. |

### Per-step verification

- After each step: run clang-tidy on touched files; check for duplicated logic (DRY); ensure no new nesting/complexity/parameter-count/break-count issues.
- Before Phase 1 complete: run `scripts/build_tests_macos.sh` (macOS); ensure Windows build and PGO rules if adding test targets.

---

## Post-merge status (merged to main 2026-01-31)

**Implemented:** Phase 1 (Core Infrastructure) and Phase 2 (UI Integration) are implemented and merged. Streaming results appear incrementally; final replace and sort run on completion; filter caches and clear-on-new-search behaviour are correct.

**Deferred by design:** Phase 3 (progressive sort on partial) — sort and filters apply to the final result set only; during streaming the table shows unfiltered partial count and label "(no filters, no sort yet)" per interaction review.

**Not implemented (optional / later):** Phase 4 (result limiting, ResultUpdateNotifier, streaming metrics); Pre-Implementation P4–P7 (formal design review, test plan, acceptance sign-off) were not completed as separate deliverables before merge.

**Gaps / possible omissions:** See [Gaps and possible omissions](#gaps-and-possible-omissions) below.

---

## Overview

This plan implements **streaming partial search results** so users see results in &lt;100 ms instead of waiting for the full search. It is **phased** and **backward compatible**: the "full result set" path remains when streaming is disabled.

**Adopted decisions (from blind-spots recommendations):**
- **Collector API:** `GetAllPendingBatches()` returns one vector of all results since last call; UI calls once per frame.
- **Completion-order loop:** Single SearchWorker thread; `wait_for(0)` on all futures; if any ready, `.get()` and push to collector; if none ready, `wait_for(5ms)` on first pending; re-check all after each consumption. On cancel: drain remaining futures, mark collector complete, keep partial results.
- **Error handling:** On first throw, catch, set error state, mark collector complete, drain remaining futures (try/catch each `.get()`), keep partial results and show error in UI.
- **Attribute loading:** Call `WaitForAllAttributeLoadingFutures` only before a **full replace** (final results or clear), not before appending partial batches.
- **Setting:** `streamPartialResults` in AppSettings (default `true` for GUI). Applies only to **next** search (no mid-search toggle).

---

## Design Patterns and Strategies (from STREAMING_SEARCH_RESULTS_ASSESSMENT.md)

The plan integrates the assessment’s **implementation strategies** and **recommended design patterns**. Each phase and step below should preserve these; verify during implementation and design review.

### Implementation strategies (assessment)

| Strategy | Plan mapping | Where |
|---------|----------------|-------|
| **GetAllPendingBatches API** | UI calls `GetAllPendingBatches()` once per frame; collector buffers batches internally. Never notify UI per single result. | 1.1 (collector), 2.1 (PollResults) |
| **Single-threaded future consumption (back-off loop)** | One SearchWorker thread; `wait_for(0)` then `wait_for(5ms)` on first pending; re-check all after any completion. No reactive libs, no thread-per-future. | 1.5.2 (completion-order loop) |
| **Progressive filtering vs. full sort** | Phase 2: display partial with minimal/no sort to keep UI snappy. Phase 3: sort on user interaction or on search complete. Never keep 1M results perfectly sorted while streaming. | 2.3.1, 3.1 |
| **Robust error/cancellation** | Cancel: stop loop, drain futures (try/catch `.get()`), mark complete, keep partial. Error: catch throw, mark failed, keep partial, show error in UI (“best effort”). | 1.5.2, 1.5.3, 2.1.4 |

### Recommended design patterns (assessment)

| Pattern | Intent | Plan mapping |
|---------|--------|----------------|
| **Pipeline** | Results flow as a stream: producer → collector → consumer. | `ParallelSearchEngine` → `StreamingResultsCollector` → `GuiState` (Phase 1.1, 1.3, 1.5; Phase 2.1). Keep this linear flow; no bypass of collector when streaming. |
| **Observer (lightweight)** | Notify when new data is available. | Assessment: “ResultUpdateNotifier to wake UI when new data available.” Plan: **poll-based** — PollResults calls `GetAllPendingBatches()` once per frame (coalesces updates, 60 FPS). Optional notifier (1.2) can be minimal or Phase 4 wake-up. |
| **Throttling/Batching** | Never push one result at a time; chunks (e.g. 500–1000) or time intervals (e.g. 50 ms). | Collector ctor `(batch_size, notification_interval_ms)`; step 1.1.2 flush on size or interval. Defaults: e.g. 500 items, 50 ms (1.5.1). |
| **Facade** | Single entry for callers; they don’t care streamed vs one-shot unless opt-in. | SearchWorker remains the interface. `GetResults()` unchanged; `GetStreamingCollector()` only for PollResults when streaming on. Callers use GetResults() or PollResults; no direct collector access elsewhere. |
| **Strategy** | “How” vs “when”: strategies do work split; SearchWorker does delivery. | Keep existing `LoadBalancingStrategy` for *how* work is split; do **not** change strategy APIs. SearchWorker only changes *when* results are consumed (completion-order) and *where* they go (collector). |

### Coordination (assessment)

- **Large result set:** Streaming and result limiting are complementary. When `LARGE_RESULT_SET_DISPLAY_PLAN` Phase 1 exists, `StreamingResultsCollector` shall be aware of `max_results`; when limit hit, signal engine to stop and mark collector complete (Phase 4).

---

## Pre-Implementation (Before Phase 1 Code)

**Compliance:** Pre-implementation deliverables (API contract, test plan, acceptance criteria) should not prescribe code that would violate AGENTS document (e.g. no implicit lambda captures, no C-style arrays, no >7 params without struct). Test plan should include clang-tidy and TSAN.

| Step | Action | Owner | Done |
|------|--------|-------|------|
| **P1** | **Lock API contract** – Add to analysis doc (or blind-spots doc): `GetAllPendingBatches()` signature; `HasNewResults()` / equivalent for "has new batch or complete"; GuiState fields: `partialResults`, `resultsComplete`, `resultsBatchNumber`, `showingPartialResults`; AppSettings: `streamPartialResults`. | Dev | [x] (in code + GuiState/Settings) |
| **P2** | **Lock cancellation semantics** – Document in analysis doc: "On cancel: stop loop on next check; drain remaining futures (try/catch .get()); MarkSearchComplete(); keep partial results in UI with 'Cancelled' indicator." | Dev | [ ] (informal; cancel = new search clears partial) |
| **P3** | **Lock default** – Document: default `streamPartialResults = true` for GUI. | Dev | [x] (Settings.h) |
| **P4** | **Design review** – Review design + recommendations + blind-spot resolutions **and** alignment with assessment design patterns (Pipeline, Throttling/Batching, Facade, Strategy, Observer, Progressive filtering, Robust error/cancellation); record approval or changes. | Tech lead / reviewer | [ ] (informal) |
| **P5** | **Test plan** – Write short test plan (1–2 pages): unit tests (StreamingResultsCollector batching/thread-safety, completion-order loop with mock futures, error/cancel); integration (SearchWorker + collector, PollResults streaming on/off); TSAN run; acceptance (first results &lt;100 ms, cancel shows partial). | Dev | [ ] (StreamingResultsCollectorTests exist; no formal plan doc) |
| **P6** | **Acceptance criteria** – Confirm Phase 1 acceptance criteria with stakeholder: collector + notifier + SearchWorker integration, no regression, cross-platform build. | PM / Tech lead | [ ] (informal) |
| **P7** | **Checklist pass** – Walk Production Readiness Checklist (Comprehensive) with streaming in mind; note gaps. | Dev | [ ] |

---

## Phase 1: Core Infrastructure (Foundation)

**Goal:** Streaming pipeline in place; no visible UI change yet. Existing `GetResults()` and full-result-set path unchanged.

### 1.1 Create StreamingResultsCollector

**Design patterns (assessment):** **Pipeline** (middle stage: producer → collector → GuiState). **Throttling/Batching** — never push one result at a time; use batch_size (e.g. 500–1000) and notification_interval_ms (e.g. 50 ms). **GetAllPendingBatches API** — UI calls once per frame; collector buffers internally.

**Compliance (this section):** Include order, naming (PascalCase/snake_case_), const refs and const member functions, `std::vector`/`std::mutex` (no C arrays, no raw new/delete). Use `std::string_view` for read-only string params. Run clang-tidy on new files before marking done.

| Step | Action | File(s) | Done |
|------|--------|---------|------|
| 1.1.1 | Add `StreamingResultsCollector.h`: class with ctor(batch_size, notification_interval_ms), `AddResult(SearchResult)`, `MarkSearchComplete()`, `HasNewBatch()`, `GetNewBatch()`, **`GetAllPendingBatches()`** (returns vector of all pending results and clears pending buffer), `GetAllResults()`, `IsSearchComplete()`, optional `SetError(std::string)` for Phase 1 error state. Internal: current_batch_, pending buffer for UI, all_results_, shared_mutex, atomics. **Verify:** Include order (pragma → system → project → forward decls); naming (PascalCase/snake_case_/kPascalCase); const member functions where non-mutating; use `std::string_view` for SetError param if only read. | `src/search/StreamingResultsCollector.h` | [x] |
| 1.1.2 | Implement batching: when current_batch_ reaches batch_size or notification_interval_ms elapsed, move batch to "pending" buffer, set has_new_batch_. | `src/search/StreamingResultsCollector.cpp` | [x] |
| 1.1.3 | Implement `GetAllPendingBatches()`: under lock, copy pending buffer to result vector, clear pending buffer, clear has_new_batch_. | `src/search/StreamingResultsCollector.cpp` | [x] |
| 1.1.4 | On `MarkSearchComplete()`: flush current_batch_ to pending, set search_complete_. | `src/search/StreamingResultsCollector.cpp` | [x] |
| 1.1.5 | Add unit tests: batching, thread safety (concurrent AddResult + GetAllPendingBatches), HasNewBatch/GetAllPendingBatches idempotence after drain, MarkSearchComplete flushes. **Verify:** No duplicated test setup >10 lines; use test helpers (DRY). | `tests/StreamingResultsCollectorTests.cpp` (or existing test file) | [x] |

### 1.2 Create ResultUpdateNotifier (Optional / Lightweight)

**Design patterns (assessment):** **Observer** — assessment recommends “lightweight notifier to wake UI when new data available.” Plan uses poll-based `GetAllPendingBatches()` once per frame; notifier can be minimal or deferred to Phase 4 (platform wake-up).

**Compliance (this section):** Same as 1.1 (include order, naming, const refs, no duplication). If callback is used, lambda must have explicit captures.

| Step | Action | File(s) | Done |
|------|--------|---------|------|
| 1.2.1 | Add `ResultUpdateNotifier.h/cpp`: holds reference to collector, optional callback when new batch available. Can be a thin wrapper that PollResults checks; or skip and rely on PollResults polling collector directly. | `src/search/ResultUpdateNotifier.h`, `.cpp` | [ ] (skipped; poll-based) |
| 1.2.2 | If implemented: unit tests for callback invocation when HasNewBatch becomes true. | tests | [ ] (N/A) |

*Note:* Assessment favours PollResults calling collector once per frame; notifier can be minimal or deferred to Phase 4.

### 1.3 Extend GuiState

**Design patterns (assessment):** **Pipeline** — consumer end of stream; holds `partialResults`, `resultsComplete`, `showingPartialResults` for UI.

**Compliance (this section):** Naming (snake_case_ for members), forward decls match struct/class. No new duplication of reset logic; reuse or extend existing clear path.

| Step | Action | File(s) | Done |
|------|--------|---------|------|
| 1.3.1 | Add to GuiState: `std::vector<SearchResult> partialResults;`, `bool resultsComplete = true;`, `uint64_t resultsBatchNumber = 0;`, `bool showingPartialResults = false;`. Optionally: `std::string searchError;` for error state. | `src/gui/GuiState.h` | [x] |
| 1.3.2 | Ensure any GuiState reset/clear logic clears partialResults and sets resultsComplete when starting a new search (or document where this is done in SearchController). | `src/gui/GuiState.cpp` or SearchController | [x] |

### 1.4 Add streamPartialResults to AppSettings

**Compliance (this section):** Naming (snake_case for member). Reuse existing JSON persist/load pattern (DRY); no new code paths for "backward compat" that are unused.

| Step | Action | File(s) | Done |
|------|--------|---------|------|
| 1.4.1 | Add `bool streamPartialResults = true;` to AppSettings. Persist/load in settings JSON. | `src/core/Settings.h`, settings load/save | [x] |

### 1.5 Refactor SearchWorker: Completion-Order Loop + Collector

**Design patterns (assessment):** **Facade** — SearchWorker stays the single interface; `GetResults()` unchanged; `GetStreamingCollector()` only for PollResults when streaming on. **Strategy** — keep LoadBalancingStrategy for “how” work is split; SearchWorker only changes “when” (completion-order) and “where” (collector). **Single-threaded back-off loop** — `wait_for(0)` then `wait_for(5ms)`; re-check all after any completion. **Robust error/cancellation** — drain futures (try/catch `.get()`), keep partial, mark complete.

**Compliance (this section):** **Critical:** Explicit lambda captures only (no `[&]`/`[=]`), especially in any template or loop. Nesting ≤3 (early returns, extract helpers); complexity ≤25; max 1–2 breaks per loop (combine conditions or extract function). Catch specific exception types; no empty catch. Reuse existing chunk→SearchResult conversion (DRY). Use C++17 init-statements where variable only used in `if`. Run clang-tidy.

| Step | Action | File(s) | Done |
|------|--------|---------|------|
| 1.5.1 | In `RunFilteredSearchPath` (or equivalent): when `streamPartialResults` is true, create `StreamingResultsCollector` (e.g. batch_size 500, interval 50 ms). When false, keep existing path (current `ProcessSearchFutures` in order, then set results_ once). | `src/search/SearchWorker.cpp` | [x] |
| 1.5.2 | Implement **completion-order loop** (when streaming): maintain list/vector of indices of not-yet-consumed futures. Loop: (a) Check `cancel_current_search_` at start of iteration; if set, drain remaining futures (try/catch each `.get()`), call `MarkSearchComplete()` on collector, exit. (b) For each pending future, `wait_for(0)`. If any is ready, `.get()` that future, convert chunk data to SearchResult (reuse existing conversion from current post-process), push to collector via `AddResult` (or add batch method), remove future from pending list, **break** and re-check from top. (c) If none ready, `wait_for(5ms)` on first pending future, then repeat. (d) When pending list empty, call `MarkSearchComplete()`. **Verify:** Nesting ≤3 (extract drain/consume helpers if needed); max 1–2 breaks per loop; no `[&]`/`[=]` in any lambda; clang-tidy clean. | `src/search/SearchWorker.cpp` | [x] |
| 1.5.3 | **Error handling in loop:** Wrap `.get()` in try/catch. On exception: set collector error state (if API exists), call `MarkSearchComplete()`, drain remaining futures in a loop (try/catch each `.get()`), exit. | `src/search/SearchWorker.cpp` | [x] |
| 1.5.4 | **Future cleanup:** Ensure every future is consumed (`.get()`) or explicitly abandoned; document. No valid future left without calling .get() to avoid blocking destructors. | `src/search/SearchWorker.cpp` | [x] |
| 1.5.5 | Expose collector to callers: add `StreamingResultsCollector* GetStreamingCollector();` (or return optional) so PollResults can call `GetAllPendingBatches()`. When streaming disabled, return nullptr or no-op. | `src/search/SearchWorker.h`, `.cpp` | [x] |
| 1.5.6 | Keep existing `GetResults()`: when streaming was used, final results can be taken from collector's `GetAllResults()` (or from accumulated partialResults) when search complete; so GetResults() still returns full vector for backward compatibility. | `src/search/SearchWorker.cpp` | [x] |

### 1.6 CMake and Build

**Compliance (this section):** Mirror existing CMake patterns (style, indentation). If new test target shares sources with main app, apply same PGO compiler flags to test target per AGENTS document. No inline `#` in `.clang-tidy` if touched.

| Step | Action | File(s) | Done |
|------|--------|---------|------|
| 1.6.1 | Add `StreamingResultsCollector.cpp` to main app sources in CMakeLists.txt. Add test target for collector tests if new file. | `CMakeLists.txt` | [x] |
| 1.6.2 | Verify Windows build; run `scripts/build_tests_macos.sh` on macOS. | — | [x] |

### Phase 1 Exit Criteria

- [x] `StreamingResultsCollector` compiles and passes unit tests.  
- [x] SearchWorker completion-order loop runs with streaming on; collector receives results.  
- [x] When streaming off, existing behaviour unchanged (ProcessSearchFutures path).  
- [x] No regressions in existing tests.  
- [ ] Thread sanitizer clean on streaming path (optional but recommended).  
- [x] **Compliance:** clang-tidy clean on all new/touched files; no new Sonar issues (nesting, complexity, breaks, const refs); no code duplication >10 lines; AGENTS document rules followed (naming, includes, lambdas, RAII).

---

## Phase 2: UI Integration (Partial Display)

**Goal:** UI shows partial results while search runs; status shows "Searching... N results"; when complete, final results and sort as today.

### 2.1 SearchController::PollResults (Streaming Path)

**Design patterns (assessment):** **GetAllPendingBatches API** — call once per frame; coalesces updates, maintains 60 FPS. Append to `state.partialResults`; full replace only on complete (with WaitForAllAttributeLoadingFutures before replace). **Robust error** — on collector error state: resultsComplete, show partial + error, clear searchActive.

**Compliance (this section):** Nesting ≤3; complexity ≤25. Const refs for read-only params; `std::string_view` where applicable. Reuse existing PollResults / attribute-loading logic (DRY). No new Sonar issues (S134, S3776, S1066).

| Step | Action | File(s) | Done |
|------|--------|---------|------|
| 2.1.1 | When streaming enabled and SearchWorker has a streaming collector: each frame, if collector has new data or is complete, call **`GetAllPendingBatches()`** once. Append returned results to `state.partialResults`. If search is complete, replace `state.searchResults` with final set (from collector or accumulated partialResults), clear `state.partialResults`, set `state.resultsComplete = true`, then call existing `WaitForAllAttributeLoadingFutures(state)` **before** the full replace. For partial append, do **not** wait for attribute futures. | `src/search/SearchController.cpp` | [x] (UpdateSearchResults calls WaitForAllAttributeLoadingFutures) |
| 2.1.2 | Unify "has new" for UI: e.g. `HasNewResults()` true when collector has pending batches or search just completed (so one shot for final replace). Ensure existing callers of PollResults still work. | `src/search/SearchWorker.cpp`, SearchController | [x] |
| 2.1.3 | Set `state.showingPartialResults = true` when first partial batch arrives; set `false` when resultsComplete set. | `src/search/SearchController.cpp` | [x] |
| 2.1.4 | On error (collector has error state): set resultsComplete, show partial results and error indicator; clear searchActive. | `src/search/SearchController.cpp` | [x] (state.searchError set; **UI does not yet show searchError** — see Gaps) |

### 2.2 ResultsTable and Display Source

**Compliance (this section):** ImGui immediate-mode rules (no widget storage; state from params). Const refs; nesting ≤3. Float literals with `F` suffix if added. NOSONAR only on same line if needed.

| Step | Action | File(s) | Done |
|------|--------|---------|------|
| 2.2.1 | In ResultsTable (or equivalent): when `!state.resultsComplete && state.showingPartialResults`, render from `state.partialResults`; otherwise render from `state.searchResults`. Virtual scrolling (ImGuiListClipper) applies to whichever source is active. | `src/ui/ResultsTable.cpp` (or current results table file) | [x] (via GetDisplayResults) |
| 2.2.2 | Add status line or tooltip: "Searching... N results" while partial; "N results" when complete. | Same file / StatusBar | [x] (StatusBar: "Displayed: N" + "(no filters, no sort yet)") |

### 2.3 SearchResultsService

**Design patterns (assessment):** **Progressive filtering vs. full sort** — Phase 2: minimal or no filtering on partial; defer full sort until resultsComplete. Keeps UI snappy; avoid sorting 1M items every batch.

**Compliance (this section):** Reuse existing GetDisplayResults/filter logic (DRY). Const refs; limit parameters (≤7 or struct). No duplicated filter logic between partial and final paths.

| Step | Action | File(s) | Done |
|------|--------|---------|------|
| 2.3.1 | When `!resultsComplete`, GetDisplayResults (or equivalent) returns partialResults with minimal or no filtering; when resultsComplete, use existing logic (filteredResults, etc.). Defer full sort until resultsComplete. | `src/search/SearchResultsService.cpp` | [x] |

### 2.4 StatusBar

**Compliance (this section):** Const refs; no new duplication with other status text paths. Follow existing StatusBar patterns.

| Step | Action | File(s) | Done |
|------|--------|---------|------|
| 2.4.1 | Show dynamic count: "Searching... N results" or "N results" when complete. | `src/ui/StatusBar.cpp` | [x] |

### Phase 2 Exit Criteria

- [x] Partial results appear incrementally during search.  
- [x] Final results replace partial when search completes; sort runs.  
- [x] Virtual scrolling works with partial results.  
- [x] No visual regressions when streaming disabled.  
- [x] Wait for attribute futures only on full replace (no wait on append).  
- [x] **Compliance:** clang-tidy clean; no new Sonar issues; DRY (no duplicated display/reset logic); ImGui rules (immediate mode, no widget storage).

---

## Phase 3: Progressive Sorting and Filtering (Optional Enhancement)

**Goal:** Lightweight sort on partial results when user interacts; full sort when complete. Avoid sorting 1M items every batch.

**Design patterns (assessment):** **Progressive filtering vs. full sort** — sort only when user interacts or when search completes. Never keep 1M results perfectly sorted while streaming (performance trap).

### 3.1 Sorting

**Compliance (this section):** Extract shared sort logic into one place (DRY); differentiate partial vs full only by data source and scope. Const refs; complexity ≤25.

| Step | Action | File(s) | Done |
|------|--------|---------|------|
| 3.1.1 | In HandleTableSorting (or equivalent): if `!resultsComplete`, perform sort on `partialResults` only (lightweight). If resultsComplete, full sort on final results. | `src/search/SearchResultsService.cpp` or table logic | [ ] (deferred: sort applies to final only; label "(no filters, no sort yet)" during streaming) |
| 3.1.2 | When search completes and we replace with final results, trigger one full sort so display is correct. | SearchController / GuiState | [x] (HandleTableSorting runs when resultsUpdated; sort on searchResults after replace) |

### 3.2 Filtering

**Compliance (this section):** Reuse existing time/size filter logic (DRY). Invalidate caches in one place; no duplicated invalidation code.

| Step | Action | File(s) | Done |
|------|--------|---------|------|
| 3.2.1 | Apply time/size filters incrementally to partial results if needed; or defer filtering until resultsComplete to keep Phase 2 simple. Document choice. | SearchResultsService | [ ] |
| 3.2.2 | Invalidate filter caches only on new batch or complete, not every frame. | Same | [ ] |

### Phase 3 Exit Criteria

- [ ] User can sort partial results; final sort on completion.  
- [ ] No performance regression on large result sets.  
- [ ] **Compliance:** Sort/filter logic shared (DRY); no new Sonar or clang-tidy issues.

---

## Phase 4: Optional (Later)

**Design patterns (assessment):** **Coordination with Large Result Set** — streaming and result limiting are complementary. Collector aware of `max_results`; when limit hit, signal engine to stop and mark collector complete.

- **Result limiting (max_results):** When LARGE_RESULT_SET_DISPLAY_PLAN Phase 1 is implemented, make StreamingResultsCollector aware of max_results; when limit hit, signal search to stop and mark collector complete (per assessment).  
- **Platform wake-up:** Optional ResultUpdateNotifier callback to wake UI when new batch available (Observer pattern from assessment; Phase 4 in analysis).  
- **Streaming metrics:** Add `time_to_first_result_ms`, `batches_delivered` to SearchMetrics; expose in metrics UI.

---

## Dependency Summary

```
Pre-Implementation (P1–P7)
    → Phase 1.1 StreamingResultsCollector
    → Phase 1.2 ResultUpdateNotifier (optional)
    → Phase 1.3 GuiState
    → Phase 1.4 AppSettings
    → Phase 1.5 SearchWorker refactor
    → Phase 1.6 CMake
    → Phase 2.1 PollResults
    → Phase 2.2 ResultsTable
    → Phase 2.3 SearchResultsService
    → Phase 2.4 StatusBar
    → Phase 3 (optional)
    → Phase 4 (optional)
```

---

## Gaps and possible omissions

Items that may have been forgotten or left for follow-up after merge:

1. **Show streaming search error in UI (2.1.4)**  
   `state.searchError` is set in `SearchController::PollResults` when the collector has an error (e.g. exception during search). It is **not** displayed anywhere: not in StatusBar, ResultsTable, or a popup. Users have no visible indication when a streaming search fails. **Recommendation:** Show `state.searchError` in the status bar (e.g. red "Error: …") or a small error banner when non-empty; clear when a new search starts.

2. **Phase 3 — Sort/filter on partial (optional)**  
   By design we deferred: during streaming we do not sort or filter `partialResults`; the label "(no filters, no sort yet)" documents this. If product wants "sort partial list when user clicks column," implement 3.1.1 and optionally 3.2.1; otherwise leave as-is.

3. **Phase 4 — Later**  
   Result limiting (max_results + collector), ResultUpdateNotifier (platform wake-up), and streaming metrics (`time_to_first_result_ms`, `batches_delivered`) are not implemented; plan marks them optional/later.

4. **Pre-Implementation P2, P4–P7**  
   Cancellation semantics (P2) are only partially documented (cancel = new search clears partial). Formal design review (P4), test plan doc (P5), acceptance sign-off (P6), and production checklist pass (P7) were not completed as separate deliverables; consider doing them post-merge for audit trail.

5. **Integration tests**  
   Plan P5 called for integration tests (SearchWorker + collector, PollResults streaming on/off). Only `StreamingResultsCollectorTests` exist; no test that starts a search with streaming and asserts PollResults/display behaviour. Consider adding in GuiStateTests or a small integration test.

---

## References (Current Documents)

| Document | Use |
|----------|-----|
| `docs/research/PARTIAL_RESULTS_STREAMING_ANALYSIS.md` | Design, components, phases, API sketches |
| `docs/research/STREAMING_SEARCH_RESULTS_ASSESSMENT.md` | **Implementation strategies** (GetAllPendingBatches, back-off loop, progressive filtering, error/cancel); **recommended design patterns** (Pipeline, Observer, Throttling/Batching, Facade, Strategy); coordination with result limiting |
| `docs/research/2026-01-31_STREAMING_BLIND_SPOTS.md` | Decisions, recommendations (§3/§7/§8), pre-implementation checklist |
| `docs/plans/features/LARGE_RESULT_SET_DISPLAY_PLAN.md` | Coordination: streaming + result limiting complementary |
| `docs/plans/production/PRODUCTION_READINESS_CHECKLIST.md` | Pre-implementation and pre-merge checklist |

---

*Last updated: 2026-01-31 (compliance: AGENTS document, DRY, Sonar, clang-tidy per step; design patterns from STREAMING_SEARCH_RESULTS_ASSESSMENT.md integrated)*
