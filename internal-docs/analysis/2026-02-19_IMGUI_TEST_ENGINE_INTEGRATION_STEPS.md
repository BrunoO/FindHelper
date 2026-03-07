# ImGui Test Engine – Integration Steps (No External Tools)

**Date:** 2026-02-19  
**Purpose:** Step-by-step plan to add the official Dear ImGui Test Engine to the app so UI tests run in-process, with no Python or OS-level automation. All modifications are in C++ and CMake.

---

## 1. Overview of Changes

| Area | What changes |
|------|----------------|
| **Dependency** | `imgui_test_engine` is provided by a **git submodule** at `external/imgui_test_engine/` (see `.gitmodules`). After a fresh clone, run `git submodule update --init --recursive`. |
| **ImGui config** | Enable test engine in `imconfig` and optional coroutine/capture defines. |
| **CMake** | New option `ENABLE_IMGUI_TEST_ENGINE`; when ON, add test engine sources and includes to the app target. |
| **Bootstrap result** | Optional `imgui_test_engine` pointer in the base result; create engine after ImGui backends, stop/destroy in platform Cleanup. |
| **Application** | Use engine in `ProcessFrame()` (PostSwap, show test engine windows); register tests in ctor. Optional: build with perftool to show perf window. |
| **Cleanup** | In each platform’s `Cleanup()`, call Stop then run existing ImGui/GLFW cleanup, then DestroyContext(engine). |

---

## 2. Test Engine Dependency (Git Submodule)

The project uses a **git submodule** for the test engine:

