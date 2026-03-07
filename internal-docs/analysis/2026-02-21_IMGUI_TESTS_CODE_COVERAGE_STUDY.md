# How ImGui Test Engine Tests Could Improve Code Coverage

**Date:** 2026-02-21  
**Purpose:** Study how the in-process ImGui Test Engine tests can contribute to the project’s code coverage, what’s missing today, and what changes would be needed.

---

## 1. Current Coverage vs ImGui Tests

### 1.1 How coverage is collected today

- **Script:** `scripts/build_tests_macos.sh --coverage` builds with `ENABLE_COVERAGE=ON` and runs **CTest** (unit test executables only).
- **Instrumented targets:** Only **test executables** get coverage in CMake: `apply_coverage_to_target()` is applied to `string_search_tests`, `path_utils_tests`, `file_index_search_strategy_tests`, etc. The **main app target `find_helper` is not instrumented** when `ENABLE_COVERAGE` is ON.
- **Report:** `scripts/generate_coverage_macos.sh` discovers `.profraw` files in `build_tests/`, merges them with `llvm-profdata`, then runs `llvm-cov` with a **fixed list of test executables**. The FindHelper app binary is **not** in that list.

So today:

- Coverage = **unit tests only** (doctest-based executables).
- **Application, UI, main loop, and platform code** are never run under coverage; they are either missing from the report or only appear via `coverage_all_sources` with 0% execution.

### 1.2 What ImGui tests run

ImGui Test Engine tests run **inside the FindHelper process**:

- They use the same main loop (`Application::Run()` → `ProcessFrame()`).
- They drive the UI via the test engine (e.g. `SetRef("Find Helper")`, `TriggerManualSearch()`, `WaitForSearchComplete()`).
- Regression tests (e.g. `regression/show_all`) trigger real searches and assert on result counts.

So when ImGui tests run, they execute:

- **Application:** `ProcessFrame()`, `UpdateIndexBuildState()`, `UpdateSearchState()`, `HandleIndexDump()`, `RenderFrame()`, and the path that calls `ImGuiTestEngine_PostSwap()`.
- **UIRenderer and UI components:** `RenderMainWindow()`, `RenderFloatingWindows()`, and all panels (SearchInputs, ResultsTable, FilterPanel, Popups, StatusBar, etc.) that are drawn each frame.
- **ApplicationLogic:** Keyboard shortcuts and actions invoked during tests.
- **SearchController / SearchWorker:** When regression tests call `TriggerManualSearch()` and the main loop runs `UpdateSearchState()` and `PollResults`.
- **Bootstrap and main:** Only when the **full app** is run (not when running unit tests).

Unit tests, by design, do **not** run this code: they run in separate executables that link only a subset of sources (e.g. path_utils, file_index, search engine). So **ImGui tests are the only automated way today to get execution counts on the main app and UI**.

---

## 2. Gap: Main App Not in Coverage Pipeline

| Aspect | Unit tests (current) | ImGui tests (current) |
|--------|----------------------|------------------------|
| Binary | Many small test executables | FindHelper.app (one process) |
| Instrumented for coverage? | Yes | **No** (find_helper not in `apply_coverage_to_target`) |
| .profraw written? | Yes (per test exe) | N/A (app not built with coverage) |
| Included in generate_coverage_macos.sh? | Yes | **No** |

So even if we run FindHelper and execute ImGui tests manually, **no coverage data is produced** for the app because:

1. The FindHelper binary is not built with `-fprofile-instr-generate` / `-fcoverage-mapping` when `ENABLE_COVERAGE=ON`.
2. The coverage script never runs FindHelper and never passes the app binary to `llvm-profdata` / `llvm-cov`.

---

## 3. How ImGui Tests Could Contribute: Requirements

To have ImGui tests contribute to coverage, the following are needed.

### 3.1 Instrument the main app when building for coverage

- In **CMakeLists.txt**, when `ENABLE_COVERAGE` is ON, call `apply_coverage_to_target(find_helper)` for the macOS (and optionally other) app target.
- Then FindHelper will be built with the same Clang coverage flags as the test executables and will write `.profraw` on exit when `LLVM_PROFILE_FILE` is set.

