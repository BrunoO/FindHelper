# 2026-02-19 Production Readiness and Windows Build Anticipation

**Scope**: Help window as normal window, Export CSV move to SearchInputs, Ctrl+E / Ctrl+S shortcuts, ExportToCsv and HandleKeyboardShortcuts guards, and related UI/state changes.

**Reference**: `docs/plans/production/PRODUCTION_READINESS_CHECKLIST.md`, AGENTS.md.

---

## Quick Checklist – Must-Check Items

| Item | Status | Notes |
|------|--------|--------|
| Headers / include order | ✅ | HelpWindow.cpp: project → imgui → project. ApplicationLogic.cpp: system (cassert, chrono) → imgui → project. No `<windows.h>` in new UI files. |
| Windows `(std::min)`/`(std::max)` | ✅ | No std::min/max in HelpWindow or ApplicationLogic. SearchInputs.cpp (Export CSV area) already uses `(std::max)` everywhere. |
| Forward declaration consistency | ✅ | No new forward declarations in touched files. |
| Float literals (F not f) | ✅ | HelpWindow.cpp uses `0.5F`, `450.0F`, `500.0F`, `120.0F`. |
| Lambda captures | ✅ | No implicit `[&]`/`[=]` in HelpWindow or ApplicationLogic (no lambdas in template context in these files). |
| CMake / new files | ✅ | HelpWindow.cpp and ApplicationLogic.cpp already in Windows, macOS, and Linux app targets. |
| `#endif` comments | ✅ | ApplicationLogic: `#endif  // _WIN32`. HelpWindow: `#endif  // __APPLE__` on all conditionals. |

---

## Windows-Specific Code Review

### Already Correct

- **Application.cpp – ExportToCsv**: Uses `#ifdef _WIN32` with `localtime_s` and `#else` with `localtime_r`; `#endif  // _WIN32` is commented. No change needed.
- **ApplicationLogic.cpp**: Maintenance interval uses `#ifdef _WIN32` and `usn_monitor_constants::kMaintenanceCheckIntervalSeconds` on Windows; macOS uses local constant. `#endif  // _WIN32` present.
- **HelpWindow.cpp**: Only `#ifdef __APPLE__` for Cmd vs Ctrl / Cmd+E vs Ctrl+E text. No Windows-specific code; Windows gets the `#else` branch (Ctrl+F, Ctrl+Enter, etc.). No `#ifdef _WIN32` required for this UI text.
- **SearchInputs.cpp**: All `(std::max)` usages already parenthesized (AGENTS.md / Windows macro safety).

### No New Windows-Only Paths

- Help window: pure ImGui; no new platform APIs.
- Export CSV: uses existing `path_utils::GetDownloadsPath()`, `GetDesktopPath()`, `path_utils::JoinPath()` and `ExportSearchResultsService::ExportToCsv`; Windows path handling is in existing code.
- Keyboard shortcuts: handled in ApplicationLogic; Ctrl vs Cmd is handled in existing shortcut code; no new Windows API calls.

---

## Anticipated Windows Build Issues (Low Risk)

| Risk | Mitigation |
|------|------------|
| **PGO** | No new source files shared between main app and test targets. HelpWindow and ApplicationLogic were already in all targets; no PGO flag change needed. |
| **Missing symbol / link error** | Unlikely: HelpWindow and ApplicationLogic are in the Windows app source list (CMakeLists.txt ~166 and ~153). |
| **ImGui on Windows** | Help window uses standard ImGui Begin/End and SetNextWindowPos/Size; same pattern as MetricsWindow (already used on Windows). |
| **UsnMonitor include on non-Windows** | ApplicationLogic includes `usn/UsnMonitor.h` for `usn_monitor_constants`; usage is inside `#ifdef _WIN32`, so no symbol use on macOS/Linux. |

### Recommended Before Release (Checklist)

- [ ] **Build on Windows**: Run full CMake configure and build (Debug and Release) on a Windows machine or CI to confirm no regressions.
- [ ] **PGO (if used)**: If ENABLE_PGO is on, ensure no new shared sources between app and tests; current state is fine.
- [ ] **Smoke test**: Open Help window, use Ctrl+E (export CSV) and Ctrl+S (Save Search) on Windows to confirm modifiers and dialogs.
- [ ] **Memory leak run**: Per checklist, run Application Verifier / VS diagnostic tools for 10–15 minutes before release (not specific to these changes).

---

## Production Readiness Summary

- **Quick checklist**: Satisfied for the changed areas (headers, std::min/max, float literals, lambdas, CMake, #endif comments, platform conditionals).
- **Windows**: No new `std::min`/`std::max` usage; existing Windows branches (Application.cpp, ApplicationLogic.cpp) are correct; Help window and shortcuts are cross-platform.
- **Next steps**: Run Windows build (and PGO build if applicable), then smoke-test Help, Ctrl+E, and Ctrl+S on Windows; run full memory leak check before release as per PRODUCTION_READINESS_CHECKLIST.md.
