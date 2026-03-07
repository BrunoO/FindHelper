# 2026-02-21 Implementation Plan: High-Value Result-Table Shortcuts

**Scope**: Enter (open selected), Ctrl+Enter (reveal in Explorer), Ctrl+Shift+C (copy path), Alt+Enter/Win (properties), Tab (focus search). No code yet—plan only.

**Reference**: `internal-docs/analysis/2026-02-21_HIGH_VALUE_SHORTCUTS_RESEARCH.md`, `internal-docs/plans/production/2026-02-21_PRODUCTION_READINESS_PIN_TO_QUICK_ACCESS_AND_WINDOWS_BUILD.md`, AGENTS.md, `docs/plans/production/PRODUCTION_READINESS_CHECKLIST.md`.

---

## 1. Review-Derived Constraints (Avoid Reintroducing Issues)

Apply these so the new shortcuts do not reintroduce issues already found and fixed in review (Pin to Quick Access, ShellContextUtils, production readiness):

| Constraint | Source | How to apply |
|------------|--------|--------------|
| **Windows-only code** | Pin production doc, AGENTS.md | All shell-verb and Windows API usage under `#ifdef _WIN32` with matching `#endif  // _WIN32` comments. No Windows APIs or Windows-only includes outside guards. |
| **Include order** | PRODUCTION_READINESS_CHECKLIST, AGENTS.md | System includes first, then project. On Windows files: `<windows.h>` (or platform headers) before other Win headers; add `shobjidl_core.h` only where needed (e.g. new shell verb in ShellContextUtils). |
| **COM / PIDL / Unicode verb** | Pin production doc, ShellContextUtils | Any new shell verb (e.g. “properties”): same pattern as Pin—COM init per call, uninit only when `CoInitializeEx` returned S_OK; PIDL via `PidlAbsolutePtr` (RAII); invoke with `CMINVOKECOMMANDINFOEX`, `fMask = CMIC_MASK_UNICODE`, `lpVerbW = L"properties"`. No LPCWSTR→LPWSTR cast. |
| **Return value + notification** | Pin behavior | Shell operations that can fail (e.g. Properties) must return bool; ResultsTable sets `state.exportNotification` / `state.exportErrorMessage` and `state.exportNotificationTime` from that return so the user sees success/failure in the status bar (same pattern as Pin). |
| **Table shortcut context** | Existing HandleResultsTableKeyboardShortcuts | Shortcuts run only when `ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)` and `!io.WantTextInput`. No change: all new shortcuts live in the same handler and use the same guard so Enter/Ctrl+Enter never fire while typing in search. |
| **No std::min/std::max without parentheses** | AGENTS.md, Windows build | Do not introduce `std::min`/`std::max` in new code; use `(std::min)`/`(std::max)` if ever needed. |
| **Float literals** | User rules | Use `F` suffix (e.g. `0.5F`), not `f`. |
| **Naming** | CXX17_NAMING_CONVENTIONS, CODE_REVIEW_V3 | New functions: PascalCase. No new namespaces; if any new module, use snake_case. Do not introduce PascalCase namespace or snake_case method names. |
| **Help and docs** | Pin pattern | HelpWindow: add each new shortcut under the correct platform block (e.g. Alt+Enter under `#ifdef _WIN32`). Keep wording consistent with existing entries. |

---

## 2. Current Behavior to Reuse (No Duplication)

- **Open file**: `file_operations::OpenFileDefault(std::string_view full_path)` — used on double-click filename column. Use for Enter (file or folder: “open” = default app for file, Explorer for folder; `OpenFileDefault` on a folder path opens it in Explorer on Windows).
- **Reveal in Explorer**: `file_operations::OpenParentFolder(std::string_view full_path)` — used on double-click path column. Use for Ctrl+Enter.
- **Copy path**: `file_operations::CopyPathToClipboard(GLFWwindow*, std::string_view full_path)` — used for Ctrl+C when hovering path. Use for Ctrl+Shift+C on selected row (no hover).
- **Focus search**: `state.focusFilenameInput = true` — used by Ctrl+F in ApplicationLogic; SearchInputs clears it and calls `ImGui::SetKeyboardFocusHere()`. Use for Tab-from-table.
- **Shell verb (Windows)**: Pin uses `ShellContextUtils::PinToQuickAccess(HWND, path)` with IContextMenu + “pintohome”. Properties will use the same pattern with verb “properties” (new function, same file/module).

---

## 3. Shell Helper Extraction (Remove Code Duplication)

Before adding `ShowProperties`, extract shared shell logic in `ShellContextUtils.cpp` so Pin, Properties, and (optionally) ShowContextMenu do not duplicate the same sequence. This keeps one place for COM/PIDL/Unicode rules and eases future shell verbs.

