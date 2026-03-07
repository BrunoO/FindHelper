# 2026-02-19 – Plan: Help Window as Normal Window (like Metrics)

## Goal

Change the **Help** window (keyboard shortcuts) from a **modal popup** to a **normal floating window** like Metrics and Settings, so users can keep it visible while learning shortcuts.

## Current Behavior

- **Help button** (in search area): Opens a **modal popup** (“Keyboard Shortcuts”) via `openKeyboardShortcutsPopup` → `Popups::RenderKeyboardShortcutsPopup` → `ImGui::BeginPopupModal("KeyboardShortcutsPopup", ...)`.
- **Search Help** (separate toggle): Already a **normal window** via `showSearchHelpWindow` → `SearchHelpWindow::Render` (“Search Syntax Guide”). Unaffected by this change.

So “Help” in this plan means **Keyboard Shortcuts** only.

## Target Behavior

- **Help** opens a **normal window** (like Metrics):
  - Non-modal, can stay open while using the app.
  - Movable, resizable, close via title bar or explicit Close button.
  - **Only** way to open/toggle Help: the **application bar** toggle (next to Settings / Metrics). The existing Help button in the search area is **removed**.

## Implementation Plan

### 1. State (GuiState)

- **Add** `bool showHelpWindow = false` in `GuiState` (next to `showSearchHelpWindow`).
- **After migration:** Remove `openKeyboardShortcutsPopup` and all references (SearchInputs, Popups).

### 2. New component: HelpWindow

- **Add** `ui/HelpWindow.h` and `ui/HelpWindow.cpp` (same pattern as MetricsWindow / SearchHelpWindow).
- **API:** `static void Render(bool* p_open);`
- **Behavior:**
  - If `p_open == nullptr` or `*p_open == false`, return.
  - Use theme `WindowBg`, `SetNextWindowPos` / `SetNextWindowSize` with `ImGuiCond_FirstUseEver` (centered, sensible default size, e.g. 450×500).
  - `ImGui::Begin("Find Helper – Help", p_open, ImGuiWindowFlags_None)` (or “Keyboard Shortcuts” as title).
  - **Content:** Move the current keyboard-shortcuts content from `Popups::RenderKeyboardShortcutsPopup` (Global Shortcuts, Search & Navigation, File Operations, Mark Mode) into this window. No `CollapsingHeader` required unless we want sections; same text/bullets as today.
  - Optional: “Close” button (like SearchHelpWindow) for consistency; closing via title bar already updates `*p_open`.
  - `ImGui::End()` and pop style if used.

### 3. Application bar (FilterPanel)

- **Extend** `FilterPanel::RenderApplicationControls` to take `GuiState& state` (in addition to existing parameters).
- **Add** a “Help” button next to Settings / Metrics:
  - Label: “Help” when closed, “Hide Help” when open (or icon + “Help” / “Hide Help” like Metrics).
  - On click: `state.showHelpWindow = !state.showHelpWindow`.
- **Layout:** Include Help in the right-aligned group width and `SetNextItemAllowOverlap` so the Help button behaves like Settings/Metrics.

**Call sites:** `UIRenderer::RenderMainWindow` already has `context.state`; pass `context.state` into `RenderApplicationControls`.

### 4. Remove Help button from search area (SearchInputs)

- **Remove** the existing “Help” button from the search area (the one that currently sets `openKeyboardShortcutsPopup = true`). Help is only toggled from the application bar.

### 5. Floating windows (UIRenderer)

- In `UIRenderer::RenderFloatingWindows`, after SearchHelpWindow:
  - If `state.showHelpWindow`, call `HelpWindow::Render(&state.showHelpWindow)`.
- Ensure `HelpWindow.h` is included and (if needed) CMakeLists.txt includes the new source.

### 6. Remove popup path and Help button references (Popups, GuiState)

- **Remove** the call to `Popups::RenderKeyboardShortcutsPopup(state)` from `UIRenderer::RenderMainWindow`.
- **Remove** `RenderKeyboardShortcutsPopup` from `Popups.cpp` and its declaration from `Popups.h`.
- **Remove** `openKeyboardShortcutsPopup` from GuiState (no longer needed; Help is only toggled from the application bar).

### 7. Naming and docs

- Window title: e.g. “Find Helper – Help” or “Keyboard Shortcuts” (decide in implementation).
- Comment in GuiState: “Window visibility flags (… Help, Search Help)”.
- FilterPanel / UIRenderer comments: mention Help alongside Settings and Metrics where relevant.

## Files to Touch (summary)

| File | Action |
|------|--------|
| `src/gui/GuiState.h` | Add `showHelpWindow`, remove `openKeyboardShortcutsPopup` after migration |
| `src/ui/HelpWindow.h` | New: Help window class |
| `src/ui/HelpWindow.cpp` | New: Render implementation (content from popup) |
| `src/ui/FilterPanel.h` / `.cpp` | Add GuiState&, add Help button in RenderApplicationControls |
| `src/ui/SearchInputs.cpp` | Remove the Help button from the search area |
| `src/ui/UIRenderer.cpp` | Call HelpWindow::Render when showHelpWindow; remove RenderKeyboardShortcutsPopup call; include HelpWindow.h |
| `src/ui/Popups.cpp` / `Popups.h` | Remove RenderKeyboardShortcutsPopup |
| `CMakeLists.txt` | Add HelpWindow.cpp to app target if needed |

## Testing / Checklist

- Help opens only from the application bar (Help / Hide Help).
- Help is a normal window (non-modal); can be toggled from the application bar.
- Window can be moved, resized, closed via title bar; state reflects close (Hide Help when open, Help when closed).
- Content matches current Keyboard Shortcuts popup (all sections and shortcuts).
- Search Help (Search Syntax Guide) and Settings/Metrics unchanged.
- No remaining references to `openKeyboardShortcutsPopup` or `RenderKeyboardShortcutsPopup`.

## Risk / Notes

- **Low risk:** Same pattern as Metrics and SearchHelpWindow; only adding a new window and one new state flag, and removing the modal popup path.
- **UX:** Users who relied on the modal may need one click to close the window; keeping “Hide Help” in the bar makes toggling explicit and consistent with Metrics/Settings.
