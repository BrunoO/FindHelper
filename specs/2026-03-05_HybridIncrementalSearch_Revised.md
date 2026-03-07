## 2026-03-05_HybridIncrementalSearch_Revised

**Supersedes:** `specs/2026-03-04_HybridIncrementalSearch_ResultsTable.md`

### Purpose

This document is the authoritative implementation spec for Hybrid Incremental Search in the
results table. It resolves the following gaps identified in the original spec:

1. Scroll position save/restore — no existing mechanism, deferred pattern required.
2. Accept semantics — "active" conflates prompt visibility with filter application; two flags needed.
3. N/P/arrow key conflict — existing shortcuts use these keys; dispatch order must be explicit.
4. `WantTextInput` guard — `ImGui::InputText` would break the shortcut block; mandated approach is manual character input.
5. Filtered results materialization — existing helpers require `const std::vector<SearchResult>&`, not an index view.
6. Post-accept filter invalidation — undefined for batch number changes after accept.
7. Prompt UI position — table outer size impact unspecified.
8. Task T1 in original spec said "header + source" but architecture said anonymous namespace — resolved in favour of proper separate files.

The feature is decomposed into **six sequential, independently verifiable phases**. Each phase ends
with a clear acceptance test before the next begins.

---

### Constraints (apply to all phases)

Follow `AGENTS.md` and `internal-docs/prompts/AGENT_STRICT_CONSTRAINTS.md` throughout. Highlights:

- C++17 only.
- No new SonarQube or clang-tidy violations. Fix causes; use `// NOSONAR` on the exact offending
  line only as a last resort with justification.
- No `new`/`delete`/`malloc`/`free`; RAII throughout.
- `std::string_view` for read-only string parameters.
- In-class initializers for all default member values (Sonar S3230).
- `const T&` for read-only reference parameters (Sonar S995/S5350).
- `(std::min)` / `(std::max)` — parenthesised to avoid Windows macro collision.
- Explicit lambda captures inside templates; no `[&]` or `[=]` in templates.
- No `} if (` on the same line as the closing brace of a previous block (use a new line or
  `else if`).
- All new `#include` directives go at the top of each file.
- New source files must be added to `CMakeLists.txt` following existing patterns
  (see AGENTS.md "Modifying CMakeLists.txt Safely").
- Float literals use `F` suffix (e.g. `0.0F`).
- `strcpy_safe` / `strcat_safe` from `StringUtils.h` for fixed-size buffers; no raw `strcpy`.
- Run `scripts/build_tests_macos.sh` after every phase before declaring it complete.

---

### Non-goals

- No new fields in `GuiState`. All incremental-search state lives in the new helper types.
- No changes to the `ResultsTable::Render` public signature.
- No new background threads. All code runs on the main/UI thread.
- No regex. Case-insensitive substring match only for the first iteration.
- No changes inside `#ifdef _WIN32`, `#ifdef __APPLE__`, or `#ifdef __linux__` blocks unless
  those blocks are the direct target of a phase.

---

### Resolved design decisions

#### D1 — Keyboard input without `ImGui::InputText`

Using `ImGui::InputText` for the incremental search query would set `ImGui::GetIO().WantTextInput`
to `true`, which causes the existing guard in `HandleResultsTableKeyboardShortcuts` to return
early. This would make all shortcuts — including the search-mode navigation keys — unreachable.

**Resolution:** Do not use `ImGui::InputText`. Instead, consume printable characters directly from
`ImGui::GetIO().InputQueueCharacters` while search mode is active. This keeps `WantTextInput`
false, so the existing guard continues to work, and the dispatch inside the shortcut handler
decides which keys to route to the search helper vs. which to pass through to normal shortcuts.

Implications:
- No `ImGuiKey_*` input queue flushing side-effect.
- Backspace is handled via `ImGui::IsKeyPressed(ImGuiKey_Backspace, /*repeat=*/true)` while
  search mode is active (allow key-repeat for comfortable editing).
- Unicode characters beyond printable ASCII are ignored in the first iteration.

#### D2 — Two flags for search state: `prompt_visible_` and `filter_active_`

The original spec used a single `active_` flag, which conflates two distinct states:
- **Prompt visible**: the I-search bar is rendered and keys are routed to the helper.
- **Filter active**: the filtered result vector is used as `display_results`.

After Accept (`Enter`), the prompt should disappear but the filter should remain. This requires
two flags:

| State | `prompt_visible_` | `filter_active_` |
|---|---|---|
| Inactive | false | false |
| Active (during search) | true | true (if query non-empty) |
| After Accept (non-empty query) | false | true |
| After Accept (empty query) | false | false |
| After Cancel | false | false |
| After batch number change | false | false |

#### D3 — Filtered results are materialised as a concrete vector

Existing helpers (`RenderVisibleTableRows`, `HandleDeleteKeyAndPopup`,
`HandleResultsTableKeyboardShortcuts`, `BuildFolderStatsIfNeeded`, etc.) all take
`const std::vector<SearchResult>&`. To avoid changing any of their contracts,
`IncrementalSearchState` owns a `std::vector<SearchResult> filtered_results_` that holds copied
`SearchResult` structs.

Copying `SearchResult` is cheap: it contains a `std::string_view` (into `searchResultPathPool`,
which outlives the search session) plus a few integer fields. No heap allocation per copy.

The filtered vector is rebuilt on every query change. Reserve capacity to avoid repeated
reallocations (reserve to the full results size on first build).

#### D4 — Scroll restore uses a deferred flag, not `ImGui::SetScrollY` at cancel time

`ImGui::SetScrollY` must be called from inside the same `BeginTable`/`EndTable` block as the
table being scrolled. Cancel is triggered from the keyboard handler, which runs after `EndTable`
(line 1405 in `ResultsTable.cpp`). Therefore:

- On `Begin()`, capture the current table scroll via `ImGui::GetScrollY()` while inside the
  `BeginTable` block. Store as `original_scroll_y_`.
- On `Cancel()`, set `restore_scroll_pending_ = true` and restore `selectedRow` to
  `original_selection_index_`.
- On the **next frame**, at the start of the `BeginTable` block (before the clipper), check
  `restore_scroll_pending_`. If true: call `ImGui::SetScrollY(original_scroll_y_)` and clear the
  flag.

This mirrors the existing `scrollToSelectedRow` deferred pattern. The helper exposes
`bool ConsumeScrollRestorePending(float& out_y)` which returns true and fills `out_y` exactly
once, then clears the flag.

Note: `scrollToSelectedRow` must also be set to `true` on cancel so the clipper includes the
restored row on that same frame. Both mechanisms fire together: pixel-accurate scroll restore via
`SetScrollY` plus clipper inclusion for the row.

#### D5 — Post-accept filter invalidation on batch number change

When `IncrementalSearchState` detects that `resultsBatchNumber` has changed compared to the value
captured at `Begin()`, it calls an internal `Reset()` that clears both flags, the query, and the
filtered vector — regardless of whether the prompt is visible or only the filter is active (post-
accept). This ensures stale `SearchResult` copies are never displayed after the underlying results
change.

#### D6 — Prompt UI placement and table outer size

The prompt bar is rendered **above** the table, in the same header area that otherwise shows the
status label ("Results table active…") or the marked-actions toolbar. The three header states are
mutually exclusive and selected in priority order each frame:

1. **I-search prompt visible** → render `RenderIncrementalSearchPrompt` (hides status label and
   marked-actions toolbar while search mode is active).
2. **Marked files present** (and no prompt) → render `RenderMarkedActionsToolbar`.
3. **Otherwise** → render the keyboard-shortcuts status label.

This requires no change to the `BeginTable` call's `outer_size` parameter (currently
`ImVec2(0.0F, 0.0F)` = fill available height). `RenderMarkedActionsToolbar` is no longer called
after `EndTable`; it is integrated into the pre-table header block.

The bar is a one-line `ImGui::Text` call showing: `I-search: <query>` with a match count suffix
such as `(3 matches)` or `(no matches)` in `Theme::Colors::Warning`.

---

### New files

```
src/ui/IncrementalSearchState.h     — IncrementalSearchState class declaration
src/ui/IncrementalSearchState.cpp   — IncrementalSearchState implementation (pure logic, no ImGui)
src/ui/ResultsTableKeyboard.h       — keyboard dispatch declarations (extracted from ResultsTable.cpp)
src/ui/ResultsTableKeyboard.cpp     — keyboard dispatch implementation
tests/IncrementalSearchStateTests.cpp — doctest unit tests for IncrementalSearchState
```

Modified files:

```
src/ui/ResultsTable.cpp             — owns IncrementalSearchState; wires display and prompt
CMakeLists.txt                      — add new source files to the build target
```

---

### Phase 1 — Keyboard handler extraction (pure refactor, no behaviour change)

