# Task: Windows Run Without Elevation — Folder Crawl Like macOS/Linux

**Date:** 2026-02-02  
**Source:** Taskmaster prompt; goal = under Windows, when users do not accept elevated privileges, the app should still work with no USN monitoring, but just crawl folders like under macOS or Linux. Currently the app stops immediately when privileges are not elevated.  
**Reference:** `docs/prompts/TaskmasterPrompt.md`, `AGENTS document`, `resources/windows/app.manifest`, `src/platform/windows/AppBootstrap_win.cpp`, `src/core/main_common.h`, `docs/plans/features/UI_FOLDER_CRAWL_SELECTION_WINDOWS_ANALYSIS.md`, `docs/plans/features/AUTO_CRAWL_ON_STARTUP_PLAN.md`

---

## Summary

On Windows, the application manifest requests `requireAdministrator`. When the user runs the app and declines the UAC elevation prompt, **Windows never starts the process** — so the app “stops immediately” because it never runs. The runtime code already supports running without elevation (no USN monitor, folder crawl via Settings or auto-crawl), but the manifest prevents that path. The task is to allow the app to start without requiring administrator rights (e.g. by changing the manifest to `asInvoker`), and to verify that when running without elevation the app behaves like macOS/Linux: UI is shown, no USN monitoring, folder crawling (auto-crawl or user-selected folder in Settings). Optionally document how users can still get USN monitoring (e.g. “Run as administrator” or in-app “Restart as administrator”).

***

