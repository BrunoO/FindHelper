# CI run investigation – 2026-03-06

## Run reference

- **Workflow:** Build (`.github/workflows/build.yml`)
- **Run ID:** 22729717042  
- **Job ID:** 65915186039  

## Accessing the failure

The run URL  
`https://github.com/BrunoO/USN_WINDOWS/actions/runs/22729717042/job/65915186039`  
returned **404** when fetched (e.g. private repo or run not found). To see what actually failed:

1. Open the repo in GitHub → **Actions**.
2. Find the run (e.g. by time or commit).
3. Open the failed job and read the **log** for the failing step (Configure, Build, or Run tests).

## What the CI does

- **Trigger:** Manual (`workflow_dispatch` only).
- **Matrix:** Windows (Release), macOS (Release), Ubuntu 24.04 (Release).
- **Options:** `BUILD_WITH_BOOST: 'true'` (Boost used on all platforms).
- **Steps (per OS):**
  1. Checkout (recursive submodules).
  2. Platform setup: Windows → vcpkg + Boost; macOS → `brew install boost`; Linux → apt (build-essential, cmake, GLFW, etc. + libboost-dev).
  3. Configure CMake (`-DBUILD_TESTS=ON`, Boost when enabled).
  4. Build (`cmake --build build`).
  5. Run tests (`ctest --test-dir build --output-on-failure`).
  6. Upload artifact (app/exe).

So a failure can be in: **Configure**, **Build**, or **Run tests (ctest)**.

## Local checks (macOS)

- `scripts/build_tests_macos.sh` was run successfully (build + all tests in its list passed).
- The script’s hardcoded test list was **missing** `incremental_search_state_tests`. That test is built by CMake and run by **ctest** in CI, but was not run by the script. It has been **added** to `scripts/build_tests_macos.sh` so local runs match CI.

## Likely failure points by platform

- **Windows:** vcpkg/Boost install, MSVC build, or a test failing under ctest (e.g. timeout, path, or Windows-only code).
- **macOS:** `brew install boost`, Xcode/macOS SDK, or a test failing under ctest.
- **Linux:** apt install, or a test failing under ctest.

## Next steps

1. In GitHub Actions, open the run and the failed job, and note the **exact step** (Configure / Build / Run tests) and the **error message** in the log.
2. Re-run the failed job (or the whole workflow) to see if it’s flaky.
3. If a **test** failed: run the same test locally (e.g. `ctest --test-dir build -R IncrementalSearchStateTests --output-on-failure` in a build that matches CI, or use the updated `scripts/build_tests_macos.sh` which now includes `incremental_search_state_tests`).

Once you have the failing step and log excerpt, we can target the fix (e.g. CMake/Boost, test code, or environment).
