# Multi-Select and Bulk Copy Paths – Implementation Plan

**Date**: 2026-02-14  
**Scope**: Results table multi-selection (Shift+Click, Ctrl+Click, Shift+Arrow) and bulk copy paths (newline-separated) for scripts/terminals.

---

## 1. Current State

### Selection Model
- **GuiState**: `int selectedRow = -1` (single selection)
- **Visual**: Row is selected when `selectedRow == row`
- **Consumers**: Delete key, context menu (Windows), drag-drop, Copy (hover-based)

### Click Handling
- Each row has 5 `ImGui::Selectable` cells (Filename, Size, Last Modified, Full Path, Extension)
- Any click sets `state.selectedRow = row`
- No modifier key handling

### Clipboard
- `clipboard_utils::SetClipboardText(GLFWwindow*, std::string_view)` – single string
- Used for: filename (hover+Ctrl+C), full path (hover+Ctrl+C on path column)

### Drag-Drop
- `drag_drop::StartFileDragDrop(std::string_view)` – **single file only**
- Doc notes: "FUTURE: Multi-file drag"

---

## 2. Target Behavior

### Multi-Select Semantics

| Action | Behavior |
|--------|----------|
| **Click** (no modifier) | Clear selection, select clicked row, set anchor = clicked |
| **Ctrl+Click** | Toggle row in selection; if added, anchor = clicked |
| **Shift+Click** | Select range from anchor to clicked (inclusive); replace selection |
| **Shift+Down** | Extend selection down by one row |
| **Shift+Up** | Extend selection up by one row |
| **Shift+PageDown** | Extend selection down by visible page |
| **Shift+PageUp** | Extend selection up by visible page |
| **Ctrl+A** | Select all rows (when table focused) |

### Bulk Copy (New)
- **Shortcut**: Ctrl+Shift+C (or Cmd+Shift+C on macOS) when table focused
- **Output**: All selected paths, newline-separated, to clipboard
- **Behavior**: If no selection, copy all displayed results (or do nothing – recommend: copy all if empty selection)

### Delete (Updated)
- **Behavior**: Delete all selected files; if none selected, no-op
- **Confirmation**: "Delete X file(s)?" with count; optionally list first few paths

### Drag-Drop (Future)
- **Phase 2**: Extend `StartFileDragDrop` to accept `std::vector<std::string_view>` for multi-file drag
- **Scope**: Out of initial plan; document as follow-up

---

## 3. Data Model Changes

### GuiState.h

```cpp
// Replace:
int selectedRow = -1;

// With:
std::set<int> selectedRows{};  // Row indices (into display_results)
int selectionAnchorRow = -1;    // For Shift+Click range; -1 when no selection
```

- **Backward compatibility**: `selectedRow` can be kept as a derived value for code that expects it: `selectedRow = selectedRows.empty() ? -1 : *selectedRows.begin()` (or last selected). Prefer migrating all consumers to `selectedRows` and removing `selectedRow`.

### Helper: Is Row Selected

```cpp
bool IsRowSelected(const GuiState& state, int row) {
  return state.selectedRows.count(row) != 0;
}
```

### Helper: Get Primary Selected Row (for legacy)

- Use `selectionAnchorRow` when non-empty; else first element of `selectedRows` when size==1; else -1 for "multiple" consumers that need to pick one.

---

## 4. Implementation Phases

### Phase 1: Data Model and Single-Select Parity (2–3 hours)

**Goal**: Replace `selectedRow` with `selectedRows` + `selectionAnchorRow`; preserve current behavior (single click = single selection).

**Tasks**:
1. Add `selectedRows` and `selectionAnchorRow` to GuiState.
2. Add `ClearSelection()`, `SelectRow(int)`, `IsRowSelected(int)` helpers.
3. On simple click (no Ctrl/Shift): `ClearSelection(); SelectRow(row); selectionAnchorRow = row`.
4. Update selection checks: `is_selected = IsRowSelected(state, row)`.
5. Update all consumers to use `selectedRows`:
   - Delete: use `selectedRows` (for now, only first selected if single; Phase 3 will delete all).
   - Context menu: use first selected.
   - Drag-drop: use first selected (single-file).
   - Clamp selection when results change.