**Goal:** Move `HandleResultsTableKeyboardShortcuts` and its private helpers out of
`ResultsTable.cpp` into `ResultsTableKeyboard.h/.cpp`. After this phase, `ResultsTable.cpp`
simply calls `ui::HandleResultsTableKeyboardShortcuts(...)`. Zero behaviour change.

**Why first:** The keyboard handler is the most brittle area. Extracting it before adding
incremental search logic makes it an independent module with a clear interface, eases review, and
prevents the Phase 3–5 changes from being buried in a 1400-line file.

**Scope:**

1. Create `src/ui/ResultsTableKeyboard.h` declaring:
   ```cpp
   namespace ui {
   void HandleResultsTableKeyboardShortcuts(
       GuiState& state,
       const std::vector<SearchResult>& display_results,
       const FileIndex& file_index,
       GLFWwindow* glfw_window,
       bool shift,
       NativeWindowHandle native_window);
   }  // namespace ui
   ```

2. Move the implementation — plus all private helpers it calls
   (`MoveSelectionDownIfPossible`, `MarkCurrentAndMoveDown`, `MarkToggleCurrentAndMove`,
   `MarkInvertAll`, `UnmarkCurrentAndMoveDown`, `CopyMarkedPathsToClipboardImpl`,
   `CopyMarkedFilenamesToClipboard`, `IsPrimaryShortcutModifierDown`) — into
   `ResultsTableKeyboard.cpp`. Helpers stay in an anonymous namespace inside that file.

3. In `ResultsTable.cpp`: remove the moved code; add
   `#include "ui/ResultsTableKeyboard.h"`; call the function unchanged.

4. Update `CMakeLists.txt` to compile `ResultsTableKeyboard.cpp`.

**What must not change:** The function signature, the dispatch logic, all key bindings, the
`WantTextInput` / `window_focused` guard. Not a single keystroke may behave differently.

**Acceptance:** `scripts/build_tests_macos.sh` passes. Diff of `ResultsTable.cpp` shows only the
function body removed and a header include added. Code review confirms moved code is character-
identical to original (no reformatting that changes logic).

---

### Phase 2 — `IncrementalSearchState`: pure logic and unit tests

**Goal:** Implement the state machine and filter logic without any ImGui or UI code. Add
comprehensive unit tests. No changes to `ResultsTable.cpp` in this phase.

**`src/ui/IncrementalSearchState.h`**

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "search/SearchWorker.h"  // for SearchResult

namespace ui {

/**
 * @brief Case-insensitive substring match over a SearchResult's filename and directory path.
 *
 * Returns true if query is empty (all rows match an empty query) or if query appears
 * (case-insensitively) in either the filename part or the directory part of result.fullPath.
 *
 * Free function so it can be unit-tested independently of IncrementalSearchState.
 */
bool IncrementalSearchMatches(std::string_view query, const SearchResult& result);

/**
 * @brief Self-contained incremental search state for the results table.
 *
 * All state lives here; no new fields in GuiState.
 * No ImGui calls; pure logic only.
 * ResultsTable.cpp owns one instance as a function-static variable in Render().
 */
class IncrementalSearchState {
 public:
  // --- Queries ---

  /** True while the I-search prompt should be shown and keys routed here. */
  [[nodiscard]] bool IsPromptVisible() const { return prompt_visible_; }

  /** True while the filtered result vector should be used as display_results. */
  [[nodiscard]] bool IsFilterActive() const { return filter_active_; }

  /** True if the caller should consume printable chars and Backspace this frame. */
  [[nodiscard]] bool IsActive() const { return prompt_visible_; }

  /** Current query string (may be empty). */
  [[nodiscard]] std::string_view Query() const { return query_; }

  /** Number of rows in the current filtered set (0 when filter inactive or no matches). */
  [[nodiscard]] int MatchCount() const;

  /**
   * @brief Filtered display results to substitute for the base results.
   *
   * Valid to call only when IsFilterActive() is true. Caller must not store the reference
   * beyond the current frame (may be invalidated by the next UpdateQuery call).
   */
  [[nodiscard]] const std::vector<SearchResult>& FilteredResults() const;

  /**
   * @brief Consume pending deferred scroll restore.
   *
   * Returns true (and fills out_y) exactly once after Cancel(), then resets.
   * Must be called from inside BeginTable/EndTable each frame.
   */
  [[nodiscard]] bool ConsumeScrollRestorePending(float& out_y);

  // --- Commands ---

