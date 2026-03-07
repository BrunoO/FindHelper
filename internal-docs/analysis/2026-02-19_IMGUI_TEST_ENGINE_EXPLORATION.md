# ImGui Test Engine – Relevance and Integration Options

**Date:** 2026-02-19  
**Purpose:** Explore whether ocornut’s official “Dear ImGui Test Engine” is relevant for UI testing in this project and how it compares to the existing Python + test-mode plan.

---

## 1. Verdict: **Relevant and Recommended as a Complement**

The ImGui Test Engine is **relevant** for this project. It fits the existing ImGui + GLFW frame flow, runs in-process with no OS-level automation, and works on **all three platforms** (macOS, Windows, Linux). It is best used **in addition to** (not instead of) the Python keystroke plan when you want fast, cross-platform, in-process UI tests.

---

## 2. What the ImGui Test Engine Provides

- **In-process automation**: No separate process; tests run inside the same binary and inject input via `ImGuiIO` around `ImGui::NewFrame()`.
- **ImGui-native API**: Find windows/items by path (e.g. `"Find Helper/##path"`), then drive mouse/keyboard with `ctx->MouseMove()`, `ctx->ItemClick()`, `ctx->KeyPress()`, `ItemInputValue()`, etc.
- **Assertions**: Helpers like `IM_CHECK`, `IM_CHECK_EQ` to assert on application state from within the test.
- **Test structure**: Each test has an optional `GuiFunc` (render test-only UI) and/or `TestFunc` (script interactions). For testing this app’s existing UI, only `TestFunc` is needed.
- **Coroutine-style TestFunc**: Driver code yields between actions so each action can span multiple frames; no parallelism with the main thread.
- **Headless and fast mode**: Can run without a visible window and with “max app speed” (e.g. skip vsync) for CI.
- **Maintained**: Same author as Dear ImGui; versioning is aligned with ImGui (e.g. 1.87+ required).

---

## 3. Fit with This Codebase

### 3.1. Frame Loop

The app already has a single, clear ImGui frame entry point:

- **`Application::ProcessFrame()`** (e.g. `src/core/Application.cpp`):
  - `renderer_->BeginFrame()`
  - `ImGui_ImplGlfw_NewFrame()`
  - **`ImGui::NewFrame()`**  ← test engine hooks here (PreNewFrame / PostNewFrame)
  - `UIRenderer::RenderMainWindow(...)` / `RenderFloatingWindows(...)`
  - `RenderFrame()` → `ImGui::Render()` + backend present

The test engine’s integration pattern is to run **before** and **after** `ImGui::NewFrame()`: inject simulated IO in the former, run `GuiFunc`/`TestFunc` in the latter. That matches this loop without changing app logic.

### 3.2. ImGui and Backend Version

- **ImGui**: 1.92.6 WIP (from `external/imgui/imgui.h`) → **≥ 1.87** requirement is satisfied.
- **Backends**: The test engine expects backends to use the `io.AddKeyEvent()`, `io.AddMousePosEvent()`, etc. API (1.87+). The project’s backends (GLFW, and platform-specific renderers) follow that pattern where applicable. No change needed for the test engine’s injection to work.

### 3.3. Widget Identification

Tests target items by **window + ID path** (e.g. `ctx->SetRef("Find Helper"); ctx->ItemClick("Button Label")`). In this codebase:

- **Main window**: `ImGui::Begin("Find Helper", ...)` → stable name.
- **Child windows**: e.g. `"Settings"`, `"Search Help"`, metrics/help titles → stable names.
- **Widgets**: Many use **hidden labels** (`"##path"`, `"##filename"`, `"##crawl_folder"`, etc.). ImGui derives IDs from these, so paths like `"Find Helper/##path"` can work. For tables and repeated items, `PushID(row)` / column IDs are used; the test engine can target by path once the tree is stable.

**Recommendation**: When adding or refactoring UI, prefer **stable, unique labels** (or consistent `##id` names) for critical controls so tests do not break when layout changes. No need to change all existing widgets at once; start with the ones used in the first tests (e.g. search input, Search button, Settings opener).

---

## 4. Integration Outline (If You Adopt It)

