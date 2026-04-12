# Windows Startup Flow

This document describes the Windows startup sequence for FindHelper. It helps contributors understand where platform-specific behavior runs and how it ties into the common entry point.

## Entry Point and Common Template

- **`src/platform/windows/main_win.cpp`** — Windows `main()` entry. It defines `WindowsBootstrapTraits` (Initialize/Cleanup delegates to `AppBootstrap`) and calls the shared template:

  `RunApplication<AppBootstrapResult, WindowsBootstrapTraits>(argc, argv)`

- **`src/core/main_common.h`** — Shared logic for all platforms. It:

  1. Installs the terminate handler and logs build config (Boost vs std).
  2. Parses command line into `CommandLineArgs`; handles `--help` / `--version` and returns.
  3. Creates `FileIndex` and `IndexBuildState`.
  4. **Windows only:** If no `--crawl-folder` and process is not elevated, offers “Restart as administrator” via `PromptForElevationAndRestart(nullptr)`. If the user accepts, we call `RestartAsAdmin()` (ShellExecuteEx “runas”), which triggers the system UAC dialog; then this process exits with 0.
  5. Calls **BootstrapTraits::Initialize** (on Windows: `AppBootstrap::Initialize`).
  6. Checks `bootstrap.IsValid()`.
  7. **Windows only:** If `bootstrap.security_fatal_error` (e.g. privilege drop failure), cleans up and returns 1.
  8. Runs **ExecuteApplicationMain** (index builder choice, start/defer index build, create `Application`, `app.Run()`).
  9. On exit, **BootstrapTraits::Cleanup** (stop monitor, ImGui/GLFW, COM on Windows).

Users who accept the in-app “Restart as administrator” see two dialogs (app prompt then UAC). To see only UAC, they can right-click the exe → Run as administrator.

## Windows Bootstrap

- **`src/platform/windows/AppBootstrap_win.cpp`** — Windows-specific initialization:

  1. GLFW error callback and `glfwInit()`.
  2. Log CPU info (via `AppBootstrapCommon`).
  3. Initialize COM/OLE for Shell APIs.
  4. If not elevated: set `running_without_elevation` and log; indexing will use folder crawl or index-from-file in the common flow.
  5. Load index from file if `--index-from-file` (via `AppBootstrapCommon`).
  6. Load settings and apply command-line overrides.
  7. Create GLFW window (NO_API for DirectX), set icon, create DirectX device/swap chain, set window resize callback.
  8. Initialize ImGui context and GLFW/DX11 backends; apply theme and fonts.
  9. Show window.
  10. If elevated: start `UsnMonitor` (NTFS check, volume path, privilege drop). On privilege-drop failure set `security_fatal_error`.
  11. Store `result.settings`, `result.renderer`, `result.monitor`, etc.

- **`src/core/AppBootstrapCommon.h`** — Shared bootstrap helpers: CPU logging, GLFW callback, window creation, index-from-file loading, ImGui/GLFW cleanup.

- **`src/platform/windows/ShellContextUtils.cpp`** — `IsProcessElevated()` (used in Settings UI and in main_common for the elevation prompt); `PromptForElevationAndRestart(HWND)` (used at startup when not elevated and no `--crawl-folder`; triggers RestartAsAdmin → UAC).

## Index Building and Application Run

Index builder choice and startup happen in **`main_common_detail::ExecuteApplicationMain`** (in `main_common.h`):

- On Windows, `use_usn_monitor` is true only when `bootstrap.monitor != nullptr` and there is no explicit crawl folder.
- The appropriate `IIndexBuilder` (USN-based or folder crawler) is created; index build is started or deferred (folder crawl after first frame).
- **`Application`** is constructed with the bootstrap result and index builder, then **`app.Run()`** runs the main loop.

Shutdown: **BootstrapTraits::Cleanup** stops the USN monitor, tears down ImGui/GLFW, and uninitializes COM.

## File Reference

| File | Role |
|------|------|
| `src/platform/windows/main_win.cpp` | Entry point; `WindowsBootstrapTraits`; calls `RunApplication<>`. |
| `src/core/main_common.h` | CLI, elevation prompt (Windows), bootstrap call, security check, `ExecuteApplicationMain`, cleanup. |
| `src/platform/windows/AppBootstrap_win.cpp` | GLFW, COM, window, DirectX, ImGui, USN monitor, settings. |
| `src/core/AppBootstrapCommon.h` | CPU log, GLFW callback, window creation, index-from-file, ImGui/GLFW cleanup. |
| `src/platform/windows/ShellContextUtils.cpp` | `IsProcessElevated`, `PromptForElevationAndRestart`. |
| `src/core/Application.cpp` | `Application` constructor and `Run()` main loop. |

## Related

- **Architecture:** `docs/design/ARCHITECTURE_COMPONENT_BASED.md`
- **Internal analysis:** `internal-docs/analysis/2026-03-07_WINDOWS_STARTING_FLOW_FEEDBACK.md`