  /**
   * @brief Activate incremental search mode.
   *
   * Captures pre-search state for cancel restore.
   *
   * @param base_results    The current display results (used to build the filtered vector).
   * @param current_selection  Current value of GuiState::selectedRow.
   * @param current_scroll_y   Current table scroll Y (captured via ImGui::GetScrollY()).
   * @param results_batch_number  Current GuiState::resultsBatchNumber.
   */
  void Begin(const std::vector<SearchResult>& base_results,
             int current_selection,
             float current_scroll_y,
             uint64_t results_batch_number);

  /**
   * @brief Update the query and rebuild the filtered result set.
   *
   * Call whenever the query string changes (character appended or deleted).
   * Updates filtered_results_ and resets current_match_index_ to 0 (first match).
   *
   * @param new_query   The complete new query string.
   * @param base_results  The current base display results (not the filtered ones).
   * @param selected_row  Out: updated to the first matching row index in display_results, or -1.
   * @param scroll_to_selected  Out: set to true when selected_row is updated.
   */
  void UpdateQuery(std::string_view new_query,
                   const std::vector<SearchResult>& base_results,
                   int& selected_row,
                   bool& scroll_to_selected);

  /**
   * @brief Move to the next match (wraps at end).
   *
   * Only valid while IsActive() and MatchCount() > 0.
   *
   * @param selected_row       Out: updated to the new match row index.
   * @param scroll_to_selected Out: set to true.
   */
  void NavigateNext(int& selected_row, bool& scroll_to_selected);

  /**
   * @brief Move to the previous match (wraps at start).
   *
   * Only valid while IsActive() and MatchCount() > 0.
   */
  void NavigatePrev(int& selected_row, bool& scroll_to_selected);

  /**
   * @brief Accept current filter: hide prompt, keep filter and selection.
   *
   * If the query is empty, also clears filter_active_.
   */
  void Accept();

  /**
   * @brief Cancel: exit prompt, restore pre-search selection and schedule scroll restore.
   *
   * Sets restore_scroll_pending_ = true. The caller must call ConsumeScrollRestorePending()
   * from inside the BeginTable block on the next frame.
   *
   * @param selected_row Out: restored to original_selection_index_.
   * @param scroll_to_selected Out: set to true so the clipper includes the row.
   */
  void Cancel(int& selected_row, bool& scroll_to_selected);

  /**
   * @brief Check for batch number change and reset if the underlying results changed.
   *
   * Must be called once per frame before any other operation that reads filtered_results_.
   * Resets both flags, query, and filter if the batch number differs from the captured value.
   *
   * @param current_batch_number  Current GuiState::resultsBatchNumber.
   * @param selected_row          Out: set to -1 if reset occurs (stale selection cleared).
   */
  void CheckBatchNumber(uint64_t current_batch_number, int& selected_row);

 private:
  void RebuildFilter(const std::vector<SearchResult>& base_results);
  void Reset(int& selected_row);

  bool prompt_visible_ = false;
  bool filter_active_ = false;
  std::string query_;
  std::vector<SearchResult> filtered_results_;
  int current_match_index_ = -1;

  // Pre-search state for cancel restore
  int original_selection_index_ = -1;
  float original_scroll_y_ = 0.0F;
  bool restore_scroll_pending_ = false;

