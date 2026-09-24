## Results Auto-Refresh Double Buffering Spec

**Date:** 2026-02-24 (status updated 2026-09-04)  
**Project:** USN_WINDOWS / FindHelper  
**Area:** Search UI – Results table, auto-refresh behavior

### 1. Overview

When auto-refresh is enabled and the underlying index changes (especially on Windows with USN monitoring), the current implementation:
*(historical problem statement — behavior before §9)*

- Cleared `GuiState` search results (`ClearSearchResults`) as soon as an auto-refresh search started.
- Left a window of time where `SearchResultsService::GetDisplayResults(state)` returned an empty vector.
- Caused the ImGui results table to briefly render 0 rows before new results arrive, producing a visible “blink”.

Goal of this spec: **eliminate the blank-table frame during auto-refresh** by introducing a **model-level front/back results buffering** approach, while preserving existing streaming, filtering, and sorting behavior.

**Implementation status (2026-09-04):**

| Layer | Status | Where |
| --- | --- | --- |
| **§9 – No early clear** | **Shipped** (auto-refresh + debounced instant search) | `BeginInFlightSearchKeepingVisibleResults`; manual search still clears at start |
| **Ephemeral back buffer in `PollResults`** | **Shipped** (2026-09 branch) | Stack-local `ResultPoolOwner` + merge/convert/reconcile/pre-sort → skip-swap or `CommitNewSearchResults` — see **§11** |
| **§1–8 – Persistent GuiState front/back pools** | **Not implemented** (and not required for shipped behavior) | Dual long-lived buffers / promotion API remain design-only |
| **Async Size/Date sort-before-promote** | **Partial / open** | Sync columns pre-sort on back buffer; Size/Last Modified still may re-sort after promote (Approach B residual) |

Key principles:

- UI (ImGui) continues to render “whatever `GetDisplayResults(state)` returns”; we change the **model semantics**, not the table rendering logic.
- Old results remain visible while an auto-refresh search is in progress, until the new results are ready to be presented.
- Behavior is cross-platform and OS-agnostic, but the main user impact is on Windows where auto-refresh is most active.

### 2. Requirements and Assumptions

#### Functional requirements

- **FR1 – No blank table on auto-refresh**
  - After at least one successful search, with auto-refresh enabled, when the index changes enough to trigger auto-refresh:
    - The results table must not show a frame with “0 rows” unless the **final** auto-refreshed result set is truly empty.
    - The user either sees the old results until new results are ready, or a smooth transition directly to the new results.

- **FR2 – Correct results after auto-refresh**
  - When auto-refresh completes:
    - The displayed results (as seen via `GetDisplayResults(state)`) reflect the new index state.
    - Status bar counts (displayed results, total indexed items) match the table content.

- **FR3 – Preserve manual search behavior; debounced matches auto-refresh §9**
  - Manual search (button/Enter) clears previous results at search start via `ClearSearchResults`.
  - Instant/debounced search keeps previous results visible until `PollResults` replaces them (same §9 semantics as auto-refresh; shipped 2026-09-02).
  - Existing streaming semantics for manual searches must be preserved where applicable.

- **FR4 – Preserve streaming behavior where enabled**
  - When streaming partial results is enabled:
    - Streaming for manual and debounced searches must continue to function as designed.
    - Auto-refresh must not regress streaming semantics; any intentional differences must be documented in code comments and this spec.

- **FR5 – Filters, sorting, and metrics remain correct**
  - Column sorting, time and size filters, and status bar metrics:
    - Must remain correct across auto-refresh runs.
    - Must apply to whatever results set is currently considered “front” (visible) after any swap.

#### Non-functional requirements

- **NFR1 – Performance**
  - The buffering approach must not introduce noticeable UI stalls or excessive allocations in hot paths.
  - Any temporary duplication of results/path pools during auto-refresh must:
    - Be bounded in time (only while auto-refresh is in progress).
    - Be cleaned up promptly once the new results are promoted.

- **NFR2 – Platform**
  - Implementation must be cross-platform and OS-agnostic in logic.
  - No changes inside platform-specific `#ifdef _WIN32` / `__APPLE__` / `__linux__` blocks to “unify” code; instead, use platform-agnostic abstractions where necessary.

- **NFR3 – Code quality**
  - Comply with:
    - `AGENTS.md` (always-applied project rules).
    - C++17 only (no older, no newer features).
    - clang-tidy and SonarQube rules (no new issues; any required `// NOSONAR` comments are on the same line as the issue).
  - Use:
    - RAII for resources (no manual `new`/`delete`).
    - `std::string_view` for read-only string parameters.
    - In-class initializers for default member values.
    - Explicit lambda captures (no `[&]` / `[=]`, especially in templates).
    - `std::vector` / `std::array` instead of C-style arrays.

#### Current architecture summary (assumptions from code)

- **Ownership of results (GuiState)** — *as of 2026-09-04*
  - `result_pool_` (`ResultPoolOwner`) – canonical front results vector + path pool (`SearchResult.fullPath` views).
  - Filter caches: `filteredResults` / `sizeFilteredResults` (invalidated on commit).
  - Flags: `resultsComplete`, `timeFilterCacheValid`, `sizeFilterCacheValid`, `deferFilterCacheRebuild`, `resultsUpdated`, etc.
  - No persistent dual front/back pools; ephemeral back buffer lives only inside `PollResults` (§11).

