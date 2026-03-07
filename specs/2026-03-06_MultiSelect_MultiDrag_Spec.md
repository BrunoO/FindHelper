2026-03-06_Multi-selection and Multi-item Drag-and-Drop Specification
===================================================================

## 1. Overview

**Feature**: Multi-row selection in the results table and multi-file drag-and-drop on Windows, with an optional extension for other platforms.

**Phases**
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

- `std::set<int> selectedRows{};`
  - Represents the set of selected **row indices** into `display_results`.
  - Alternatives (`std::vector<int>`, `std::unordered_set<int>`) may be considered, but a sorted `std::set<int>` offers:
    - Deterministic iteration order (ascending row index) for bulk operations (delete, drag).
    - Reasonable performance for the expected number of selected rows.

- `int selectionAnchorRow = -1;`
  - Index used as the **anchor** for Shift+Click / Shift+Arrow range operations.
  - `-1` signals “no anchor currently set”.

**Helpers**

Add small helpers (header-level or in an appropriate `ui` utility namespace) to centralize selection logic:

- `void ClearSelection(GuiState& state);`
  - Clears `selectedRows` and sets `selectionAnchorRow` to `-1`.

- `void SelectRow(GuiState& state, int row);`
  - Inserts `row` into `selectedRows`.
  - Sets `selectionAnchorRow = row`.

- `void ToggleRow(GuiState& state, int row);`
  - If `row` is in `selectedRows`, erases it.
  - If not present, inserts it and sets `selectionAnchorRow = row`.
  - If after erase `selectedRows` is empty, sets `selectionAnchorRow = -1`.

- `bool IsRowSelected(const GuiState& state, int row);`
  - Returns `state.selectedRows.find(row) != state.selectedRows.end();`

- `int GetPrimarySelectedRow(const GuiState& state);`
  - Returns a “primary” row index to preserve compatibility with single-row consumers:
    - Prefer `selectionAnchorRow` if it is valid and present in `selectedRows`.
    - Otherwise, if `selectedRows` has exactly one element, return that element.
    - Otherwise, return a sentinel (`-1`) where a single primary cannot be clearly defined.

- `void ClampSelectionToRowCount(GuiState& state, std::size_t row_count);`
  - Removes any entries in `selectedRows` `>= static_cast<int>(row_count)`.
  - If `selectionAnchorRow` is out of range, sets it to `-1`.

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
   - Delete and context menu operations respect the multi-selection model:
     - When a row in the selection is right-clicked, operations apply to the entire selection.
     - When a non-selected row is right-clicked, selection becomes that row only and operations apply to it.

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
   - Tests (unit and/or ImGui regression where applicable) added or extended to cover new behaviors.

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

**Phase 1 – Multi-selection (all platforms)**
- Extend `GuiState` with `selectedRows` and `selectionAnchorRow`.
- Add selection helper functions.
- Wire click and keyboard handling in `ResultsTable.cpp` to use the new model.
- Migrate delete, context menu, and any other selection consumers to use multi-selection helpers.

**Phase 2 – Windows multi-file drag**
- Extend `DragDropUtils` with a multi-path overload and `CreateHDropForPaths`.
- Integrate selection-aware drag in `ResultsTable.cpp`, constructing multi-file lists when drag starts from a selected row with multiple selections.
- Validate behavior across Windows drop targets; ensure regression-free single-file drag.

**Phase 3 – Optional macOS / Linux multi-drag**
- Implement `StartFileDragDrop(const std::vector<std::string_view>&)` in `DragDrop_mac.mm` and any Linux drag-drop module, respecting the same selection semantics.
- Reuse the existing selection-aware drag initiation logic without change.

This document serves as the authoritative specification for implementing multi-selection in the results table and multi-file drag-and-drop, guiding both the implementation and review process.

