## UI Testing Plan – Python Keystrokes + Test Mode on macOS

**Date:** 2026-02-19  
**Purpose:** Define a concrete, incremental plan to exercise the app’s UI on macOS “like a real user,” using Python-generated keystrokes plus an internal `--test-mode` for deterministic behavior.

---

## 1. Goals and Scope

- **Primary goal**: Add a lightweight, automatable UI smoke-test layer on macOS that:
  - Launches the existing mac build of the app.
  - Drives it via real keyboard shortcuts (through macOS events).
  - Verifies that key user flows succeed (via logs or simple status hooks).
- **Out of scope (for now)**:
  - Deep functional coverage of every UI feature.
  - Windows-only / platform-specific behavior (still covered by unit/integration tests and Windows runs).

Initial flows to cover:
- **Smoke 1**: Launch → wait for main window → clean quit.
- **Smoke 2**: Launch → run a basic search via shortcuts → quit.
- **Smoke 3**: Launch → open settings via shortcut → toggle a simple option → quit.

The plan is explicitly incremental: get these smokes stable first, then extend.

---

## 2. App `--test-mode` (Deterministic Behavior)

We add a `--test-mode` command-line flag and related hooks inside the C++ app. This mode should be available in all builds but only do “test-specific” behavior in debug/test builds if needed.

### 2.1. Flag Parsing and Propagation

- Extend the existing command-line parsing to recognize:
  - `--test-mode`
  - Optionally: `--test-scenario <name>` (for future, scenario-specific behavior).
- Store the state in central app structures:
  - A `bool test_mode_` in the main `Application` (or equivalent), and/or
  - A flag in `AppSettings` / `GuiState` that is visible in the UI renderer.

### 2.2. Deterministic Window and Layout

When `test_mode_` is true:
- Force a **fixed window size** (for example, 1280×720).
- Force a **fixed window position** (e.g. top-left or centered).
- Disable any non-essential variability that can move or resize UI elements unexpectedly (animations, dynamic initial placement heuristics, etc.).

Rationale:
- Coordinate-based input (and screenshots later) becomes repeatable.

### 2.3. Deterministic Data and State

In `--test-mode`:
- Use a small, well-known data set:
  - Point indexing/search at a tiny test folder (for example under `tests/data/ui_smoke/`) checked into the repo.
  - Alternative: prebuild a tiny in-memory index if that is simpler than managing a physical folder.
- Force known defaults:
  - Fixed theme, font size, language, and any other options that affect layout.
  - Disable auto-opening of previously used folders, restore sessions, etc., so every run starts from the same state.

Rationale:
- Reduces flakiness and makes it easier to reason about “what the UI should look like” for each scenario.

### 2.4. Minimal Internal Test Signals

To observe UI-level events from tests without brittle pixel inspection:
- In `--test-mode`, emit **structured log lines** on key events, for example:
  - `TEST: Ready` when the main window is fully initialized and ready for input.
  - `TEST: SearchStarted <query>`
  - `TEST: SearchComplete <query> results=<count>`
  - `TEST: SettingsOpened`
  - `TEST: SettingsClosed`
- These should go to stdout/stderr so the Python harness can read them via the child process’ pipes.

Future extension (optional, not required for first iteration):
- Add a tiny local status API (e.g. TCP/Unix domain socket) that returns a short JSON snapshot of key state, such as:
  - Current active view (`"Main"`, `"Settings"`, etc.).
  - Last search query and result count.
  - Whether a modal popup is currently open.

---

## 3. Python UI Harness on macOS

We build a small Python-based harness that:
- Launches the app with `--test-mode`.
- Waits for readiness by parsing `TEST:` log lines.
- Sends keystrokes using macOS Quartz events.
- Asserts on simple conditions (e.g. presence of specific `TEST:` lines, process exit code).

### 3.1. Location and Dependencies

- Place scripts under something like:
  - `scripts/ui_tests/`
- Define Python dependencies (for example in `requirements.txt`):
  - `pyobjc` (for Quartz / CGEvent access).
  - Optionally `Pillow` (or similar) for future screenshot-based checks.

Example files:
- `scripts/ui_tests/run_ui_smoke_macos.py` – entry point for running all smoke tests.
- `scripts/ui_tests/input.py` – low-level input helpers (keystrokes, focus, delays).
- `scripts/ui_tests/scenarios.py` – higher-level “user flows” composed from keystrokes.

### 3.2. Launching the App Under Test

In `run_ui_smoke_macos.py`:
- Use `subprocess.Popen` to start the app, for example:
  - Via the app bundle:
    - `open -n /path/to/App.app --args --test-mode`
  - Or directly via the executable:
    - `/path/to/binary --test-mode`
- Capture `stdout` and `stderr`:
  - For detecting readiness.
  - For reading `TEST:` sentinel lines used by assertions.

Provide a small helper:
- `launch_app()`:
  - Starts the process.
  - Calls `wait_for_ready()` (see below).
  - Returns a Python object with:
    - The `Popen` instance.
    - A helper to read/peek buffered log lines.

### 3.3. Waiting for “Ready”

Implement `wait_for_ready(max_seconds)`:
- Loop until timeout:
  - Read from the child process’s `stdout` (non-blocking or with short timeouts).
  - Check for the presence of `TEST: Ready`.
  - Abort early if the process exits with a non-zero status.
- If timeout is reached without seeing `TEST: Ready`, consider the test failed.

Rationale:
- Avoids arbitrary fixed sleeps and makes tests fail fast when startup is broken.

### 3.4. Keystroke Injection via Quartz / CGEvent

In `input.py`:
- Use `pyobjc` and the Quartz APIs to send keyboard events:
  - `CGEventCreateKeyboardEvent`
  - `CGEventPost`
