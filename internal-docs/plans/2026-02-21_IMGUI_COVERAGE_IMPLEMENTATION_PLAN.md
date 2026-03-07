# Plan: ImGui Test Engine Contributing to Code Coverage

**Date:** 2026-02-21  
**Based on:** [2026-02-21_IMGUI_TESTS_CODE_COVERAGE_STUDY.md](../analysis/2026-02-21_IMGUI_TESTS_CODE_COVERAGE_STUDY.md)  
**Goal:** Make builds that run the ImGui test engine contribute to the project’s code coverage with minimal, clear steps for the user.

---

## 1. User experience (target)

- **Current:** `./scripts/build_tests_macos.sh --coverage` → run CTest → `./scripts/generate_coverage_macos.sh` → coverage from **unit tests only**.
- **After:** Same workflow, but when coverage is enabled and the FindHelper app exists (macOS bundle), the coverage pipeline also runs FindHelper once with “run ImGui tests then exit”, merges its `.profraw` with the unit-test ones, and includes the FindHelper binary in the report. **One command** for the user: build with coverage, then run `generate_coverage_macos.sh` (which can optionally run FindHelper internally), or a single wrapper script that does build + CTest + run FindHelper for coverage + generate report.

**Simplest for the user:** Keep using `build_tests_macos.sh --coverage` and `generate_coverage_macos.sh`. Extend `generate_coverage_macos.sh` so that when it runs, it:
1. Runs CTest if not already run (or assume user ran it via build_tests_macos.sh).
2. If FindHelper.app exists and was built with coverage, runs FindHelper once with `--run-imgui-tests-and-exit` and `--index-from-file=...`, with `LLVM_PROFILE_FILE` set.
3. Merges **all** `.profraw` (including FindHelper’s) and passes **all** objects (unit test executables + FindHelper) to `llvm-cov`.

No new user-facing script is strictly required; the same two scripts do more. Optionally, a small wrapper (e.g. `run_full_coverage_macos.sh`) can run build + tests + generate in one go for convenience.

---

## 2. Implementation steps (minimal)

### Step 1: Instrument FindHelper when building for coverage (CMake)

- **Where:** `CMakeLists.txt`, in the block that defines the `find_helper` target for **macOS** (`elseif(APPLE)`).
- **What:** After the block that sets up `find_helper` (e.g. after `target_link_libraries(find_helper ...)` and before the next major `if`), add:
  - If `ENABLE_COVERAGE` is ON and we are on macOS, call `apply_coverage_to_target(find_helper)`.
- **Why:** So the FindHelper binary is built with `-fprofile-instr-generate` / `-fcoverage-mapping` and writes `.profraw` on exit when `LLVM_PROFILE_FILE` is set.
- **Scope:** macOS only (coverage for ImGui tests is only needed where we run them; the study assumes macOS for the current coverage scripts).

**Exact location:** In the Apple branch, after the existing `find_helper` setup (link libraries, compile options, etc.) and before the `else()` that starts the Linux branch, add:

```cmake
if(ENABLE_COVERAGE)
  apply_coverage_to_target(find_helper)
  message(STATUS "macOS: find_helper enabled for code coverage (ImGui tests)")
endif()
```

---

### Step 2: Add CLI flag `--run-imgui-tests-and-exit`

- **Where:** `CommandLineArgs.h`, `CommandLineArgs.cpp`.
- **What:**
  - In `CommandLineArgs`, add a bool: `bool run_imgui_tests_and_exit = false;`.
  - In `ParseCommandLineArgs`, recognize `--run-imgui-tests-and-exit` (no value) and set it to true. Only parse this when the app is built with ImGui Test Engine (guarded by `#ifdef ENABLE_IMGUI_TEST_ENGINE` in the .cpp, or always parse and document that the flag is ignored when test engine is disabled).
  - In `ShowHelp`, add one line for the new option (e.g. only when `ENABLE_IMGUI_TEST_ENGINE` is defined, or always with a note “(ImGui Test Engine builds only)”).
- **Why:** So the coverage script (or user) can run FindHelper in “run all ImGui tests then exit” mode for a clean process exit and profile write.