- **Controller (SearchController)** — *as of 2026-09-04*
  - `HandleAutoRefresh` / debounced path: call `BeginInFlightSearchKeepingVisibleResults` (no early `ClearSearchResults`); keep `result_pool_` visible until handoff.
  - Manual search: still calls `ClearSearchResults` at start.
  - `PollResults(state, search_worker, folder_aggregator, file_index, show_path_hierarchy_indentation)`:
    - Builds an ephemeral back buffer (`ResultPoolOwner`), merges worker data, reconciles directory attrs from the front buffer, pre-sorts with the same hierarchy/sort settings as `ResultsTable`, then either **skips swap** (`AreSearchResultsEqual`) or **commits** via `CommitNewSearchResults` (moves results + path pool into `state.result_pool_`).
    - Superseded generations are skipped when `search_worker.IsBusy()`.
  - Streaming partial-result fields referenced in older §2–§4 text are **not** present in the current codebase (non-streaming handoff only).

- **Display service (SearchResultsService)**
  - `GetDisplayResults(const GuiState&)`:
    - If filter caches are valid and rebuild not deferred: returns filtered vectors.
    - Otherwise: returns `state.result_pool_->Results()` (single front buffer; ephemeral back buffer never displayed).

- **UI (ResultsTable, SearchInputs, StatusBar, Application)**
  - Use `SearchResultsService::GetDisplayResults(state)` or equivalent logic to determine which results to render or export.
  - All are driven from the same `GuiState` instance on the UI (ImGui) thread.
  - `ResultsTable` passes `settings.showPathHierarchyIndentation` into sorting; `Update` passes the same flag into `PollResults` so back-buffer order matches.

### 3. User Stories

| ID | Priority | Summary | Gherkin (abridged) |
| --- | --- | --- | --- |
| US1 | P0 | No blank table on auto-refresh | **Given** I have run a search and see non-empty results in the table **And** auto-refresh is enabled **When** the index changes enough to trigger auto-refresh **Then** the table must not show an empty “0 results” frame **And** I either see the old results until new ones are ready or a smooth transition directly to the new results. |
| US2 | P0 | Correct results after auto-refresh | **Given** auto-refresh is enabled **And** files have been added/removed/renamed in the indexed paths **When** auto-refresh completes **Then** the displayed results reflect the new index state correctly **And** counts in the status bar match the table content. |
| US3 | P0 | Manual search unchanged; debounced matches §9 | **Given** I run a manual search (button/Enter) **When** the search runs **Then** results clear at start and behavior is unchanged. **Given** instant/debounced search **When** the debounce fires **Then** previous rows stay visible until new results arrive (§9). |
| US4 | P1 | Preserve streaming behavior where enabled | **Given** streaming of partial results is enabled in settings **And** auto-refresh starts while streaming is active **When** new partial batches arrive **Then** I can still see progressive updates for the new run (where configured) **And** I do not see a temporary blank table before the first new batch is visible. |
| US5 | P1 | Filters and sorting preserved across auto-refresh | **Given** I have an active time/size filter and a specific column sort order **When** auto-refresh triggers and completes **Then** the new visible results are filtered and sorted with the same options **And** the UI does not briefly show unfiltered zero rows. |
| US6 | P1 | Status bar remains accurate | **Given** auto-refresh is enabled and runs periodically on Windows with USN events **When** auto-refresh starts, runs, and completes **Then** the status bar continues to show accurate search status and displayed result count **And** these indicators are consistent with what the table actually shows. |
| US7 | P2 | Identical results auto-refresh stays invisible | **Given** auto-refresh is enabled **And** the logical result set does not change when the index updates **When** auto-refresh runs **Then** the user does not perceive any visual change (no blink, no re-sorting noise) **And** skip-swap via `AreSearchResultsEqual` avoids unnecessary commits (**shipped 2026-09-04** for sync sort columns + directory reconcile; Size/Date attr gaps remain — §11). |
| US8 | P2 | Large result sets still performant | **Given** a search returns a very large result set **When** auto-refresh triggers **Then** the buffering approach does not introduce noticeable UI stalls **And** memory usage stays within acceptable bounds for this feature. |

### 4. Architecture Overview

#### 4.1 Existing flow (simplified)

- **Background (SearchWorker, background thread)**
  - Produces raw `SearchResultData` batches from the `FileIndex`.

- **SearchController (UI thread)**
  - `Update(state, search_worker, monitor, is_index_building, settings, file_index)` orchestrates:
    - Debounced searches (search-as-you-type).
    - Manual searches.
    - Auto-refresh triggered by index size changes.
    - Polling results via `PollResults`.
  - `HandleAutoRefresh`:
    - Currently clears all search results (front buffer) via `ClearSearchResults`, then kicks off a new search.
  - `PollResults`:
    - For streaming: uses `GetStreamingCollector`, `ConsumePendingStreamingBatches`, `FinalizeStreamingSearchComplete`.
    - For non-streaming: uses `HasNewResults`, `GetResultsData`, `ApplyNonStreamingSearchResults`.

- **GuiState (UI thread, model)**
  - Stores:
    - `searchResults`, `partialResults`, filter caches, folder stats.
    - `searchResultPathPool` (backing store for `SearchResult.fullPath`).
    - Flags that indicate streaming/complete state, filter validity, etc.

- **SearchResultsService (UI thread)**
  - `GetDisplayResults` chooses which vector should be displayed:
    - Partial results when streaming and incomplete.
    - Otherwise, filtered caches if valid.
    - Falls back to `searchResults`.