### 3.1 Duplication today

- **ShowContextMenu** and **PinToQuickAccess** both repeat (with only log context differing):
  - Path → `path_str`, `w_path = Utf8ToWide(path_str)`
  - `CoInitializeEx` / `need_uninit` / `CoUninitialize` on every error path
  - `SHParseDisplayName(w_path, …)` → `PidlAbsolutePtr pidl_abs(raw_pidl)`
  - `SHBindToParent(pidl_abs.get(), IID_IShellFolder, &p_parent_folder, &pidl_child)`
  - `p_parent_folder->GetUIObjectOf(hwnd, 1, &pidl_child, IID_IContextMenu, …, &p_context_menu)`
  - Then each does its own InvokeCommand and cleanup (Release + CoUninitialize).

Adding Properties by copying this block again would triple the duplication.

### 3.2 Recommended extraction

Introduce **internal** (file-static or anonymous-namespace) helpers in `ShellContextUtils.cpp` only; no new public API beyond existing callers.

| Helper | Purpose | Used by |
|--------|---------|--------|
| **GetContextMenuForPath** (or similar name) | Given `HWND hwnd`, `std::string_view path`, and a short `context` string for logging: perform COM init (set `need_uninit`), `Utf8ToWide` → `SHParseDisplayName` → `PidlAbsolutePtr`, `SHBindToParent`, `GetUIObjectOf` → `IContextMenu*`. Return a small struct or out-params: `IContextMenu*`, `IShellFolder*`, `bool need_uninit`. On any failure: log with `context`, release any acquired interfaces, call `CoUninitialize` when `need_uninit`, return “failure” (e.g. nullptrs / false). Caller does **not** take ownership of COM init; caller is responsible for calling `Release()` on both pointers and `CoUninitialize()` when `need_uninit` is true. | ShowContextMenu, PinToQuickAccess, ShowProperties |
| **InvokeVerbByUnicodeName** (or similar) | Given `IContextMenu* p_context_menu`, `HWND hwnd`, `const wchar_t* verb` (e.g. `L"pintohome"`, `L"properties"`), and optional `context` for logging: fill `CMINVOKECOMMANDINFOEX` with `fMask = CMIC_MASK_UNICODE`, `lpVerbW = verb`, `nShow = SW_SHOWNORMAL`, call `InvokeCommand`, return success/failure. Does **not** Release or CoUninitialize. | PinToQuickAccess, ShowProperties |

Flow after extraction:

- **PinToQuickAccess**: Call GetContextMenuForPath; on success, call InvokeVerbByUnicodeName(p_context_menu, hwnd, L"pintohome", …); then Release both, CoUninitialize if need_uninit; return InvokeVerbByUnicodeName result.
- **ShowProperties**: Same, with verb L"properties".
- **ShowContextMenu**: Call GetContextMenuForPath; on success, run existing QueryContextMenu + TrackPopupMenu + InvokeCommand (ordinal) logic; then Release and CoUninitialize. Optional: if the ordinal path can use a small shared InvokeCommand wrapper, use it; otherwise leave as-is to avoid unnecessary churn.

### 3.3 Constraints when implementing helpers

- **Single responsibility**: GetContextMenuForPath only acquires IContextMenu (and parent folder) and COM state; InvokeVerbByUnicodeName only invokes one verb by Unicode name. No mixing of “get menu” and “show popup” in the same helper.
- **Ownership**: Clearly document that the caller must Release both interfaces and call CoUninitialize when need_uninit. No RAII that hides Release/CoUninitialize across function boundaries unless the whole pattern is refactored (optional later).
- **Logging**: Pass a short `context` string (e.g. `"PinToQuickAccess"`, `"ShowProperties"`, `"ShowContextMenu"`) into the get-helper so error logs stay identifiable.
- **No new public API**: Helpers stay in the .cpp (static or anonymous namespace). Public API remains `ShowContextMenu`, `PinToQuickAccess`, and (new) `ShowProperties`.
- **Review compliance**: Same COM/PIDL/Unicode/RAII rules: PidlAbsolutePtr, no LPCWSTR→LPWSTR cast, CMINVOKECOMMANDINFOEX with CMIC_MASK_UNICODE for verb-by-name. All `#endif` commented.

### 3.4 Implementation order relative to shortcuts

Do **shell helper extraction first** (Phase 0), then implement shortcuts 1–4 (Enter, Ctrl+Enter, Ctrl+Shift+C, Tab), then add **ShowProperties** (Phase 5) implemented on top of the new helpers so that Pin and ShowProperties both go through GetContextMenuForPath + InvokeVerbByUnicodeName. Refactor **PinToQuickAccess** to use the helpers in the same change as adding ShowProperties (or in an immediate follow-up) so duplication is removed in one pass.

