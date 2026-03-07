# Theme Refactor: Production Readiness & Windows Build Anticipation

**Date:** 2026-02-14

## Summary

Production-readiness check and Windows build anticipation for the Theme extraction (Theme.h / Theme.cpp) and related UI changes.

---

## Quick Checklist (Theme & Touched Files)

| Item | Status |
|------|--------|
| **Headers / include order** | OK – Theme.h has `imgui.h` only; Theme.cpp has project then imgui. AppBootstrapCommon includes Theme.h after imgui. |
| **Windows (std::min)/(std::max)** | OK – Theme does not use std::min/max. Other UI files (SettingsWindow, SearchInputs, Popups, EmptyState) already use `(std::min)` / `(std::max)`. |
| **Forward declarations** | OK – Theme is a class in namespace ui; no forward decls in Theme. |
| **Exception handling** | OK – Theme::Apply() only touches ImGui style; no I/O or throwing code. |
| **Naming** | OK – Theme, Colors, Surface, SurfaceHover, etc. follow conventions. |
| **PGO** | OK – Theme.cpp is in APP_SOURCES for find_helper on all platforms; inherits PGO flags on Windows when ENABLE_PGO=ON. |
| **Linux build** | **FIXED** – Linux bootstrap was still calling removed `ConfigureImGuiStyleLinux()`. Now calls `ui::Theme::Apply((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0)`. |

---

## Windows Build Anticipation

### 1. No Windows-specific code in Theme

- **Theme.h** – Only includes `imgui.h`; no `windows.h`, no `#ifdef _WIN32`.
- **Theme.cpp** – Only ImGui API (GetStyle(), Colors[], StyleColorsDark()). No platform APIs.
- **Conclusion:** No `(std::min)`/`(std::max)` or `strcpy_safe` needed in Theme. No `#endif // _WIN32` to add.

### 2. Call sites (Theme::Colors, Theme::Apply)

- **AppBootstrap_win.cpp** – Calls `ui::Theme::Apply(...)`. Includes go through AppBootstrapCommon.h → Theme.h. No extra Windows includes in Theme.
- **FilterPanel.cpp, ResultsTable.cpp, UIRenderer.cpp** – Use `Theme::Colors::*` (constexpr ImVec4). Same on all platforms.
- **Conclusion:** No Windows-only paths or macros required in Theme or these call sites.

### 3. ImGui on Windows

- imgui.h / ImGui backends are already used on Windows (DirectX, GLFW). Theme only sets style after context creation; no change to ImGui build or linkage.
- **Conclusion:** No expected ImGui-related Windows build or link issues from Theme.

### 4. CMake / PGO

- **Theme.cpp** is in `APP_SOURCES` for the Windows `find_helper` target (around line 143). It gets the same compile/link options as the rest of the app.
- With **ENABLE_PGO=ON**, find_helper uses `/GL /Gy` and PGO link flags; Theme.cpp is compiled and linked with them. No separate PGO handling for Theme.
- **Conclusion:** No LNK1269 or PGO issues expected for Theme.

### 5. Possible Windows-only issues (unrelated to Theme)

- **Pre-commit / clang-tidy on macOS:** Analyzing `AppBootstrap_win.cpp` can report `'windows.h' file not found` because the check runs in a non-Windows environment. This is environmental, not a bug in Theme or bootstrap.
- **NOMINMAX / WIN32_LEAN_AND_MEAN:** Already set for find_helper on Windows. Theme does not include windows.h, so no conflict.

---

## Linux Bootstrap Fix (Done)

- **Issue:** After removing `ApplyCommonImGuiColors` and `ConfigureImGuiStyleLinux` from AppBootstrapCommon (replaced by Theme), Linux still called `AppBootstrapCommon::ConfigureImGuiStyleLinux()` → **undefined reference**, link failure.
- **Change:** In `AppBootstrap_linux.cpp`, `InitializeImGuiBackend()` now calls `ui::Theme::Apply((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0)` and includes `ui/Theme.h`.
- **Note:** Linux now uses the same theme as Windows/macOS (FrameRounding=8, GrabRounding=6). The previous Linux-only rounding (3.0) is no longer used.

---

## Float Literals (Project Rule)

- Float literals use `F` suffix (e.g. `0.15F`, `2.0F`). In **UIRenderer.cpp**, `2.0f` and `120.0f` in the manual search header were updated to `2.0F` and `120.0F`.

---

## Memory / Leaks

- Theme does not allocate heap memory or hold static state beyond constexpr color definitions. No containers or caches. No extra leak risk from Theme.

---

## Reference

- **Production checklist:** `docs/plans/production/PRODUCTION_READINESS_CHECKLIST.md`
- **Windows / AGENTS:** `AGENTS.md` (std::min/max, include order, #endif comments, PGO)