- **UI (ResultsTable, SearchInputs, StatusBar, Application logic)**
  - Read from `GetDisplayResults` (or equivalent logic) and `GuiState`.
  - Render the ImGui table, export CSV, handle shortcuts, and display metrics.

This design is already strongly model-driven (UI reads from `GuiState`), which makes it well-suited to model-level double buffering.

#### 4.2 New design: front/back results buffering for auto-refresh

**Core idea:** maintain two logical result buffers in `GuiState`:

- **Front buffer** – the results and path pool that the UI currently considers authoritative and renders via `GetDisplayResults`.
- **Back buffer** – a temporary container where auto-refresh searches write their data until ready to be promoted.

We implement this as:

- New fields in `GuiState` (names indicative, final naming must follow project conventions):
  - `std::vector<char> autoRefreshPathPool{};`
  - `std::vector<SearchResult> autoRefreshResults{};`
  - `bool autoRefreshInProgress = false;`
  - Optionally `bool autoRefreshHasNewResults = false;` (if we decide to show back-buffer streaming intermediate states).

**Invariants:**

- When `autoRefreshInProgress == false`:
  - `autoRefreshResults` and `autoRefreshPathPool` are empty (or at least unused).
  - `searchResults` / `searchResultPathPool` represent the **current** authoritative results.
- When `autoRefreshInProgress == true`:
  - `autoRefreshResults` / `autoRefreshPathPool` contain data for the **in-progress auto-refresh search**.
  - Front buffers remain valid until we explicitly promote the back buffer.

#### 4.3 Auto-refresh start sequencing (HandleAutoRefresh)

Today:

- `HandleAutoRefresh`:
  - Checks `state.autoRefresh`, `state.searchResults.empty()`, index size, and `search_worker.IsBusy()`.
  - When all conditions pass:
    - Calls `ClearSearchResults(state, "auto-refresh search started")`.
    - Sets search flags and starts a new search.
  - This clears `searchResults` and related vectors, causing the blank-table frame.

New behavior for **auto-refresh only**:

- When auto-refresh conditions pass:
  - Do **not** call `ClearSearchResults` for the front buffer.
  - Instead:
    - Set `state.autoRefreshInProgress = true;`
    - Clear and reset back-buffer fields:
      - `autoRefreshResults.clear();`
      - `autoRefreshPathPool.clear();`
      - Any back-buffer-specific caches or flags if introduced.
    - Keep front-buffer `searchResults` and `searchResultPathPool` intact.
    - Start the new search via `search_worker.SetStreamPartialResults(settings.streamPartialResults)` and `StartSearch(...)` as today.

Manual/debounced searches may still use `ClearSearchResults` as before; the new semantics are specific to auto-refresh.

#### 4.4 Polling results into the back buffer

We extend `SearchController::PollResults` to be **aware of auto-refresh state**:

- **Non-streaming path (`GetStreamingCollector() == nullptr`):**
  - When `autoRefreshInProgress == false`:
    - Behavior unchanged:
      - Materialize `new_results` using `searchResultPathPool`.
      - Pass to `ApplyNonStreamingSearchResults` (front buffer).
  - When `autoRefreshInProgress == true`:
    - Use `autoRefreshPathPool` as the path backing store for new `SearchResult` instances.
    - Introduce a helper, e.g. `ApplyAutoRefreshResults(GuiState&, std::vector<SearchResult>&& new_results, bool is_complete)`, which:
      - Writes to `autoRefreshResults`.
      - When `is_complete == true`, triggers promotion of the back buffer to front (see 4.5).

- **Streaming path (`GetStreamingCollector() != nullptr`):**
  - When `autoRefreshInProgress == false`:
    - Behavior unchanged, writing into `partialResults` and then into `searchResults` on completion.
  - When `autoRefreshInProgress == true`:
    - `ConsumePendingStreamingBatches` should append streaming batches to `autoRefreshResults`, using `autoRefreshPathPool`.
    - `FinalizeStreamingSearchComplete` should:
      - Use streaming collector data to produce a final `autoRefreshResults` set in `autoRefreshPathPool`.
      - Then promote the back buffer to front (see 4.5).

The precise UX decision—whether to expose partial back-buffer results mid-run or to keep showing front-buffer results until the new run completes—can be tuned; the core mechanism supports both by controlling **when** we promote and which buffer `GetDisplayResults` sees.

#### 4.5 Promoting back buffer to front buffer

Promotion happens when an auto-refresh search is considered **ready** to replace the visible results. For the first implementation, promotion is likely tied to:

- Non-streaming: `is_complete == true` in `ApplyAutoRefreshResults`.
- Streaming: `collector->IsSearchComplete() == true` in `FinalizeStreamingSearchComplete`.

Promotion steps:

1. Ensure all futures for attribute loading and cloud file loading for the **front buffer** are resolved (similar to `WaitForAllAttributeLoadingFutures` semantics).
2. Swap the buffers atomically:
   - `std::swap(state.searchResults, state.autoRefreshResults);`
   - `std::swap(state.searchResultPathPool, state.autoRefreshPathPool);`
3. Invalidate caches and stats, mirroring `UpdateSearchResults`:
   - `timeFilterCacheValid = false;`
   - `sizeFilterCacheValid = false;`
   - `InvalidateDisplayedTotalSize();`
   - `InvalidateFolderStats();`
   - `filteredResults.clear();`
   - `sizeFilteredResults.clear();`
   - Mark `resultsUpdated = true;`
   - Set `resultsComplete` and `showingPartialResults` appropriately for the new front buffer.
