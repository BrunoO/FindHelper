# SetNext Functions Audit - 2026-01-17

## Summary

After fixing the regression where `SetNextItemAllowOverlap()` was called only once before multiple buttons, this audit checks for similar issues with other ImGui "SetNext" functions across the codebase.

## Issue Fixed

**File:** `src/ui/FilterPanel.cpp`  
**Function:** `RenderApplicationControls()`  
**Problem:** `SetNextItemAllowOverlap()` was called once before two buttons, but it only affects the next item, so the second button (Metrics) didn't have overlap enabled.  
**Fix:** Call `SetNextItemAllowOverlap()` before each button that needs overlap.

## Audit Results

### ✅ SetNextItemAllowOverlap()

**Status:** Only used in `FilterPanel.cpp`, already fixed.

- `src/ui/FilterPanel.cpp:208` - Called before Settings button ✓
- `src/ui/FilterPanel.cpp:215` - Called before Metrics button ✓

### ✅ SetNextItemWidth()

**Status:** All usages are correct. Each call is immediately followed by a single widget.

1. **FilterPanel.cpp:490**
   ```cpp
   ImGui::SetNextItemWidth(260.0f);
   RenderSavedSearchCombo(...);  // Only affects this combo
   ```
   ✓ Correct - only affects the combo

2. **FilterPanel.cpp:495**
   ```cpp
   ImGui::SetNextItemWidth(160.0f);
   ImGui::Combo(...);  // Only affects this combo
   ```
   ✓ Correct - only affects this combo

3. **SearchInputs.cpp:149**
   ```cpp
   ImGui::SetNextItemWidth(final_width);
   ImGui::InputText(...);  // Only affects this input
   ```
   ✓ Correct - only affects the input field

4. **SearchInputs.cpp:214**
   ```cpp
   ImGui::SetNextItemWidth(gemini_input_width);
   ImGui::InputTextMultiline(...);  // Only affects this input
   ```
   ✓ Correct - only affects the multiline input

**Note:** In some cases, `SameLine()` is used after a widget with `SetNextItemWidth()`, but this is correct - the width only applies to the widget immediately following the `SetNextItemWidth()` call, not subsequent widgets on the same line.

### ✅ SetNextWindowPos() / SetNextWindowSize()

**Status:** All usages are correct. These functions are for windows/popups, where one call per window is the correct pattern.

**Files checked:**
- `SearchHelpWindow.cpp` - Used correctly for popup positioning
- `FolderBrowser.cpp` - Used correctly for window positioning
- `UIRenderer.cpp` - Used correctly for main window positioning
- `SettingsWindow.cpp` - Used correctly for window positioning
- `Popups.cpp` - Used correctly for popup positioning
- `ResultsTable.cpp` - Used correctly for popup positioning
- `MetricsWindow.cpp` - Used correctly for window positioning

**Pattern:** All follow the correct pattern of calling `SetNextWindowPos()` every frame before `BeginPopupModal()` (as documented in `AGENTS document`).

## Conclusion

✅ **No other issues found.** All "SetNext" function usages in the codebase are correct:

- `SetNextItemAllowOverlap()` - Fixed, now called before each button
- `SetNextItemWidth()` - All usages correct, each affects only the next widget
- `SetNextWindowPos()` / `SetNextWindowSize()` - All usages correct, one call per window

## Key Takeaway

ImGui's "SetNext" functions (like `SetNextItemAllowOverlap()`, `SetNextItemWidth()`, `SetNextWindowPos()`) only affect the **immediately following widget**. When you need the same setting for multiple widgets, you must call the "SetNext" function before each widget.

**Example:**
```cpp
// ❌ WRONG - Only first button gets overlap
ImGui::SetNextItemAllowOverlap();
ImGui::Button("Button 1");
ImGui::SameLine();
ImGui::Button("Button 2");  // No overlap!

// ✅ CORRECT - Both buttons get overlap
ImGui::SetNextItemAllowOverlap();
ImGui::Button("Button 1");
ImGui::SameLine();
ImGui::SetNextItemAllowOverlap();  // Must call again
ImGui::Button("Button 2");
```