  // Invalidation anchor
  uint64_t captured_batch_number_ = 0;
};

}  // namespace ui
```

**`src/ui/IncrementalSearchState.cpp`**

- `IncrementalSearchMatches`: converts both `query` and the relevant parts of `result.fullPath`
  to lowercase via `std::tolower` character-by-character (no `<locale>` dependency; ASCII only in
  first iteration), then calls `std::string::find`.
- `RebuildFilter`: iterates `base_results`, calls `IncrementalSearchMatches(query_, result)` for
  each, pushes matching results into `filtered_results_` (pre-reserved).
  - If `filtered_results_` is non-empty after rebuild, set `current_match_index_ = 0`.
  - Else set `current_match_index_ = -1`.
- `NavigateNext` / `NavigatePrev`: increment/decrement `current_match_index_` with modulo wrap;
  set `selected_row = current_match_index_`; set `scroll_to_selected = true`.
- `Cancel`: sets `restore_scroll_pending_ = true`; clears both flags and query; calls
  `Reset(selected_row)` which sets `selected_row = original_selection_index_`.
- `CheckBatchNumber`: if `current_batch_number != captured_batch_number_` and either flag is
  true, calls `Reset(selected_row)`.

**`tests/IncrementalSearchStateTests.cpp`**

Test cases (use `doctest`; do not include `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN` if another test
file in the same binary already defines it — check `CMakeLists.txt` for the test binary
structure):

| Test | Verifies |
|---|---|
| Default construction | `IsPromptVisible()` false, `IsFilterActive()` false, `MatchCount()` 0 |
| `Begin` sets prompt and filter active | After Begin with non-empty results |
| `UpdateQuery` — matching query | `FilteredResults()` contains only matching rows |
| `UpdateQuery` — empty query | Filter cleared, `IsFilterActive()` false, selected_row reset |
| `UpdateQuery` — no matches | `MatchCount()` == 0, selected_row == -1 |
| `NavigateNext` wrap | After last match, wraps to index 0 |
| `NavigatePrev` wrap | At index 0, wraps to last match |
| `Accept` — non-empty query | `IsPromptVisible()` false, `IsFilterActive()` true |
| `Accept` — empty query | Both flags false |
| `Cancel` | Both flags false; selected_row restored; `ConsumeScrollRestorePending` returns true once |
| `CheckBatchNumber` — changed | Both flags cleared, selected_row reset |
| `CheckBatchNumber` — unchanged | No change |
| `IncrementalSearchMatches` — case insensitive | "FOO" matches "foobar" |
| `IncrementalSearchMatches` — empty query | Always true |
| `IncrementalSearchMatches` — dotfile | ".bashrc" matched by "bash" |

**Acceptance:** All unit tests pass under `scripts/build_tests_macos.sh`. No ImGui calls in the
two new files (verify by `grep -n "ImGui" src/ui/IncrementalSearchState.*`).

---

### Phase 3 — Activation skeleton (prompt on/off, no filtering)

**Goal:** Wire the `/` key to activate, and `Esc` / `Enter` to deactivate. The I-search prompt
bar appears and disappears. No filtering; `display_results` is unchanged.

**Changes to `ResultsTableKeyboard.h/.cpp`:**

Extend the function signature to receive a reference to `IncrementalSearchState`:

```cpp
void HandleResultsTableKeyboardShortcuts(
    GuiState& state,
    const std::vector<SearchResult>& display_results,
    const FileIndex& file_index,
    GLFWwindow* glfw_window,
    bool shift,
    NativeWindowHandle native_window,
    ui::IncrementalSearchState& incremental_search);
```

Note: the **public** `ResultsTable::Render` signature does not change. The
`IncrementalSearchState` reference is an internal parameter between `ResultsTable.cpp` and
`ResultsTableKeyboard.cpp`.

Inside `HandleResultsTableKeyboardShortcuts`, add a top section **before** any existing shortcut
checks:

```
// --- Incremental search activation / deactivation ---
if incremental_search.IsPromptVisible():
    if Enter or Escape or Ctrl+G pressed:
        if Enter: incremental_search.Accept()
        else:     incremental_search.Cancel(state.selectedRow, state.scrollToSelectedRow)
    return early from search block (do not fall through to normal shortcuts below)

else (prompt not visible):
    if Slash pressed (ImGuiKey_Slash, no modifier check per spec — layout tolerant):
        if display_results non-empty:
            incremental_search.Begin(display_results, state.selectedRow,
                                     /* scroll_y captured in Render, passed in */, state.resultsBatchNumber)
        // do not return — let normal guard logic continue for this frame
```

The `original_scroll_y` is captured in `ResultsTable::Render` inside the `BeginTable` block, just
before the clipper, and passed as a parameter into `HandleResultsTableKeyboardShortcuts` only on
the frame that `Begin()` is called. Simplest approach: always pass the current `ImGui::GetScrollY()`
and let `Begin()` store it.

```cpp
// In ResultsTable::Render, inside BeginTable block, before clipper:
const float current_table_scroll_y = ImGui::GetScrollY();

// ... later, passing to keyboard handler:
HandleResultsTableKeyboardShortcuts(state, display_results, file_index,
                                    glfw_window, shift, native_window,
                                    incremental_search);