4. Clear auto-refresh state:
   - `autoRefreshResults.clear();`
   - `autoRefreshPathPool.clear();`
   - `autoRefreshInProgress = false;`
   - Any additional back-buffer flags reset.

After this point, `SearchResultsService::GetDisplayResults` continues to operate as before, but now “front buffer” refers to the newly swapped-in results.

#### 4.6 Interaction with SearchResultsService::GetDisplayResults

The design intentionally keeps `GetDisplayResults` **front-buffer-centric**:

- It only ever reasons about:
  - `partialResults`, `searchResults`.
  - Filter caches (`filteredResults`, `sizeFilteredResults`) and validity flags.
  - `deferFilterCacheRebuild`.
- The auto-refresh back buffer affects which data ends up in `searchResults` and when, but:
  - `GetDisplayResults` itself does not need to know about `autoRefreshResults`.
  - All UI consumers continue to go through the same API.

This keeps UI code simple and localized; the double buffering is purely a **model and controller** concern.

#### 4.7 Threading and platform considerations

- **Threading**
  - All modifications to `GuiState` happen on the UI thread.
  - `SearchWorker` remains responsible for background work; communication is via its existing APIs (`HasNewResults`, `GetResultsData`, `GetStreamingCollector`, etc.).
  - No new cross-thread sharing of `GuiState` or its buffers is introduced.

- **Platform**
  - Auto-refresh logic is already cross-platform, but is primarily exercised on Windows with USN monitoring.
  - No changes are made inside platform-specific `#ifdef` blocks; behavior is controlled through `SearchController` and `GuiState` abstractions.

### 5. Acceptance Criteria

| Story | Criterion | Measurable check |
| --- | --- | --- |
| US1 | No blank table frame on auto-refresh | With auto-refresh on and a non-empty result set, trigger an index change; observe frames (via ImGui test or manual instrumentation): there must be no frame where `GetDisplayResults(state)` is empty unless the final auto-refreshed result set is actually empty. |
| US1 | Old results remain visible until new data usable | During auto-refresh start, verify (logs/asserts) that `searchResults` is not cleared until either (a) we are ready to promote the back buffer, or (b) the new search legitimately results in 0 items. |
| US2 | Correctness of refreshed results | After auto-refresh completion, verify that added files appear and removed files disappear in the results table; status bar counts match the visible rows. Add or extend regression tests using `IRegressionTestHook` for a small fixture. |
| US3 | Manual/debounced behavior unchanged | Existing regression tests for manual and instant/debounced search must pass unchanged. Subjective UI behavior should show no new flicker or regressions. |
| US4 | Streaming still works (where enabled) | With streaming enabled, auto-refresh must either (a) keep old results visible until the new run produces visible data, or (b) show partial new results without an intervening 0-row frame. Tests/logs confirm no blank frame. |
| US5 | Filters/sorting preserved across refresh | Before auto-refresh, set filters and sort order. After auto-refresh completion, `GetDisplayResults` must yield a vector that obeys these same filters and sort order; cache invalidation and rebuild behavior must be consistent. |
| US6 | Status bar consistent | During auto-refresh, the status bar’s “displayed results” and search status (Searching/Idle/Loading attributes) must always be consistent with what the table shows (no transient “0 displayed” when the table shows old or new non-empty results). |
| US7 | No visible change when results identical | When the logical results do not change under auto-refresh, logs must show `ShouldUpdateResults == false` and `CompareSearchResults` reporting “identical”; the UI must show no visible update or blink. |
| US8 | Performance and memory acceptable | On large indexes (e.g. 100k+ results), auto-refresh must not introduce perceptible stutters beyond current behavior. Any temporary front/back buffer duplication is short-lived and released on completion. clang-tidy / Sonar must stay clean regarding performance/hot-path rules. |

Global quality gates:

- No new clang-tidy or SonarQube issues in modified files (or each is locally suppressed with a justified `// NOSONAR` comment on the same line).
- C++17 only; apply patterns from `AGENTS.md` (init-statements, `std::string_view`, explicit lambda captures, RAII, no `void*`, no `reinterpret_cast`).
- No changes to platform-specific blocks to unify behavior.
- DRY: no duplicated constants or duplicated “select display results” logic.

### 6. Task Breakdown