- Provide higher-level helpers:
  - `send_key(key_code, modifiers=None)` – sends a single key press + release.
  - `type_text(text)` – types arbitrary text via key events.
- Before sending keystrokes:
  - Bring the app to the foreground:
    - Either via `osascript` (“activate application …”) using `subprocess.run`.
    - Or via Quartz APIs to find and focus the main window (can be a later improvement).

The tests should rely on **real shortcuts** that users use, for example:
- Focus search: `Cmd+F` (or whatever the current binding is).
- Execute search: `Enter`.
- Open settings: `Cmd+,` (if that is the pattern you use).
- Quit app: standard mac shortcut (usually `Cmd+Q`), or a dedicated test shortcut.

---

## 4. Scenario DSL (High-Level Actions)

To keep tests maintainable, build simple higher-level actions on top of keystrokes.

In `scenarios.py`, define functions like:

- `launch_app()`:
  - Wraps `subprocess.Popen` + `wait_for_ready`.

- `run_basic_search(query: str)`:
  - Ensures the app window is active.
  - Sends the “focus search” shortcut.
  - Calls `type_text(query)`.
  - Sends `Enter`.
  - Waits for a `TEST: SearchComplete` line (optionally parsing out a `results=<count>`).

- `open_settings()`:
  - Sends the settings shortcut.
  - Waits for `TEST: SettingsOpened`.

- `close_app()`:
  - Sends quit shortcut (e.g. `Cmd+Q`).
  - Waits for the process to exit and checks the exit code is 0.

Tests (in `run_ui_smoke_macos.py`) then read like:
- **Smoke 1 – Launch and Quit**
  - `app = launch_app()`
  - `close_app(app)`
- **Smoke 2 – Basic Search**
  - `app = launch_app()`
  - `run_basic_search("needle")`
  - Assert that at least one `TEST: SearchComplete` line was seen.
  - `close_app(app)`
- **Smoke 3 – Settings Toggle**
  - `app = launch_app()`
  - `open_settings()`
  - (Optional) Navigate via keys to a simple toggle and flip it.
  - Assert that a log line like `TEST: SettingChanged <name>=<value>` appears.
  - `close_app(app)`

---

## 5. Integration with Existing macOS Workflows

We keep UI smokes clearly separated from the C++ test suite.

### 5.1. macOS Test Entry Points

- Continue to use `scripts/build_tests_macos.sh` as the **only** entry point for building and running C++ tests (per project rules).
- Add a new script, for example:
  - `scripts/run_ui_smoke_macos.sh`:
    - Assumes the app is already built, or triggers the existing build step if appropriate.
    - Runs `python3 scripts/ui_tests/run_ui_smoke_macos.py`.
    - Exits with a non-zero status if any smoke scenario fails.

### 5.2. When to Run UI Smokes

- On developer machines:
  - Before cutting a release or merging a larger feature branch.
  - When fixing a UI-specific bug (add a targeted scenario and run it locally).
- On CI (if mac runners are available):
  - After `scripts/build_tests_macos.sh` passes, as a separate stage.
  - Fail the pipeline if UI smokes fail, but keep their scope small and stable to avoid noise.

---

## 6. ImGui Test Engine (Complement)

As a **complement** to this Python + test-mode plan, consider the official **Dear ImGui Test Engine** (ocornut/imgui_test_engine). It provides in-process, cross-platform UI automation by injecting input into `ImGuiIO` around `ImGui::NewFrame()` and scripting interactions via a C++ API (`SetRef`, `ItemClick`, `KeyPress`, `IM_CHECK`, etc.). Tests run in the same binary on macOS, Windows, and Linux; they can run headless and at “max speed” for CI.

- **Use it for**: Fast, cross-platform UI regression tests (e.g. “click Search, assert result count”) without OS-level automation.
- **Keep Python + test-mode for**: macOS “real user” smoke tests (real keystrokes, `TEST:` log parsing) and when the test engine is not built or not desired.

Full exploration (relevance, integration outline, widget IDs, comparison): **`docs/analysis/2026-02-19_IMGUI_TEST_ENGINE_EXPLORATION.md`**.

---

## 7. Future Extensions (After Initial Smokes Are Stable)

Once the first 2–3 scenarios are reliable, we can extend the system in small, focused steps.

### 7.1. Status API for Stronger Assertions

- Add a local status endpoint in `--test-mode`:
  - Example: a small TCP or Unix domain socket server that accepts:
    - `GET state` → returns JSON with:
      - `view`, `last_query`, `result_count`, `has_modal`, etc.
  - Python tests can then assert directly on state:
    - `assert state["result_count"] >= 1`
    - `assert state["view"] == "Settings"`

### 7.2. Visual / Screenshot Regression Checks

- Add a small helper in Python to:
  - Capture a screenshot of the app window once it is in a known state.
  - Compare it against a stored “golden” image with a small pixel tolerance.
- Use this sparingly for UI layout/regression-sensitive areas (e.g. main results table, settings layout).

### 7.3. Scenario Parameterization and Tagging

- Allow `run_ui_smoke_macos.py` to accept CLI arguments:
  - `--scenario basic_search`, `--scenario settings`, `--all`.
- Tag tests by “speed” or “criticality” so CI can run a smaller subset on every commit and a larger set on nightly builds.

---

## 8. Summary

This plan introduces a **small, deterministic test mode** inside the app and a **Python-based keystroke harness** on macOS.  
Short term, it gives us 2–3 reliable UI smoke tests that exercise the app “like a real user” using actual keyboard shortcuts.  
Longer term, the same infrastructure can be extended with a lightweight status API and optional screenshot-based regression tests, without coupling UI verification too tightly to pixel-perfect layouts or platform-specific tools.

