# Specification: Search Inputs Layout Reorganization & Alignment Guide

**Feature Name:** Search Inputs Layout Reorganization (Trigram-First UI)  
**Status:** Approved Specification  
**Target Version:** 2026-Q4  
**Date:** 2026-09-30  
**Target Platforms:** macOS, Windows, Linux  
**Relevant Components:** `src/ui/SearchInputs.*`, `src/ui/ResultsTable.*`, `src/ui/UIRenderer.*`, `src/gui/GuiState.h`  

---

## 1. Executive Summary & Motivation

With the introduction of the **Inverted Filename Trigram Index** (< 0.2 ms latency), filename searching becomes the fastest, most effective, and highest-frequency search path in FindHelper (>90% of user queries).

The legacy UI layout was designed around path search as the primary hero bar (Line 1), relegating the `Name` input to Line 3 and completely hiding it in `Minimalistic` and `Simplified` modes. Furthermore, past revisions resulted in historical compromises (such as placing `[Export CSV]` into the search criteria inputs merely to fill vacant space left by a moved `Help` button).

This specification defines the reorganization to **Option 1 ("Compact Power Duo")**:
1. **Line 1 (The Power Duo):** `Name` + `Ext` + `[Search now]`.
2. **Line 2 (Scope Filter):** `Path` + `[Clear All]`.
3. **Line 3 (Options & Filter Drawer):** Search Options + `[Show / Hide Quick Filters]`.
4. **Results Table Toolbar:** Relocate `[Export CSV]` down to the Results Table header bar.
5. **Fixed Coordinate Alignment:** Enforce 3 invariant anchors to permanently eliminate multi-row alignment drift and wrapping bugs.

---

## 2. Layout Architecture (Before vs. After)

### 2.1 Legacy Layout (Current)
```
Line 1: Path Search:  [ ? ][ R ] [                     path input                     ] [ Search now ]
Line 2: Extensions:              [                extensions input                    ] [ Clear All  ]
Line 3: Name:         [ ? ][ R ] [                  filename input                    ] [ Export CSV ]
Line 4: Options:      [ All items ▾ ]  [x] Case sensitive  [x] Auto-refresh ...         [ Show Quick ]
```

### 2.2 Reorganized Layout (Target)
```
┌──────────────────────────────────────────────────────────────────────────────────────────────────┐
│ Line 1: Name: [ ? ][ R ] [          my_report*          ]  Ext: [ pdf;docx ]   [ Search now    ] │
│ Line 2: Path: [ ? ][ R ] [          C:\Projects\**      ]                      [ Clear All     ] │
│ Line 3: Options: [ All items ▾ ]  [x] Case sensitive  [x] Auto-refresh         [ Quick Filters ] │
└──────────────────────────────────────────────────────────────────────────────────────────────────┘
  ▼ (Quick Filters drawer expands directly below Line 3)
┌──────────────────────────────────────────────────────────────────────────────────────────────────┐
│ Quick Filters: [ Size: Small (100KB-1MB) ] [ Date: This Month ] ...                              │
└──────────────────────────────────────────────────────────────────────────────────────────────────┘
...
┌──────────────────────────────────────────────────────────────────────────────────────────────────┐
│ Results: 42 items found (1.2 MB)                                                [📥 Export CSV ] │
│ ┌───────────────┬────────────┬─────────────────────┬───────────────────────────────────────────┐ │
│ │ Filename      │ Size       │ Modified            │ Path                                      │ │
```

---

## 3. The 3 Invariant Alignment Anchors

Past commit analysis (`commit f122c652`, `commit 9df39322`, lines 520–537 in `SearchInputs.cpp`) identified that row-by-row dynamic cursor sampling (`ImGui::GetCursorPosX()`) caused alignment drift and wrapping.

To guarantee pixel-perfect alignment across all themes, DPI scaling, and screen widths, layout math must be computed **once per frame** at the top of `SearchInputs::Render` using 3 invariant anchors:

```
Anchor 1: Left Edge of Inputs (kInputStartX)
 │
 │                                     Anchor 2: End of Inputs (kInputsRightEdge)
 │                                      │
 │                                      │         Anchor 3: Right Button Column (kButtonX)
 │                                      │          │
 ▼                                      ▼          ▼
┌───────────────────────────────────────┬──────────┬────────────────────────┐
│ Name: [?][R] [   Name input box     ] │Ext:[txt] │ [ Search now         ] │ Line 1
├───────────────────────────────────────┴──────────┼────────────────────────┤
│ Path: [?][R] [       Path input box            ] │ [ Clear All          ] │ Line 2
├──────────────────────────────────────────────────┼────────────────────────┤
│ Options: [ All items ▾ ] [x] Case sensitive ...  │ [ Show Quick Filters ] │ Line 3
└──────────────────────────────────────────────────┴────────────────────────┘
```

