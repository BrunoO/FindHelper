2026-03-16_Multi-select and Multi-drag Test Matrix
==================================================

## 1. Scope

This document complements `2026-03-06_MultiSelect_MultiDrag_Spec.md` with a concrete test matrix and recommendations for:

- **Unit tests** (pure C++ logic where feasible).
- **ImGui Test Engine tests** (UI, keyboard, selection, and drag behaviors).
- **Manual tests** (cross-platform shell integration, UX verification).

The focus is the results table multi-selection model and selection-aware drag-and-drop (Windows multi-drag; optional macOS).

---

## 2. Test Matrix

### 2.1 Interactions vs. Selection State

| ID | Interaction | Initial selection state | Expected behavior | Suggested test type |
|----|-------------|-------------------------|-------------------|---------------------|
| S1 | Single Click | None | Clicked row becomes the only selected row; anchor set to clicked row. | ImGui test |
| S2 | Single Click | Some other row selected | Selection cleared; clicked row becomes only selected; anchor updated. | ImGui test |
| S3 | Ctrl+Click (add) | Some rows selected (excluding target) | Target row toggled on; others unchanged; anchor set to target row. | ImGui test |
| S4 | Ctrl+Click (remove) | Target row selected (possibly among others) | Target row removed from selection; anchor set to `-1` if selection becomes empty. | ImGui test |
| S5 | Shift+Click (existing anchor) | Anchor row valid | All rows in `[min(anchor, clicked), max(anchor, clicked)]` selected; others cleared; anchor unchanged. | ImGui test |
| S6 | Shift+Click (no anchor) | Anchor = -1 | Behaves like simple click: single selection on clicked row, anchor = clicked row. | ImGui test |
| S7 | Ctrl+A | Any | All rows selected; anchor set to row 0 (or defined primary); selection container contains all row indices. | ImGui + unit test |

### 2.2 Keyboard Range and Navigation

| ID | Interaction | Precondition | Expected behavior | Suggested test type |
|----|-------------|-------------|-------------------|---------------------|
| K1 | Shift+Down | Anchor and at least one selected row valid | Extends selection one row down from current edge; anchor semantics preserved; new edge row visible. | ImGui test |
| K2 | Shift+Up | Anchor and at least one selected row valid | Extends selection one row up; clamped at top; new edge row visible. | ImGui test |
| K3 | Shift+PageDown | Anchor valid | Extends selection roughly one page down (based on visible rows); edge row visible. | ImGui test |
| K4 | Shift+PageUp | Anchor valid | Extends selection roughly one page up; clamped at first row. | ImGui test |
| K5 | Arrow keys without Shift | Some selection | Moves primary selection up/down (single-row movement); clears other rows if model requires single active row for navigation while preserving multi-selection invariants as specified. | ImGui test |
| K6 | Shortcuts with text input active | Any | When ImGui `WantTextInput` is true, table shortcuts (including selection changes) do not fire. | ImGui test |

### 2.3 Delete and Context Menu

| ID | Interaction | Selection | Expected behavior | Suggested test type |
|----|-------------|-----------|-------------------|---------------------|
| D1 | Delete with 0 selected rows | None | No popup opened; no items deleted. | ImGui test |
| D2 | Delete with 1 selected row | 1 row | Confirmation popup shows singular wording; on confirm, item removed; selection cleared or moved per spec; logs success. | ImGui + manual |
| D3 | Delete with N selected rows | N > 1 | Confirmation popup shows plural wording including N; on confirm, each item attempted independently; successes removed; failures remain selected; aggregate message if partial failure. | ImGui + manual |
| D4 | Right-click on selected row | Some rows selected including target | Context menu operations apply to all selected rows; selection unchanged. | ImGui test |
| D5 | Right-click on unselected row | Some rows selected excluding target | Selection cleared; target row becomes sole selection; context menu applies to that row only. | ImGui test |

### 2.4 Drag-and-Drop (Windows; optional macOS)

| ID | Interaction | Selection / drag origin | Expected behavior | Suggested test type |
|----|-------------|-------------------------|-------------------|---------------------|
| W1 | Single-file drag | Exactly 1 row selected; drag originates from that row | Behavior identical to today: single file in payload; same cursors and keyboard semantics. | Manual (Windows) |
| W2 | Multi-drag from selected row | Multiple eligible rows selected; drag from one of the selected rows | `CF_HDROP` (Windows) or `NSDraggingSession` (macOS) contains all eligible selected files; no directories; drop in Explorer/Desktop/Outlook includes exactly those files. | Manual (Windows/macOS) |
| W3 | Drag from unselected row | Multiple rows selected; drag from unselected row | Selection cleared; dragged row becomes sole selection; single-file drag initiated. | ImGui + manual |
| W4 | Multi-drag with invalid candidates | Selection includes missing files, directories, or pending-deletion entries | Drag starts with only valid file paths; invalid ones skipped; each invalid logged; optional short status indicating some items not dragged. | Unit (path filtering) + manual |
| W5 | Cancelling drag | Any | Pressing Esc or dropping on non-accepting target cancels; selection unchanged; logging matches existing semantics. | Manual |

### 2.5 Selection Stability Under Changes