| Phase | Task | Depends on | Est. (h) | Notes |
| --- | --- | --- | --- | --- |
| Spec | Finalize this spec and confirm scope | – | 0.5 | Done once approved. |
| Spec | §10 instant-search findings and recommendations | Spec | 0.5 | Done 2026-09-02. |
| Analysis | Map all auto-refresh and results paths | Spec | 0.5 | Confirm all uses of `searchResults`, `partialResults`, `GetDisplayResults`, and `ClearSearchResults`. |
| Design | Design `GuiState` front/back additions | Spec | 1.0 | Choose names, document invariants (especially around path pools and string_view lifetimes). |
| Impl-1 | Extend `GuiState` with back-buffer fields | Design | 0.5 | Add `autoRefreshResults`, `autoRefreshPathPool`, and flags with in-class initializers. |
| Impl-2 | Adjust `HandleAutoRefresh` start sequence | Impl-1 | 1.0 | **Done (§9):** removed `ClearSearchResults` for auto-refresh; set flags and keep front buffer. |
| Impl-2b | Extend §9 to debounced instant search | Impl-2 | 0.5–1.0 | **Done (2026-09-02):** `SearchControllerDetail.h` + debounced path in `SearchController::Update`. |
| Impl-3 | Add auto-refresh–aware helpers in `SearchController` | Impl-2 | 2.0 | Implement `ApplyAutoRefreshResults` / streaming-aware promotion helpers; ensure swap logic matches `UpdateSearchResults` invariants. |
| Impl-4 | Integrate with `PollResults` | Impl-3 | 1.0 | Route non-streaming and streaming paths to back-buffer logic when `autoRefreshInProgress` is true; keep manual/debounced behavior unchanged. |
| Impl-5 | Confirm `SearchResultsService::GetDisplayResults` remains front-buffer-centric | Impl-4 | 0.5 | Verify that promotion semantics alone ensure correct display; no or minimal changes needed in `GetDisplayResults`. |
| Impl-6 | Update comments/docs | Impl-5 | 0.5 | Update streaming design and auto-refresh docs to describe front/back buffering. |
| Tests-1 | Extend ImGui regression tests | Impl-4 | 2.0 | Add test(s) that enable auto-refresh, trigger index changes, and assert absence of a 0-row intermediate frame. |
| Tests-2 | Add unit/functional tests for `SearchController` | Impl-4 | 1.5 | Test auto-refresh state invariants and promotion logic at a non-UI level where feasible. |
| Verify | Run `scripts/build_tests_macos.sh` (macOS) | Impl+Tests | 0.5 | Required by project rules after C++ changes. |
| Review | Code review & static analysis check | Verify | 1.0 | Ensure no new clang-tidy/Sonar issues, verify invariants and performance. |
| Polish | Small refactors / cleanup (Boy Scout) | Review | 0.5 | Clarify comments, reduce duplication, simplify control flow where possible. |

Estimates are rough and assume a single developer familiar with the codebase.

### 7. Risks and Mitigations

| Risk | Impact | Mitigation |
| --- | --- | --- |
| Incorrect path-pool lifetime with double buffer | Could leave `SearchResult.fullPath` string_views dangling, causing crashes or corrupt paths under auto-refresh. | Maintain a strict invariant: each results vector only ever refers into its matching pool; pools are only cleared when their corresponding results are no longer used. Audit all `clear()`/`reserve()` calls and add assertions where appropriate. |
| Regression in streaming behavior | Streaming partial results for manual/instant/auto-refresh could stop appearing or behave inconsistently. | Restrict changes to auto-refresh-specific code paths. Keep streaming logic otherwise intact. Add at least one regression test covering auto-refresh with streaming enabled. |
| Increased memory usage for large result sets | Temporary duplication of large results/path pools during auto-refresh could be heavy. | Limit back-buffer lifetime strictly to `autoRefreshInProgress`; clear/shrink back buffers immediately after promotion. Consider additional guards or heuristics for extremely large sets if needed. |
| Auto-refresh vs manual search race | Manual search started while auto-refresh is in progress could cause confusing front/back states. | Define policy that manual/debounced searches cancel any in-progress auto-refresh before starting (clear `autoRefreshInProgress`, clear back buffers), and enforce it in `SearchController`. |
| Increased complexity in `SearchController` | Additional logic could push cognitive complexity beyond thresholds. | Extract new logic into small helpers (e.g. `ApplyAutoRefreshResults`, `PromoteAutoRefreshBuffersIfComplete`), use C++17 init-statements and early returns to keep nesting shallow. |
| Windows-only regressions hard to see on macOS dev | Auto-refresh behavior is most visible on Windows; issues may slip by if only tested on macOS. | Add cross-platform tests that simulate index changes without full USN stack, and a Windows-focused manual test checklist. When possible, run Windows builds/tests before merging. |

### 8. Validation and Handoff

#### Review checklist

- **Behavior**
  - Auto-refresh no longer produces a transient blank results table (unless the final result set is empty).
  - Manual, debounced, and streaming behaviors are unchanged except where explicitly and intentionally adjusted for auto-refresh.
  - Filters, sorting, and metrics remain correct after each auto-refresh run.

- **Correctness**
  - Path-pool lifetimes are correct for both front and back buffers; no string_view refers into a cleared pool.
  - `autoRefreshInProgress` and related flags are consistent and well-documented.
  - Cache invalidation and promotion logic match `UpdateSearchResults` invariants.

- **Quality gates**
  - No new clang-tidy or SonarQube violations (or each is justified with same-line `// NOSONAR`).
  - C++17 features only; follow `AGENTS.md` and `docs/standards/CXX17_NAMING_CONVENTIONS.md`.
  - No changes inside platform-specific `#ifdef` blocks purely to unify behavior.
  - DRY: no duplicated logic for selecting display results or managing buffers.

- **Verification**
  - `scripts/build_tests_macos.sh` run and passing on macOS after code changes.
  - ImGui test engine regression tests (including new auto-refresh tests) passing.
  - Manual smoke tests on Windows with USN monitor and auto-refresh enabled confirm the blinking issue is fixed.

#### Using this spec with Cursor Agent/Composer

- Treat this document as the **single source of truth** for implementing the auto-refresh double-buffering feature.

