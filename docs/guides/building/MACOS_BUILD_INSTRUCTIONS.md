# macOS Build Instructions for Tests

In the commands below, `$PROJECT_ROOT` is the path to your clone of the repository (the directory containing `CMakeLists.txt`). For example: `export PROJECT_ROOT=$(pwd)` when you are in the repo root.

## Quick Start

### Option 1: Use the Build Script (Recommended)

```bash
cd $PROJECT_ROOT
./scripts/build_tests_macos.sh
```

This script will:
1. Create/clean the `build_tests` directory
2. Configure CMake with cross-platform test support
3. Build all test executables
4. Run all tests

### Option 2: Manual Build Steps

If you prefer to build manually:

```bash
cd $PROJECT_ROOT

# Create build directory
mkdir -p build_tests
cd build_tests

# Configure CMake (this creates CMakeCache.txt)
cmake .. \
    -DBUILD_TESTS=ON \
    -DCROSS_PLATFORM_TESTS=ON \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_STANDARD=17

# Build a specific test (or all tests)
cmake --build . --target string_search_tests
cmake --build . --target cpu_features_tests
cmake --build . --target string_search_avx2_tests
cmake --build . --target path_utils_tests
cmake --build . --target file_index_search_strategy_tests

# Run a test
./string_search_tests
```

## Common Issues

### Error: "not a CMake build directory (missing CMakeCache.txt)"

**Solution**: You need to run `cmake` first to configure the build directory.

```bash
cd build_tests  # or create it first: mkdir -p build_tests && cd build_tests
cmake .. -DBUILD_TESTS=ON -DCROSS_PLATFORM_TESTS=ON
```

### Error: "CMake Error: The source directory does not appear to contain CMakeLists.txt"

**Solution**: Make sure you're running cmake from the build directory, pointing to the source directory:

```bash
cd build_tests
cmake ..  # The ".." points to the parent directory (where CMakeLists.txt is)
```

### Error: Compilation errors about Windows.h or Windows types

**Solution**: Make sure `USN_WINDOWS_TESTS` is defined. Check that:
1. You're using `-DCROSS_PLATFORM_TESTS=ON`
2. You're on a non-Windows platform (macOS/Linux)
3. The CMakeLists.txt correctly defines `USN_WINDOWS_TESTS` for non-Windows

### Error: Missing dependencies (doctest, nlohmann/json)

**Solution**: These are git submodules. Make sure they're initialized:

```bash
git submodule update --init --recursive
```

## Building Individual Tests

You can build and run individual tests:

```bash
cd build_tests

# Build just one test
cmake --build . --target string_search_tests

# Run it
./string_search_tests
```

## Available Test Targets

- `string_search_tests` - String search algorithm tests
- `cpu_features_tests` - CPU feature detection tests
- `string_search_avx2_tests` - AVX2 string search tests
- `path_utils_tests` - Path utility tests
- `file_index_search_strategy_tests` - FileIndex search strategy tests

## Clean Build

To start fresh:

```bash
rm -rf build_tests
mkdir build_tests
cd build_tests
cmake .. -DBUILD_TESTS=ON -DCROSS_PLATFORM_TESTS=ON
cmake --build . --target string_search_tests
```

## Troubleshooting

### Check CMake Version

```bash
cmake --version  # Should be 3.16 or higher
```

### Check Compiler

```bash
clang++ --version  # macOS default compiler
```

### Verbose Build Output

```bash
cmake --build . --target string_search_tests --verbose
```

### Check What CMake Found

After running `cmake ..`, check the output for:
- Compiler detection
- Test configuration
- Cross-platform mode status

Look for lines like:
```
-- Building unit tests with doctest
-- Cross-platform test mode enabled for file_index_search_strategy_tests
```

## Next Steps

Once tests are building successfully, you can:
1. Run all tests: `ctest --output-on-failure`
2. Run specific test: `./string_search_tests`
3. Use your IDE to debug tests (point it to the build_tests directory)
