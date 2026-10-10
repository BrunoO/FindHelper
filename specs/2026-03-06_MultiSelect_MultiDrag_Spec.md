2026-03-06_Multi-selection and Multi-item Drag-and-Drop Specification
===================================================================

## 1. Overview

**Feature**: Multi-row selection in the results table and multi-file drag-and-drop on Windows, with an optional extension for other platforms.

**Phases**
- **Preliminary refactorings (all platforms, behavior-preserving)**:
  - Introduce a small selection facade around the existing `GuiState::selectedRow` and centralize scroll-to-selection, keyboard navigation, and incremental-search selection handling through it.
  - Extract a single, explicit “results table focused” check (window focused + no text input) and use it consistently for table keyboard shortcuts and selection behavior.
- **Phase 1 (core, all platforms)**: Introduce a robust multi-selection model for the results table and migrate all selection consumers.
- **Phase 2 (Windows)**: Enable multi-file drag-and-drop from the results table to Windows shell targets (Explorer, Desktop, other apps).
- **Phase 3 (optional, macOS and later Linux)**: Provide platform-native multi-file drag behavior using the same selection model and public drag API.

**Goals**
- Provide Windows-standard multi-selection semantics (Click, Ctrl+Click, Shift+Click, keyboard variants).
- Keep the UI responsive and predictable while respecting ImGui’s immediate-mode paradigm.
- Reuse the existing drag gesture logic and `drag_drop` abstraction; avoid duplicating platform-specific code paths.
- Preserve current single-selection and single-file drag behavior as a subset of the new model.

**Non-goals**
- Persisting selection across new searches beyond existing behavior.
- Per-cell or per-column selection.
- Speculative abstractions unrelated to the results table and drag-drop.

---

## 2. Current State

### 2.1 Selection Model

- `GuiState` uses a **single-selection** model:
  - `int selectedRow = -1;` (row index into the current `display_results` vector).
- The selected row is used for:
  - **Delete key** + confirmation popup.
  - **Context menu** (Windows, shell operations) when right-clicking.
  - **Single-file drag-drop** (`drag_drop::StartFileDragDrop`).
  - Some keyboard navigation flows (Up/Down, etc.).
- Rendering in `ResultsTable.cpp` checks `selectedRow == row` to decide whether to highlight a row.

### 2.2 Drag-and-Drop

- The results table drag gesture is implemented in `ResultsTable.cpp` via:
  - Internal state for drag candidates (`drag_candidate_active`, `drag_candidate_row`, `drag_start_pos`, `drag_started`).
  - `HandleDragAndDrop` which:
    - Detects a drag gesture (left mouse button down, move beyond threshold).
    - On threshold exceed and valid row index, calls:
      - `TryStartFileDragDropIfAllowed(const SearchResult& drag_result, const GuiState& state)`.
  - `TryStartFileDragDropIfAllowed` currently:
    - Rejects directories and entries pending deletion.
    - On Windows and macOS:
      - Calls `drag_drop::StartFileDragDrop(drag_result.fullPath)` for a **single** path.

### 2.3 Platform Drag APIs

- **Windows (`DragDropUtils.cpp`)**
  - `namespace drag_drop { bool StartFileDragDrop(std::string_view full_path_utf8); }`
  - Validates and converts UTF-8 path → UTF-16 `std::wstring`.
  - Verifies the path exists and is a **file** (directories rejected).
  - Builds a `CF_HDROP` payload with a single path via `CreateHDropForPath`.
  - Uses custom `IDataObject` (`FileDropDataObject`) and `IDropSource` (`BasicDropSource`).
  - Calls `DoDragDrop(...)` on the UI thread with `DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK`.

- **macOS (`DragDrop_mac.mm`)**
  - Implements the same `drag_drop::StartFileDragDrop(std::string_view)` signature.
  - Uses AppKit:
    - Converts UTF-8 path to `NSString`.
    - Validates file existence (non-directory).
    - On the main thread, creates an `NSDraggingSession` with a single `NSURL *` as pasteboard writer.

---

## 3. Requirements

### 3.1 Functional Requirements — Selection (Phase 1)

1. **Single Click Selection**
   - Clicking a row with **no modifiers**:
     - Clears any existing selection.
     - Selects exactly the clicked row.
     - Sets the **selection anchor** to that row.

2. **Ctrl+Click Toggle**
   - Ctrl+Click (Cmd+Click on macOS, mapped via ImGui IO) on a row:
     - If the row is not selected, adds it to the selection and sets the anchor to that row.
     - If the row is already selected, removes it from the selection.
     - If this removal empties the selection, anchor becomes “none” (e.g. `-1`).

3. **Shift+Click Range Selection**
   - Shift+Click on a row:
     - If a valid anchor exists, selects all rows in the **inclusive range** `[min(anchor, clicked), max(anchor, clicked)]` and clears all others.
     - If no valid anchor exists, behaves like a simple click (single selection and anchor = clicked row).

4. **Keyboard Range Extension**
   - With table focused and ImGui not in text-input mode:
     - **Shift+Down**: extends selection down by one row (if within bounds), updating anchor and primary selection appropriately.
     - **Shift+Up**: extends selection up by one row (if within bounds).
     - **Shift+PageDown / Shift+PageUp**: extends selection by approximately one visible page of rows in the respective direction.
     - When selection changes via keyboard, the newly selected edge row is scrolled into view (or nearest feasible).

5. **Select All**
   - With the table focused:
     - **Ctrl+A** selects all rows when there is at least one row.
     - `selectionAnchorRow` is set to the first row (index 0) or another clearly defined primary row.
     - Ctrl+A has no effect when focus is in a text input and `ImGui::GetIO().WantTextInput` is true.

6. **Delete Uses Multi-selection**
   - Pressing **Delete**:
     - If one or more rows are selected:
       - Opens a delete confirmation dialog that reflects a **count** of selected items.
       - On confirm, applies delete semantics to **all selected rows** (possibly in a later step; minimally, delete should not conflict with multi-selection).
       - On cancel, leaves selection unchanged.
     - If no rows are selected:
       - Either behaves as a no-op or preserves existing legacy behavior, but must not delete unintended items.