### 3.1 Anchor Coordinate Formulas

```cpp
// 1. Right Button Column (Anchor 3)
const float uniform_button_width = CalculateMaxButtonWidth(state);
const float window_right_edge = ImGui::GetWindowContentRegionMax().x;
const float kButtonX = window_right_edge - uniform_button_width;
constexpr float kItemSpacingX = 8.0F;

// 2. Left Input Start (Anchor 1)
// Compute maximum label width between "Name:" and "Path:" plus helper buttons
const float name_label_width = ImGui::CalcTextSize("Name:").x;
const float path_label_width = ImGui::CalcTextSize("Path:").x;
const float helper_buttons_width = (ImGui::GetFrameHeight() * 2.0F) + (kItemSpacingX * 2.0F); // [?][R]
const float kInputStartX = ImGui::GetCursorPosX() + 
                           (std::max)(name_label_width, path_label_width) + 
                           helper_buttons_width + kItemSpacingX;

// 3. Inputs Span & Dual-Input Split on Line 1 (Anchor 2)
const float kTotalInputSpan = kButtonX - kInputStartX - kItemSpacingX;

// Ext box bounded width (comfortable for 4-5 extensions, e.g. "txt;pdf;docx;cpp")
const float ext_label_width = ImGui::CalcTextSize("Ext:").x + kItemSpacingX;
const float kExtInputWidth = (std::clamp)(kTotalInputSpan * 0.22F, 140.0F, 180.0F);
const float kExtGroupWidth = ext_label_width + kExtInputWidth;

// Name box takes remaining flex space
const float kNameInputWidth = kTotalInputSpan - kExtGroupWidth - kItemSpacingX;

// Path box on Line 2 spans full input span
const float kPathInputWidth = kTotalInputSpan;
```

### 3.2 Invariant Guarantee
Because:
$$\text{NameInputWidth} + \text{ItemSpacingX} + \text{ExtGroupWidth} \equiv \text{TotalInputSpan} \equiv \text{PathInputWidth}$$
The right edge of the `Ext` box on Line 1 and the right edge of the `Path` box on Line 2 are **mathematically identical down to the sub-pixel**.

---

## 4. Past Struggles & Lessons Learned (Git History Audit)

| Past Bug / Issue | Root Cause | Mandated Guardrail / Rule |
| :--- | :--- | :--- |
| **Ragged Button Column** (`commit f122c652`) | Different button label lengths caused ragged right edges. | `CalculateMaxButtonWidth(state)` scans all buttons; all 3 buttons must use `ImVec2(uniform_button_width, 0)`. |
| **Multi-Input Overflow** (`commit f122c652`) | Percentages without min/max bounds caused controls to push buttons off-screen on `< 800px` screens. | `kExtInputWidth` clamped to `[140px, 180px]`; `NameInputWidth` floored at `250px`. |
| **Export CSV "Vacant Space" Trap** (`commit 36551c25`) | `Export CSV` was placed next to Name solely to fill space left by an old Help button. | Move `[Export CSV]` to the Results Table header bar where result actions belong. |
| **Shortcut Collision on Enter** (`commit 1b67ea93`) | `Cmd+Enter` triggered both search and "Reveal in Explorer". | Plain `Enter` only triggers search; `Cmd+Enter` / `Ctrl+Enter` is strictly reserved for the Results Table. |
| **Popup ID Collisions** (`commit cd5af957`) | `?` and `[R]` buttons conflicted across inputs. | Wrap buttons in `ImGui::PushID(id)` and defer opening to specific boolean flags (`open_regex_generator_popup_filename` vs `open_regex_generator_popup`). |
| **Cursor Position Sampling Drift** (`SearchInputs.cpp:520`) | Calling `GetCursorPosX()` after variable-width labels compounded discrepancies. | Use explicit anchor coordinates (`kInputStartX`, `kButtonX`) computed up-front. |

---

## 5. UI Mode Evolution (`Minimalistic`, `Simplified`, `Full`)

With the trigram index, filename search is the primary search. The UI modes in `AppSettings::UIMode` must update as follows:

```
┌────────────────────────────────────────────────────────────────────────┐
│ MINIMALISTIC MODE                                                      │
│ Name: [ ? ][ R ] [          filename search         ]  Ext: [  txt  ]  │
└────────────────────────────────────────────────────────────────────────┘

┌────────────────────────────────────────────────────────────────────────┐
│ SIMPLIFIED MODE                                                        │
│ Line 1: Name: [ ? ][ R ] [          filename search         ] Ext: [...]│
│ Line 2: Path: [ ? ][ R ] [          folder scope            ] [Clear]  │
└────────────────────────────────────────────────────────────────────────┘

┌────────────────────────────────────────────────────────────────────────┐
│ FULL MODE                                                              │
│ Line 1: Name: [ ? ][ R ] [ ... ] Ext: [ ... ] [ Search now ]           │
│ Line 2: Path: [ ? ][ R ] [ ... ]              [ Clear All  ]           │
│ Line 3: Search Options: [ All ▾ ] [x] Case    [ Show Quick Filters ]   │
│ Drawer: Quick Filters (Size / Date / Ext badges)                       │
└────────────────────────────────────────────────────────────────────────┘
```

* **Minimalistic Mode Fix:** In `UIRenderer.cpp:307`, replace the legacy path-only render call with Line 1 (`Name` + `Ext`). Users in minimal mode now get sub-millisecond search immediately.
* **Simplified Mode:** Renders Line 1 and Line 2; hides Line 3 options.
* **Full Mode:** Renders all 3 lines plus the collapsible Quick Filters drawer.

---

## 6. Export CSV Relocation & Results Toolbar

### 6.1 Placement in Results Header
In `src/ui/ResultsTable.cpp` (or directly before `BeginTable("SearchResults")`):
```cpp
// Results toolbar above table
ImGui::TextColored(Theme::Colors::TextDim, "%zu items found", display_results_count);
if (total_size_string != nullptr) {
  ImGui::SameLine();
  ImGui::TextColored(Theme::Colors::TextDim, "(%s)", total_size_string);
}

// Right-aligned Export CSV button
ImGui::SameLine();
ImGui::SetCursorPosX(window_right_edge - export_button_width);

const bool can_export = display_results_count > 0;
if (!can_export) {
  ImGui::BeginDisabled();
}
if (ImGui::Button(ICON_FA_FILE_EXPORT " Export CSV", ImVec2(export_button_width, 0))) {
  actions->ExportToCsv(state);
}
if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
  ImGui::SetTooltip("Export currently displayed results to CSV file (Ctrl+E / Cmd+E)");
}
if (!can_export) {
  ImGui::EndDisabled();
}
```

### 6.2 Shortcuts & Feedback
* `Ctrl+E` / `Cmd+E` continues to trigger CSV export globally when results are present.
* The modal export feedback popup (`Popups::RenderExportResultPopup`) remains unchanged.

---

## 7. Keyboard Navigation & Tab Traversal

ImGui processes tab traversal in order of item emission:
1. **Tab 1:** `Name` (`##filename`) — Primary focus on app launch and on `Ctrl+F` / `Cmd+F`.
2. **Tab 2:** `Extensions` (`##extensions`) — Secondary extension filter.
3. **Tab 3:** `Path` (`##path`) — Tertiary path/scope filter.
4. **Tab 4:** Action buttons & Results Table.

---

## 8. Step-by-Step Implementation Plan

### Step 1: Export CSV Relocation
- [ ] Move `Export CSV` button rendering from `SearchInputs::RenderFilenameInput` to the Results Table header bar in `src/ui/ResultsTable.cpp`.
- [ ] Update `CalculateMaxButtonWidth` in `SearchInputs.cpp` to remove `export_csv_width`.

### Step 2: Implement 3 Invariant Anchors in `SearchInputs.cpp`
- [ ] Compute `kInputStartX`, `kButtonX`, `kTotalInputSpan`, `kExtGroupWidth`, and `kNameInputWidth` at the start of `SearchInputs::Render`.
- [ ] Refactor `RenderFilenameInput` and `RenderExtensionsInput` to accept explicit start positions and widths.

### Step 3: Reorder Search Inputs Rows
- [ ] Line 1: Emit `Name` label/buttons, `Name` input box, `Ext` label, `Ext` input box, and right-aligned `[Search now]` button.
- [ ] Line 2: Emit `Path` label/buttons, `Path` input box, and right-aligned `[Clear All]` button.
- [ ] Line 3: Emit `Search Options:` label, `ItemType` combo, checkboxes, and right-aligned `[Show / Hide Quick Filters]` button.

### Step 4: Update UI Modes in `UIRenderer.cpp`
- [ ] In `UIRenderer.cpp`, update `Minimalistic` and `Simplified` mode branches to render Line 1 (`Name` + `Ext`).

### Step 5: Verification & Quality Gates
- [ ] Build with `./scripts/build_tests_macos.sh --no-asan`.
- [ ] Run ImGui test engine tests (`./scripts/build_tests_macos.sh --run-imgui-tests`).
- [ ] Run `scripts/pre-commit-clang-tidy.sh` (0 warnings).
- [ ] Visual verification across light/dark themes and varying window widths (800px to 2560px).