- When starting implementation with Cursor Agent/Composer, the prompt should explicitly ask the agent to:
  - **Respect project constraints**
    - Read and follow `AGENTS.md`, `internal-docs/prompts/AGENT_STRICT_CONSTRAINTS.md`, and this spec.
    - Use C++17 only, RAII, `std::string_view`, explicit lambda captures, and the other rules called out in NFR3 and the Code Quality sections.
    - Run `scripts/build_tests_macos.sh` after any C++ changes on macOS and ensure all tests pass.

  - **Work in small, reviewable steps**
    - Break the implementation into **small, logically independent steps** (e.g., “extend `GuiState` model”, “adjust `HandleAutoRefresh` semantics”, “wire `PollResults` to back buffer”, “add tests”), where each step:
      - Leaves the codebase in a **buildable, shippable** state.
      - Is small enough for a human to review in isolation.
    - For each step, state upfront:
      - The **goal** of the step.
      - The main files/functions expected to change.
      - The **acceptance criteria** and which tests (unit, ImGui regression, manual checks) will be run.
    - Do not proceed to the next step until the current step’s acceptance criteria are met and tests have been run.

  - **Identify and encode invariants for robustness**
    - Before coding, review sections 2–4 of this spec and:
      - Enumerate the key **invariants** that must always hold (e.g., correspondence between results vectors and their path pools, correctness of `autoRefreshInProgress` flags, cache validity, threading constraints).
      - Consider what **additional invariants** can be enforced in code (beyond those already documented) to make the system more robust.
    - For each invariant, decide:
      - Where it should be checked (API boundaries, promotion/swap points, cache invalidation, threading boundaries).
      - Whether to enforce it via **types** (e.g., enums, non-null wrappers) or via **debug assertions**.
    - Add assertions in debug builds (and comments only where intent is non-obvious) to document these invariants, especially around:
      - Swapping front/back buffers.
      - Managing `searchResultPathPool` vs `autoRefreshPathPool` lifetimes.
      - Search/auto-refresh state transitions (`searchActive`, `resultsComplete`, `showingPartialResults`, `autoRefreshInProgress`).

  - **Be explicit about failure modes and threading**
    - Enumerate key **failure modes** relevant to this feature (e.g., worker errors, index changes mid-run, oversized result sets, memory pressure) and ensure the implementation:
      - Detects them where feasible.
      - Either surfaces them via logs/status, degrades gracefully, or fails fast with clear assertions.
    - Reaffirm the **threading model** in the implementation:
      - All `GuiState` and ImGui interactions on the UI thread.
      - Background work isolated to `SearchWorker` and existing APIs, with no new cross-thread sharing of `GuiState`.

  - **Preserve observability and testability**
    - Where it aids debugging, extend or add:
      - Logging around auto-refresh start/completion, buffer promotion, and cache invalidation.
      - Lightweight counters/metrics for auto-refresh runs (e.g., number of promotions, duration, whether a run produced identical results and was skipped).
    - Update or add:
      - **Unit tests** for `SearchController` and related helpers, focusing on state transitions and invariants.
      - **ImGui test engine** scenarios that assert the absence of 0-row frames and validate filters/sorting/metrics across auto-refresh.

  - **Stick to KISS / YAGNI / DRY and SOLID**
    - Prefer the simplest design that satisfies this spec:
      - Avoid introducing new abstractions or indirection layers unless clearly justified by this spec.
      - Eliminate duplicated logic and constants (especially around “which results are displayed” and cache invalidation); reuse helpers or factor new ones as needed.
    - Keep each new helper or class focused on a **single responsibility**, with clear, minimal interfaces.

#### “What’s New” entry (for future release)

- After this feature ships, add a single high-level bullet to the “What’s new” section in `SearchHelpWindow.cpp` with the **git commit date** (YYYY-MM-DD), for example:
  - `2026-02-24 – Reduced auto-refresh flicker in the results table on Windows.`
- Keep only the last calendar month of such entries, removing older bullets to keep that list concise.

### 9. Addendum: Minimal Alternative (“No Early Clear”) Specification

This section documents a **lower-complexity alternative** to full front/back double buffering. It is intended to be considered *before* implementing the more complex design above.

#### 9.1 Core idea

Instead of adding a second results buffer and path pool, we **keep the existing single buffer** (`GuiState::searchResults` + `searchResultPathPool`) and change **when** the buffer is cleared for auto-refresh:

- Today, `SearchController::HandleAutoRefresh` calls `ClearSearchResults(state, "auto-refresh search started")` before starting a new search. Between this clear and the first new results arriving, `SearchResultsService::GetDisplayResults(state)` returns an empty vector, and the table draws 0 rows (blink).
- The minimal alternative is:
  - **Do not clear results at auto-refresh start.**
  - Only clear and replace `searchResults` / `searchResultPathPool` inside `PollResults`, in the same frame where we also assign new results.
  - The UI then transitions from “old results” directly to “new results”, without an intermediate blank frame.

No new buffers, flags, or path pools are introduced; the change is localized to the auto-refresh trigger path.

#### 9.2 Behavioral changes

**Auto-refresh start (`SearchController::HandleAutoRefresh`)**

- Preconditions remain the same:
  - `state.autoRefresh` is true.
  - `!state.searchResults.empty()` (at least one successful search completed before).
  - `current_index_size != state.lastIndexSize`.
  - `!search_worker.IsBusy()`.
- New behavior:
  - **Remove the call to `ClearSearchResults` in the auto-refresh path.**
  - Still:
    - Update `state.lastIndexSize`.
    - Set per-search flags (`searchActive = true`, `resultsComplete = false`, `showingPartialResults = false`, `searchWasManual = false`).
    - Configure `SearchWorker` and call `StartSearch(...)` exactly as today.
  - Leave:
    - `searchResults`, `filteredResults`, `sizeFilteredResults`, `searchResultPathPool`, folder stats, etc. unchanged at the moment auto-refresh begins.

From the user’s perspective:

- The table continues to show the previous results while auto-refresh is in progress.
- The status bar should still reflect “Searching…” as it does today.

**Non-streaming `PollResults` path** *(superseded detail — see §11 for current handoff)*

- Historically (§9 alone): clear + assign front buffer in the same frame when worker data arrived (removed early clear only).
- **As of 2026-09-04:** `PollResults` builds an ephemeral back buffer, pre-sorts, compares, and either skips or commits — documented in **§11**. Empty-table flicker remains solved by §9 keep-visible; sort flicker and identical-set churn are reduced by §11.

**Streaming `PollResults` path**

- Streaming partial-result APIs described in early drafts are **not** in the current codebase. Non-streaming handoff is the only path.

Manual search continues to use `ClearSearchResults` at search start. Debounced instant search uses the same §9 keep-visible semantics as auto-refresh (shipped 2026-09-02).

#### 9.3 Data model and API impact

- **GuiState**
  - No new members for §9; §11 also adds **no** persistent dual pools (ephemeral stack back buffer only).
  - Lifetimes and invariants for `result_pool_` remain: only the front pool is visible; back pool is moved in on commit or discarded on skip.
  - Flags (`resultsComplete`, `timeFilterCacheValid`, `sizeFilterCacheValid`, `deferFilterCacheRebuild`, etc.) continue to drive `GetDisplayResults`.

- **SearchResultsService::GetDisplayResults**
  - No changes expected for §9/§11: single front buffer.

- **SearchController**
  - §9: `HandleAutoRefresh` / debounce use `BeginInFlightSearchKeepingVisibleResults`.
  - §11: `PollResults` implements ephemeral double-buffer handoff + skip-swap.

#### 9.4 Acceptance criteria (minimal alternative)

- **AC-M1 – No auto-refresh blank frame**
  - With non-empty `searchResults` and auto-refresh enabled, when the index size changes and auto-refresh triggers:
    - Before auto-refresh: table shows the old results.
    - Between auto-refresh start and the first new results/batch: table still shows old results.
    - Once new results/batches are available: table switches directly to new results (partial or complete).
    - At no time should `GetDisplayResults(state)` be empty unless the **final** refreshed result set is truly empty.

- **AC-M2 – Manual vs debounced**
  - Manual searches still clear previous results at search start via `ClearSearchResults`.
  - Instant/debounced searches **match auto-refresh** (§9 keep-visible; shipped 2026-09-02) — they do **not** clear at debounce fire.
  - Existing regression tests for these modes must pass.

- **AC-M3 – Streaming behavior preserved**
  - When streaming is enabled:
    - Partial results continue to be displayed progressively for all search types.
    - Auto-refresh no longer shows a 0-row frame before the first new partial batch; old results remain until streaming has something to show.

- **AC-M4 – No new state or invariants**
  - No new fields or complex invariants added to `GuiState`.
  - Path-pool lifetime rules stay identical; any modifications to `ClearSearchResults` are carefully scoped to auto-refresh only.

#### 9.5 Trade-offs vs. full double-buffering

- **Advantages**
  - Far less complex than front/back buffering:
    - No extra buffers or pools.
    - No promotion/swap semantics.
  - Much lower surface area for subtle bugs or UB.
  - Change is localized to `HandleAutoRefresh` and its immediate call site.

- **Limitations**
  - During auto-refresh, users see **stale** results until new data arrives (though with accurate “Searching…” status); they do not get an atomic “snap” to fresh results once complete — §11 improves the snap (pre-sorted commit / skip).
  - Other sources of visual churn (e.g., filter cache rebuilds) are unchanged by §9 alone.
  - **Instant/debounced search uses §9 keep-visible semantics** as of 2026-09-02; **§11** addresses sort-order and identical-set churn for the PollResults handoff.
  - Persistent GuiState dual buffers (§1–8) remain unnecessary unless a future feature needs long-lived off-screen snapshots beyond a single PollResults tick.

This addendum exists so that maintainers can choose between:

- Implementing this **minimal, low-risk change** to address the worst blinking behavior quickly, or
- Proceeding with the more powerful but higher-risk **double-buffering design** described in sections 1–8, if and when the additional complexity is justified.

### 10. Addendum: Instant Search Flicker — Findings and Recommendations (2026-09-02)

This section records a 2026-09-02 investigation into visual flicker when **Search as you type** (`instantSearch`) is enabled, especially with an active column sort. It extends this spec with follow-up scope beyond auto-refresh.

#### 10.1 What is already shipped

| Search mode | Start of search | Visible during in-flight search | Replace timing |
| --- | --- | --- | --- |
| **Auto-refresh** | Does **not** call `ClearSearchResults` | Previous rows remain | §11 ephemeral back buffer → skip-swap or `CommitNewSearchResults` in the same UI tick |
| **Instant/debounced** | Does **not** call `ClearSearchResults` | Previous rows remain | Same §11 `PollResults` handoff |
| **Manual (Enter/button)** | Calls `ClearSearchResults` | Empty table until results arrive | Same §11 `PollResults` path once complete |

The auto-refresh and debounced paths match **§9**; handoff matches **§11**. Non-streaming handoff is the only active path today.

#### 10.2 Two distinct flicker effects

Users report flicker with instant search; these are **separate mechanisms**:

1. **Empty-table flicker (fixed by §9)**
   - **Cause (historical):** Debounced search called `ClearSearchResults` when the 200 ms debounce fired.
   - **Status:** Fixed 2026-09-02 via `BeginInFlightSearchKeepingVisibleResults`.

