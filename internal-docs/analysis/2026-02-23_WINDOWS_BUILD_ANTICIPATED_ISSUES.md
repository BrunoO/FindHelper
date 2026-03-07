# 2026-02-23 Anticipated Windows Build Issues

This document lists possible Windows build issues introduced by recent Sonar-targeted and critical-fix changes, and how to verify or mitigate them.

---

## 1. DragDropUtils.cpp – COM self-deletion with `std::unique_ptr<ComBase>`

**Change:** `ComBase::Release()` now uses `std::unique_ptr<ComBase> self(this)` instead of `delete this`.

**Risk:** With multiple inheritance (e.g. `FileDropDataObject : IDataObject, ComBase`), the run-time pointer value of `ComBase*` can differ from `this` (pointer adjustment). Destroying through `unique_ptr<ComBase>` must use the same address that was returned by `new` (usually the object’s primary base). If `ComBase` is not the primary base, UB or crash is possible.

**Mitigation:** In this codebase, `ComBase` is the second base (e.g. `IDataObject, ComBase`). So in `Release()`, `this` is a `ComBase*` that may be offset from the full object address. Both the previous `delete this` and the current `unique_ptr<ComBase> self(this)` destroy via that same base pointer. C++ requires `delete` to be called with the same pointer value returned by `new`; destroying through an offset base pointer is undefined behavior. So this is a pre-existing COM pattern risk, not introduced by the unique_ptr change. If you see a crash in `Release()` during or after drag-drop, the fix is to delete through the most-derived type (e.g. a virtual `void DeleteSelf()` implemented in the derived class as `delete this` where `this` is the full object, and call it from `ComBase::Release()` via a virtual call).

**Verify on Windows:** Run the app, trigger drag-and-drop (e.g. drag a result row), then drop or cancel. Confirm no crash when the data object is released.

---

## 2. UsnMonitor.cpp – `auto handle_to_close`

**Change:** `HANDLE handle_to_close = INVALID_HANDLE_VALUE` → `auto handle_to_close = INVALID_HANDLE_VALUE`.

**Risk:** On Windows, `INVALID_HANDLE_VALUE` is `(HANDLE)(LONG_PTR)-1`. MSVC deduces `auto` as `HANDLE`. Behavior is unchanged.

**Verify on Windows:** Build; no compile error. Run with USN monitoring (e.g. run as admin, then stop the app or disable monitoring) and confirm clean shutdown.

---

## 3. Popups.cpp – removal of `RenderPopupCloseButton()`

**Change:** Unused function `RenderPopupCloseButton()` was removed (S1144).

**Risk:** A Windows-only code path might have called it via a different build or macro.

**Mitigation:** Grep showed no call sites. `RenderSaveCancelButtons` and other popup helpers are still present and used.

**Verify on Windows:** Open all popups that have a close button (Help, Settings, Search Syntax, Metrics, Save Search, Delete confirmation, etc.) and close them. Confirm no link errors and no missing close button.

---

## 4. ResultsTable.cpp – merged `if` (context menu)

**Change:** Nested `if (hovered && right-click)` and `if (!state.contextMenuOpen)` merged into one condition.

**Risk:** Logic or evaluation order could differ (e.g. short-circuit), changing when the context menu opens.

**Mitigation:** Condition is equivalent: same guards and same body. No change to `state.contextMenuOpen` or debounce logic.

**Verify on Windows:** Right-click a result row; context menu should open once per debounce. Right-click again after closing; it should open again. No double-open or no-open.

---

## 5. ShellContextUtils.cpp – `reinterpret_cast` and `std::remove_pointer_t`

**Change:** Only comments (NOSONAR) and `std::remove_pointer_t<PIDLIST_ABSOLUTE>` (replacing `typename std::remove_pointer<...>::type`). No change to cast usage.

**Risk:** None expected. C++14/C++17 features used are supported by MSVC.

**Verify on Windows:** Build. Use “Pin to Quick Access” and context menu (e.g. right-click result row) and confirm they work.

---

## 6. Windows headers – `#include <windows.h>` and NOSONAR

**Change:** Comments added next to `#include <windows.h>` in Application.cpp, DragDropUtils.cpp, StatusBar.cpp. No path or macro change.

**Risk:** None. Windows is case-insensitive; `<windows.h>` is correct. NOSONAR does not affect the preprocessor.

---

## 7. ImGuiTestEngineTests.cpp – NOSONAR on `RunRegressionTestCase`

**Change:** Single-line comment (NOSONAR) added on the function declaration. No signature or parameter change.

**Risk:** None for build or runtime. Test binary still links and runs the same.

---

## 8. AppBootstrap_win.cpp / Application.cpp – NOSONAR on catch and PGO handle

**Change:** NOSONAR comments only. No change to exception handling or PGO logic.

**Risk:** None.

---

## Recommended Windows verification checklist

1. **Build (Release and Debug)**  
   - `cmake -B build -DCMAKE_BUILD_TYPE=Release` (or Debug) then build.  
   - Fix any missing-include or type-deduction errors (none expected from the changes above).

2. **NOMINMAX / min–max**  
   - No `std::min`/`std::max` were added in the modified files. If a new warning appears about min/max, wrap calls in parentheses per AGENTS.md.

3. **Drag-and-drop**  
   - Start app, run a search, drag a result row to Explorer or another app; cancel or complete drop. No crash on release.

4. **USN monitor**  
   - Run with admin rights so USN starts; exit or change volume. No crash in `UsnMonitor::Stop()`.

5. **Popups**  
   - Open and close Help, Settings, Search Syntax, delete confirmation, save search. All close buttons and behaviors unchanged.

6. **Context menu**  
   - Right-click result row; context menu and “Pin to Quick Access” (if used) work as before.

7. **PGO (if used)**  
   - If you use Profile-Guided Optimization, ensure instrumented and optimized builds still link and run; only comments were added in PGO-related code.

If any of these checks fail, the failure is likely due to an environmental or toolchain difference (e.g. SDK version, NOMINMAX not set). Use this list to narrow down the offending change (e.g. DragDropUtils vs UsnMonitor vs Popups).
