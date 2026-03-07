## 2026-03-04_HybridIncrementalSearch_ResultsTable

### Overview

**Goal**: Add an Emacs-style **hybrid incremental search** over the existing **results table** that lets the user:

- **Type a query** and:
  - **Narrow the visible rows** in the table (filter).
  - **Navigate between matches** (jump next/previous) within that narrowed view.
- **Minimize additional complexity** in `ResultsTable` by encapsulating behavior in a small, focused helper/state module.

The feature operates only on the **current in-memory results**, does **not** trigger new searches, and respects ImGui’s immediate mode and the project’s constraints (C++17, AGENTS.md, strict lint/Sonar rules).

**Complexity constraints for `ResultsTable`:**

- Do **not** change the public interface in `ResultsTable.h` (no new parameters or return values).
- Do **not** add new fields to `GuiState`; incremental search state lives entirely in `ResultsTable.cpp`.
- Keep `ResultsTable::Render` as the single integration point:
  - It chooses which result vector to render (base vs. filtered).
  - It delegates incremental-search behavior to a small helper in the anonymous namespace.
- Avoid introducing multiple “modes” or state flags in `ResultsTable`; the helper owns all incremental-search state.
- **Mandatory**: The helper must track `GuiState::resultsBatchNumber` and current search results size to detect changes and invalidate string views/caches instantly.

---

### Functional Requirements

- **Activation / lifecycle**
  - When the **results table has keyboard focus** and a non-empty result set:
    - Pressing `/` (slash, without modifiers) **activates incremental search mode**.
    - An inline prompt appears (top or bottom of table), e.g. `I-search:`.
    - Initial query is empty; no filtering applied yet.
  - Incremental search mode remains active until:
    - User presses **Enter** (accept), or
    - User presses **Esc** / **Ctrl+G** (cancel), or
    - Underlying result set is replaced (e.g. new search).

- **Query editing & filtering (hybrid “Filter + Jump”)**
  - While search mode is active, text input should ideally be handed off to a hidden or inline `ImGui::InputText` to allow ImGui to handle keyboard layouts, backspace, and cursor movement natively.
    - If `ImGui::InputText` cannot be used cleanly, printable key input appends/edit characters in the query, and Backspace deletes characters from the query.
  - For **non-empty query**:
    - A **filtered subset of row indices (not copies of SearchResult)** is computed based on a predicate (initially: case-insensitive substring match over selected fields such as filename, path, and/or line text).
    - Only rows in this subset are **considered visible** and rendered by the table.
    - The **first matching row** becomes the current match and is selected and scrolled into view.
  - For **empty query**:
    - The full, unfiltered result set is visible.
    - Selection behavior reverts to “no filter” semantics (details below under invariants).

- **Navigation within filtered matches**
  - Within the filtered subset, a **current match index** is tracked:
    - If no matches exist → index is “no selection” (e.g. `-1`), navigation keys have no effect.
  - Navigation keys while search mode is active:
    - **Next match**: Down arrow or `N`.
    - **Previous match**: Up arrow or `P`.
  - On **Next**:
    - If there are matches, current match index increments; wraps to 0 after the last match.
  - On **Previous**:
    - If there are matches, current match index decrements; wraps to last index when going before 0.
  - Whenever the current match changes:
    - The table’s **selection** is updated to that row.
    - The table **scrolls** to ensure the row is visible (reusing existing scrolling utilities).

- **Accept vs cancel**
  - **Accept (`Enter`)**:
    - Exits incremental search mode (prompt disappears).
    - **Keeps**:
      - The current filtered subset (if query non-empty).
      - The current selected row.
    - Subsequent navigation acts as if the table naturally had only the filtered rows.
  - **Cancel (`Esc` / `Ctrl+G`)**:
    - Exits incremental search mode (prompt disappears).
    - **Restores**:
      - The pre-search selection (row index).
      - The pre-search scroll position.
      - The **unfiltered** view (full result set).

- **No-results behavior**
  - If the query yields **no matching rows**:
    - The table renders either:
      - No rows; or
      - A standard “no matches” UI (to be chosen at implementation time).
    - The “current match” index is invalid.
    - Navigation keys do nothing until a query with matches is entered.
    - Cancel still restores original view and selection.

- **Non-destructive and local**
  - Feature operates **only** on the **already computed result set**.
  - Never triggers a new filesystem / content search or alters the search pattern.
  - Filtering is **ephemeral** and strictly a UI concern:
    - A new global search or background auto-refresh that changes `resultsBatchNumber` overwrites any current filtered view and forces search mode to exit (cancel).
  - Activating incremental search and filtering results **does not clear** globally marked rows (`GuiState::markedFileIds`).
    - Visually, marked rows are hidden if they don't match the query.
    - Pressing Delete will respect existing table semantics (e.g. prompt for marked rows if any exist, bypassing single-row selection).