7. **Context Menu Respecting Selection (Windows)**
   - Right-clicking on a row:
     - If that row is already selected, context menu operations apply to the entire selection.
     - If that row is not selected, selection is cleared, that row becomes the sole selected row, and context menu applies to it.
   - **Phase 1 implementation note:** The context menu always operates on the hovered path (the path under the right-click cursor), regardless of `selectedRows`. This single-item behavior is the accepted Phase 1 default. Multi-selection-aware context menu (applying operations to the full `selectedRows` set) is deferred to a future phase.

8. **Selection Persistence During Minor State Changes**
   - When the contents of `display_results` change due to streaming or filtering:
     - Selection indices are clamped to the valid `[0, size-1]` range.
     - Any out-of-range indices are dropped from the selection.
     - `selectionAnchorRow` is updated or cleared appropriately.

### 3.2 Functional Requirements — Multi-drag on Windows (Phase 2)

1. **Single-file Drag (Regression)**
   - When exactly one row is selected and the drag originates from that selected row:
     - Behavior is identical to today’s single-file drag:
       - One file in the `CF_HDROP`.
       - Same drag cursors and keyboard semantics (Esc to cancel; left button release to drop).

2. **Multi-file Drag from Selected Row**
   - When **multiple rows** are selected and the drag originates from a selected row:
     - Drag-drop packages all **eligible selected rows**:
       - Eligible means:
         - Not a directory (matching current single-drag restriction unless we explicitly change it).
         - Not marked as pending deletion in `GuiState`.
     - A single `DoDragDrop` call is issued using a `CF_HDROP` that contains all selected file paths.
     - Drop targets (Explorer, Desktop, Outlook, etc.) receive a multi-file drop.

3. **Drag from Unselected Row**
   - When multiple rows are selected but the drag originates from an unselected row:
     - Current selection is cleared.
     - That row becomes the sole selected row.
     - A single-file drag is initiated for that row.

4. **Error Handling in Multi-drag**
   - If **all** candidate paths fail validation (non-existent, conversion failure, directory when disallowed, etc.):
     - No drag operation is started.
     - A descriptive log message is emitted via `LOG_ERROR_BUILD` / `LOG_WARNING_BUILD`.
   - If **some** candidates are invalid but at least one valid file remains:
     - Preferred behavior for first iteration: **fail fast** and log, to keep behavior simple and predictable.
     - Optionally, later iterations may choose to ignore invalid entries while still dragging valid ones, with clear logging.

5. **Threading and COM Constraints**
   - Drag initiation remains on the **UI thread**.
   - COM/OLE remains initialized via existing `AppBootstrap_win` logic (`OleInitialize`).
   - No drag APIs are called from background threads.

### 3.3 Functional Requirements — Optional Multi-drag on macOS (Phase 3)

1. **Multi-file Drag Using Same Selection Model**
   - When multiple rows are selected and the drag originates from a selected row on macOS:
     - A single `NSDraggingSession` is started with multiple `NSURL *` pasteboard writers, one per eligible selected path.
     - Directories are treated consistently with Windows policy (accepted or rejected uniformly).

2. **API Parity**
   - macOS implementation uses the same public `drag_drop` overloads as Windows:
     - `bool StartFileDragDrop(const std::vector<std::string_view>& full_paths_utf8);`
   - ResultsTable logic is platform-agnostic; platform differences reside only in the per-OS implementation.

---

## 4. Data Model and APIs

### 4.1 `GuiState` Extensions (Phase 1)

**New members**

- `std::vector<int> selectedRows{};`
  - Represents the set of selected **row indices** into `display_results`.
  - The vector is kept **sorted and unique**; helpers are responsible for maintaining this invariant.
  - `IsRowSelected` uses `std::binary_search` over `selectedRows`, which is cache-friendly for the common case of “a few” selected rows (typically \< 10) and still acceptable for the occasional large selection (e.g. `Ctrl+A`).
  - Semantically, selection is **entity-based**: when `display_results` is re-sorted or regenerated, selection should follow the same logical items (e.g. by stable result identity such as full path or an internal ID), not their old row positions. A separate remapping step is responsible for updating `selectedRows` to the new row indices after such changes.

- `int selectionAnchorRow = -1;`
  - Index used as the **anchor** for Shift+Click / Shift+Arrow range operations.
  - `-1` signals “no anchor currently set”.

**Helpers**

Add small helpers (header-level or in an appropriate `ui` utility namespace) to centralize selection logic:

- `void ClearSelection(GuiState& state);`
  - Clears `selectedRows` and sets `selectionAnchorRow` to `-1`.

- `void SelectRow(GuiState& state, int row);`
  - Inserts `row` into `selectedRows` while preserving the sorted/unique invariant (e.g. via `std::lower_bound` and conditional insert).
  - Sets `selectionAnchorRow = row`.

- `void ToggleRow(GuiState& state, int row);`
  - If `row` is in `selectedRows`, erases it (using `std::lower_bound` to locate it in the sorted vector).
  - If not present, inserts it in sorted position and sets `selectionAnchorRow = row`.
  - If after erase `selectedRows` is empty, sets `selectionAnchorRow = -1`.

- `bool IsRowSelected(const GuiState& state, int row);`
  - Returns `std::binary_search(state.selectedRows.begin(), state.selectedRows.end(), row);`

- `int GetPrimarySelectedRow(const GuiState& state);`
  - Returns a “primary” row index to preserve compatibility with single-row consumers:
    - Prefer `selectionAnchorRow` if it is valid and present in `selectedRows`.
    - Otherwise, if `selectedRows` has exactly one element, return that element.
    - Otherwise, return a sentinel (`-1`) where a single primary cannot be clearly defined.

- `void ClampSelectionToRowCount(GuiState& state, std::size_t row_count);`
  - Removes any entries in `selectedRows` `>= static_cast<int>(row_count)`.
  - If `selectionAnchorRow` is out of range, sets it to `-1`.

- `void RemapSelectionAfterDisplayResultsChange(GuiState& state,
                                               const DisplayResults& old_results,
                                               const DisplayResults& new_results);`
  - Rebuilds `selectedRows` (and adjusts `selectionAnchorRow` if needed) so that selection continues to refer to the same logical items after `display_results` has been regenerated or re-sorted (using a stable identity such as full path or a dedicated result identifier).

All selection consumers (delete, drag, context menu, keyboard navigation) must use these helpers instead of manipulating `selectedRow` directly.

### 4.2 Windows Drag API Extensions (Phase 2)

**Public API**

Extend `platform/windows/DragDropUtils.h`:

```cpp
namespace drag_drop {

  bool StartFileDragDrop(std::string_view full_path_utf8);

  bool StartFileDragDrop(const std::vector<std::string_view>& full_paths_utf8);

}  // namespace drag_drop
```

**Implementation outline**

- New helper:

```cpp
HGLOBAL CreateHDropForPaths(const std::vector<std::wstring>& paths);
```

- Algorithm:
  - If `paths` is empty, return `nullptr` and log.
  - Compute total number of wide characters:
    - For each path `p`: `p.length() + 1` (including its `L'\0'`).
    - Add a final `+1` for the list-terminating extra `L'\0'`.
  - Allocate:
    - `SIZE_T total_size = sizeof(DROPFILES) + total_chars * sizeof(wchar_t);`
    - `HGLOBAL h_global = GlobalAlloc(GHND, total_size);`
  - Lock and fill `DROPFILES` header:
    - `pFiles = sizeof(DROPFILES);`
    - `fWide = TRUE;`
    - Other fields as in the existing single-path helper.
  - Fill paths:
    - Treat `buffer` as `wchar_t*` immediately after `DROPFILES`.
    - For each path `p`:
      - `memcpy` its characters, then append `L'\0'`.
    - After the loop, append an extra `L'\0'`.
  - Unlock and return `h_global`.

- New overload `StartFileDragDrop(const std::vector<std::string_view>&)`:
  - Validate input:
    - Non-empty vector.
    - Each element passes `ValidatePath`.
  - For each path:
    - Convert to `std::wstring` via `Utf8ToWide`.
    - Check existence and file attributes with `GetFileAttributesW` (reject directories if preserving current policy).
  - Assemble validated wide paths into a `std::vector<std::wstring>`.
  - If final vector is empty (all invalid), log and return `false`.
  - Call `CreateHDropForPaths`.
  - Use the existing `FileDropDataObject` and `BasicDropSource` with the multi-path HGLOBAL.
  - Call `DoDragDrop` and preserve current result semantics and logging.

- Existing single-path overload:
  - May remain as-is, or:
    - Be reimplemented as:
      - `return StartFileDragDrop(std::vector<std::string_view>{full_path_utf8});`
    - Implementation choice can be deferred, but in either case behavior must remain backward-compatible.

---

## 5. UI Integration

### 5.1 Multi-selection in `ResultsTable.cpp` (Phase 1)

**Click handling**

- Introduce a helper in `ResultsTable.cpp`:

```cpp
void HandleRowClick(GuiState& state, int row);
```

- Behavior:
  - Read `ImGuiIO& io = ImGui::GetIO();`
  - If `io.KeyCtrl` (or platform-appropriate modifier):
    - Call `ToggleRow(state, row)`.
  - Else if `io.KeyShift`:
    - If `selectionAnchorRow` is valid:
      - Compute `start = min(selectionAnchorRow, row)`, `end = max(selectionAnchorRow, row)`.
      - Call `ClearSelection(state)` and then `SelectRow(state, i)` for each `i` in `[start, end]`.
      - Keep `selectionAnchorRow` unchanged.
    - Else:
      - Behave like a simple click (clear and `SelectRow`).
  - Else (no modifiers):
    - `ClearSelection(state);`
    - `SelectRow(state, row);`

- For each `ImGui::Selectable` representing a row:
  - Replace existing logic that directly sets `selectedRow` with:
    - `if (ImGui::Selectable(...)) { HandleRowClick(state, row); }`

**Keyboard handling**

- Add:

```cpp
void HandleTableKeyboardSelection(GuiState& state,
                                  std::size_t row_count,
                                  bool table_focused);
```

- Behavior:
  - Only active when:
    - `table_focused` is true (e.g. determined via `ImGui::IsWindowFocused` or table identifier).
    - `!ImGui::GetIO().WantTextInput`.
  - Handles:
    - Shift+Up / Shift+Down: Extend selection up/down one row from the current primary selected row.
    - Shift+PageUp / Shift+PageDown: Extend selection by an estimated page size (derived from row height and viewport).
    - Ctrl+A: Select all rows.
  - Always enforces that:
    - Selection indices remain in `[0, row_count - 1]`.
    - `selectionAnchorRow` either points to a selected row or is `-1`.

**Rendering**

- Replace `selectedRow == row` checks with `IsRowSelected(state, row)` when:
  - Deciding highlight color for the row.
  - Any other conditional logic dependent on selection.

**Selection count display**

- Extend the existing status/footer area that shows aggregate information (e.g. number of marked items) to also display the current **selection count**:
  - When `selectedRows` is empty: no selection count is shown (or “0 selected” if a consistent phrasing is preferred).
  - When at least one row is selected: show “N selected” alongside the existing “M marked” indicator, using the same visual style and placement conventions.
  - On platforms without drag support (e.g. Linux), the selection count is still shown and kept accurate; only drag behavior differs by platform.

### 5.2 Drag-and-drop Integration (Phase 2 & 3)

**Selection-aware drag initiation**

- Update `TryStartFileDragDropIfAllowed` (or split it into smaller helpers) to:

1. Confirm the drag candidate row is valid for drag based on:
   - Not a directory (unless policy is changed).
   - Not pending deletion (`state.pendingDeletions`).

2. Determine the set of rows to drag:
   - If `state.selectedRows` contains the drag row and `state.selectedRows.size() > 1`:
     - Compute a sorted sequence of selected row indices (if `std::set` is used, they are inherently sorted).
     - For each index:
       - Ensure within `display_results.size()`.
       - Respect the same eligibility criteria (file vs directory, not pending deletion).
     - Collect corresponding `fullPath` values into a `std::vector<std::string_view>` and call:
       - `drag_drop::StartFileDragDrop(full_paths);`
   - Else:
     - Treat as a single-file drag:
       - Optionally clear selection and `SelectRow(state, drag_row)` if it was not selected.
       - Call single-path `StartFileDragDrop`.

3. Maintain existing cursor feedback:
   - The pre-drag **hand cursor** behavior (`ImGui::SetMouseCursor(ImGuiMouseCursor_Hand)` while drag gesture is in progress) should remain unchanged.

**Platform guards**

- Keep existing compile-time guards in `ResultsTable.cpp`:

```cpp
#if defined(_WIN32) || defined(__APPLE__)
  // selection-aware call to drag_drop::StartFileDragDrop(...)
#endif  // defined(_WIN32) || defined(__APPLE__)
```

