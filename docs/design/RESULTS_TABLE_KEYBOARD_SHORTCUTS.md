# Results Table Keyboard Shortcuts – Implementation

**Date:** 2026-02-18  
**Purpose:** Document the correct behavior and implementation of results table keyboard shortcuts (mark/unmark, navigation, copy, bulk delete).

---

## Overview

Shortcuts are handled in `HandleResultsTableKeyboardShortcuts()` in `src/ui/ResultsTable.cpp`. The table must have focus and must not be in text-input mode (`WantTextInput` false). The **shift** modifier is read at the call site in `ResultsTable::Render()` via `io.KeyShift` and passed in so behavior is consistent for the frame.

---

## Key principles

### 1. One physical press = one action (no key repeat)

All shortcuts are implemented so that **one physical key hold triggers at most one action**:

- For most shortcuts we use **`ImGui::IsKeyPressed(key, false)`** via the `KeyPressedOnce(key)` helper so repeat is ignored.
- For **M/T/U**, we use **`HandleDebouncedMarkKey`**: trigger only on **`ImGui::IsKeyPressed(key, false)`** (key-down transition), then a **50ms cooldown after release** before the next press is accepted. This gives one action per physical press and absorbs OS key-repeat Down events (especially on macOS). **Do not** trigger M/T/U from `ImGui::IsKeyDown(key)` alone—that can fire every frame and cause “one press marks many rows” regressions.

### 2. Shift takes priority for global actions (M, T, U)

For **M**, **T**, and **U**:

- **If Shift is held:** always perform the **global** action (mark all / invert all / unmark all), regardless of whether a row is selected.
- **If Shift is not held and a row is selected:** perform the **row** action (mark one, toggle one, unmark one) and move to the next row.

This gives both behaviors: **m** = mark current row and move; **Shift+M** = mark all visible rows (and similarly for T and U).

### 3. Row actions require a valid selection

Row-specific actions (mark one, toggle one, unmark one) run only when `state.selectedRow` is in range for `display_results`. Navigation (arrows, N, P) can create or move the selection.

---

## Shortcut reference

| Key           | With Shift        | Without Shift (with selection)     |
|---------------|-------------------|------------------------------------|
| **M**         | Mark all visible  | Mark current row and move down     |
| **T**         | Invert all marks  | Toggle current row and move down   |
| **U**         | Unmark all        | Unmark current row and move down   |
| **W**         | Copy filenames    | Copy full paths                   |
| **D**         | Bulk delete popup | —                                 |
| **F**         | Toggle folder stats| —                                |
| **Down / N**  | —                 | Move selection down (or select first) |
| **Up / P**    | —                 | Move selection up                 |

Copy (W), bulk delete (Shift+D), and folder stats (Shift+F) are not row-scoped; they use marked state or table options.

---

## Regression prevention (shortcut handling)

To avoid “one press triggers many actions” regressions:

1. **Never trigger an action from `ImGui::IsKeyDown(key)` alone.** Use `ImGui::IsKeyPressed(key, false)` (or `KeyPressedOnce(key)`) for one-shot shortcuts, or `HandleDebouncedMarkKey` for M/T/U.
2. **One-shot shortcuts:** Use the in-handler `KeyPressedOnce(key)` lambda only; it maps to `IsKeyPressed(key, false)`.
3. **M/T/U only:** Use `HandleDebouncedMarkKey(key, key_state, time_now, action)`. Do not replace it with a plain `KeyPressedOnce(ImGuiKey_M)` without the 50ms cooldown unless the cooldown is intentionally removed and documented.
4. **Backspace in incremental search** is the only exception: it uses `IsKeyPressed(ImGuiKey_Backspace, true)` so repeat is allowed while typing.

The implementation in `ResultsTableKeyboard.cpp` includes an in-file comment block “Shortcut implementation rules (regression prevention)” that repeats these points; keep it in sync when changing behavior.

---

## Implementation details

- **Focus and text input:** Handler returns early if the results window is not focused or if `io.WantTextInput` is true (e.g. typing in search box).
- **Shift:** Passed from `ResultsTable::Render()` as `const bool shift = io.KeyShift` so the same value is used for the whole frame.
- **Selection:** `has_selection = (state.selectedRow >= 0 && state.selectedRow < (int)display_results.size())`.
- **Directories:** For row actions, if the selected row is a directory, `MarkAllInFolder()` is used to mark/unmark/toggle that folder’s contents instead of a single file.

---

## NOSONAR and NOLINT in this handler

- **NOSONAR(cpp:S3776):** Function cognitive complexity; shortcut handling is kept in one place.
- **NOSONAR(cpp:S6004):** `want_text_input` and `kResultColumnCount` kept as separate variables (not init-statements) to avoid regressions in shortcut behavior.
- **NOSONAR(cpp:S1066):** UpArrow/P kept as nested `if` for same reason.
- **NOLINT(readability-function-cognitive-complexity):** Same as S3776.

See `AGENTS.md` and in-code comments for rationale.

---

## References (ImGui keyboard best practices)

- **One press, no repeat:** Use `IsKeyPressed(key, false)` so one physical key hold triggers at most one action; the default `repeat = true` uses `io.KeyRepeatDelay` / `io.KeyRepeatRate` and can fire every frame or twice in a row (see e.g. ocornut/imgui#6590).
- **Shortcuts with modifiers:** For chords (e.g. Ctrl+S), ImGui recommends `Shortcut(ImGuiKeyChord)` or `IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)`; check modifier state and key press together. We read `io.KeyShift` at call site and pass `shift` into the handler for consistency.
- **Focus and capture:** Use `IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)` (or `ChildWindows`) so shortcuts only run when the right window has focus. If your code needs to claim keyboard input, call `SetNextFrameWantCaptureKeyboard(true)` when the window is focused, not inside the key handler (ocornut/imgui#7490).
- **WantCaptureKeyboard:** When `io.WantCaptureKeyboard` is true (e.g. text input active), do not run app-level shortcuts; we return early when `io.WantTextInput` is true.
- **Internal summary:** `internal-docs/analysis/2026-03-15_IMGUI_KEYBOARD_SHORTCUTS_BEST_PRACTICES.md`.