```

`Begin()` stores the scroll Y it receives; it is always passed but only meaningful when
`Begin()` is actually called (the first `Begin()` call of a search session).

**Deferred scroll restore** (inside `BeginTable` block, before clipper):
```cpp
if (float restore_y = 0.0F; incremental_search.ConsumeScrollRestorePending(restore_y)) {
    ImGui::SetScrollY(restore_y);
}
```

**Prompt bar rendering** — replaces the pre-table header block (the area that normally shows the
status label or marked-actions toolbar). Priority order each frame:

```cpp
if (incremental_search.IsPromptVisible()) {
    RenderIncrementalSearchPrompt(incremental_search);
} else if (!state.markedFileIds.empty()) {
    RenderMarkedActionsToolbar(state, glfw_window, file_index, shift);
} else {
    // status label (shortcuts active / inactive)
}
```

`RenderIncrementalSearchPrompt` renders a single line:
```
I-search: _                     [query is empty at this point]
```

**`IncrementalSearchState` lifetime:** declared as a function-level `static` inside
`ResultsTable::Render` (analogous to the existing `drag_candidate_active` statics):
```cpp
static IncrementalSearchState incremental_search;
```

**CheckBatchNumber** call: add at the start of the `BeginTable` block (after
`GetDisplayResults`):
```cpp
incremental_search.CheckBatchNumber(state.resultsBatchNumber, state.selectedRow);
```

**Acceptance:**
- Pressing `/` in the results table shows the `I-search:` bar.
- Pressing `Esc` or `Enter` hides it.
- All existing shortcuts (N/P/arrows/M/T/U/W/D/Delete/Enter) work identically when the prompt
  is not visible.
- `scripts/build_tests_macos.sh` passes.

---

### Phase 4 — Query editing and filtering

**Goal:** While the prompt is visible, printable characters build the query and the filtered
result vector replaces `display_results`. The first matching row is auto-selected.

**Character input** (inside `HandleResultsTableKeyboardShortcuts`, inside the "search active"
branch, before the early return):

```cpp
// Consume printable ASCII from the input queue
const ImGuiIO& kb_io = ImGui::GetIO();
bool query_changed = false;
for (int i = 0; i < kb_io.InputQueueCharacters.Size; ++i) {
    const ImWchar ch = kb_io.InputQueueCharacters[i];
    if (ch >= 32 && ch < 127) {  // printable ASCII only
        appended_char = static_cast<char>(ch);
        query_changed = true;
    }
}
if (ImGui::IsKeyPressed(ImGuiKey_Backspace, /*repeat=*/true) && !current_query.empty()) {
    new_query = current_query.substr(0, current_query.size() - 1);
    query_changed = true;
}
if (query_changed) {
    incremental_search.UpdateQuery(new_query, base_results,
                                   state.selectedRow, state.scrollToSelectedRow);
}
```

The query string is built incrementally. `UpdateQuery` does the rebuild and updates
`selectedRow`.

**Display results substitution** (in `ResultsTable::Render`, after `GetDisplayResults`):

```cpp
const std::vector<SearchResult>* display_results_ptr =
    search::SearchResultsService::GetDisplayResults(state);

if (incremental_search.IsFilterActive()) {
    display_results_ptr = &incremental_search.FilteredResults();
}