- On macOS, once Phase 3 is implemented:
  - The same selection-aware logic will be used; per-platform behavior resides only in `drag_drop` implementations.

---

## 6. Acceptance Criteria

1. **Selection Semantics**
   - Single-click, Ctrl+Click, Shift+Click, Shift+Arrow, Shift+PageUp/PageDown, and Ctrl+A behave as described in Section 3.1 under manual testing.
   - Visually, the correct set of rows is highlighted at all times.

2. **Selection Invariants**
   - For all code paths, `selectedRows` never contains indices outside `[0, display_results.size() - 1]`.
   - `selectionAnchorRow` is either `-1` or a valid index and (if valid) should generally be part of `selectedRows`.
   - Assertions or explicit checks protect against invalid indices in debug builds.

3. **Delete and Context Menu Behavior**
   - Delete respects the multi-selection model: pressing Delete opens a confirmation dialog listing all `selectedRows` and attempts deletion of each item with aggregate feedback.
   - Context menu (Windows): **Phase 1 accepted default** — always operates on the hovered path only, ignoring `selectedRows`. Multi-selection-aware context menu is deferred to a future phase.

4. **Windows Multi-drag Correctness**
   - Single-file drag continues to work exactly as before.
   - Multi-file drag from a selected row:
     - Drops all selected eligible files into Windows Explorer, Desktop, and at least one other application (e.g. Outlook).
     - No extraneous or missing items appear in the drop target.

5. **macOS Optional Multi-drag**
   - If implemented, multi-file drag behavior mirrors Windows at a functional level (subject to platform UI differences).

6. **Quality Gates**
   - No new clang-tidy or SonarQube issues in modified files.
   - All new string parameters use `std::string_view` for read-only inputs.
   - No C-style arrays in new code; use `std::array`/`std::vector`.
   - No `} if (` on the same line; follow `AGENTS.md` formatting guidelines.
   - Tests (unit, ImGui regression, and manual where applicable) added or extended to cover new behaviors, using the dedicated test matrix in `2026-03-16_MultiSelect_MultiDrag_TestMatrix.md` as the reference.

---

## 7. Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|-----------|
| Selection indices become invalid after search/filter updates | Crashes, undefined behavior, incorrect selection | Centralize and call `ClampSelectionToRowCount` whenever `display_results` changes; add debug assertions when using selection indices. |
| Selection logic increases complexity in `ResultsTable.cpp` | Hard-to-maintain code, Sonar S3776 | Isolate logic into small helpers (`HandleRowClick`, `HandleTableKeyboardSelection`, selection utilities) and keep render loops shallow. |
| Incorrect DROPFILES construction for multi-file drag on Windows | Drag fails or behaves inconsistently across targets | Implement `CreateHDropForPaths` carefully; add a small Windows-only test that inspects the memory layout; manually test with Explorer, Desktop, Outlook. |
| Performance issues with large selections | UI stutters when starting drag or changing selection | Use efficient containers, reserve capacity where possible, and keep selection/dnd code O(N) in the number of selected rows with minimal overhead. |
| UX inconsistency across platforms during rollout | Confusing user experience when switching OS | Document platform-specific support in the Help window; ensure macOS retains correct single-file drag semantics until multi-drag is implemented. |

---

## 8. Implementation Phases (Summary)

### Phase 0 – Preliminary, behavior-preserving refactorings (all platforms)

Complete these refactorings before introducing any multi-selection state. They must not change user-visible behavior:

- **Selection facade for `selectedRow`**
  - Introduce helpers (initially implemented in terms of `GuiState::selectedRow`):
    - `bool HasSelection(const GuiState& state);`
    - `int GetSelectedRow(const GuiState& state);`
    - `void SetSelectedRow(GuiState& state, int row);`
    - `void ClearSelection(GuiState& state);`
  - Replace all direct reads/writes of `state.selectedRow` at call sites with these helpers.
  - Add `void EnsureSomeSelection(GuiState& state, const DisplayResults& display_results);` and use it instead of ad-hoc “select row 0 if none selected” logic.

- **Canonical “results table keyboard active” check**
  - Extract the combined condition “results table window focused AND `!ImGui::GetIO().WantTextInput`” into a single helper or a `table_focused`/`resultsTableKeyboardActive` boolean.
  - Ensure all keyboard shortcut handlers that mutate selection use this one concept and perform an early return when it is false.

- **Centralized scroll-to-selected behavior**
  - Add `void ScrollToSelectedRowIfNeeded(GuiState& state, const DisplayResults& display_results);` in `ResultsTable.cpp`.
  - Replace all open-coded scroll-to-selected logic with calls to this helper.

- **Incremental-search selection helpers**
  - Introduce:
    - `int GetIncrementalSearchAnchorRow(const GuiState& state);`
    - `void SetIncrementalSearchSelection(GuiState& state, int row, bool scroll_to_row);`
  - Update incremental search code to use these helpers rather than touching `selectedRow` directly.

- **Grouped keyboard navigation**
  - Refactor scattered Up/Down/PageUp/PageDown handlers in `ResultsTableKeyboard.cpp` into clearly named functions (e.g. `MoveSelectionUp`, `MoveSelectionDown`, and page variants) that only talk to the selection facade instead of raw `selectedRow`.

- **Primary selected row helper**
  - Introduce `int GetPrimarySelectedRow(const GuiState& state, const DisplayResults& display_results);` and use it wherever a single row index is needed (incremental search, open/reveal/copy, delete, drag).
  - For Phase 0, this helper simply wraps the existing single-selection model.

- **Index-list helpers for selection/marked rows**
  - Add helpers such as:
    - `std::vector<int> GetSingleSelectionRowIndices(const GuiState& state, const DisplayResults& display_results);`
    - `std::vector<int> GetMarkedRowIndices(const GuiState& state, const DisplayResults& display_results);`
  - Replace ad-hoc index-vector construction in delete, copy-as-CSV, and related features with these helpers.

- **Separate “compute selection” from “act on selection”**
  - Define a small `SelectionInfo` structure (Phase 0: still representing the current single-selection model) and refactor operations like delete, open, reveal, copy path, copy CSV, and Quick Access pinning into:
    - A function that computes `SelectionInfo` from `GuiState` and `display_results`.
    - A function that performs the action given `SelectionInfo`.