### 3.2 Run FindHelper in “run ImGui tests then exit” mode

- The test engine supports **queuing tests** and **checking when the queue is empty**:
  - `ImGuiTestEngine_QueueTests(engine, ImGuiTestGroup_Unknown, "all", run_flags)` (or a specific filter like `"regression"`).
  - `ImGuiTestEngine_IsTestQueueEmpty(engine)` returns true when all queued tests have finished.
- The reference app (imgui_test_suite) does: **if tests were queued and not “pause on exit”, break the main loop when `ImGuiTestEngine_IsTestQueueEmpty(engine)`**; then stop the engine and exit. That way the process exits normally and the LLVM profile runtime writes `.profraw`.
- For FindHelper we need an equivalent:
  - **CLI flag**, e.g. `--run-imgui-tests-and-exit` (only valid when `ENABLE_IMGUI_TEST_ENGINE` is ON).
  - **Startup:** After bootstrap and engine start, if this flag is set, call `ImGuiTestEngine_QueueTests(..., "all", ImGuiTestRunFlags_RunFromCommandLine)` (or a chosen filter).
  - **Main loop** (in `Application::Run()`): In the same iteration where we call `ProcessFrame()`, after the frame (e.g. after `ImGuiTestEngine_PostSwap`), if `--run-imgui-tests-and-exit` is set and `ImGuiTestEngine_IsTestQueueEmpty(imgui_test_engine_)`, then call `glfwSetWindowShouldClose(window_, 1)` (or break and return) so the loop exits, cleanup runs, and the process exits. That ensures profile data is flushed.
- For **regression tests** that need an index, the app must be run with `--index-from-file=tests/data/std-linux-filesystem.txt` (or equivalent) so the tests don’t fail for “wrong index size”; the same run can be used for coverage.

### 3.3 Ensure a .profraw is written for FindHelper

- Run FindHelper with `LLVM_PROFILE_FILE` set so the profile is written to a known location, e.g.:
  - `LLVM_PROFILE_FILE="$BUILD_DIR/FindHelper_%p.profraw" ./build_tests/FindHelper.app/Contents/MacOS/FindHelper --run-imgui-tests-and-exit --index-from-file=tests/data/std-linux-filesystem.txt`
- On normal exit, the runtime will write `FindHelper_<pid>.profraw` into `build_tests/`.

### 3.4 Merge and report FindHelper coverage together with unit tests

- In **generate_coverage_macos.sh** (or a wrapper):
  - After CTest (or in addition to it), if FindHelper exists and was built with coverage, run FindHelper once with `--run-imgui-tests-and-exit` and `--index-from-file=...` (and `LLVM_PROFILE_FILE` as above).
  - Include **all** `.profraw` under `build_tests/` in the `llvm-profdata merge` step (so FindHelper’s file(s) are merged with the unit test profraw files).
  - When calling `llvm-cov report` and `llvm-cov show`, add the **FindHelper binary** to the list of objects, e.g. `-object "$BUILD_DIR/FindHelper.app/Contents/MacOS/FindHelper"` (or the path where the unbundled binary lives if different).
- Then the report will show:
  - **Unit tests:** Coverage of the code paths exercised by doctest executables.
  - **ImGui tests:** Additional coverage of Application, UIRenderer, SearchController, search path, and any UI/platform code executed during the queued ImGui tests.

---

## 4. Code Paths That Would Gain Coverage

With the above in place, ImGui tests would add **execution** (and thus non-zero coverage) for code that unit tests do not run:

