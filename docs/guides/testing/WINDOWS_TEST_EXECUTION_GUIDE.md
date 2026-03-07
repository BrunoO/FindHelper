# Windows Test Execution Guide

**Purpose:** Quick reference for running tests on Windows with Visual Studio generator

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

## Alternative: Use Convenience Target

You can also use the convenience target (but it still requires the configuration to be specified during build):

```cmd
cmake --build build --config Debug --target run_tests
```

---

## Common Issues

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

Available test names:
- `StringSearchTests`
- `CpuFeaturesTests`
- `StringSearchAVX2Tests`
- `PathUtilsTests`
- `PathPatternMatcherTests`
- `PathPatternIntegrationTests`
- `SimpleRegexTests`
- `StringUtilsTests`
- `FileSystemUtilsTests`
- `LazyAttributeLoaderTests`
- `ParallelSearchEngineTests`
- `TimeFilterUtilsTests`
- `GuiStateTests`
- `FileIndexSearchStrategyTests`
- `GeminiApiUtilsTests`
- `SettingsTests`
- `SearchContextTests`
- `IndexOperationsTests`
- `PathOperationsTests`
- `DirectoryResolverTests`
- `FileIndexMaintenanceTests`
- `StdRegexUtilsTests`

---

## Running Tests Directly (Without CTest)

You can also run test executables directly:

```cmd
cd build\Debug
string_search_tests.exe
cpu_features_tests.exe
path_utils_tests.exe
```

This bypasses ctest but doesn't provide the same reporting and output formatting.

---

## Summary

**Key Command for Windows:**

```cmd
cd build
ctest -C Debug --output-on-failure
```

**Remember:**
- ✅ Always use `-C Debug` or `-C Release` on Windows
- ✅ Match the configuration used during build (`--config Debug` → `-C Debug`)
- ✅ Use `--output-on-failure` to see test output when tests fail
- ✅ Use `--verbose` for detailed output

---

**Last Updated:** 2026-01-05

