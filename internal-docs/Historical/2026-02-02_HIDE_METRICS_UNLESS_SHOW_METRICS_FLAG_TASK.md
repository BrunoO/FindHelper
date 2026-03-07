# Task: Hide Metrics Button Unless --show-metrics Flag

**Date:** 2026-02-02  
**Source:** Taskmaster prompt; goal = hide the metrics button for users unless a command-line flag `--show-metrics` is present.  
**Reference:** `docs/prompts/TaskmasterPrompt.md`, `AGENTS document`, `src/core/CommandLineArgs.h`, `src/core/CommandLineArgs.cpp`, `src/core/Application.h`, `src/core/Application.cpp`, `src/ui/UIRenderer.h`, `src/ui/UIRenderer.cpp`, `src/ui/FilterPanel.h`, `src/ui/FilterPanel.cpp`, `docs/prompts/AGENT_STRICT_CONSTRAINTS.md`

---

## Summary

The application currently shows a "Metrics" button in the UI that toggles a floating metrics window (USN/journal metrics, search performance, FPS, etc.). The goal is to hide this metrics UI from normal users and only show it when the user explicitly passes `--show-metrics` on the command line. No change to metrics collection or logic—only visibility of the button and window is gated by the flag. Implementation involves: (1) adding a `--show-metrics` flag to command-line parsing and storing it in `CommandLineArgs`; (2) initializing application/UI state from that flag so the Metrics button and Metrics window are only available when the flag is set; (3) updating help text to document the new option.

***