| Area | Examples | Currently covered by unit tests? |
|------|----------|----------------------------------|
| Application | `Application::Run()`, `ProcessFrame()`, main loop, `UpdateIndexBuildState()`, `UpdateSearchState()`, `HandleIndexDump()`, `SaveSettingsOnShutdown()` | No |
| UIRenderer | `RenderMainWindow()`, `RenderFloatingWindows()`, layout and menu logic | No |
| UI components | SearchInputs, ResultsTable, FilterPanel, Popups, StatusBar, EmptyState, FolderBrowser, MetricsWindow, SettingsWindow | No |
| ApplicationLogic | Keyboard shortcuts, menu actions, “trigger manual search” path | No |
| Search flow | SearchController + SearchWorker path when tests trigger search and poll results | Partially (worker/engine are tested in isolation; not the full UI→controller→worker path) |
| Bootstrap / main | main_common, platform bootstrap (e.g. macOS), window/ImGui init | No |
| Platform (macOS) | MetalManager, GLFW, ImGui backends (used during app run) | No |

So ImGui tests would **not** replace unit tests; they would **complement** them by covering the **main application and UI** that only run inside the FindHelper process.

---

## 5. Implementation Outline (Summary)

1. **CMake:** When `ENABLE_COVERAGE` is ON, add `apply_coverage_to_target(find_helper)` for the macOS app target (and optionally others if you enable coverage there).
2. **Command-line:** Parse `--run-imgui-tests-and-exit` (only when `ENABLE_IMGUI_TEST_ENGINE` is defined); store in `CommandLineArgs` or similar.
3. **Startup:** In Application ctor or first frame, if `--run-imgui-tests-and-exit` is set and the test engine exists, call `ImGuiTestEngine_QueueTests(engine, ImGuiTestGroup_Unknown, "all", ImGuiTestRunFlags_RunFromCommandLine)` (or a narrower filter if preferred).
4. **Main loop:** In `Application::Run()`, after each frame (e.g. after the block that calls `ImGuiTestEngine_PostSwap`), if the flag is set and `ImGuiTestEngine_IsTestQueueEmpty(imgui_test_engine_)`, request window close (or break and return) so the process exits cleanly.
5. **Coverage script:** Optionally run FindHelper with `--run-imgui-tests-and-exit` and `--index-from-file=...` when generating coverage; merge all `.profraw` from `build_tests/` and pass the FindHelper binary to `llvm-cov` along with the test executables.

---

## 6. Caveats and Options

- **Regression tests and index file:** Regression tests expect a fixed index (e.g. 789531 entries). For a single automated coverage run, use `--index-from-file=tests/data/std-linux-filesystem.txt` so those tests pass and exercise the search path; otherwise they fail early and may reduce coverage.
- **Headless / CI:** The test engine can run with “max app speed” and simulated input; it does not strictly require a visible window. For CI, you may still need a virtual display (e.g. `xvfb`) on Linux if the backend requires a display; macOS can often run the app without a visible window depending on backend.
- **Profile file name:** Using `LLVM_PROFILE_FILE=.../FindHelper_%p.profraw` avoids overwriting when multiple processes run; for a single run, one file is produced.
- **Optional:** You could add a separate script (e.g. `scripts/run_imgui_tests_for_coverage.sh`) that builds with coverage + test engine, runs CTest, runs FindHelper with `--run-imgui-tests-and-exit` and `LLVM_PROFILE_FILE`, then calls `generate_coverage_macos.sh` so the full pipeline is one command.

---

## 7. References

- **ImGui Test Engine integration:** `specs/2026-02-20_IMGUI_TEST_ENGINE_INTEGRATION_SPEC.md`
- **Exploration (queue tests, exit after tests):** `internal-docs/analysis/2026-02-19_IMGUI_TEST_ENGINE_EXPLORATION.md`
- **Coverage today:** `scripts/build_tests_macos.sh`, `scripts/generate_coverage_macos.sh`, `internal-docs/guides/testing/COVERAGE_INCLUDING_ALL_FILES.md`
- **Test engine API:** `external/imgui_test_engine/imgui_test_engine/imgui_te_engine.h` (`ImGuiTestEngine_QueueTests`, `ImGuiTestEngine_IsTestQueueEmpty`), `external/imgui_test_engine/imgui_test_suite/imgui_test_suite.cpp` (main loop exit when queue empty).