**Simplest:** Parse the flag unconditionally; if test engine is disabled, the Application code that uses it is not compiled, so the flag is effectively ignored.

---

### Step 3: Queue ImGui tests at startup and exit when queue is empty (Application)

- **Where:** `Application.cpp` (and optionally `Application.h` if a small accessor is needed).
- **What:**
  1. **Startup (first frame or after engine start):** If `cmd_args_.run_imgui_tests_and_exit` is true and `imgui_test_engine_` is non-null, call:
     - `ImGuiTestEngine_QueueTests(imgui_test_engine_, ImGuiTestGroup_Unknown, "all", ImGuiTestRunFlags_RunFromCommandLine)`.
     - Do this once (e.g. in the first frame after the engine is ready, or in `Run()` before the main loop).
  2. **Main loop (in `Run()`):** After the block that calls `ImGuiTestEngine_PostSwap(imgui_test_engine_)`, if `cmd_args_.run_imgui_tests_and_exit` is true and `ImGuiTestEngine_IsTestQueueEmpty(imgui_test_engine_)` is true, call `glfwSetWindowShouldClose(window_, 1)` so the loop exits on the next iteration.
- **Guards:** All of the above inside `#ifdef ENABLE_IMGUI_TEST_ENGINE`; use existing forward declarations or include `imgui_te_engine.h` where needed for `ImGuiTestEngine_QueueTests` and `ImGuiTestEngine_IsTestQueueEmpty`.
- **Why:** Matches the reference pattern in imgui_test_suite: queue tests at startup, then break the main loop when the queue is empty so the process exits normally and the LLVM profile runtime writes `.profraw`.

**Reference:** `external/imgui_test_engine/imgui_test_suite/imgui_test_suite.cpp` (queue tests with `ImGuiTestRunFlags_RunFromCommandLine`, set `exit_after_tests`, then in main loop `if (exit_after_tests && ImGuiTestEngine_IsTestQueueEmpty(engine)) break;`).

---

### Step 4: Ensure a .profraw is written for FindHelper (scripting)

- **Where:** `scripts/generate_coverage_macos.sh` (and optionally a wrapper script).
- **What:**
  - Before merging `.profraw` files, if the build is Clang and `ENABLE_COVERAGE` was ON:
    - If FindHelper.app exists (e.g. `$BUILD_DIR/FindHelper.app/Contents/MacOS/FindHelper`), run it once with:
      - `LLVM_PROFILE_FILE="$BUILD_DIR/FindHelper_%p.profraw"`
      - `--run-imgui-tests-and-exit`
      - `--index-from-file=tests/data/std-linux-filesystem.txt` (so regression tests that expect a fixed index don’t fail; document that this file must exist for full regression coverage).
    - Run from `$PROJECT_ROOT` so the relative path to `tests/data/...` resolves.
  - When collecting `.profraw` files, keep using `find "$BUILD_DIR" -name "*.profraw"` so the new FindHelper profraw is included.
  - When building the `llvm-cov` command, add the FindHelper binary as an additional `-object` (e.g. `-object "$BUILD_DIR/FindHelper.app/Contents/MacOS/FindHelper"`) if that file exists.
- **Why:** One run of FindHelper produces one (or more) `.profraw`; merging with existing unit-test profraw and adding the app binary to `llvm-cov` makes ImGui test execution contribute to the same report.

**User note:** If `tests/data/std-linux-filesystem.txt` is missing, the script can still run FindHelper with `--run-imgui-tests-and-exit` (regression tests may fail, but other ImGui tests will run and contribute coverage). Optionally skip the FindHelper run if the index file is missing and print a short message.

---

### Step 5: (Optional) Single entry-point script for “full” coverage

- **Where:** `scripts/run_full_coverage_macos.sh` (new).
- **What:** Runs in order: `scripts/build_tests_macos.sh --coverage` (which builds and runs CTest), then `scripts/generate_coverage_macos.sh` (which now also runs FindHelper with coverage and merges). User runs one command to get coverage including ImGui tests.
- **Why:** Maximum simplicity for the user; not required if we only extend `generate_coverage_macos.sh` and document the workflow.

---

## 3. Order of implementation