- **Keyboard handling & focus**
  - All keyboard handling for the results table passes through a **single centralized dispatch block**.
  - When **search mode is inactive**:
    - Existing table shortcuts behave exactly as before.
    - The global **Ctrl+F / Cmd+F** shortcut (handled in application logic) continues to focus the filename input; `/` does **not** trigger incremental search when text inputs are focused.
    - Pressing `/` inside the results table starts incremental search mode. The activation handler must be tolerant of keyboard layouts where `/` requires `Shift` (e.g. AZERTY, QWERTZ). Thus, avoid strict "no modifiers" checks if `ImGuiKey_Slash` is pressed.
  - When **search mode is active**:
    - Search-related keys (query characters, Backspace, `Enter`, `Esc`, `N`, `P`, Up/Down arrows) are handled **first** by the incremental search helper.
    - Only unhandled keys fall back to default table behavior, and global shortcuts such as Ctrl+F / Cmd+F, Ctrl+S, Ctrl+E remain bound to their existing functions.
  - Behavior must be robust to ImGui’s per-frame key APIs and key repeat.

---

### Non-Functional Requirements

- **Performance**
  - Filtering and navigation should feel **instant** on realistic result sizes (thousands of rows).
  - Minimize per-keystroke allocations:
    - Use `std::string_view` for read-only string parameters and row views.
    - Reuse `std::vector<size_t>` buffers where possible (reserve capacity).
    - Avoid regex or heavy parsing in the first iteration (simple substring match initially).

- **Code quality & constraints**
  - C++17 only; no newer / older language features.
  - Follow `AGENTS.md` and `internal-docs/prompts/AGENT_STRICT_CONSTRAINTS.md`:
    - RAII; no manual `new`/`delete`/`malloc`/`free`.
    - `std::string_view` for read-only strings where appropriate.
    - `const T&` for read-only references.
    - In-class initializers for default member values.
    - No C-style arrays (`std::array` / `std::vector` instead).
    - `(std::min)` / `(std::max)` style where Windows headers are involved.
    - Explicit lambda captures inside templates; avoid `[&]` / `[=]`.
    - No `} if (` on the same line; either `}` + newline + `if`, or `else if`.
  - No modifications inside platform-specific `#ifdef _WIN32`, `__APPLE__`, `__linux__` blocks to “unify” behavior.
  - No new clang-tidy or SonarQube violations on changed files; if suppression is unavoidable, use `// NOSONAR` on the offending line with justification.

- **ImGui / threading**
  - All ImGui calls remain on the **main thread**.
  - `IncrementalSearchState` is a UI state helper; it must not call ImGui directly.
  - No new background threads; feature is purely synchronous and UI-local.

---

### User Stories

| ID | Priority | Summary | Gherkin Scenario |
|----|----------|---------|------------------|
| US1 | High | Activate incremental search in table | **Given** the results table has focus and contains at least one row, **when** the user presses `/` (slash) while no text input is active, **then** an inline “I-search” prompt appears, search mode becomes active, and the query is initially empty. |
| US2 | High | Filter rows as user types | **Given** incremental search mode is active, **when** the user types characters so the query becomes non-empty, **then** only rows that match the query according to the matching predicate are visible in the table, and the first match is selected and scrolled into view. |
| US3 | High | Navigate between matches | **Given** incremental search mode is active and the current query has multiple matches, **when** the user presses a “next” key (Down arrow or `N`), **then** the selection moves to the next matching row, wrapping at end; **and when** the user presses a “previous” key (Up arrow or `P`), **then** the selection moves to the previous matching row, wrapping at start. |
| US4 | High | Cancel restores original view and selection | **Given** incremental search mode was started from a known initial selection and scroll position, **when** the user cancels with `Esc` or `Ctrl+G`, **then** search mode exits, the full unfiltered result set is visible, and both selection and scroll position are restored to their initial state. |
| US5 | Medium | Accept keeps narrowed view | **Given** incremental search mode is active and the user has a non-empty query, **when** the user presses `Enter`, **then** search mode exits but the filtered subset and the current selected row remain, and subsequent navigation behaves as though the table naturally contains only the filtered rows. |
| US6 | Medium | Editing query is safe and consistent | **Given** incremental search mode is active, **when** the user edits the query (including repeated typing and backspacing), **then** the filtered subset and current match are recalculated without crashes, stale indices, or inconsistent selection, and when the query becomes empty, the full result set is shown with consistent selection rules. |
| US7 | Medium | No-results behavior is graceful | **Given** incremental search mode is active and the user enters a query that matches no rows, **then** the table shows an empty result set (or clear “no matches” indication), navigation keys do not change selection, and cancelling still restores the original view and selection. |
| US8 | Low | No interference with existing shortcuts | **Given** existing keyboard shortcuts are used in the results table outside of incremental search, **when** search mode is inactive, **then** their behavior is unchanged, and **when** search mode is active, **then** only keys explicitly bound to the search feature alter behavior, avoiding regressions or brittle cross-interactions. |

