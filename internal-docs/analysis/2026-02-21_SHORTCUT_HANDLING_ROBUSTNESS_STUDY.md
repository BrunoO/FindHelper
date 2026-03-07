# Study: Shortcut Handling Robustness

**Date:** 2026-02-21  
**Purpose:** Identify and fix inconsistencies in keyboard shortcut handling so behavior is correct on all platforms (Windows: Ctrl; macOS: Cmd; Linux: Ctrl) and to avoid duplicate logic and subtle bugs.

---

## 1. Current pattern: primary shortcut modifier

The project uses **Ctrl** on Windows/Linux and **Cmd (Super)** on macOS for “primary” shortcuts (e.g. Ctrl+F, Ctrl+S). This is centralized in:

- **`gui/ImGuiUtils.h`**: `IsPrimaryShortcutModifierDown(const ImGuiIO& io)` → `io.KeyCtrl || io.KeySuper`
- **`IsCopyShortcutPressed()`** uses it for Ctrl+C / Cmd+C (copy hovered cell).

**ApplicationLogic.cpp** uses it consistently for global shortcuts:

- Ctrl+F / Cmd+F, Ctrl+S / Cmd+S, Ctrl+E / Cmd+E, Ctrl+Shift+H / Cmd+Shift+H

**ResultsTable.cpp** was inconsistent:

- **Ctrl+Enter** (reveal in Explorer): uses `IsPrimaryShortcutModifierDown(io)` → correct on macOS (Cmd+Enter).
- **Ctrl+Shift+C** (copy path of selected row): used raw `io.KeyCtrl` → **Bug:** on macOS only Ctrl+Shift+C worked; Cmd+Shift+C did nothing (and Help documents “Cmd+Shift+C” on macOS).
- **Ctrl+Shift+P** (Pin to Quick Access): used raw `io.KeyCtrl` → Windows-only feature; if ever enabled on macOS, would need Cmd+Shift+P.
- **Plain Enter** (open selected): used `!io.KeyCtrl && !shift` → did not exclude `KeySuper`; behavior was still correct because Ctrl+Enter is checked first, but for clarity and future-proofing, “no primary modifier” should use the same helper.
- **P (previous row)**: used `!io.KeyCtrl` to avoid treating Ctrl+P as “previous” → on macOS, Cmd+P was treated as “previous row” instead of being reserved; should use `!IsPrimaryShortcutModifierDown(io)`.

---

## 2. Robustness improvements applied

1. **Use primary modifier everywhere for chord shortcuts**
   - **Ctrl+Shift+C / Cmd+Shift+C:** Check `IsPrimaryShortcutModifierDown(io) && shift` instead of `io.KeyCtrl && shift` so Cmd+Shift+C works on macOS and Help text matches behavior.
   - **Ctrl+Shift+P:** Use `IsPrimaryShortcutModifierDown(io) && shift` for consistency and so a future macOS Pin shortcut could use Cmd+Shift+P.

2. **Use primary modifier for “no modifier” cases**
   - **Plain Enter (open selected):** Use `!IsPrimaryShortcutModifierDown(io) && !shift` so “open” only fires when neither Ctrl/Cmd nor Shift is held.
   - **P (previous row):** Use `!IsPrimaryShortcutModifierDown(io)` so that Cmd+P (and Ctrl+P) do not trigger “previous row”; reserves primary+P for future use.

3. **No structural change**
   - Shortcut order (Ctrl+Enter → Ctrl+Shift+C → plain Enter → Tab; then Pin on Windows) is already correct.
   - `KeyPressedOnce` already uses `ImGui::IsKeyPressed(key, false)` to ignore key repeat.
   - Focus guard (`window_focused && !WantTextInput`) remains the single gate; no extra helpers were added to avoid duplication in a single function.

---

## 3. Optional future improvements (not done)

- **Shared helper**  
  A helper such as `IsPrimaryAndShiftDown(io)` could be added to ImGuiUtils.h and used for Ctrl+Shift+C and Ctrl+Shift+P. Currently only two call sites; the inline `IsPrimaryShortcutModifierDown(io) && shift` is clear and matches ApplicationLogic (e.g. Ctrl+Shift+H). Can be extracted later if more Ctrl+Shift shortcuts are added.

- **Alt key**  
  Shortcuts do not currently consider Alt; Alt+Enter still triggers “open selected.” That matches common app behavior; no change unless product explicitly reserves Alt chords.

- **KeypadEnter**  
  Already handled: plain-Enter branch uses `KeyPressedOnce(ImGuiKey_Enter) || KeyPressedOnce(ImGuiKey_KeypadEnter)`.

---

## 4. Verification

- After changes: build and run `scripts/build_tests_macos.sh`.
- Manually on macOS: Cmd+Enter (reveal), Cmd+Shift+C (copy path), Cmd+P (no action / reserved), plain Enter (open) and Tab (focus search) behave as intended.
- Help text already documents Cmd vs Ctrl per platform; behavior now matches.

---

## 5. References

- **Modifier helper:** `src/gui/ImGuiUtils.h` – `IsPrimaryShortcutModifierDown`, `IsCopyShortcutPressed`
- **Global shortcuts:** `src/core/ApplicationLogic.cpp` – `HandleKeyboardShortcuts`
- **Results table shortcuts:** `src/ui/ResultsTable.cpp` – `HandleResultsTableKeyboardShortcuts`
- **High-value shortcuts:** `internal-docs/analysis/2026-02-21_HIGH_VALUE_SHORTCUTS_RESEARCH.md`
