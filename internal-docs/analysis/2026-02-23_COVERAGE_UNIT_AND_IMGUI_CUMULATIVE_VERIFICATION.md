# Verification: Unit Test and ImGui Test Coverage Are Correctly Cumulative

**Date:** 2026-02-23

This document verifies that when both doctest unit tests and the ImGui Test Engine (FindHelper) run with coverage enabled, their coverage data is **correctly cumulative**: merged into a single profile and reported together.

---

## 1. How coverage is produced

### 1.1 Unit tests (`build_tests_macos.sh`)

- Each test executable is run with a **unique** `LLVM_PROFILE_FILE`:

  ```bash
  LLVM_PROFILE_FILE="${test_name}.profraw" ./"$test_exe"
  ```

- So each target (e.g. `file_index_search_strategy_tests`, `settings_tests`) writes its own `.profraw` in `build_tests/` (e.g. `file_index_search_strategy_tests.profraw`). **No overwrite**: every run produces a separate file.

### 1.2 ImGui Test Engine (FindHelper)

- When `--coverage --run-imgui-tests` is used and FindHelper is built with the test engine, it is run once with:

  ```bash
  LLVM_PROFILE_FILE="$BUILD_DIR/FindHelper_%p.profraw" "$FINDHELPER_EXE" \
    --run-imgui-tests-and-exit --show-metrics --index-from-file=...
  ```

- So FindHelper writes its own `.profraw` file(s) in `build_tests/` (e.g. `FindHelper_12345.profraw`). **Separate from unit-test profraws**; no overwrite.

---

## 2. How coverage is merged and reported

### 2.1 Merge step (`generate_coverage_macos.sh`)

- The script finds **all** `.profraw` files under the build directory:

  ```bash
  PROFRAW_FILES=$(find "$BUILD_DIR" -name "*.profraw" -type f 2>/dev/null | sort || true)
  ```

- It then merges them into a **single** indexed profile:

  ```bash
  "$LLVM_PROFDATA" merge -sparse $PROFRAW_FILES -o "$COVERAGE_DIR/coverage.profdata"
  ```

- `llvm-profdata merge` is **cumulative by design**: counts from each input file are combined (see [LLVM Profile Data](https://llvm.org/docs/CommandGuide/llvm-profdata.html)). So unit-test and FindHelper profiles are **added together** in `coverage.profdata`.

### 2.2 Report step

- `llvm-cov report` and `llvm-cov show` are run with:

  - `-instr-profile="$COVERAGE_DIR/coverage.profdata"` (the merged profile)
  - One `-object <path>` per test executable and for FindHelper

- The merged profile holds data for every binary that produced a `.profraw`. Passing every such binary with `-object` ensures all of their coverage is attributed and included in the summary and HTML report. The report therefore reflects **cumulative** coverage from all unit tests and from the ImGui test run.

---

## 3. Conclusion

| Aspect | Status |
|--------|--------|
| Unit tests use unique `LLVM_PROFILE_FILE` per executable | Yes (no overwrite) |
| FindHelper uses distinct `LLVM_PROFILE_FILE` in build dir | Yes |
| All `.profraw` files are found and merged | Yes (`find` + `llvm-profdata merge -sparse`) |
| Merge is cumulative | Yes (llvm-profdata merges counts) |
| Report uses merged profile + all relevant executables | Yes (multiple `-object`; script must list all run executables) |

**Coverage from unit tests and ImGui tests is correctly cumulative** when:

1. You run with coverage and (optionally) ImGui tests:  
   `./scripts/build_tests_macos.sh --coverage [--run-imgui-tests]`
2. You generate the report:  
   `./scripts/generate_coverage_macos.sh`

**Note:** `run_full_coverage_macos.sh` runs only `build_tests_macos.sh --coverage` (no `--run-imgui-tests`). For **unit + ImGui** cumulative coverage, run explicitly:

```bash
./scripts/build_tests_macos.sh --coverage --run-imgui-tests
./scripts/generate_coverage_macos.sh
```

---

## 4. Executable list alignment

For the report to include coverage from **every** unit test that ran, `generate_coverage_macos.sh` must pass to `llvm-cov` every test executable that is run by `build_tests_macos.sh` (plus `coverage_all_sources` and FindHelper when present). The script’s `TEST_EXECUTABLES` list is kept in sync with the build script’s `TEST_TARGETS` so that no run produces a `.profraw` that is merged but then omitted from the `-object` list.