- **Isolate open/reveal/copy behaviors**
  - Move “open selected file”, “reveal in Explorer/Finder”, “copy path”, “pin to Quick Access”, and “copy selected/marked rows as CSV” into a small set of utility functions that accept `SelectionInfo` or a primary row index.
  - Ensure keyboard handlers only compute selection and call these utilities.

- **Centralized post-action selection updates**
  - Introduce helpers like:
    - `void UpdateSelectionAfterDelete(GuiState& state, const DisplayResults& display_results);`
    - `void UpdateSelectionAfterNavigation(GuiState& state, const DisplayResults& display_results);`
  - Replace open-coded “what is selected after delete/navigation” logic with these helpers so Phase 1 can later implement consistent multi-selection-aware rules in one place.

### Phase 1 – Multi-selection (all platforms)

- Extend `GuiState` with `selectedRows` and `selectionAnchorRow`.
- Add and wire selection helper functions, replacing the Phase 0 facade internals.
- Implement entity-based selection remapping for sort/filter/stream changes.
- Update rendering, keyboard handling, delete, context menu, open/reveal/copy, and drag initiation to use the new multi-selection model.
- Add the selection count display in the status/footer region.

### Phase 2 – Windows multi-file drag

> **Implementation status**: not yet started. See gaps #2 and #3 in the
> "Implementation gaps" section below for the prerequisite work (drag from
> unselected row) and the full Phase 2 scope.

