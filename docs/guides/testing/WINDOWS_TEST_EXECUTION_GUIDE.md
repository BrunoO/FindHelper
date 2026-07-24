# Windows Test Execution Guide

**Purpose:** Quick reference for running tests on Windows with Visual Studio generator

---

## Critical: Debug vs Release output folders

Visual Studio is a **multi-config** generator. Configure once; pick the config at **build** time:

| Command | Output folder |
|---------|----------------|
| `cmake --build build --config Debug` | `build\Debug\` |
| `cmake --build build --config Release` | `build\Release\` |

`-DCMAKE_BUILD_TYPE=Debug` at configure time is **ignored** by the Visual Studio generator. It does not create or select `build\Debug`.

Presets:

```cmd
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```

Or without presets:

```cmd
cmake -S . -B build -A x64
cmake --build build --config Debug --target find_helper
dir build\Debug\FindHelper.exe
```

If you build with the `windows-release` preset (or `--config Release`), binaries stay under `build\Release\` even if an IDE status bar says “Debug”.

---

## Quick Answer

**On Windows, you MUST specify the configuration when running ctest:**

```cmd
cd build
ctest -C Debug --output-on-failure
```

Or for Release:

```cmd
cd build
ctest -C Release --output-on-failure
```

---

## Why `-C` is Required on Windows

When using Visual Studio generator (the default on Windows), CMake creates a multi-configuration build system. This means test executables are built in configuration-specific directories:
- `build/Debug/string_search_tests.exe`
- `build/Release/string_search_tests.exe`

Without the `-C` flag, ctest doesn't know which configuration to run and will fail.

---

## Complete Test Workflow

### Step 1: Build Tests

```cmd
# Debug configuration
cmake --build build --config Debug

# Or Release configuration
cmake --build build --config Release
```

### Step 2: Run Tests

```cmd
cd build

# Debug configuration
ctest -C Debug --output-on-failure

# Or Release configuration
ctest -C Release --output-on-failure
```

---

## Alternative: Use Convenience Targets

**Build all test executables without running ctest:**

```cmd
cmake --build build --config Debug --target build_tests
```

**Build all test executables, then run ctest** (recommended on Windows):

```cmd
cmake --build build --config Debug --target run_tests
```

`run_tests` runs `scripts/run_tests_target.cmake`, which builds `build_tests` for the **same** `--config` then invokes `ctest -C <that config>`. You must still pass `--config` so MSVC emits binaries under `build\Debug\` or `build\Release\`.

---

## Common Issues

### Issue: Built “Debug” but only `build\Release` has executables

**Cause:** Multi-config VS ignores `CMAKE_BUILD_TYPE`. A Release build preset, IDE configuration, or missing `--config Debug` left outputs in `Release`.

**Solution:**

1. `cmake --build build --config Debug --target find_helper` (or `--preset windows-debug`)
2. Confirm: `dir build\Debug\FindHelper.exe`
3. For tests: `cmake --build build --config Debug --target run_tests` then expect `build\Debug\*_tests.exe`

### Issue: Tests run before all test executables are compiled

**Cause:** On the Visual Studio generator, `add_dependencies(run_tests build_tests)` alone does not always delay the `ctest` custom command until every `*_tests.exe` has finished linking (especially with parallel MSBuild). Bare `ctest` after a partial build has the same symptom.

**Solution:**

1. Prefer `cmake --build build --config Debug --target run_tests` — that target now runs a synchronous `cmake --build … --target build_tests` **before** `ctest`.
2. Or build explicitly then test: `cmake --build build --config Debug --target build_tests` then `ctest -C Debug --output-on-failure`
3. Do **not** rely on bare `ctest` until a full build or `build_tests` has completed

**Note:** `add_custom_target(... DEPENDS other_target)` does **not** create target-level ordering on the Visual Studio generator. The project uses `add_dependencies(build_tests …)` plus an explicit nested `--build --target build_tests` inside `run_tests`.

### Issue: "No tests were found"

**Cause:** Tests weren't built, or wrong configuration specified

**Solution:**
1. Verify tests were built: `cmake --build build --config Debug`
2. Use correct configuration: `ctest -C Debug --output-on-failure`
3. Check test executables exist: `dir build\Debug\*_tests.exe`

### Issue: "ctest: command not found"

**Cause:** CMake/CTest not in PATH

**Solution:**
1. Use full path: `C:\Program Files\CMake\bin\ctest.exe -C Debug --output-on-failure`
2. Or use Visual Studio Developer Command Prompt (has CMake in PATH)

### Issue: "Test executable not found"

**Cause:** Test executable wasn't built or is in different configuration directory

**Solution:**
1. Verify build completed: `cmake --build build --config Debug`
2. Check executable exists: `dir build\Debug\string_search_tests.exe`
3. Ensure you're using the correct configuration flag: `-C Debug` matches `--config Debug`

---

## Verbose Output

For more detailed test output:

```cmd
ctest -C Debug --output-on-failure --verbose
```

Or use the CMake options:

```cmd
cmake -S . -B build -DTEST_VERBOSE_OUTPUT=ON
cmake --build build --config Debug
cd build
ctest -C Debug --output-on-failure
```

---

## Running Individual Tests

To run a specific test:

```cmd
cd build
ctest -C Debug -R StringSearchTests --output-on-failure
```

---

## Running Tests Directly (Without CTest)

```cmd
cd build\Debug
string_search_tests.exe
cpu_features_tests.exe
path_utils_tests.exe
```

---

## Summary

```cmd
cd build
ctest -C Debug --output-on-failure
```

**Remember:**
- Always use `-C Debug` or `-C Release` on Windows
- Match the configuration used during build (`--config Debug` → `-C Debug`)
- `-DCMAKE_BUILD_TYPE=` does not select Debug/Release folders on VS

---

**Last Updated:** 2026-07-24