6. Remove `selectedRow` once all consumers migrated.
7. Run tests; fix any regressions.

**Files**: `GuiState.h`, `ResultsTable.cpp`, `ApplicationLogic.cpp` (if any).

---

### Phase 2: Multi-Select Click Handling (2–3 hours)

**Goal**: Ctrl+Click and Shift+Click work as specified.

**Tasks**:
1. Add `HandleRowClick(GuiState& state, int row)` with modifier check:
   - `ImGui::GetIO().KeyCtrl` and `KeyShift`.
2. **Simple click**: `ClearSelection(); SelectRow(row); selectionAnchorRow = row`.
3. **Ctrl+Click**: Toggle `row` in `selectedRows`; if adding, `selectionAnchorRow = row`.
4. **Shift+Click**: Compute range `[min(anchor, row), max(anchor, row)]`, clear and fill `selectedRows` for that range; keep `selectionAnchorRow` unchanged.
5. **Anchor**: If `selectedRows` is empty after Ctrl+Click toggle, set `selectionAnchorRow = -1`.
6. Call `HandleRowClick` from all 5 Selectable click handlers (when `ImGui::Selectable` returns true).

**Edge cases**:
- Shift+Click with no anchor: treat as anchor = clicked (single selection).
- Shift+Click with anchor out of bounds: clamp range to valid rows.

**Files**: `ResultsTable.cpp`.

---

### Phase 3: Shift+Arrow and Ctrl+A (2–3 hours)

**Goal**: Keyboard selection extension.

**Tasks**:
1. Add `HandleTableKeyboardSelection()` in ResultsTable (or ApplicationLogic) when table is focused and `!WantTextInput`.
2. **Shift+Down**: `last = max(selectedRows)`; if `last < size-1`, add `last+1`; update anchor to `last+1`.
3. **Shift+Up**: `first = min(selectedRows)`; if `first > 0`, add `first-1`; update anchor to `first-1`.
4. **Shift+PageDown** / **Shift+PageUp**: Use `ImGui::GetFrameHeightWithSpacing()` to estimate visible rows; extend by that many.
5. **Ctrl+A**: `ClearSelection()`; fill `selectedRows` with 0..size-1; `selectionAnchorRow = 0`.
6. **Scroll-into-view**: When selection changes via keyboard, scroll so selected row is visible (optional).

**Files**: `ResultsTable.cpp`.

---

### Phase 4: Bulk Copy Paths (1–2 hours)

**Goal**: Ctrl+Shift+C copies all selected paths, newline-separated.

**Tasks**:
1. Add shortcut in `HandleKeyboardShortcuts` or in ResultsTable: `Ctrl+Shift+C` when table focused.
2. Implement `CopySelectedPathsToClipboard(state, display_results, glfw_window)`:
   - Build `std::string` from `display_results[i].fullPath` for each `i` in `selectedRows` (sorted by row index).
   - Join with `\n`.
   - Call `clipboard_utils::SetClipboardText(glfw_window, result)`.
3. **Empty selection**: Option A) Copy all displayed paths; Option B) No-op. **Recommend**: Option B (no-op) for clarity.
4. Add to Keyboard Shortcuts popup: "Ctrl+Shift+C - Copy selected paths (newline-separated)".

**Files**: `ResultsTable.cpp`, `ApplicationLogic.cpp` (if shortcut there), `Popups.cpp`.

---

### Phase 5: Delete All Selected (2–3 hours)

**Goal**: Delete key deletes all selected files with confirmation.

**Tasks**:
1. Change Delete key handling: when `!selectedRows.empty()`, open delete confirmation.
2. **Confirmation UI**: "Delete X file(s)?" with count; optionally list first 3–5 paths + "..." if more.
3. **On confirm**: Loop over `selectedRows`, call `DeleteFileToRecycleBin` for each; add to `pendingDeletions`; clear selection.
4. **On cancel**: Close popup, keep selection.
5. **State**: Replace `fileToDelete` (single) with `filesToDelete` (vector) or keep single and add `pendingDeletePaths` for the batch. Simpler: use `std::vector<std::string> filesToDelete` for the popup.