const std::vector<SearchResult>& display_results = *display_results_ptr;
```

All existing code below that line (clipper, keyboard handler, delete handler, folder stats)
continues to use `display_results` unchanged — they operate on whatever vector is pointed at.

**Prompt bar** update — show query and match count:
```
I-search: foo                    (3 / 12)
```
or, when no matches:
```
I-search: zzz                    [no matches]
```

**Acceptance:**
- Typing while the prompt is visible filters the table rows in real time.
- Backspace removes the last character; the table expands accordingly.
- When the query becomes empty, all rows are shown again.
- First match is auto-selected and scrolled into view.
- Existing shortcuts (`W`, `D`, `M`, `T`, `U`, `Ctrl+Enter`, etc.) are not triggered by
  character keys typed into the search query.
- Delete key does not fire the delete popup while search mode is active (the search handler
  returns early before `HandleDeleteKeyAndPopup` — confirm by adding a return after consuming
  search keys).
- `scripts/build_tests_macos.sh` passes.

---

### Phase 5 — Navigation and cancel scroll restore

**Goal:** While the prompt is visible and a query is active, `N` / `DownArrow` and `P` /
`UpArrow` navigate between filtered matches with wrapping. Cancel restores the original row
and scroll position.

**Navigation dispatch** (inside "search active" branch in `HandleResultsTableKeyboardShortcuts`,
after character input, before the early return):

```cpp
if (MatchCount > 0) {
    if (KeyPressedOnce(ImGuiKey_DownArrow) || KeyPressedOnce(ImGuiKey_N)) {
        incremental_search.NavigateNext(state.selectedRow, state.scrollToSelectedRow);
    } else if (KeyPressedOnce(ImGuiKey_UpArrow) ||
               (KeyPressedOnce(ImGuiKey_P) && !IsPrimaryShortcutModifierDown(io))) {
        incremental_search.NavigatePrev(state.selectedRow, state.scrollToSelectedRow);
    }
}
```

These keys are consumed here and **not** passed to the normal navigation block at the bottom of
the function (because the search active branch returns early). This is the resolution of the key
conflict: the early return after the search block prevents double-handling.

**Cancel scroll restore** — already wired in Phase 3 via `ConsumeScrollRestorePending`. Verify
now with a real prior selection and scroll offset.

**Acceptance:**
- With 5 matching rows, pressing `N` cycles through them in order and wraps after the last.
- Pressing `P` cycles backwards and wraps before the first.
- `UpArrow` / `DownArrow` behave identically to `P` / `N` in search mode.
- After cancel, the row selected before `/` was pressed is restored and scrolled into view.
- After cancel, the table shows the full (unfiltered) result set.
- `scripts/build_tests_macos.sh` passes.

---

### Phase 6 — Accept semantics, post-accept filter, Ctrl+G, and polish

**Goal:** Implement the two-flag Accept behaviour, invalidate the post-accept filter on batch
change, wire Ctrl+G as a cancel alias, add the "no matches" UI, and address all lint/Sonar
concerns.

**Accept:**

`Accept()` is already implemented in Phase 2. In Phase 6, confirm end-to-end:
- After Enter, the prompt disappears but the filtered rows remain as `display_results`.
- Normal navigation (N/P/arrows, now in the non-search branch) operates within the filtered set.
- The `I-search:` bar is not rendered (since `IsPromptVisible()` is false).

**Ctrl+G as cancel alias** (in the "search active" branch, alongside `Esc`):

```cpp
const bool cancel_pressed =
    KeyPressedOnce(ImGuiKey_Escape) ||
    (IsPrimaryShortcutModifierDown(io) && KeyPressedOnce(ImGuiKey_G));
```

Note: `IsPrimaryShortcutModifierDown` checks `Ctrl` on Windows/Linux and `Cmd` on macOS. Verify
that `Ctrl+G` does not collide with any existing shortcut.

**Post-accept filter invalidation:**

`CheckBatchNumber` (wired in Phase 3) already clears both flags when the batch changes. This
covers the post-accept case. Verify:
- Run a new search while a post-accept filter is active → table returns to full results.

**No-matches UI:**

When `incremental_search.IsPromptVisible() && incremental_search.MatchCount() == 0`:
- Render the prompt bar with a distinct color: `I-search: zzz  [no matches]` in
  `Theme::Colors::Warning` or equivalent.

**Prompt bar final form** — `RenderIncrementalSearchPrompt` lives in `ResultsTableKeyboard.cpp`
(logically related to the keyboard/search interaction), declared in `ResultsTableKeyboard.h`.
`ResultsTable.cpp` calls it from the pre-table header block (see D6).

**CMakeLists.txt:** Confirm both new source files (`IncrementalSearchState.cpp`,
`ResultsTableKeyboard.cpp`) are present in the build target.

**Clang-tidy and Sonar clean pass:**
- Run `scripts/build_tests_macos.sh` to confirm the test binary compiles cleanly.
- Review changed files against `internal-docs/prompts/AGENT_STRICT_CONSTRAINTS.md` checklist.

**Acceptance (full feature — AC1–AC8 from original spec):**
- AC1: Pressing `/` enters search mode; pressing it again while active is a no-op (Begin is
  guarded by `!IsPromptVisible()`).
- AC2: Controlled result set, known query → exactly expected rows, repeated typing/backspacing
  stable.
- AC3: Next/Previous cycle through known matches in correct order with wrapping.
- AC4: Cancel restores selection and scroll, full results visible.
- AC5: Accept keeps filtered rows; next `/` session starts fresh on the filtered view (since
  `display_results` is now the filtered vector — this is acceptable and consistent).
- AC6: No-matches state: empty table, navigation no-ops, cancel restores.
- AC7: All existing shortcuts unchanged when search mode inactive; only search keys intercepted
  when active.
- AC8: No new lint/Sonar issues.

---

### Keyboard dispatch summary (all phases combined)

```
HandleResultsTableKeyboardShortcuts called with: state, display_results, ..., incremental_search

[Always first]
  CheckBatchNumber(state.resultsBatchNumber, state.selectedRow)  // in Render(), not here

[Guard — always present]
  if (!window_focused || want_text_input) return;