---

## 4. Shortcut Implementation Plan

### 4.1 Enter – Open selected file or folder

| Item | Plan |
|------|------|
| **Action** | With results table focused and a row selected, **Enter** (or Keypad Enter) opens the selected item: file → default application; folder → open in Explorer (same as “open folder”). |
| **Where** | `HandleResultsTableKeyboardShortcuts` in `ResultsTable.cpp`, after existing `has_selection` and before/alongside other key checks. |
| **Logic** | `(KeyPressedOnce(ImGuiKey_Enter) \|\| KeyPressedOnce(ImGuiKey_KeypadEnter)) && !io.KeyCtrl && !io.KeyShift && has_selection` → call `file_operations::OpenFileDefault(display_results[state.selectedRow].fullPath)`. No modifier: plain Enter only. |
| **Conflict** | Enter in search triggers search; table shortcuts run only when table has focus and `!WantTextInput`, so no conflict. |
| **Platform** | Cross-platform (reuse existing `OpenFileDefault` on all platforms). |
| **Help** | Add “Enter - Open selected file or folder” (or equivalent) in the result-table / file-ops section; Cmd vs Ctrl for other shortcuts already documented elsewhere. |

---

### 4.2 Ctrl+Enter – Reveal in Explorer

| Item | Plan |
|------|------|
| **Action** | With a row selected, **Ctrl+Enter** (Cmd+Enter on macOS) opens the parent folder in Explorer/Finder/file manager and selects the file. |
| **Where** | Same handler, same `has_selection` guard. |
| **Logic** | `IsPrimaryShortcutModifierDown(io) && KeyPressedOnce(ImGuiKey_Enter) && has_selection` (and no Shift) → `file_operations::OpenParentFolder(display_results[state.selectedRow].fullPath)`. |
| **Conflict** | Ctrl+Enter in search triggers search; again, table handler only when table focused and !WantTextInput. |
| **Platform** | Cross-platform. |
| **Help** | Add “Ctrl+Enter / Cmd+Enter - Reveal in Explorer (open parent folder and select file)” (or platform-appropriate wording). |

---

### 4.3 Ctrl+Shift+C – Copy full path of selected row

| Item | Plan |
|------|------|
| **Action** | With a row selected, **Ctrl+Shift+C** copies the full path of the selected item to the clipboard, independent of hover. |
| **Where** | Same handler. |
| **Logic** | `io.KeyCtrl && io.KeyShift && KeyPressedOnce(ImGuiKey_C) && has_selection` → `file_operations::CopyPathToClipboard(glfw_window, display_results[state.selectedRow].fullPath)`. Optionally set a short-lived success notification (e.g. “Path copied”) for consistency with Pin; if so, use same `exportNotification` / `exportNotificationTime` pattern. |
| **Conflict** | None in current shortcut list. |
| **Platform** | Cross-platform. |
| **Help** | Add “Ctrl+Shift+C / Cmd+Shift+C - Copy full path of selected row”. Clarify in help that in the table this is “selected row” vs Ctrl+C which is “hovered cell (filename or path)”. |

---

### 4.4 Alt+Enter (Windows) – Properties of selected item

| Item | Plan |
|------|------|
| **Action** | With a row selected, **Alt+Enter** opens the shell Properties dialog for that file or folder (Windows). |
| **Where** | New function in `ShellContextUtils` (e.g. `ShowProperties(HWND hwnd, std::string_view path)`), invoked from `HandleResultsTableKeyboardShortcuts` under `#ifdef _WIN32`. |
| **Logic** | In handler: `io.KeyAlt && KeyPressedOnce(ImGuiKey_Enter) && has_selection` under `#ifdef _WIN32` → call `ShowProperties(static_cast<HWND>(native_window), path)`. In ShellContextUtils: implement **via the extracted helpers** (§3): GetContextMenuForPath then InvokeVerbByUnicodeName(hwnd, L"properties"); caller performs Release and CoUninitialize. Return bool; on failure set `state.exportErrorMessage` (and optionally clear `state.exportNotification`) and `state.exportNotificationTime`. |
| **Avoiding review issues** | Use shared helpers so COM/PIDL/Unicode/RAII live in one place; no duplicated block. No new includes in the middle of the file; `#endif  // _WIN32` comments; HelpWindow entry under `#ifdef _WIN32`. |
| **Platform** | Windows only. macOS/Linux: no Alt+Enter in this plan (future: “Get Info” etc. could be separate). |
| **Help** | Under `#ifdef _WIN32`: “Alt+Enter - Properties of selected file or folder”. |