**Files**: `GuiState.h`, `ResultsTable.cpp`, `ShellContextUtils` (if context menu delete also needs update).

---

### Phase 6: Context Menu (Optional, 1–2 hours)

**Goal**: Windows context menu operates on all selected files.

**Tasks**:
1. Extend `ShowContextMenu` to accept `std::vector<std::string_view>` or `const std::vector<std::string>&`.
2. Build shell context menu for multiple files (Windows shell supports this).
3. When right-clicking: if any selected row is under cursor, use selected set; else select clicked row only and show menu.

**Files**: `ShellContextUtils.cpp`, `ShellContextUtils.h`, `ResultsTable.cpp`.

**Note**: Windows shell context menus can work with multiple files; the exact API varies.

---

### Phase 7: Multi-File Drag (Optional, Future)

**Goal**: Drag multiple selected files.

**Tasks**:
1. Extend `StartFileDragDrop` to accept `std::vector<std::string_view>`.
2. Build CF_HDROP with multiple paths.
3. When starting drag: if multiple selected, pass all; else single.

**Files**: `DragDropUtils.h`, `DragDropUtils.cpp`, `ResultsTable.cpp`.

**Reference**: DragDropUtils.h already notes "Multi-file drag" as future.

---

## 5. File Change Summary

| File | Change |
|------|--------|
| `GuiState.h` | `selectedRows`, `selectionAnchorRow`; remove `selectedRow`; add `filesToDelete` (vector) for batch delete |
| `ResultsTable.cpp` | Click handling with modifiers; keyboard selection; bulk copy; delete-all |
| `ApplicationLogic.cpp` | Add Ctrl+Shift+C, Ctrl+A (if not in ResultsTable) |
| `Popups.cpp` | Add bulk copy shortcut to list |
| `ShellContextUtils.cpp` (optional) | Multi-file context menu |
| `DragDropUtils.cpp` (optional) | Multi-file drag |

---

## 6. Testing Checklist

- [ ] Single click: select one row
- [ ] Ctrl+Click: toggle row in selection
- [ ] Shift+Click: select range
- [ ] Shift+Arrow: extend selection
- [ ] Ctrl+A: select all
- [ ] Bulk copy: Ctrl+Shift+C copies newline-separated paths
- [ ] Delete: deletes all selected with confirmation
- [ ] Selection cleared when results change (filter/sort)
- [ ] Selection clamped when display size shrinks
- [ ] No text input focus: shortcuts don't steal from input fields

---

## 7. Effort Estimate

| Phase | Effort | Notes |
|-------|--------|-------|
| Phase 1 | 2–3 h | Data model migration |
| Phase 2 | 2–3 h | Click modifiers |
| Phase 3 | 2–3 h | Keyboard selection |
| Phase 4 | 1–2 h | Bulk copy |
| Phase 5 | 2–3 h | Delete all |
| Phase 6 | 1–2 h | Context menu (optional) |
| Phase 7 | 2–4 h | Multi-file drag (optional) |

**Total (Phases 1–5)**: ~10–14 hours  
**With optional phases**: ~15–20 hours

---

## 8. Risks and Mitigations

| Risk | Mitigation |
|------|------------|
| ImGui Selectable doesn't expose modifier keys at click | Use `ImGui::GetIO().KeyCtrl` / `KeyShift` – they are set at click time |
| Performance with large `selectedRows` | `std::set<int>` is O(log n) per lookup; acceptable for thousands of rows |
| macOS/Windows modifier key differences | Use `IsPrimaryShortcutModifierDown` for Ctrl/Cmd; Shift is same |
| Context menu for multi-file | Windows shell supports it; may need different API path |

---

## 9. References

- `src/gui/GuiState.h` – selection state
- `src/ui/ResultsTable.cpp` – selection handling
- `src/utils/ClipboardUtils.h` – copy
- `src/platform/windows/DragDropUtils.h` – drag (single-file)
- `src/platform/windows/ShellContextUtils.cpp` – context menu
- `docs/review/2026-02-14-v2/2026-02-14_RESULTS_TABLE_FEATURE_OPPORTUNITIES_RESEARCH.md`