- **Dependency**: Add the [imgui_test_engine](https://github.com/ocornut/imgui_test_engine) repository (copy or submodule) and include its sources in the build. Keep versions aligned with the existing ImGui copy (1.92.x).
- **ImGui config**: In the same `imconfig.h` (or equivalent) used to build ImGui, add:
  - `#include "imgui_te_imconfig.h"` (or the directives from it), and
  - `#define IMGUI_ENABLE_TEST_ENGINE`
- **Coroutines**: Either implement `ImGuiTestCoroutineInterface` or use the engine’s default by defining `IMGUI_TEST_ENGINE_ENABLE_COROUTINE_STDTHREAD_IMPL 1` (uses `std::thread`; fine for this project).
- **App startup** (e.g. after creating ImGui context and backends):
  - Create test engine context: `ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext()`.
  - Configure `ImGuiTestEngine_GetIO(engine)` (e.g. verbosity).
  - Register tests: `RegisterMyTests(engine)` with `IM_REGISTER_TEST(e, "group", "name")` and `TestFunc` lambdas.
  - Start: `ImGuiTestEngine_Start(engine, ImGui::GetCurrentContext())`.
- **Main loop** (in `ProcessFrame()` or the place that calls `ImGui::NewFrame()`):
  - The wiki shows calling the test engine **around** `ImGui::NewFrame()`; the engine registers hooks so that `ImGui::NewFrame()` triggers PreNewFrame / PostNewFrame internally. Confirm with the latest “Setting Up” instructions (e.g. whether to call `ImGuiTestEngine_PreNewFrame` explicitly).
  - Optionally show the test engine UI: `ImGuiTestEngine_ShowTestEngineWindows()` for interactive test selection during development.
- **Shutdown**: Before destroying the ImGui context, call `ImGuiTestEngine_Stop(engine)` then `ImGuiTestEngine_ShutdownContext(engine)`.

**Guarded by build**: Compile and link the test engine only when a CMake option (e.g. `BUILD_IMGUI_TESTS` or `ENABLE_IMGUI_TEST_ENGINE`) is ON, so production builds stay unchanged.

---

## 5. Comparison with the Python + Test-Mode Plan

| Aspect | ImGui Test Engine | Python + test-mode (macOS) |
|--------|-------------------|----------------------------|
| **Process** | In-process, same binary | Out-of-process, Python drives app |
| **Input** | Injects into `ImGuiIO` (simulated) | Real OS input (e.g. Quartz on macOS) |
| **Platform** | macOS, Windows, Linux (one test codebase) | Primarily macOS (plan doc); Windows/Linux would need separate scripts or tools) |
| **Speed** | Fast; can run headless and at “max speed” | Slower (real process, real input, optional sleeps) |
| **CI** | Easy: run the app with test engine, queue tests, exit with status | Requires running the built app and parsing stdout / TEST: lines |
| **“Real user” fidelity** | Simulates ImGui’s view of input only | Closer to real user (real keyboard/mouse, window manager, focus) |
| **App changes** | Link test engine, optional `--run-imgui-tests` or similar | `--test-mode` + TEST: log lines (and optionally small status API) |
| **Assertions** | Direct C++ (`IM_CHECK`, app state) | Via stdout parsing or future status API |

**Conclusion**: They address different needs. Use **ImGui Test Engine** for fast, cross-platform, in-process UI automation and regression (e.g. “click Search, then assert result count”). Keep **Python + test-mode** for macOS-focused “real user” smoke tests and for cases where you do not want to link or run the test engine (e.g. minimal build).

---

## 6. Suggested Place in the Overall Plan

- **Short term**: Keep implementing the [Python + test-mode plan](docs/analysis/2026-02-19_UI_TESTING_PLAN_PYTHON_TEST_MODE_MACOS.md) for macOS smoke tests and `--test-mode` (deterministic window + TEST: signals). No conflict with the test engine.
- **Next step**: Add the ImGui Test Engine behind an optional build flag. Implement 2–3 tests (e.g. “Launch and quit”, “Run basic search and check result count”, “Open Settings and close”) using `TestFunc` only, targeting existing windows and widgets. Run these on all three platforms in CI where builds exist.
- **Ongoing**: Prefer the test engine for new UI regression tests (faster, cross-platform). Use Python + test-mode for “real input” smoke on macOS and for validations that are easier to express outside the app (e.g. parsing logs, future HTTP/socket status).

---

## 7. References

- [imgui_test_engine](https://github.com/ocornut/imgui_test_engine) – repository and wiki (Overview, Setting Up).
- [Dear ImGui Test Suite & Automation](https://github.com/ocornut/imgui_test_engine/wiki/Overview) – overview and example.
- [Setting Up](https://github.com/ocornut/imgui_test_engine/wiki/Setting-Up) – integration, main loop, coroutines, version requirements.
- Project UI testing plan: `docs/analysis/2026-02-19_UI_TESTING_PLAN_PYTHON_TEST_MODE_MACOS.md`.