2. **Unsorted-then-sorted flicker (partially fixed by §11)**
   - **Cause:** `SearchWorker` returns rows in **match order**. A naive commit set `resultsUpdated = true` and let `HandleTableSorting` reorder on the next table render.
   - **§11 mitigation:** `PollResults` pre-sorts the back buffer with `CreateSearchResultComparator(lastSortColumn, lastSortDirection, show_path_hierarchy_indentation)` before commit, so sync columns (Name / Extension / Path / FolderFiles / hierarchy) land already ordered.
   - **Residual:** Size / Last Modified still rely on async attribute loading after promote when values are not yet in the index cache; Approach B (defer paint until async sort ready) remains open.

While `resultsComplete == false` (in-flight search with §9-style buffering), `HandleTableSorting` is skipped — stale rows stay stable mid-flight.

#### 10.3 Complexity assessment (relative to current UI)

| Approach | Scope | Status (2026-09-04) |
| --- | --- | --- |
| **A. Extend §9 to instant search** | Stop early clear on debounce | **Done** (2026-09-02) |
| **B. Defer first paint until sort ready** | Gate display until async Size/Date sort ready | **Open** (sync columns covered by §11 pre-sort) |
| **C. Full §1–8 GuiState double buffer** | Persistent dual pools | **Not done** — §11 ephemeral back buffer covers needed compare/sort-before-promote without GuiState dual ownership |
| **D. Sort in worker before handoff** | Worker/UI coupling | **Not pursued** |

#### 10.4 Recommendations

1. **§9 + debounce:** ~~Done~~ (2026-09-02).
2. **Ephemeral double-buffer handoff (§11):** ~~Done~~ (2026-09-04) — pre-sort, hierarchy flag, path/`fileId` ties, directory reconcile, skip-swap.
3. **Follow-up (P2 residual):** Extend reconcile / equality so Size/Date-sorted identical membership can skip when front has richer lazy attrs than the back buffer (or defer paint until async sort ready — classic Approach B).
4. **Defer persistent §1–8** unless a feature needs long-lived dual pools beyond one `PollResults` tick.
5. **Windows instant-search gating (separate, 2026-09):** Crawl-primary sessions must not block search on `UsnMonitor::IsPopulatingIndex()` (`ShouldGateSearchOnUsnPopulation`) — not a buffering issue.

#### 10.5 Acceptance criteria — instant search §9 (shipped)

| ID | Criterion | Status |
| --- | --- | --- |
| IS1 | No blank table on debounced search | Done |
| IS2 | Stale rows while searching | Done |
| IS3 | Manual search still clears at start | Done |
| IS4 | Auto-refresh AC-M1–M4 still hold | Done |
| IS5 | Sort flicker | Partial — §11 for sync columns; Size/Date async still open |

#### 10.6 “What’s New” entries

- `2026-09-02` – Search as you type: previous results stay visible (Help window).
- `2026-09-04` – Auto-refresh / search handoff: identical result sets skip redraw; results land pre-sorted for most columns (Help window).

### 11. Shipped: Ephemeral Double-Buffer Handoff in `PollResults` (2026-09-04)

This is the design that landed on `feature/double-buffered-results-*`. It is **not** the full §1–8 GuiState dual-pool design; it is a **per-completion** back buffer used only inside `SearchController::PollResults`.

#### 11.1 Pipeline

1. Guard: `HasNewResults()` and `!IsBusy()` (skip superseded generations).
2. `GetResultsData()` → `MergeAndConvertToSearchResults` into a **stack-local** `ResultPoolOwner` (back buffer).
3. `ReconcileComputedDirectoryAttributes(back, front)` — copy loaded folder size / descendant counts from front so equality and display do not reset to `"..."`.
4. Pre-sort with `CreateSearchResultComparator(sort_col, sort_dir, show_path_hierarchy_indentation)` — same hierarchy setting as `ResultsTable` / `HandleTableSorting`.
5. If `AreSearchResultsEqual(back, front)`: mark search complete, **do not** bump batch / remap selection / move pools (skip-swap).
6. Else: `CommitNewSearchResults` moves back results + path pool into `state.result_pool_`, invalidates filter caches, bumps batch, remaps selection.

#### 11.2 Supporting correctness pieces

| Piece | Role |
| --- | --- |
| `ComparePathThenFileId` + column helpers | Deterministic ties so parallel merge order cannot make equal sets look different after sort |
| `show_path_hierarchy_indentation` on `PollResults` | Filename hierarchy order matches the visible table |
| `CommitNewSearchResults` | Single commit path (replaced unused `UpdateSearchResults` wrapper) |
| Tests in `SearchResultSortTests.cpp` | Skip-swap, hierarchy skip-swap, tie-break determinism, reconcile |

#### 11.3 Known residual gaps

- **File (non-directory) lazy attrs:** Reconcile is directory-only. If the front buffer has UI/async-loaded sizes/times not yet reflected in what Merge fills from the index cache, equality fails and a swap still occurs.
- **Size / Last Modified:** Pre-sort uses whatever attrs Merge attached; async sort after promote may still reorder when attrs finish loading.
- **Equality is positional + attribute-sensitive:** Correct for “did anything meaningful change,” but stricter than pure membership — by design for skip-swap UX.

#### 11.4 Relation to §1–8

§1–8 remains a valid design for persistent dual buffers. §11 delivers the practical benefits needed for US1/US7 (no blank frame + skip identical refresh + sort-before-promote for sync columns) without dual long-lived ownership in `GuiState`.