```markdown
# Prompt: Windows — Run Without Elevation, Use Folder Crawl (Like macOS/Linux)

## Mission Context (The "Why")

On Windows, when users do not accept the UAC elevation prompt, the application currently stops immediately. The desired behavior is for the app to still run: without USN monitoring, using folder crawling (like on macOS or Linux). The root cause is that the embedded application manifest requests `requireAdministrator`, so Windows does not start the process at all when the user declines elevation. The runtime logic already supports running without elevation (no USN monitor, folder crawl); the fix is to allow the process to start without requiring admin, then ensure the existing “no elevation” path is correct and documented.

## Core Objective (The "What")

Change the Windows application so that it can start without administrator privileges. When running without elevation, the app must show the UI and use folder crawling only (no USN monitoring), matching macOS/Linux behavior. Ensure the manifest and any startup/privilege logic support this; verify and fix code paths so that “no elevation” does not cause an immediate exit.

## Desired Outcome (The "How it Should Be")

- **With elevation (user accepts UAC or “Run as administrator”):** Unchanged: USN monitoring can start; real-time monitoring when configured.
- **Without elevation (user declines UAC or runs normally):** The process starts; the main window is shown; no USN monitoring; indexing uses folder crawling (auto-crawl from settings or HOME, or user-selected folder in Settings > Index Configuration). Behavior is equivalent to macOS/Linux: crawl-based indexing only, no real-time USN.
- No new Sonar/clang-tidy violations; AGENTS document and strict constraints followed. Documentation updated to describe optional elevation for USN and default run without elevation for folder crawl.

## Visual Workflow (Mermaid)

    ```mermaid
    graph TD
        A[User starts app on Windows] --> B{UAC / elevation?}
        B -->|User accepts| C[Process runs elevated]
        B -->|User declines / normal run| D[Process runs as invoker]
        C --> E[AppBootstrap: IsProcessElevated = true]
        D --> F[AppBootstrap: IsProcessElevated = false, running_without_elevation = true]
        E --> G[TryStartUsnMonitor; USN if configured]
        F --> H[Skip USN monitor; log message]
        G --> I[ExecuteApplicationMain: use_usn_monitor or folder crawl]
        H --> I
        I --> J[Show UI; index via USN or folder crawl / auto-crawl]
        J --> K[User can search; Settings can set crawl folder]
        D -.->|Manifest must allow| L[asInvoker so process can start]
        L --> F
    ```

## The Process / Workflow

1. **Identify root cause**
   - Confirm that `resources/windows/app.manifest` uses `requestedExecutionLevel level="requireAdministrator"`. This causes Windows to not start the process when the user declines UAC.
   - Confirm that when the process does run without elevation, existing code in `AppBootstrap_win.cpp` sets `result.running_without_elevation = true`, skips `TryStartUsnMonitor`, and that `main_common.h` uses folder crawl / auto-crawl when `use_usn_monitor` is false.

2. **Change manifest to allow run without elevation**
   - In `resources/windows/app.manifest`, change the requested execution level from `requireAdministrator` to `asInvoker` (or equivalent so the app is not required to run as admin at startup).
   - Ensure the manifest remains valid XML and is still embedded via the build (e.g. resource.rc / CMake as currently used). Do not remove DPI or other settings.
   - Update any comment in the manifest that says the app “requires” administrator for full functionality to state that administrator is optional (for USN real-time monitoring).

3. **Verify bootstrap and main flow when not elevated**
   - In `AppBootstrap_win.cpp`: When `!IsProcessElevated()`, `HandleIndexingWithoutAdmin` must return true so the bootstrap continues (it currently does). No early return that would leave `result` invalid (e.g. window/renderer/settings null).
   - Ensure no other code path assumes elevation or exits immediately when not elevated (e.g. `main_common.h` must not require admin for `IsValid()` or for `ExecuteApplicationMain`). Confirm `security_fatal_error` is only set for privilege-drop failure after USN init, not for “not elevated”.
   - Confirm that when `bootstrap.monitor == nullptr` and there is no `--index-from-file`, `ExecuteApplicationMain` triggers auto-crawl (settings crawl folder or `path_utils::GetUserHomePath()`) and that the Application receives `should_auto_crawl` and `auto_crawl_folder` so indexing starts after UI is shown.

4. **Windows-specific index builder and UI**
   - In `main_common.h`, when `_WIN32` and not `use_usn_monitor`, the folder crawler path must be used (existing logic: `CreateFolderCrawlerIndexBuilder` or auto-crawl). Verify that when `running_without_elevation` and no crawl folder from command line, settings or HOME is used for auto-crawl.
   - Ensure Settings UI (e.g. Index Configuration) shows the appropriate message when not elevated (e.g. “Running without administrator rights. Select a folder below to start indexing…” and “For real-time monitoring, restart as administrator”) and that selecting a folder starts folder crawl. Do not change macOS/Linux code paths.

5. **Documentation and optional elevation**
   - Update `docs/security/SECURITY_MODEL.md` or equivalent to state that on Windows the app runs by default without administrator rights and uses folder crawling; administrator (or “Run as administrator”) is optional for USN Journal real-time monitoring.
   - Optionally add a short note in README or user-facing docs: to get real-time USN monitoring on Windows, run the app as administrator or use the in-app “Restart as administrator” from Settings if available (`RestartAsAdmin`).

6. **Verification**
   - Build the Windows target and ensure the manifest change is applied (e.g. run the app without elevation and confirm the process starts and the main window appears).
   - Manually verify: start without elevation → UI appears; no USN monitor; auto-crawl or Settings folder crawl runs; search works. Start with elevation (e.g. “Run as administrator”) → USN monitoring can start when configured; behavior unchanged.
   - Confirm no new Sonar/clang-tidy issues; follow AGENTS document and strict constraints.

## Anticipated Pitfalls

- **Manifest embedding:** Changing the manifest file must not break resource compilation or linking. Keep the same resource ID and embedding method (e.g. resource.rc reference to app.manifest).
- **Assuming elevation elsewhere:** Do not add new checks that exit or disable features when not elevated except where already designed (e.g. USN is skipped). The goal is to keep full functionality except USN when not elevated.
- **Cross-platform:** Do not change macOS or Linux bootstrap or manifest logic; this task is Windows-only (manifest and Windows bootstrap/flow).
- **Security:** `security_fatal_error` must remain only for the privilege-drop failure after acquiring the volume handle (USN), not for “user ran without elevation”. Running without elevation is a supported mode.

## Acceptance Criteria / Verification Steps

- [ ] `resources/windows/app.manifest` uses `asInvoker` (or equivalent) so the app can start without administrator privileges.
- [ ] When the user declines UAC (or runs the app without “Run as administrator”), the process starts, the main window is shown, and the app does not exit immediately.
- [ ] When running without elevation: USN monitor is not started; indexing uses folder crawl (auto-crawl from settings or HOME, or user-selected folder in Settings); search works. Behavior matches the intended “no USN, folder crawl only” mode as on macOS/Linux.
- [ ] When running with elevation, existing behavior is unchanged: USN monitoring can start when configured; no regressions.
- [ ] No new SonarQube or clang-tidy violations; AGENTS document and docs/prompts/AGENT_STRICT_CONSTRAINTS.md followed.
- [ ] Documentation updated to state that Windows runs without admin by default and that admin is optional for USN real-time monitoring.

## Strict Constraints / Rules to Follow

**Code quality**
- Do not introduce new SonarQube or clang-tidy violations. Apply preferred style (in-class initializers, const refs, no `} if (` on one line) per AGENTS document and docs/prompts/AGENT_STRICT_CONSTRAINTS.md.
- If you must add a suppression, use // NOSONAR only as a last resort, on the same line as the flagged code.

**Platform and Windows**
- Do not modify code inside `#ifdef __APPLE__` or `#ifdef __linux__` for this task. Windows-only changes: manifest and Windows bootstrap/flow verification. Do not change platform-specific blocks to make them “cross-platform”; the goal is to make Windows behave like macOS/Linux when not elevated (folder crawl only).
- When editing code that compiles on Windows: use (std::min) and (std::max) with parentheses; use explicit lambda captures in template functions; match forward declarations to the definition (struct vs class). See AGENTS document.

**Includes**
- Keep all #include directives in the top block of the file. Use lowercase <windows.h> where applicable.

**Build and verification**
- Use the project’s Windows build and test process. Do not run build commands (cmake, make, etc.) unless the task requires it; when it does, use the project’s scripts or standard Windows build steps.
- Verification: confirm app starts without elevation and that folder crawl/auto-crawl and search work; confirm with elevation USN path is unchanged.

## Context and Reference Files

- **Manifest:** resources/windows/app.manifest (requestedExecutionLevel; change requireAdministrator → asInvoker).
- **Windows bootstrap:** src/platform/windows/AppBootstrap_win.cpp (IsProcessElevated, HandleIndexingWithoutAdmin, running_without_elevation, TryStartUsnMonitor).
- **Main flow:** src/core/main_common.h (ExecuteApplicationMain, use_usn_monitor, should_auto_crawl, auto_crawl_folder, CreateFolderCrawlerIndexBuilder).
- **Base result:** src/core/AppBootstrapResultBase.h (IsValid). src/platform/windows/AppBootstrap_win.h (AppBootstrapResult, running_without_elevation, security_fatal_error).
- **Settings UI:** src/ui/SettingsWindow.cpp (elevation message, crawl folder selection).
- **Plans:** docs/plans/features/UI_FOLDER_CRAWL_SELECTION_WINDOWS_ANALYSIS.md, docs/plans/features/AUTO_CRAWL_ON_STARTUP_PLAN.md.
- **Strict constraints:** docs/prompts/AGENT_STRICT_CONSTRAINTS.md.
- **Project rules:** AGENTS document.

Proceed with the task.
```