---

### Architecture Overview

#### Components

- **`IncrementalSearchState` (new helper)**
  - **Responsibility**: Encapsulate all state and logic for incremental searching within the results table, without modifying `GuiState` or public UI APIs.
  - **Location**: Implemented as a small helper type (and related free functions) in the anonymous namespace of `ResultsTable.cpp`; not exposed in headers.
  - **Key fields** (conceptual):
    - `bool active_` – whether incremental search mode is active.
    - `std::string query_` – current search query.
    - A representation of the **filtered view**, implemented specifically as `std::vector<size_t>` indices or `std::vector<const SearchResult*>` to avoid copying full `SearchResult` structs.
    - `int original_selection_index_` – selection index at the moment search was started (used for cancel).
    - A mandatory snapshot of `resultsBatchNumber` and the results vector `.size()` to detect changes.
  - **Key operations** (signatures to be finalized, but semantically):
    - `void Begin(const ResultsView&, int current_selection);`
    - `void UpdateQuery(std::string_view new_query, const ResultsView&);`
    - `void HandleNavigationNext(const ResultsView&, int& selected_row);`
    - `void HandleNavigationPrev(const ResultsView&, int& selected_row);`
    - `void Accept();`
    - `void Cancel(int& selected_row);`
    - Query helpers to indicate whether a filtered view is active and to provide that view to `ResultsTable::Render`.
  - **Matching implementation**:
    - One dedicated function (or set of helpers) encapsulates the matching predicate:
      - Takes `std::string_view query` and a row view (e.g. filename/path/text) and returns `bool`.
      - Initially simple (case-insensitive substring); allows future evolution without widespread changes.

- **`ResultsTable` (existing component)**
  - **New responsibilities** (minimal):
    - Call incremental-search helper functions from within `ResultsTable::Render` only; avoid spreading incremental-search conditionals across multiple helpers.
    - When rendering:
      - Obtain the **base** display results as today (from `SearchResultsService`).
      - Ask the incremental-search helper (if active) for a **filtered view** of those results.
      - Choose which vector to render (base or filtered) and pass that vector through to existing row-rendering logic unchanged.
      - Keep using `GuiState::selectedRow` and `GuiState::scrollToSelectedRow` as the single source of truth for selection and scrolling; the helper only updates these fields via small, explicit calls.
      - Render the search prompt UI at the bottom of the table based on the helper’s state (active + query + match count).

#### Data Flow

1. **User presses activation key** in table:
   - `ResultsTable::Render` detects `/` pressed while the table is focused and no text input is active, and calls an incremental-search helper `Begin(...)` with the current results view and current selection index.

2. **User types / edits query**:
   - Still within `ResultsTable::Render`, keyboard handling routes character input and Backspace to the incremental-search helper’s `UpdateQuery(...)`.
   - `UpdateQuery`:
     - Updates the internal query string.
     - Recomputes the helper’s filtered view (if the query is non-empty) using the shared matching predicate.
     - Updates `GuiState::selectedRow` to the first match (or preserves the previously selected file when possible by matching `fileId`), and sets `GuiState::scrollToSelectedRow` so the next frame scrolls to it.

3. **Rendering pass**:
   - `ResultsTable`:
     - Obtains the base results as usual from `SearchResultsService`.
     - If incremental search is active and has a non-empty query, asks the helper for the filtered view and uses that as `display_results`; otherwise uses the base results.
     - Passes `display_results` to existing helper functions (`RenderVisibleTableRows`, delete/bulk-delete handlers, marked toolbar, etc.) without changing their contracts.
     - Uses `GuiState::selectedRow` as before to decide which row is selected; the incremental-search helper only adjusts this field via its navigation and query-update operations.

4. **Navigation keys (`NextMatch`, `PrevMatch`)**:
   - While incremental search is active and the query is non-empty, navigation keys are dispatched to the helper’s navigation operations instead of the general table shortcut handler.
   - The helper adjusts `GuiState::selectedRow` within bounds of the current `display_results` (base or filtered), with wrapping semantics, and sets `GuiState::scrollToSelectedRow` so the next frame scrolls the selected row into view.

5. **Accept / cancel**:
   - `Accept` sets `active_ = false` but leaves `visible_row_indices_` and `query_` unchanged.
   - `Cancel`:
     - Restores selection and scroll using `original_selection_index_` and `original_scroll_offset_`.
     - Clears query and returns to unfiltered view.
     - Sets `active_ = false`.