```markdown
# Prompt: Hide Metrics Button Unless --show-metrics Flag

## Mission Context (The "Why")

Users see a "Metrics" button that opens a technical metrics window (USN monitoring, search performance, FPS, thread count, etc.). This is intended for power users and debugging. For normal users, the metrics button and window should be hidden unless they opt in via a command-line flag `--show-metrics`.

## Core Objective (The "What")

Add a command-line flag `--show-metrics` and hide the Metrics button and Metrics window from the UI unless that flag is present. When the flag is present, behavior remains as today (button toggles the metrics window).

## Desired Outcome (The "How it Should Be")

- **Without `--show-metrics`:** The Metrics button is not shown in the UI. The Metrics window is never shown. No metrics-related UI is visible. All other behavior (Settings, Search Help, search, indexing) is unchanged.
- **With `--show-metrics`:** The Metrics button is visible and toggles the Metrics window as it does today. No other behavior change.
- Help text (`--help`) documents the new `--show-metrics` option.
- No new Sonar/clang-tidy violations; AGENTS document and strict constraints followed.

## Visual Workflow (Mermaid)

    ```mermaid
    graph TD
        A[App start] --> B{Parse argv}
        B --> C[CommandLineArgs.show_metrics = true if --show-metrics]
        B --> D[CommandLineArgs.show_metrics = false otherwise]
        C --> E[Application: metrics_available = true]
        D --> F[Application: metrics_available = false]
        E --> G[UI: show Metrics button and allow Metrics window]
        F --> H[UI: hide Metrics button; do not render Metrics window]
        G --> I[User can toggle Metrics window]
        H --> J[No metrics UI]
    ```

## The Process / Workflow

1. **Command-line parsing**
   - In `CommandLineArgs.h`, add a member `bool show_metrics = false;` to the `CommandLineArgs` struct (e.g. after `show_version` / `exit_requested` or with other boolean flags).
   - In `CommandLineArgs.cpp`, in `ParseCommandLineArgs()`, add handling for the flag `--show-metrics` (no value): when present, set `args.show_metrics = true`. Use the same pattern as `--help` / `--version` (e.g. `strcmp(arg, "--show-metrics") == 0`).
   - In `CommandLineArgs.cpp`, in `ShowHelp()`, add a line documenting the new option, e.g. `"  --show-metrics              Show Metrics button and window (for power users / debugging)\n"`.

2. **Application state**
   - In `Application`, the existing `show_metrics_` (std::atomic<bool>) controls whether the Metrics window is open. You need a second, immutable (after construction) notion: "metrics UI is available." Add a member such as `bool metrics_available_` (or `show_metrics_ui_`) and initialize it in the constructor from `cmd_args.show_metrics`. Do not change the meaning of `show_metrics_` (it remains the window open/closed state when metrics are available).
   - When building the render context for the UI (e.g. `RenderMainWindowContext` and `RenderFloatingWindowsContext`), pass this "metrics available" flag so the UI can hide the Metrics button and skip rendering the Metrics window when it is false.

3. **UI context and rendering**
   - Add a `bool metrics_available` (or equivalent name) to `RenderMainWindowContext` and `RenderFloatingWindowsContext` in `UIRenderer.h`. In `Application.cpp`, when constructing these contexts, set `metrics_available` from the Application’s `metrics_available_` (i.e. from command line).
   - In `UIRenderer::RenderFloatingWindows`: when `!context.metrics_available`, do not call `MetricsWindow::Render` and do not update `show_metrics` for the metrics window. When `metrics_available` is false, the Metrics window should never be shown.
   - In the code path that renders the Metrics/Settings buttons (e.g. `FilterPanel::RenderApplicationControls` and any similar place such as `UIRenderer::RenderManualSearchHeader`), pass the "metrics available" flag. When it is false, draw only the Settings button (do not draw the Metrics button). When true, draw both buttons as today. Adjust layout (e.g. right-align) so that with one button the layout still looks correct.
   - Ensure any other call site that renders the Metrics button (e.g. SearchInputs or manual search header) also respects `metrics_available` so the Metrics button is never visible when the flag was not passed.

4. **Verification**
   - Build and run without `--show-metrics`: confirm the Metrics button is not visible and the Metrics window never appears.
   - Build and run with `--show-metrics`: confirm the Metrics button is visible and toggles the Metrics window as before.
   - Run with `--help` and confirm `--show-metrics` is listed.
   - Confirm no new Sonar/clang-tidy issues; follow AGENTS document and strict constraints.

## Anticipated Pitfalls

- **Two concepts:** Do not confuse "metrics window is open" (`show_metrics_`) with "metrics UI is available" (`metrics_available_` from CLI). The first is toggled by the user when the feature is available; the second is fixed at startup from the command line.
- **Layout:** When the Metrics button is hidden, the Settings button alone should still be right-aligned (or laid out correctly). Reuse existing layout logic; only the number of buttons changes.
- **All call sites:** Every place that draws the Metrics button or the Metrics window must respect `metrics_available`. Check FilterPanel, UIRenderer (RenderManualSearchHeader, RenderFloatingWindows), and any SearchInputs path that shows Settings/Metrics.
- **Platform:** This is cross-platform (Windows, macOS, Linux). No platform-specific #ifdef is required for the flag or UI visibility.

## Acceptance Criteria / Verification Steps

- [ ] `CommandLineArgs` has a `show_metrics` (or equivalent) member; `--show-metrics` sets it to true when present.
- [ ] `ShowHelp()` documents `--show-metrics`.
- [ ] When the app is run without `--show-metrics`, the Metrics button is not visible anywhere in the UI.
- [ ] When the app is run without `--show-metrics`, the Metrics window is never rendered/displayed.
- [ ] When the app is run with `--show-metrics`, the Metrics button is visible and toggles the Metrics window as before.
- [ ] No new SonarQube or clang-tidy violations; AGENTS document and docs/prompts/AGENT_STRICT_CONSTRAINTS.md followed.

## Strict Constraints / Rules to Follow

**Code quality**
- Do not introduce new SonarQube or clang-tidy violations. Apply preferred style (in-class initializers, const refs, no `} if (` on one line) per AGENTS document and docs/prompts/AGENT_STRICT_CONSTRAINTS.md.
- If you must add a suppression, use // NOSONAR only as a last resort, on the same line as the flagged code.

**Platform and Windows**
- When editing code that compiles on Windows: use (std::min) and (std::max) with parentheses; use explicit lambda captures in template functions; match forward declarations to the definition (struct vs class). See AGENTS document.

**Includes**
- Keep all #include directives in the top block of the file. Use lowercase <windows.h> where applicable.

**Build and verification**
- Use the project’s build and test process. On macOS, use scripts/build_tests_macos.sh when the task requires running tests. Do not run cmake/make directly unless the task explicitly requires it.

## Context and Reference Files

- **Command line:** src/core/CommandLineArgs.h (add show_metrics), src/core/CommandLineArgs.cpp (parse --show-metrics, ShowHelp).
- **Application:** src/core/Application.h (add metrics_available_ or equivalent), src/core/Application.cpp (initialize from cmd_args, pass to UI context).
- **UI:** src/ui/UIRenderer.h (add metrics_available to RenderMainWindowContext and RenderFloatingWindowsContext), src/ui/UIRenderer.cpp (RenderFloatingWindows skip Metrics when !metrics_available; RenderManualSearchHeader pass flag to RenderApplicationControls), src/ui/FilterPanel.h, src/ui/FilterPanel.cpp (RenderApplicationControls: add metrics_available parameter, draw Metrics button only when true).
- **Strict constraints:** docs/prompts/AGENT_STRICT_CONSTRAINTS.md.
- **Project rules:** AGENTS document.

Proceed with the task.
```