- **Prerequisite (gap #2)**: fix drag from unselected row — when the drag
  gesture originates on a row that is *not* in `selectedRows`, clear the
  selection, make that row the sole selection, and start a single-file drag.
  This must be in place before multi-drag so the two code paths are consistent.
- Extend `DragDropUtils` with a multi-path overload and `CreateHDropForPaths`.
- Integrate selection-aware drag in `ResultsTable.cpp`, constructing multi-file
  lists when drag starts from a selected row with multiple selections (gap #3).
- Validate behavior across Windows drop targets; ensure regression-free
  single-file drag.

### Phase 3 – Optional macOS / Linux multi-drag

- Implement `StartFileDragDrop(const std::vector<std::string_view>&)` in `DragDrop_mac.mm` and any Linux drag-drop module, respecting the same selection semantics.
- Reuse the existing selection-aware drag initiation logic without change.

This document serves as the authoritative specification for implementing multi-selection in the results table and multi-file drag-and-drop, guiding both the implementation and review process.

---

### 9. 2026-03-16 – Gaps and Clarifications Appendix

The following items capture identified gaps and clarification points for the multi-selection and multi-drag behavior. They are intended to guide implementation choices and potential future revisions of this spec.

1. **Selection container and performance**
   - Decision: `selectedRows` uses a sorted, unique `std::vector<int>` with `std::binary_search` in `IsRowSelected`.
   - Rationale:
     - Optimized for the common case of a small number of selected rows (typically fewer than 10).
     - Still acceptable for the occasional large selection via `Ctrl+A`, where the one-time cost of populating the vector is amortized, and per-frame binary search remains fast in practice.

2. **Selection stability under sorting and reordering**
   - Decision: selection is **entity-based**, matching familiar behavior from file managers and list views (Explorer, Finder, etc.): when the view is re-sorted, the same logical items remain selected even though their row positions change.
   - Implementation note:
     - A dedicated remapping step (e.g. `RemapSelectionAfterDisplayResultsChange`) must be invoked whenever `display_results` is regenerated or re-sorted so that `selectedRows` is updated to the new row indices based on a stable identity (such as full path or an internal result ID).

3. **Full migration of `selectedRow` consumers**
   - Enumerated consumers of the legacy `GuiState::selectedRow`:
     - `src/ui/ResultsTable.cpp`
       - Uses `selectedRow` to:
         - Highlight the selected row during rendering.
         - Scroll the table to the selected row when `scrollToSelectedRow` is true.
         - Update selection on mouse interactions (clicks, context menu, drag initiation).
       - **Decision**: Migrate all of this logic to use `selectedRows` / `selectionAnchorRow` and the helpers (`SelectRow`, `ToggleRow`, `IsRowSelected`, `GetPrimarySelectedRow`, `HandleRowClick`, `HandleTableKeyboardSelection`). Row highlight and scroll-to-selected will be driven by the multi-selection model, using `GetPrimarySelectedRow` when a single row index is needed.
     - `src/ui/ResultsTableKeyboard.cpp`
       - Uses `selectedRow` for:
         - Keyboard navigation (Up/Down, PageUp/PageDown) and associated scrolling.
         - Incremental search navigation (moving the “current” selection to next/previous match and cancelling search).
         - Building row index lists for operations that act on the current selection.
         - Determining the currently selected row when triggering actions (open file, delete, drag, etc.).
       - **Decision**: Replace direct `selectedRow` access with the multi-selection helpers:
         - Use `GetPrimarySelectedRow` as the “current selection” for incremental search and navigation.
         - When keyboard actions should operate on multiple rows (e.g. delete), use `selectedRows` directly.
         - Keep `scrollToSelectedRow` semantics by scrolling to `GetPrimarySelectedRow` when appropriate.
     - `src/core/Application.cpp`
       - Sets `state.selectedRow = 0;` in a few places after new results are available to ensure there is an initial selection.
       - **Decision**: Replace this with multi-selection-aware initialization:
         - Clear existing selection and anchor.
         - If there is at least one result, call `SelectRow(state, 0)` and set any “scroll to selection” flag as needed.
     - `src/ui/IncrementalSearchState.h` (and its implementation)
       - Treats `GuiState::selectedRow` as the current anchor row for incremental search navigation APIs.
       - **Decision**: Update documentation and call sites so incremental search takes/uses the primary selected row via `GetPrimarySelectedRow`, not a raw `selectedRow` field.
     - `GuiState.h`
       - Declares `int selectedRow = -1;` as part of the selection state.
       - **Decision**: After all call sites are migrated to `selectedRows` / `selectionAnchorRow` and helpers, remove `selectedRow` entirely from `GuiState` to avoid mixed-model behavior.
   - Mandatory preliminary refactorings (behavior-preserving, to be done before introducing multi-selection state):
     - **Introduce a single-selection facade**:
       - Add small helpers (initially implemented in terms of `selectedRow`) such as:
         - `bool HasSelection(const GuiState& state);`
         - `int GetSelectedRow(const GuiState& state);`
         - `void SetSelectedRow(GuiState& state, int row);`
         - `void ClearSelection(GuiState& state);`
       - Replace direct reads/writes of `state.selectedRow` at all call sites with these helpers so selection behavior remains identical but is routed through a single abstraction point.
     - **Centralize scroll-to-selected behavior**:
       - Add a helper such as `void ScrollToSelectedRowIfNeeded(GuiState& state, const DisplayResults& display_results);` in `ResultsTable.cpp`.
       - Ensure all scroll-to-selection logic calls this helper instead of open-coding the conditions.
     - **Isolate incremental-search selection handling**:
       - Introduce helpers like:
         - `int GetIncrementalSearchAnchorRow(const GuiState& state);`
         - `void SetIncrementalSearchSelection(GuiState& state, int row, bool scroll_to_row);`
       - Update incremental-search code to use these helpers rather than accessing `selectedRow` directly.
     - **Group keyboard navigation that mutates selection**:
       - Refactor scattered Up/Down/PageUp/PageDown handlers in `ResultsTableKeyboard.cpp` into clearly named functions (e.g. `MoveSelectionUp`, `MoveSelectionDown`) that only talk to the selection facade instead of raw `selectedRow`.
     - **Unify “ensure something is selected” logic**:
       - Replace ad-hoc patterns such as `if (state.selectedRow == -1 && !display_results.empty()) { state.selectedRow = 0; }` with a shared helper like `void EnsureSomeSelection(GuiState& state, const DisplayResults& display_results);`.
       - This helper will later map cleanly to initializing the multi-selection state (e.g. selecting row 0 in `selectedRows`) without changing today’s behavior.
      - **Extract helpers that compute row index lists**:
        - Introduce helpers that build vectors of row indices for operations that currently assume a single selected row or rely on marked items, for example:
          - `std::vector<int> GetSingleSelectionRowIndices(const GuiState& state, const DisplayResults& display_results);`
          - `std::vector<int> GetMarkedRowIndices(const GuiState& state, const DisplayResults& display_results);`
        - Replace ad-hoc index-vector construction in delete, copy-as-CSV, and similar features with these helpers so Phase 1 only needs to update their internals for multi-selection.
      - **Separate “compute selection” from “act on selection”**:
        - For operations like delete, open, reveal parent folder, copy path, and copy CSV, refactor into:
          - A small function that computes a `SelectionInfo` structure from `GuiState` and `display_results` (today wrapping a single row index).
          - A function that performs the action given `SelectionInfo`.
        - This keeps behavior unchanged while allowing `SelectionInfo` to evolve to “multiple rows” in Phase 1 with minimal call-site changes.
      - **Normalize primary-selected-row usage**:
        - Standardize on a single helper (e.g. `int GetPrimarySelectedRow(const GuiState& state, const DisplayResults& display_results);`) and use it wherever a single row index is needed (incremental search, open file, delete, drag).
        - This helper can delegate to `selectedRow` today and be updated to derive a primary from `selectedRows` during Phase 1.
      - **Isolate “open / reveal / copy” behavior**:
        - Ensure actions such as opening the selected file, revealing it in Explorer/Finder, copying its path, pinning to Quick Access, and copying rows as CSV are implemented in a small set of utility functions that take a primary row (or `SelectionInfo`) as input.
        - This minimizes the number of places that depend directly on “there is only one selected row”.
      - **Centralize post-action selection updates**:
        - Introduce helpers like:
          - `void UpdateSelectionAfterDelete(GuiState& state, const DisplayResults& display_results);`
          - `void UpdateSelectionAfterNavigation(GuiState& state, const DisplayResults& display_results);`
        - Use these helpers instead of open-coding “what is selected after delete/navigation” in multiple places so Phase 1 can define consistent multi-selection-aware rules in one location.

4. **Keyboard focus and interaction scope**
   - Existing behavior:
     - `HandleResultsTableKeyboardShortcuts` in `ResultsTableKeyboard.cpp` already derives a focus concept via `ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)` combined with `!ImGui::GetIO().WantTextInput`; when this compound condition fails, all table keyboard shortcuts (including marking) are ignored.
     - Some specific shortcuts (e.g. Delete in `HandleResultsTableKeyboardShortcuts`) perform their own `ImGui::IsWindowFocused` checks inline, effectively duplicating the focus logic.
   - Decision:
     - Reuse this existing window-focus-plus-no-text-input pattern as the canonical definition of “results table focused” for selection and shortcut handling.
     - As a preliminary refactoring, extract this into a small helper (e.g. `bool IsResultsTableKeyboardActive();` or a `table_focused` boolean computed once and threaded through lower-level helpers like `HandleTableKeyboardSelection`) so that all selection/shortcut code relies on a single, obvious concept instead of scattered `IsWindowFocused` calls.
   - Clarify precedence rules:
     - When the results table window is not focused or ImGui wants text input, table selection shortcuts (including multi-selection and marking) must be inactive.
     - When the results table window is focused and ImGui does not want text input, table selection shortcuts have precedence for the key combinations they own (Shift+Arrow, Shift+PageUp/PageDown, Ctrl+A, marking, delete, etc.).

5. **Delete semantics and partial failures**
   - Decision: Provide Explorer/Finder-like multi-delete behavior with clear, aggregate feedback:
     - Confirmation dialog:
       - For a single selected item: use singular wording (“Delete this item?”) and show its name/path as today.
       - For multiple selected items: use plural wording (“Delete these N items?”) and clearly state that all N will be deleted according to the existing delete semantics (Recycle Bin vs permanent, as currently implemented).
     - Execution semantics:
       - Attempt deletion independently for each selected item.
       - On success, remove the item from `display_results` and from the selection.
       - On failure (e.g. permissions, already removed or locked), keep the item in the list and keep it selected so the user can retry or inspect it.
     - User feedback and logging:
       - If all items are deleted successfully, close the popup; optionally show a short status such as “Deleted N items”.
       - If some deletions fail, show a concise message such as “Deleted X of N items. N − X could not be deleted (permissions or already removed).”
       - Log each failure via `LOG_WARNING_BUILD` with the path and error details.
     - Safety when nothing is selected:
       - Pressing Delete with no selected rows is a no-op (no popup), ensuring that delete operations always act on an explicit selection only.

6. **Drag-and-drop invalid items and directories**
   - Decision:
     - Keep dragging directories **disallowed** on all platforms for this feature set, matching the current single-drag restriction and avoiding partial folder drops.
     - For multi-drag error handling, match common shell behavior:
       - When some candidates are invalid (nonexistent, directories, pending deletion, conversion failure), ignore those items but still start a drag containing all remaining valid files.
       - Log each invalid candidate with a clear reason, and, if any were skipped, optionally expose a short status such as “Some selected items could not be dragged (directories or missing files); see log for details.”

7. **Linux drag-and-drop scope**
   - Decision:
     - Linux multi-file drag (and external shell drag-and-drop in general) is **out of scope** for this spec. The current implementation only supports external file drag-drop on Windows and macOS.
     - This limitation should be explicitly documented in user-facing materials:
       - Help / documentation: clarify that multi-file drag from the results table is available on Windows (and optionally macOS once Phase 3 is implemented), but not on Linux.
       - Release notes and “What’s New” sections: call out platform support for multi-selection + multi-drag and note that Linux remains unsupported for external drag-drop in this iteration.

8. **Testing and regression coverage**
   - Add a short test matrix to the spec (or a referenced document) that lists:
     - Interactions (Click, Ctrl+Click, Shift+Click, Shift+Arrow, Shift+PageUp/PageDown, Ctrl+A, Delete, right-click, drag from selected vs unselected rows).
     - Contexts (1 row, few rows, many rows; with/without streaming; before/after sort).
   - Specify which scenarios must be covered by ImGui Test Engine regression tests and which are expected to be validated manually.

9. **Assertions and invariants**
   - Identify canonical locations where selection invariants should be asserted in debug builds, such as:
     - At the end of `HandleRowClick` and `HandleTableKeyboardSelection`.
     - Immediately before using selection indices to index into `display_results` in delete, drag, and context menu paths.
   - TLA+-style state variables and invariants to make explicit in code and assertions:
     - **Selection state**:
       - State: `selectedRows` (subset of row indices) and `selectionAnchorRow` (index or `-1`).
       - Invariants:
         - Every `r` in `selectedRows` satisfies `0 <= r < display_results.size()`.
         - If `selectionAnchorRow != -1` then `0 <= selectionAnchorRow < display_results.size()` and, if we choose that model, `selectionAnchorRow` is present in `selectedRows`.
       - Assert these invariants in helpers such as `SelectRow`, `ToggleRow`, `ClearSelection`, `ClampSelectionToRowCount`, and `RemapSelectionAfterDisplayResultsChange`.
     - **Display results state**:
       - State: `display_results` (sorted/filtered view of logical results).
       - Invariant: selection indices always refer to valid entries in `display_results`; after any change to `display_results`, either selection is remapped (via `RemapSelectionAfterDisplayResultsChange`) or explicitly cleared.
     - **Focus / keyboard routing state**:
       - Derived state: `resultsTableKeyboardActive = window_focused && !io.WantTextInput`.
       - Invariant: when `resultsTableKeyboardActive` is false, no code path should mutate selection in response to table keyboard shortcuts.
       - Assert this by centralizing the check at the top of keyboard handlers and avoiding selection changes when it fails.
     - **Delete popup state**:
       - State: `showDeletePopup` flag and a `pendingDeleteSelection` snapshot of the current selection at the moment Delete is pressed.
       - Invariants:
         - If `showDeletePopup` is true, then `pendingDeleteSelection` is non-empty and contains only valid indices into `display_results` at the time of snapshot.
         - While the popup is open, `pendingDeleteSelection` is not mutated by further selection changes.
       - Assert ranges of `pendingDeleteSelection` before executing deletes on confirm.
     - **Drag initiation state**:
       - State: `dragCandidateRow` (index or `-1`) and a `dragGestureActive` flag.
       - Invariants: whenever a drag is about to start, `dragCandidateRow` and any indices used to build the multi-drag list satisfy `0 <= index < display_results.size()`.
       - Assert index bounds at the entry to selection-aware drag-start helpers before calling `drag_drop::StartFileDragDrop`.

10. **UX and discoverability**
    - Decision:
      - Show the current selection count in the same status/footer region that already displays the number of marked items, using consistent styling (e.g. “N selected, M marked”).
      - This count must update immediately as selection changes (via mouse, keyboard, or delete) and is shown on all platforms, regardless of whether external drag-and-drop is available.
    - Update the Help / keyboard shortcuts UI (if applicable) to document:
      - Multi-selection gestures (Click, Ctrl+Click, Shift+Click).
      - Keyboard range selection (Shift+Arrows, Shift+PageUp/PageDown, Ctrl+A).
      - Multi-drag behavior (dragging from a selected vs unselected row), and the meaning of the “N selected” indicator.

---

## Implementation gaps as of 2026-03-17

This section records known gaps between the spec and the current implementation.
Completed items are noted for traceability. Open items are prioritised by user impact.

### Completed

- **Sort remap**: `RemapSelectionAfterDisplayResultsChange` is called in `HandleTableSorting`
  (immediate sort) and `CheckAndCompleteAsyncSort` (async Size / LastModified sort). Selected
  items follow their logical entries to their new row positions after any column sort.

- **Filter remap**: `RemapSelectionAfterDisplayResultsChange` is called in `UpdateFilterCaches`
  whenever a time-filter or size-filter cache rebuild is detected (flag was invalid or filter
  setting changed). Items filtered out are deselected; surviving items map to their new indices.

- **Multi-delete**: Delete key snapshots all `selectedRows` paths into `filesToDelete`; the
  confirmation dialog scales from single-item to multi-item; `ExecuteMultiDelete` applies per-item
  deletion and produces aggregate success / failure feedback.

- **Invariant enforcement**: `AssertSelectionInvariant` is called at the end of every selection
  helper (`SetSelectedRow`, `ClearSelection`, `SelectRow`, `ToggleRow`, `SetSelectionRange`,
  `ClampSelectionToRowCount`, `RemapSelectionAfterDisplayResultsChange`).

- **macOS Cmd key**: All multi-select paths use `IsPrimaryShortcutModifierDown(io)` (returns
  `KeyCtrl || KeySuper`) so Cmd works on macOS everywhere Ctrl works on Windows/Linux.

### Open gaps

#### 1. Selection behavior when search results are replaced

**Current behavior**: when a search completes and `searchResults` is replaced, `selectedRows` is
not explicitly cleared or remapped. Indices are only clamped to the new result count, which can
leave selection pointing at wrong items if the result count is unchanged.

**Auto-refresh consideration**: this is intentionally nuanced. When *Auto-refresh* is on the
search re-runs automatically because the index changed (files were created, modified, or deleted),
not because the user changed the query. In that scenario, keeping selection on the same *logical
items* is the expected and desirable behavior — the user is watching a live view and their
selection should track the files they care about, exactly as it does after a sort. Clearing
selection on every auto-refresh would be disruptive.

**Proposed unified rule**: apply entity-based remap (`RemapSelectionAfterDisplayResultsChange`)
whenever `searchResults` is replaced, regardless of whether the replacement is triggered by a
manual search, instant search, or auto-refresh. Items that are no longer in the new results are
naturally deselected; items still present stay selected at their new positions. This satisfies
both the manual-search case (new unrelated query → old paths absent → selection empties) and the
auto-refresh case (same files mostly present → selection preserved).

**Implementation note**: the remap must be performed at the moment `searchResults` is replaced
(inside `UpdateSearchResults` in `SearchController.cpp`, or immediately after in the render loop
where `resultsUpdated` is consumed) and must snapshot the old display *before* the replacement
occurs. The filter remap added to `UpdateFilterCaches` does not cover this case because by the
time it runs, `searchResults` already holds the new data and the old results are gone.

#### 2. Drag from unselected row does not reset selection

**Spec requirement** (section 3.2.3): when a drag gesture starts on a row that is *not* in the
current selection, the selection should be cleared, that row should become the sole selected row,
and a single-file drag should be initiated for it.

**Current behavior**: `HandleDragAndDrop` uses `drag_candidate_row` regardless of whether it is
selected; it does not inspect or modify `selectedRows`.

**Fix**: in the drag threshold check inside `HandleDragAndDrop`, before calling
`TryStartFileDragDropIfAllowed`, check `state.IsRowSelected(drag_candidate_row)`. If false, call
`state.SetSelectedRow(drag_candidate_row)` before initiating the drag.

#### 3. Phase 2 — Multi-file drag (Windows)

Packaging all eligible selected file paths into a single `CF_HDROP` and issuing one `DoDragDrop`
call is not yet implemented. This is the bulk of Phase 2 as described in section 3.2. It depends
on gap #2 being resolved first (drag-from-unselected-row) since that determines whether the drag
uses the current selection or falls back to a single row.

#### 4. Incremental search uses raw `selectedRow` instead of `GetPrimarySelectedRow`

The spec (sections 4.1 and 5.4) says incremental search navigation should go through
`GetPrimarySelectedRow` so it is consistent with the multi-selection model. Current
`IncrementalSearchState` APIs accept and return a raw `int& selected_row` tied to
`GuiState::selectedRow`. Low functional impact today (single-selection still mirrors the
primary), but should be aligned before incremental search and multi-selection interact in
unexpected ways.

---

## Phase 4 (Future Option) — Drop Target: Accept Drops onto Rows

### Motivation

Currently FindHelper only acts as a **drag source**: files can be dragged out to other
applications. A complementary feature would make it a **drop target**: accepting files or
folders dropped onto a row in the results table. The primary use case is intra-app reordering
or relocation — drag one or more selected rows and drop them onto a directory row to move those
files into that directory.

### Use Cases

1. **Intra-app move**: select several files in the results table, drag them, and drop onto a
   directory row → move the files into that directory.
2. **External source → FindHelper**: drag files from Explorer/Finder and drop onto a directory
   row in the results table → move or copy them into that directory (semantics TBD).

### Platform Implementation Outline

**Windows**
- Implement `IDropTarget` (`DragEnter`, `DragOver`, `DragLeave`, `Drop`) in a new
  `DropTarget_win.cpp` under `src/platform/windows/`.
- Register the main window with `RegisterDragDrop` at startup; revoke with `RevokeDragDrop` at
  shutdown.
- In `DragOver`, compute which results-table row is under the cursor (see *Row Hit-Testing*
  below) and store it in a shared state variable so the render loop can highlight it.
- In `Drop`, extract the `CF_HDROP` payload, collect the source paths, and call the file-move
  helper for each path targeting the directory of the hovered row.

**macOS**
- Implement `NSDraggingDestination` on the content `NSView`: `draggingEntered:`,
  `draggingUpdated:`, `draggingExited:`, `performDragOperation:`.
- Register accepted types with `registerForDraggedTypes:@[NSFilenamesPboardType]` (or
  `NSPasteboardTypeFileURL` on newer SDKs).
- Same row-highlighting and file-operation pattern as Windows.

### Row Hit-Testing from Native Callbacks

ImGui does not expose a "row at position" query. The recommended approach:

- During the `ResultsTable` render pass, record the screen-space Y range of each visible row
  into a small auxiliary vector (e.g., `struct RowHitRect { int row; float y_min; float y_max; }`).
- Store this vector in `GuiState` (or a dedicated `DragDropState` struct), protected by a mutex
  if the native callbacks run on a different thread.
- The native `DragOver` / `draggingUpdated:` callback reads the vector, finds the row whose
  Y range contains the cursor, and sets `hovered_drop_row` accordingly.
- The render loop reads `hovered_drop_row` to draw the highlight (e.g., a colored separator or
  row background tint).

### Accepted Drop Semantics

- Only **directory** rows are valid drop targets (files cannot contain other files).
- The default operation is **move** for intra-app drags (source and destination are on the same
  volume) and **copy** for cross-app or cross-volume drags.
- A row that is a valid target displays a visual highlight (e.g., blue border or tinted
  background) during hover; invalid rows show no change.
- Dropping onto a non-directory row is a no-op (the drag is rejected in `DragOver` /
  `draggingUpdated:`).

### Dependencies and Risks

| Item | Note |
|---|---|
| File-move operation | Requires a new `MoveFile` helper (analogous to the existing delete path). Error handling (name conflicts, permissions) needed. |
| Thread safety | Native drop callbacks may fire on a non-main thread (Windows COM STA vs MTA); row-hit-rect vector access must be synchronized. |
| Undo / recovery | File moves are destructive; consider a confirmation dialog or undo stack before shipping. |
| Linux | Out of scope for this spec (no native shell drag protocol currently integrated). |

### Non-Goals for This Phase

- Reordering rows within the table (no logical ordering in a search result set).
- Accepting drops onto file rows.
- Drag-and-drop between two FindHelper windows.
- Linux support.