---

### Acceptance Criteria

| ID | Criterion | Measurable Check |
|----|-----------|------------------|
| AC1 | Activation works and is idempotent | With results table focused, pressing activation key consistently enters search mode; pressing it again while already in search mode does not corrupt state (either ignored or ignored by design). |
| AC2 | Filtering correct and stable | On a controlled result set, a given query yields exactly the expected rows (per predicate) and no others; repeated typing/backspacing does not crash or leave stale matches. |
| AC3 | Match navigation correct | On a controlled result set with known matching rows, pressing Next/Previous cycles selection through them in the correct order, with proper wrapping. |
| AC4 | Cancel restores selection and scroll | Starting from a known selection and scroll offset, activating search, navigating, then cancelling returns both selection and scroll to the original state, with full results visible. |
| AC5 | Accept preserves narrowed view | After filtering to a subset and pressing Enter, search mode exits but the narrowed set and selected row remain; clearing the query in a new search session restores the full view. |
| AC6 | No-results behavior is correct | Entering a query with no matches produces an empty (or clearly “no matches”) view; navigation keys have no effect; cancel still correctly restores state. |
| AC7 | No regression of existing keyboard shortcuts | All existing shortcuts behave unchanged when search mode is inactive; when active, only search-specific bindings change behavior. This is confirmed by manual testing and, where possible, ImGui regression tests. |
| AC8 | No new lint/Sonar issues | Running clang-tidy and Sonar on modified files shows no new issues; all relevant rules from AGENTS.md and strict constraints are satisfied. |

---

### Task Breakdown

| Phase | Task | Dependencies | Est. (h) | Notes |
|-------|------|--------------|----------|-------|
| T1 | Define and implement `IncrementalSearchState` | Spec | 1.5–2 | Create helper type (header + source), implement fields and basic methods; include unit tests for filter/matching where feasible. |
| T2 | Integrate helper into `ResultsTable` rendering | T1 | 2–3 | Add member to table state; change row iteration to respect `VisibleRows()` and selection to respect `CurrentRowIndex()`. Confirm behavior unchanged when helper inactive. |
| T3 | Implement centralized keyboard dispatch with search routing | T2 | 2–3 | Refactor results table input handling into a single block; wire activation, query editing, navigation, accept, cancel to helper. Ensure no regressions to existing shortcuts. |
| T4 | Implement search prompt UI | T3 | 1–2 | Draw a small ImGui bar for the prompt when `IsActive()` is true; show current query and optional “no matches” hint. Ensure it fits current UI style. |
| T5 | Add ImGui regression tests | T4 | 2–4 | Extend existing ImGui test engine to: activate search, type a query, navigate matches, accept/cancel; assert on visible rows and selection. |
| T6 | Manual cross-platform validation | T5 | 1–2 | On macOS and Windows, smoke-test with large and small result sets, typing speed, cancellation, and interaction with other shortcuts. On macOS, run `scripts/build_tests_macos.sh` after C++ changes. |

---

### Risks & Mitigations

| Risk | Impact | Mitigation |
|------|--------|-----------|
| Keyboard handling regression in table | Medium–High | Centralize keyboard handling into a single dispatch; keep search-specific keys clearly separated; add regression tests covering basic navigation and search sequences. |
| Incorrect restoration of selection/scroll on cancel | Medium | Store pre-search selection and scroll only inside helper; assert invariants; add tests that capture state before search and verify restore after cancel. |
| Performance issues on large datasets | Medium | Use `std::string_view` and pre-allocated vectors for matching; avoid regex initially; measure typing latency on large result sets and optimize predicate or data access patterns as needed. |
| Complexity creep in `ResultsTable` | Medium | Keep new logic isolated inside `IncrementalSearchState`; limit `ResultsTable` changes to owning helper, delegating input, and using helper outputs for row iteration and selection. Refactor any emerging ad-hoc flags back into helper. |

---

### Validation & Handoff

- **Before merging:**
  - All acceptance criteria AC1–AC8 are met.
  - No new linter/Sonar issues are introduced.
  - Behavior is verified manually on at least one macOS and one Windows environment.
  - On macOS, `scripts/build_tests_macos.sh` has been run successfully after changes.

- **For implementation prompts (Cursor Agent/Composer):**
  - Reference this document as the **authoritative spec** for the feature.
  - Explicitly instruct the implementing agent to:
    - Read and follow `AGENTS.md` and `internal-docs/prompts/AGENT_STRICT_CONSTRAINTS.md`.
    - Implement in small steps following the **Task Breakdown**.
    - Maintain invariants inside `IncrementalSearchState` with assertions where helpful.
    - Add/extend tests as described and avoid introducing new tech debt (KISS, YAGNI, DRY, SOLID).