---

### 4.5 Tab (from table) – Focus search input

| Item | Plan |
|------|------|
| **Action** | With results table focused, **Tab** (without Shift) requests focus for the filename search input so the user can refine the query without the mouse. |
| **Where** | Same handler; no new state. |
| **Logic** | `KeyPressedOnce(ImGuiKey_Tab) && !io.KeyShift` when table is focused → `state.focusFilenameInput = true`. SearchInputs already consumes this flag and calls `SetKeyboardFocusHere()` on the filename field; no change there. |
| **Conflict** | ImGui may also use Tab for focus tab order. Setting `focusFilenameInput` and applying it next frame when rendering the search input should still move focus to the filename field; if testing shows double-focus or wrong order, consider consuming the key (e.g. early return after setting the flag) so ImGui does not move focus to another widget. |
| **Platform** | Cross-platform. |
| **Help** | Add “Tab - Focus filename search (from results table)” (or similar) in the table/shortcuts section. |

---

## 5. Order of Implementation (Recommended)

0. **Shell helper extraction (Phase 0)** – In `ShellContextUtils.cpp`, add internal helpers (e.g. GetContextMenuForPath, InvokeVerbByUnicodeName) and refactor **PinToQuickAccess** and **ShowContextMenu** to use them so the duplicated COM/PIDL/GetUIObjectOf block lives in one place. See §3.
1. **Enter** – Open selected (reuses OpenFileDefault; no new APIs; simple condition).
2. **Ctrl+Enter** – Reveal in Explorer (reuses OpenParentFolder; same).
3. **Ctrl+Shift+C** – Copy path (reuses CopyPathToClipboard; optional notification).
4. **Tab** – Focus search (one line: set `focusFilenameInput`; reuse existing focus path).
5. **Alt+Enter (Windows)** – Add **ShowProperties** implemented via the new helpers (GetContextMenuForPath + InvokeVerbByUnicodeName with L"properties"); wire in ResultsTable and HelpWindow.

Rationale: remove shell duplication first so Pin and Properties share one path; then cross-platform shortcuts; then the new Windows-only verb using the shared helpers.

---

## 6. Files to Touch (Summary)

| File | Changes (plan only) |
|------|---------------------|
| `src/ui/ResultsTable.cpp` | In `HandleResultsTableKeyboardShortcuts`: add Enter, Ctrl+Enter, Ctrl+Shift+C, Tab; under `#ifdef _WIN32` add Alt+Enter and call to `ShowProperties`. Keep all `#endif` commented. No new includes except existing `ShellContextUtils.h` on Windows. |
| `src/platform/windows/ShellContextUtils.cpp` | **Phase 0:** Extract internal helpers (GetContextMenuForPath, InvokeVerbByUnicodeName); refactor ShowContextMenu and PinToQuickAccess to use them. **Phase 5:** Add `ShowProperties(HWND, std::string_view)` using those helpers (verb L"properties"). Same includes; no new pragmas. |
| `src/platform/windows/ShellContextUtils.h` | Declare `ShowProperties(HWND, std::string_view)` under `#ifdef _WIN32`. |
| `src/ui/HelpWindow.cpp` | Add one line per new shortcut in the correct section; Alt+Enter under `#ifdef _WIN32`. Use Cmd vs Ctrl for macOS where applicable. |

---

## 7. Testing and Production Checklist (Before Merge)

- **Focus rule**: Confirm Enter/Ctrl+Enter do not trigger when the user is typing in the search box (WantTextInput) or when another window has focus.
- **Conflict**: Confirm Ctrl+Enter in search input still triggers search when focus is in search, and Ctrl+Enter in table reveals in Explorer when focus is in table.
- **Windows**: Build on Windows (Debug/Release); run smoke tests for Enter, Ctrl+Enter, Ctrl+Shift+C, Tab, Alt+Enter; check Properties dialog and notification on failure.
- **macOS/Linux**: Build and smoke test Enter, Ctrl+Enter, Ctrl+Shift+C, Tab (no Alt+Enter).
- **Production checklist**: Headers, `#endif` comments, no std::min/max, naming, Help text; for Properties, COM/PIDL/Unicode and return+notification same as Pin.
- **AGENTS.md**: Run `scripts/build_tests_macos.sh` after any C++/CMake change.

---

## 8. Out of Scope for This Plan

- F2 Rename, Shift+Delete permanent delete, and other shortcuts from the research doc are not in this plan.
- No change to double-click behavior (filename/path columns remain as today).
- No new popups (only existing notification area and system Properties dialog).
