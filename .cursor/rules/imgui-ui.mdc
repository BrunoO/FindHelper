---
description: ImGui immediate mode and popup management (UI/popup code)
globs: "**/ui/**/*.cpp,**/ui/**/*.h"
alwaysApply: false
paths:
  - "**/ui/**/*.cpp"
  - "**/ui/**/*.h"
---

# ImGui immediate mode and popups

When editing UI or popup code under `src/ui/`:

## Immediate mode

- **No widget storage** — widgets don’t exist as objects; call ImGui each frame.
- **State from data** — UI reflects application state; change data, not “widgets.”
- **All ImGui on main thread** — never call ImGui from background threads.

## Popups — critical

1. **Window context:** `OpenPopup()` and `BeginPopupModal()` must run in the **same** window context. From a child window, set a flag and call `OpenPopup()` at parent level.
2. **SetNextWindowPos:** Call **every frame** before `BeginPopupModal()`, not only when opening.
3. **CollapsingHeader:** Close button **outside** the header; use `SetItemDefaultFocus()` on the close button.
4. **CloseCurrentPopup:** Call only **inside** the `BeginPopupModal()` block (e.g. in button handler).
5. **IDs:** Same string for `OpenPopup("Id")` and `BeginPopupModal("Id")` (use a constant).

Reference: `ResultsTable.cpp::HandleDeleteKeyAndPopup()`, `Popups.cpp::RenderKeyboardShortcutsPopup()`. Full checklist and examples: AGENTS.md § ImGui Immediate Mode Paradigm and § ImGui Popup Management Rules; `docs/design/IMGUI_IMMEDIATE_MODE_PARADIGM.md`.