[Search-active branch — new]
  if incremental_search.IsPromptVisible():
      consume InputQueueCharacters → append to query
      handle Backspace → remove from query
      if query changed: UpdateQuery(...)
      if Enter: Accept()
      if Esc or Ctrl+G: Cancel(state.selectedRow, state.scrollToSelectedRow)
      if N or DownArrow: NavigateNext(...)
      if P or UpArrow: NavigatePrev(...)
      return  ← search branch always returns; normal shortcuts below are skipped

[Search activation — new, only when !IsPromptVisible()]
  if Slash (ImGuiKey_Slash) and display_results non-empty:
      Begin(display_results, state.selectedRow, scroll_y, state.resultsBatchNumber)
      // does NOT return; activation is not a shortcut that needs early exit

[Existing shortcuts — unchanged]
  W / Shift+W → copy paths / filenames
  Shift+D     → bulk delete popup
  Enter / Ctrl+Enter / Ctrl+Shift+C → open / reveal / copy path
  Tab         → focus filename input
  Ctrl+Shift+P (Windows) → pin to Quick Access
  M / Shift+M / T / Shift+T / U / Shift+U → mark operations
  DownArrow / N → next row
  UpArrow / P   → previous row
```

The existing guard (`!window_focused || want_text_input`) is unchanged and fires before both the
search branch and the existing shortcuts. Since `WantTextInput` is never set true by this feature
(no `ImGui::InputText` used), the guard behaves identically to today.

---

### Task breakdown

| Phase | New files | Modified files | Est. (h) | Verification gate |
|---|---|---|---|---|
| P1 | `ResultsTableKeyboard.h/.cpp` | `ResultsTable.cpp`, `CMakeLists.txt` | 1 | Build + no behaviour change |
| P2 | `IncrementalSearchState.h/.cpp`, `tests/IncrementalSearchStateTests.cpp` | `CMakeLists.txt` | 2 | All unit tests pass, no ImGui in new files |
| P3 | — | `ResultsTable.cpp`, `ResultsTableKeyboard.cpp/.h` | 1.5 | Prompt appears/disappears, shortcuts unchanged |
| P4 | — | `ResultsTable.cpp`, `ResultsTableKeyboard.cpp` | 2 | Filtering correct, no shortcut regressions |
| P5 | — | `ResultsTableKeyboard.cpp`, `IncrementalSearchState.cpp` | 1.5 | Navigation wraps, cancel restores |
| P6 | — | All modified files | 1.5 | AC1–AC8, lint/Sonar clean |

Total estimated: 9.5 hours.

---

### Risks and mitigations

| Risk | Mitigation |
|---|---|
| Keyboard regression in search-active branch | The early-return pattern isolates search from normal shortcuts entirely. Phase 3 acceptance includes explicit verification of every existing shortcut while search mode is active. |
| `ImGui::SetScrollY` called outside BeginTable | Phase 3 wires `ConsumeScrollRestorePending` inside the BeginTable block; Phase 5 verifies with a real test. |
| Post-accept filter showing stale results after new search | `CheckBatchNumber` clears both flags; Phase 6 explicitly tests this. |
| `SearchResult::string_view` members dangling in `filtered_results_` | `searchResultPathPool` lives in `GuiState` for the full search session lifetime; it is only cleared in `ClearInputs()`, which also sets `resultsBatchNumber`. The batch change clears `filtered_results_` before the pool is freed. Document this invariant in a comment in `IncrementalSearchState.cpp`. |
| Windows build failure | Use `(std::min)`/`(std::max)`; explicit lambda captures; lowercase `<windows.h>`; match forward declarations. Phase 1 pure refactor is the highest-risk step for this — verify on Windows after P1. |

---

### Validation and handoff

Before merging:
- All six phase acceptance gates passed in order.
- `scripts/build_tests_macos.sh` green after each phase.
- Manual smoke test on macOS: large result set (10k+ rows), typing speed, cancel, accept,
  new search after accept, Ctrl+G.
- Manual smoke test on Windows: same sequence plus Quick Access shortcut unaffected.
- No new clang-tidy or SonarQube issues on any modified file.

For implementation prompts (Cursor Agent/Composer):
- Implement one phase at a time. Do not start the next phase until the acceptance gate for
  the current phase is confirmed.
- Reference this document as the authoritative spec. If a detail is ambiguous, resolve it
  conservatively (smallest change).
- Follow `AGENTS.md` and `internal-docs/prompts/AGENT_STRICT_CONSTRAINTS.md`.