- **Submodule path:** `external/imgui_test_engine/`
- **URL:** [ocornut/imgui_test_engine](https://github.com/ocornut/imgui_test_engine) (see `.gitmodules`).
- **Library location:** The library lives at `external/imgui_test_engine/imgui_test_engine/` (sources: `imgui_te_*.cpp`, `imgui_te_*.h`, `imgui_te_imconfig.h`).

**After a fresh clone of the main repo**, initialize and fetch the submodule:

```bash
git submodule update --init --recursive
```

Align the submodule with your ImGui version (e.g. 1.92.x) if needed by checking out a matching tag or commit inside `external/imgui_test_engine/`.

For integration you only need the **library** part:

- **Required:** Everything under `imgui_test_engine/imgui_test_engine/` (sources and headers).
- **Not needed for basic integration:** `app_minimal/`, `imgui_test_suite/`, `shared/`, `build_scripts/` (reference only).

---

## 3. ImGui Compile-Time Config

The test engine requires ImGui to be built with `IMGUI_ENABLE_TEST_ENGINE` and optionally other defines. Your app already uses a custom config via `IMGUI_USER_CONFIG` → `src/imgui_config.h`.

### 3.1. Include the test engine config

In **`src/imgui_config.h`** (after the existing `#include "../external/imgui/imconfig.h"`), add a guarded block so the test engine is only enabled when the CMake option is ON:

```cpp
// Include the default imconfig.h first
#include "../external/imgui/imconfig.h"

#ifdef ENABLE_IMGUI_TEST_ENGINE
#include "../external/imgui_test_engine/imgui_test_engine/imgui_te_imconfig.h"
// Optional: use std::thread coroutine implementation (recommended)
#define IMGUI_TEST_ENGINE_ENABLE_COROUTINE_STDTHREAD_IMPL 1
// Optional: enable screen capture (default 1 in imgui_te_imconfig.h)
// #define IMGUI_TEST_ENGINE_ENABLE_CAPTURE 1
#endif

//---- Enable FreeType ...
#define IMGUI_ENABLE_FREETYPE
```

- `imgui_te_imconfig.h` sets `IMGUI_ENABLE_TEST_ENGINE` and other engine options.
- `IMGUI_TEST_ENGINE_ENABLE_COROUTINE_STDTHREAD_IMPL 1` uses the engine’s built-in `std::thread` coroutine implementation so you don’t implement `ImGuiTestCoroutineInterface` yourself.

### 3.2. Path

If you put the repo under a different path (e.g. `external/imgui_test_engine` with the inner folder named `imgui_test_engine`), adjust the include to match, e.g.:

`#include "../external/imgui_test_engine/imgui_test_engine/imgui_te_imconfig.h"`

---

## 4. CMake Changes

### 4.1. Option

Near other options (e.g. `BUILD_APPLICATION`, `ENABLE_MFT_METADATA_READING`):

```cmake
option(ENABLE_IMGUI_TEST_ENGINE "Build with Dear ImGui Test Engine for in-process UI tests" OFF)
```

### 4.2. Test engine source list

Define the list of test engine sources when the option is ON (path relative to your project root):

```cmake
if(ENABLE_IMGUI_TEST_ENGINE)
  set(IMGUI_TEST_ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/imgui_test_engine/imgui_test_engine")
  set(IMGUI_TEST_ENGINE_SOURCES
    ${IMGUI_TEST_ENGINE_DIR}/imgui_te_context.cpp
    ${IMGUI_TEST_ENGINE_DIR}/imgui_te_engine.cpp
    ${IMGUI_TEST_ENGINE_DIR}/imgui_te_ui.cpp
    ${IMGUI_TEST_ENGINE_DIR}/imgui_te_utils.cpp
    ${IMGUI_TEST_ENGINE_DIR}/imgui_te_coroutine.cpp
    ${IMGUI_TEST_ENGINE_DIR}/imgui_te_exporters.cpp
    ${IMGUI_TEST_ENGINE_DIR}/imgui_capture_tool.cpp
  )
  # Optional: perf window (record/compare frame times). Requires ImPlot; enable IMGUI_TEST_ENGINE_ENABLE_IMPLOT in imconfig and add:
  # list(APPEND IMGUI_TEST_ENGINE_SOURCES ${IMGUI_TEST_ENGINE_DIR}/imgui_te_perftool.cpp)
endif()
```

### 4.3. Per-platform app target (Windows, macOS, Linux)

For each branch where you build the GUI app (e.g. `if(BUILD_APPLICATION)` then `if(WIN32)` / `elseif(APPLE)` / `elseif(UNIX AND NOT APPLE)`):

1. **Add sources:** Append `IMGUI_TEST_ENGINE_SOURCES` to the target’s source list when `ENABLE_IMGUI_TEST_ENGINE` is ON.
2. **Include directory:** Add the test engine directory so you can `#include "imgui_te_engine.h"` etc.:
   - `target_include_directories(<target> PRIVATE ${IMGUI_TEST_ENGINE_DIR})` when `ENABLE_IMGUI_TEST_ENGINE` is ON.
3. **Compile definition:** So `src/imgui_config.h` can `#ifdef ENABLE_IMGUI_TEST_ENGINE` and include the engine config:
   - `target_compile_definitions(<target> PRIVATE ENABLE_IMGUI_TEST_ENGINE)` when the option is ON.

Apply the same three (sources, include dir, define) to the **find_helper** (or main app) target on Windows, and to the macOS and Linux app targets if they are separate. Do not add the test engine to test-only targets unless they actually link and run the GUI app with the engine.

---

## 5. Bootstrap Result and Engine Lifecycle

The engine must be created **after** ImGui context and backends are initialized, and destroyed **after** `ImGui::DestroyContext()` (see test engine wiki). So the engine is created in the platform bootstrap and shut down in the platform Cleanup.

### 5.1. Add optional engine pointer to bootstrap result

In **`src/core/AppBootstrapResultBase.h`**:

- Add a forward declaration (when the define is set) and a member to hold the engine pointer. Use an opaque pointer so the base header does not include any test engine headers:

```cpp
// Optional: Dear ImGui Test Engine (only when ENABLE_IMGUI_TEST_ENGINE)
#ifdef ENABLE_IMGUI_TEST_ENGINE
struct ImGuiTestEngine;
#endif

struct AppBootstrapResultBase {
  // ... existing members ...
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes)
  int* last_window_height = nullptr;

#ifdef ENABLE_IMGUI_TEST_ENGINE
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes)
  ImGuiTestEngine* imgui_test_engine = nullptr;  // Created in bootstrap, destroyed in platform Cleanup
#endif

  // ...
};
```

### 5.2. Create and start the engine in platform Initialize

After ImGui context and backends are initialized (and before the window is shown / before returning the result), when `ENABLE_IMGUI_TEST_ENGINE` is defined:

1. Create: `ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();`
2. Configure: e.g. `ImGuiTestEngine_GetIO(engine)` and set `ConfigVerboseLevel`, `ConfigVerboseLevelOnError`, optionally `ConfigRunSpeed` (e.g. `ImGuiTestRunSpeed_Fast` for CI).
3. Start: `ImGuiTestEngine_Start(engine, ImGui::GetCurrentContext());`
4. Optional (compile-time gated): `ImGuiTestEngine_InstallDefaultCrashHandler();` — only compiled when CMake option `IMGUI_TEST_ENGINE_INSTALL_CRASH_HANDLER` is ON. Operators enable/disable at build time via `-DIMGUI_TEST_ENGINE_INSTALL_CRASH_HANDLER=ON/OFF`; no runtime flag.
5. Store in result: `result.imgui_test_engine = engine;`

**Do not** register tests here—do that in `Application` so tests can use app state if needed. So in bootstrap you only create, configure, and start the engine.

**Where to add this (per platform):**

- **Windows (**`AppBootstrap_win.cpp`**):** In the success path after `InitializeImGuiBackend()` (and any font/theme setup). Include `imgui_te_engine.h` and `imgui_te_ui.h` at the top when `ENABLE_IMGUI_TEST_ENGINE` is defined.
- **macOS (**`AppBootstrap_mac.mm`**):** After Metal and ImGui backends are initialized and font settings applied, before `glfwShowWindow(window)`.
- **Linux (**`AppBootstrap_linux.cpp`**):** After OpenGL and ImGui backends are initialized.

If you prefer to keep bootstrap code minimal, you can factor “create and start test engine” into a small helper in `AppBootstrapCommon.h` (e.g. `InitializeImGuiTestEngine(ImGuiTestEngine*& out_engine)`) that is implemented in a single .cpp that includes the test engine headers, and call that from each platform; the helper would only be compiled when `ENABLE_IMGUI_TEST_ENGINE` is defined.

### 5.3. Shutdown in platform Cleanup

The wiki requires: **first** stop the engine, **then** destroy the ImGui context, **then** destroy the test engine context. Your current flow is: platform `Cleanup()` calls `AppBootstrapCommon::CleanupImGuiAndGlfw(result)`, which does renderer shutdown, `ImGui_ImplGlfw_Shutdown()`, `ImGui::DestroyContext()`, then renderer cleanup, window destroy, `glfwTerminate()`.

So in each platform’s `Cleanup()` (Windows, macOS, Linux), when `ENABLE_IMGUI_TEST_ENGINE` is defined:

1. Read the engine pointer: `ImGuiTestEngine* engine = result.imgui_test_engine` (cast from `result.imgui_test_engine` if stored as opaque).
2. If non-null: `ImGuiTestEngine_Stop(engine);` (may block until test coroutine exits).
3. Call the **existing** `AppBootstrapCommon::CleanupImGuiAndGlfw(result)` (which destroys the ImGui context).
4. If engine was non-null: `ImGuiTestEngine_DestroyContext(engine); result.imgui_test_engine = nullptr;`

Include the test engine header only in the .cpp that implements `Cleanup()` so the base and common code stay free of test engine includes.

---

## 6. Application: Use Engine in Frame and Register Tests

### 6.1. Hold the engine pointer in Application

In **`src/core/Application.h`**, add an optional engine pointer (only when the define is set), e.g.:

```cpp
#ifdef ENABLE_IMGUI_TEST_ENGINE
struct ImGuiTestEngine;
#endif

class Application : public ui::UIActions {
  // ...
private:
  // ...
#ifdef ENABLE_IMGUI_TEST_ENGINE
  ImGuiTestEngine* imgui_test_engine_ = nullptr;  // Not owned; created/destroyed in bootstrap/cleanup
#endif
};
```

In the **constructor**, when `ENABLE_IMGUI_TEST_ENGINE` is defined, set `imgui_test_engine_ = bootstrap.imgui_test_engine` (with appropriate cast if the base stores it as `void*`). Then, if `imgui_test_engine_ != nullptr`, call a registration function, e.g. `RegisterFindHelperTests(imgui_test_engine_);`.

### 6.2. ProcessFrame: PostSwap and test engine UI (test window + optional perf window)

In **`src/core/Application.cpp`**, in `ProcessFrame()`, when the test engine is enabled:

1. **Show test engine windows** (before `RenderFrame()`):  
   Call `ImGuiTestEngine_ShowTestEngineWindows(imgui_test_engine_, nullptr);` after your app UI (e.g. after `UIRenderer::RenderMainWindow` / `RenderFloatingWindows`) and before `RenderFrame()`.  
   This shows the **Dear ImGui Test Engine** UI: browse/run tests, view logs, queue tests, change run speed, etc. If you build with the **perf tool** (see below), the same UI typically exposes the **perf window** (performance tool) as a tab or sub-window for recording and comparing run times.

2. **After present:**  
   Call `ImGuiTestEngine_PostSwap(imgui_test_engine_);` after your backend swap/present. Required for screen capture and correct test timing.

Order in the frame: `ImGui::NewFrame()` → … your app UI … → **ImGuiTestEngine_ShowTestEngineWindows(...)** → `RenderFrame()` → (backend swap) → **ImGuiTestEngine_PostSwap(...)**.

For headless or CI builds you may skip `ShowTestEngineWindows` (e.g. guard with a compile-time or runtime flag) and only run queued tests.

Include the test engine header in `Application.cpp` only when `ENABLE_IMGUI_TEST_ENGINE` is defined.

### 6.3. Optional: Perf window (ImGui Perf Tool)

The test engine can show a **performance tool** window (record/compare frame times, etc.). It is implemented in **`imgui_te_perftool.cpp`** and uses **ImPlot** for plotting. To include it:

- In **imconfig** (when `ENABLE_IMGUI_TEST_ENGINE` is defined): set `IMGUI_TEST_ENGINE_ENABLE_IMPLOT 1` and ensure your project links **ImPlot** and provides it to the test engine as per the engine’s docs.
- In **CMake**: add `imgui_te_perftool.cpp` to `IMGUI_TEST_ENGINE_SOURCES` when `IMGUI_TEST_ENGINE_ENABLE_IMPLOT` is enabled.
- No extra call is needed: **`ImGuiTestEngine_ShowTestEngineWindows()`** exposes the perf tool as part of the test engine UI (tab or sub-window). If you do not add perftool/ImPlot, the test engine window still works; only the perf tool will be unavailable.

### 6.4. New file: register tests

Add a new C++ file, e.g. **`src/ui/ImGuiTestEngineTests.cpp`** (and a matching header if you prefer), compiled only when `ENABLE_IMGUI_TEST_ENGINE` is defined. In it:

- Include `imgui_te_engine.h` and any app headers needed to inspect state.
- Define `void RegisterFindHelperTests(ImGuiTestEngine* engine)`.
- Inside it, register one or more tests with `IM_REGISTER_TEST(engine, "category", "name")` and set `TestFunc` to a lambda that uses `ImGuiTestContext* ctx` to drive the UI (e.g. `ctx->SetRef("Find Helper"); ctx->ItemClick("…");` and `IM_CHECK(...)` for assertions).

Example (conceptual):

```cpp
#ifdef ENABLE_IMGUI_TEST_ENGINE
#include "imgui_te_engine.h"

void RegisterFindHelperTests(ImGuiTestEngine* engine) {
  ImGuiTest* t = IM_REGISTER_TEST(engine, "smoke", "launch_quit");
  t->TestFunc = [](ImGuiTestContext* ctx) {
    ctx->SetRef("Find Helper");
    // Optional: e.g. ctx->ItemClick("SomeButton");
    ctx->Yield();
  };
}
#endif
```

Add this source to the same target that links the test engine (and guard the file content or the whole file with `#ifdef ENABLE_IMGUI_TEST_ENGINE` if your build still compiles it when the option is OFF).

---

## 7. Main Loop: No Explicit PreNewFrame/PostNewFrame

The test engine **hooks into ImGui** so that when you call `ImGui::NewFrame()`, it runs its own PreNewFrame / PostNewFrame internally. You do **not** need to call `ImGuiTestEngine_PreNewFrame` or `ImGuiTestEngine_PostNewFrame` yourself. You only need to:

- Call `ImGuiTestEngine_PostSwap(engine)` **after** the frame is presented (each frame).
- Call `ImGuiTestEngine_ShowTestEngineWindows(engine, nullptr)` before `ImGui::Render()` to show the test engine window (and perf window if perftool is built).

So the only main-loop changes are the two calls in `ProcessFrame()` described above.

---

## 8. Optional: Running at “Max Speed” and Headless

For CI or faster runs, the test engine can set `test_io.IsRequestingMaxAppSpeed`. You can poll that in your run loop and, when true, skip vsync or even skip swap/rendering to run tests as fast as possible. That is optional and can be added later.

---

## 9. Summary Checklist

- [ ] Add `imgui_test_engine` under `external/` (submodule or copy).
- [ ] In `src/imgui_config.h`: `#ifdef ENABLE_IMGUI_TEST_ENGINE` → include `imgui_te_imconfig.h` and set `IMGUI_TEST_ENGINE_ENABLE_COROUTINE_STDTHREAD_IMPL 1`.
- [ ] CMake: option `ENABLE_IMGUI_TEST_ENGINE`; when ON, add test engine sources, include dir, and `target_compile_definitions(… ENABLE_IMGUI_TEST_ENGINE)` for the app target(s).
- [ ] `AppBootstrapResultBase`: add optional `ImGuiTestEngine* imgui_test_engine` (with forward decl when the define is set).
- [ ] In each platform Initialize (after ImGui backends): create engine, configure IO, `ImGuiTestEngine_Start`, store in `result.imgui_test_engine`.
- [ ] In each platform Cleanup: `ImGuiTestEngine_Stop(result.imgui_test_engine)`; then `CleanupImGuiAndGlfw(result)`; then `ImGuiTestEngine_DestroyContext(result.imgui_test_engine)` and clear the pointer.
- [ ] Application: store `bootstrap.imgui_test_engine` in `imgui_test_engine_`; in ctor call `RegisterFindHelperTests(imgui_test_engine_)` when non-null.
- [ ] Application::ProcessFrame: before render call `ImGuiTestEngine_ShowTestEngineWindows(imgui_test_engine_, nullptr)` (test window; perf window if perftool built), after present call `ImGuiTestEngine_PostSwap(imgui_test_engine_)`.
- [ ] Add `ImGuiTestEngineTests.cpp` (or similar) with `RegisterFindHelperTests()` and at least one `IM_REGISTER_TEST` with a `TestFunc`.

After this, building with `-DENABLE_IMGUI_TEST_ENGINE=ON` gives an executable that runs with the test engine; you can open the test engine window from the UI to run tests interactively, or later add a CLI/startup path to queue and run tests and exit with a status code for CI.