| Order | Task | Delivers |
|-------|------|----------|
| 1 | CMake: `apply_coverage_to_target(find_helper)` when `ENABLE_COVERAGE` and macOS | FindHelper built with coverage flags |
| 2 | CLI: `--run-imgui-tests-and-exit` in CommandLineArgs | Flag available for scripts |
| 3 | Application: queue tests at startup, exit when queue empty | FindHelper exits after ImGui tests and writes .profraw |
| 4 | generate_coverage_macos.sh: run FindHelper with LLVM_PROFILE_FILE, merge all .profraw, add FindHelper to llvm-cov | Report includes app + UI coverage from ImGui tests |
| 5 | (Optional) run_full_coverage_macos.sh | One-command full coverage for macOS |

---

## 4. Files to touch (summary)

| File | Changes |
|------|--------|
| `CMakeLists.txt` | In macOS `find_helper` block, add `if(ENABLE_COVERAGE) apply_coverage_to_target(find_helper)` (+ message). |
| `src/core/CommandLineArgs.h` | Add `bool run_imgui_tests_and_exit = false;`. |
| `src/core/CommandLineArgs.cpp` | Parse `--run-imgui-tests-and-exit`; add to ShowHelp (optionally guarded). |
| `src/core/Application.cpp` | Under `#ifdef ENABLE_IMGUI_TEST_ENGINE`: (1) once after engine ready, if flag set, call `ImGuiTestEngine_QueueTests(..., "all", ImGuiTestRunFlags_RunFromCommandLine)`; (2) after `ImGuiTestEngine_PostSwap`, if flag set and `ImGuiTestEngine_IsTestQueueEmpty`, call `glfwSetWindowShouldClose(window_, 1)`. |
| `scripts/generate_coverage_macos.sh` | If FindHelper.app exists and coverage build: run FindHelper with `LLVM_PROFILE_FILE`, `--run-imgui-tests-and-exit`, `--index-from-file=tests/data/std-linux-filesystem.txt`; merge all .profraw; add FindHelper binary to llvm-cov objects. |
| `scripts/run_full_coverage_macos.sh` | (Optional) New script: build_tests_macos.sh --coverage then generate_coverage_macos.sh. |

---

## 5. Caveats (from study)

- **Regression tests:** They expect the std-linux-filesystem index. For coverage, run with `--index-from-file=tests/data/std-linux-filesystem.txt` so those tests pass and exercise the search path. If the file is missing, either skip the FindHelper run or run without it and accept that regression tests may fail (other tests still contribute coverage).
- **Headless/CI:** ImGui Test Engine can run at “max app speed” with simulated input; on macOS the app may run without a visible window. For Linux CI, a virtual display (e.g. xvfb) may still be needed.
- **Profile file:** `LLVM_PROFILE_FILE="$BUILD_DIR/FindHelper_%p.profraw"` avoids overwriting when multiple processes run; for a single run, one file is produced.

---

## 6. Success criteria

- Building with `./scripts/build_tests_macos.sh --coverage` (and ImGui Test Engine enabled) produces an instrumented FindHelper.
- Running `./scripts/generate_coverage_macos.sh` (after tests have run) runs FindHelper once with `--run-imgui-tests-and-exit` and `--index-from-file=...`, merges its `.profraw` with unit-test data, and includes the FindHelper binary in the coverage report.
- The generated report shows non-zero coverage for application/UI code paths executed during the ImGui tests (e.g. `Application::Run`, `ProcessFrame`, `UIRenderer`, search path when regression tests run).

---

## 7. References

- Study: `internal-docs/analysis/2026-02-21_IMGUI_TESTS_CODE_COVERAGE_STUDY.md`
- ImGui Test Engine integration: `specs/2026-02-20_IMGUI_TEST_ENGINE_INTEGRATION_SPEC.md`
- Coverage scripts: `scripts/build_tests_macos.sh`, `scripts/generate_coverage_macos.sh`
- Test engine API: `external/imgui_test_engine/imgui_test_engine/imgui_te_engine.h` (`ImGuiTestEngine_QueueTests`, `ImGuiTestEngine_IsTestQueueEmpty`)
- Reference app loop: `external/imgui_test_engine/imgui_test_suite/imgui_test_suite.cpp` (queue + exit when empty)