| ID | Change | Initial selection | Expected behavior | Suggested test type |
|----|--------|-------------------|-------------------|---------------------|
| C1 | Streaming / incremental results | Some rows selected; new results appended | Selection remains on the same logical items; any indices out of bounds dropped; invariants hold. | Unit (remap helper) + ImGui |
| C2 | Sort by column | Several items selected by path/ID | After re-sort, the same logical items are selected at new row positions (entity-based selection). | Unit (remap helper) + ImGui |
| C3 | Filter change that removes items | Some selected items filtered out | Removed items disappear from view and selection; remaining selected items still selected; anchor updated or cleared as specified. | Unit + ImGui |

---

## 3. Unit Test Recommendations

Where possible, isolate non-ImGui logic into pure functions and cover with unit tests:

- **Selection helpers**
  - `SelectRow`, `ToggleRow`, `ClearSelection`, `IsRowSelected`, `GetPrimarySelectedRow`.
  - `ClampSelectionToRowCount`, `RemapSelectionAfterDisplayResultsChange`.
  - Test typical and edge cases:
    - Empty selection; single item; multiple items.
    - Removing last item; anchor in/out of range.
    - Remapping across sort and filtering (based on full path or stable ID).

- **Multi-drag candidate construction**
  - A small helper that, given `display_results`, `selectedRows`, and `pendingDeletions`, returns the vector of eligible paths.
  - Unit-test:
    - All files valid.
    - Mixed valid/invalid (missing, directories, pending deletion).
    - All invalid → empty result (no drag should start).

- **Delete result aggregation**
  - Helper that, given a list of selection indices and a simulated delete outcome (success/failure per path), computes:
    - New `display_results`.
    - Updated selection.
    - Aggregate message (“Deleted X of N items …”).

---

## 4. ImGui Test Engine Recommendations

ImGui Test Engine is the primary tool for verifying keyboard and mouse interactions in a deterministic way:

- **Selection semantics**
  - Dedicated tests for:
    - Click, Ctrl+Click, Shift+Click (S1–S6).
    - Ctrl+A (S7).
    - Shift+Arrow and Shift+PageUp/PageDown (K1–K4).
  - Each test should:
    - Set up a deterministic `display_results` fixture.
    - Drive interactions using the regression hook.
    - Assert both:
      - Internal state (`selectedRows`, `selectionAnchorRow`).
      - Visual/ImGui state (rows flagged as selected).

- **Keyboard focus rules**
  - Tests that verify:
    - When a text input in another window is active, table shortcuts do not fire (K6).
    - When the results table window is focused and `WantTextInput == false`, shortcuts do fire.

- **Delete and context menu**
  - Tests for D1–D5:
    - Open delete popup via Delete.
    - Verify wording (singular/plural) and item count.
    - Confirm and assert resulting selection and `display_results` state (using a fake delete backend if necessary).

- **Drag initiation logic (ImGui-level)**
  - While external shell drag is best covered manually, the *decision path* inside `ResultsTable.cpp` can be tested:
    - Drag from selected vs unselected row (W2/W3).
    - Selections with mixed valid/invalid candidates (ensuring invalid indices/rows are filtered before calling `drag_drop::StartFileDragDrop`).

---

## 5. Manual Test Recommendations

Certain behaviors require interaction with the OS shell and external applications:

- **Windows**
  - Verify:
    - Single-file drag still works to Explorer, Desktop, and at least one app (e.g. Outlook).
    - Multi-drag drops the correct set of files (no extras, no omissions) across:
      - Explorer.
      - Desktop.
      - At least one third-party app that accepts file drops.
    - Cancelling drag (Esc, non-accepting target) leaves selection unchanged and logs appropriately.
    - Mixed selections with invalid entries:
      - Directories or missing files are skipped.
      - Valid files still drag correctly.
      - Skipped items logged and optional status phrasing is correct.

- **macOS (if Phase 3 implemented)**
  - Mirror Windows tests:
    - Single vs multi-file NSDraggingSession behavior.
    - Drops into Finder, Desktop, Mail/other apps.
    - Handling of invalid entries as above.

- **Cross-platform selection UX**
  - On each platform, manually sanity-check:
    - Selection and range semantics (click / Ctrl/Cmd+Click / Shift+Click).
    - Keyboard selection shortcuts.
    - Delete behaviors and wording (singular/plural, partial failures).
    - Stability of selection across streaming updates and sorts in real datasets.

---

## 6. Regression Checklist Before Release

Before shipping multi-selection and multi-drag:

- All unit tests passing, including new helpers for selection, remapping, delete aggregation, and drag candidate filtering.
- ImGui Test Engine suite extended to cover:
  - Core selection semantics (click/cmd/shift, Ctrl+A).
  - Keyboard range selection and focus behavior.
  - Delete and context menu interactions for 0/1/N selections.
- Manual OS-level drag-and-drop tests completed on:
  - Windows (required).
  - macOS (if multi-drag implemented in this release).
- Documentation updated (Help, release notes, “What’s New”) to:
  - Describe multi-selection gestures and shortcuts.
  - Describe platform support and limitations for multi-drag (Windows/macOS only; Linux out of scope).

