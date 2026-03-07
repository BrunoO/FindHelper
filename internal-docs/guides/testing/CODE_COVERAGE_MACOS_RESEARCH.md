# Code Coverage Metrics for macOS - Research and Implementation Guide

## Executive Summary

This document researches options for obtaining code coverage metrics from tests on macOS for the USN_WINDOWS C++ project. The project uses:
- **CMake** for build configuration
- **Clang** compiler on macOS (via Xcode Command Line Tools)
- **doctest** framework for unit tests
- **Debug builds** for test executables

**Recommended Solution:** Use **llvm-cov** (native to Clang) with coverage instrumentation flags. This is the most straightforward and well-integrated solution for macOS.

---

## Recommended Approach: llvm-cov

### Overview

**llvm-cov** is the native code coverage tool that comes with Clang/LLVM. It's already available on macOS when Xcode Command Line Tools are installed. This is the recommended approach because:

- ✅ **Native integration** with Clang compiler
- ✅ **No additional dependencies** required
- ✅ **Works seamlessly** with CMake
- ✅ **Generates detailed reports** (line, function, branch coverage)
- ✅ **HTML output** available for easy viewing
- ✅ **Compatible** with existing test infrastructure

### How It Works

1. **Compile with coverage flags:** `-fprofile-instr-generate -fcoverage-mapping`
2. **Run tests:** Generates `.profraw` files (raw coverage data)
3. **Merge coverage data:** `llvm-profdata merge` combines all `.profraw` files
4. **Generate report:** `llvm-cov show/report` creates human-readable reports

### Implementation Steps

#### Step 1: Add Coverage Flags to CMake

Add a CMake option to enable coverage instrumentation:

```cmake
# In CMakeLists.txt, add after ENABLE_ASAN option (around line 680)
option(ENABLE_COVERAGE "Enable code coverage instrumentation (Clang/GCC only)" OFF)

# Helper function to apply coverage flags (similar to apply_asan_to_target)
function(apply_coverage_to_target target_name)
    if(ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(${target_name} PRIVATE
            -fprofile-instr-generate
            -fcoverage-mapping
        )
        target_link_options(${target_name} PRIVATE
            -fprofile-instr-generate
            -fcoverage-mapping
        )
    elseif(ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        # GCC uses different flags
        target_compile_options(${target_name} PRIVATE
            --coverage
            -fprofile-arcs
            -ftest-coverage
        )
        target_link_options(${target_name} PRIVATE
            --coverage
        )
    endif()
endfunction()
```

#### Step 2: Apply Coverage to Test Targets

In the test configuration section, apply coverage to all test targets:

```cmake
# After applying ASan (around line 1417-1429)
if(ENABLE_COVERAGE)
    apply_coverage_to_target(string_search_tests)
    apply_coverage_to_target(cpu_features_tests)
    apply_coverage_to_target(string_search_avx2_tests)
    apply_coverage_to_target(path_utils_tests)
    apply_coverage_to_target(path_pattern_matcher_tests)
    apply_coverage_to_target(path_pattern_integration_tests)
    apply_coverage_to_target(simple_regex_tests)
    apply_coverage_to_target(string_utils_tests)
    apply_coverage_to_target(file_system_utils_tests)
    apply_coverage_to_target(file_index_search_strategy_tests)
    apply_coverage_to_target(gemini_api_utils_tests)
    message(STATUS "  Code coverage enabled for all test targets")
endif()
```

#### Step 3: Create Coverage Script

Create a script to generate coverage reports:

**`scripts/generate_coverage_macos.sh`:**

```bash
#!/bin/bash
# Generate code coverage report for macOS tests
# Usage: ./scripts/generate_coverage_macos.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build_tests"
COVERAGE_DIR="$PROJECT_ROOT/coverage"

echo "=========================================="
echo "Generating Code Coverage Report"
echo "=========================================="

# Check if coverage was enabled
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ] || ! grep -q "ENABLE_COVERAGE:BOOL=ON" "$BUILD_DIR/CMakeCache.txt"; then
    echo "ERROR: Coverage was not enabled during build."
    echo "Rebuild with: cmake -DENABLE_COVERAGE=ON ..."
    exit 1
fi

# Create coverage output directory
mkdir -p "$COVERAGE_DIR"

# Find all .profraw files
PROFRAW_FILES=$(find "$BUILD_DIR" -name "*.profraw" 2>/dev/null || true)

if [ -z "$PROFRAW_FILES" ]; then
    echo "ERROR: No .profraw files found. Did you run the tests?"
    echo "Run tests first: cd $BUILD_DIR && ctest --output-on-failure"
    exit 1
fi

echo "Found .profraw files, merging coverage data..."
llvm-profdata merge -sparse $PROFRAW_FILES -o "$COVERAGE_DIR/coverage.profdata"

# Find test executables
TEST_EXECUTABLES=(
    "$BUILD_DIR/string_search_tests"
    "$BUILD_DIR/cpu_features_tests"
    "$BUILD_DIR/string_search_avx2_tests"
    "$BUILD_DIR/path_utils_tests"
    "$BUILD_DIR/path_pattern_matcher_tests"
    "$BUILD_DIR/path_pattern_integration_tests"
    "$BUILD_DIR/simple_regex_tests"
    "$BUILD_DIR/string_utils_tests"
    "$BUILD_DIR/file_system_utils_tests"
    "$BUILD_DIR/file_index_search_strategy_tests"
    "$BUILD_DIR/gemini_api_utils_tests"
)

# Generate summary report
echo ""
echo "Generating coverage summary..."
llvm-cov report \
    -instr-profile="$COVERAGE_DIR/coverage.profdata" \
    ${TEST_EXECUTABLES[@]} \
    -ignore-filename-regex="external/|tests/" \
    > "$COVERAGE_DIR/coverage_summary.txt"

cat "$COVERAGE_DIR/coverage_summary.txt"

# Generate HTML report
echo ""
echo "Generating HTML report..."
llvm-cov show \
    -instr-profile="$COVERAGE_DIR/coverage.profdata" \
    ${TEST_EXECUTABLES[@]} \
    -ignore-filename-regex="external/|tests/" \
    -format=html \
    -output-dir="$COVERAGE_DIR/html" \
    -show-line-counts-or-regions \
    -show-branches=count

echo ""
echo "=========================================="
echo "Coverage report generated!"
echo "=========================================="
echo "Summary: $COVERAGE_DIR/coverage_summary.txt"
echo "HTML:    $COVERAGE_DIR/html/index.html"
echo ""
echo "Open HTML report: open $COVERAGE_DIR/html/index.html"
```

#### Step 4: Update Build Script

Modify `scripts/build_tests_macos.sh` to support coverage:

```bash
# Add coverage option
ENABLE_COVERAGE="OFF"
if [[ "$1" == "--coverage" ]] || [[ "$2" == "--coverage" ]]; then
    ENABLE_COVERAGE="ON"
    echo "Code coverage ENABLED"
fi

# In CMake configuration, add:
-DENABLE_COVERAGE=$ENABLE_COVERAGE
```

### Usage

```bash
# Build tests with coverage enabled
./scripts/build_tests_macos.sh --coverage

# Or manually:
cd build_tests
cmake .. -DBUILD_TESTS=ON -DENABLE_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build .

# Run tests (generates .profraw files)
ctest --output-on-failure

# Generate coverage report
./scripts/generate_coverage_macos.sh

# View HTML report
open coverage/html/index.html
```

### Coverage Report Features

- **Line coverage:** Shows which lines were executed
- **Function coverage:** Shows which functions were called
- **Branch coverage:** Shows which branches (if/else) were taken
- **Region coverage:** Shows coverage of code regions
- **HTML output:** Interactive browser-based reports with source code highlighting
- **Summary statistics:** Overall coverage percentages

---

## Alternative Approaches

### Option 2: gcov (GCC Coverage)

**When to use:** If using GCC instead of Clang, or for compatibility with existing gcov workflows.

**Pros:**
- ✅ Works with GCC
- ✅ Well-established toolchain
- ✅ Good integration with lcov for HTML reports

**Cons:**
- ❌ Requires GCC (not default on macOS)
- ❌ Less integrated with Clang toolchain
- ❌ Different flags than llvm-cov

**Implementation:**
```cmake
# GCC coverage flags
target_compile_options(${target_name} PRIVATE
    --coverage
    -fprofile-arcs
    -ftest-coverage
)
target_link_options(${target_name} PRIVATE --coverage)
```

**Tools:**
- `gcov` - generates coverage data
- `lcov` - generates HTML reports from gcov data
- `genhtml` - creates HTML output

### Option 3: grcov (Mozilla)

**When to use:** If you need cross-platform coverage (works with both Clang and GCC).

**Pros:**
- ✅ Works with both Clang and GCC
- ✅ Can process multiple coverage formats
- ✅ Good for CI/CD pipelines

**Cons:**
- ❌ Requires Rust toolchain (to build grcov)
- ❌ Additional dependency
- ❌ More setup complexity

**Installation:**
```bash
cargo install grcov
```

**Usage:**
```bash
# Generate .profraw files (same as llvm-cov)
# Then convert:
grcov . -s . --binary-path ./build_tests -t html -o coverage/html
```

### Option 4: kcov

**When to use:** For simple coverage without compiler instrumentation.

**Pros:**
- ✅ No recompilation needed
- ✅ Works with any binary
- ✅ Simple to use

**Cons:**
- ❌ Less accurate than compiler-based coverage
- ❌ May miss some coverage details
- ❌ Requires installation via MacPorts/Homebrew

**Installation:**
```bash
# MacPorts
sudo port install kcov

# Homebrew
brew install kcov
```

**Usage:**
```bash
kcov --exclude-pattern=external/,tests/ coverage/output ./build_tests/string_search_tests
```

### Option 5: Xcode Code Coverage

**When to use:** If migrating to Xcode project format.

**Pros:**
- ✅ Native Xcode integration
- ✅ Visual coverage in Xcode IDE
- ✅ Automatic report generation

**Cons:**
- ❌ Requires Xcode project (not CMake)
- ❌ Less suitable for command-line workflows
- ❌ Tied to Xcode ecosystem

---

## Comparison Matrix

| Tool | Compiler | Setup Complexity | HTML Reports | CI/CD Ready | Recommended |
|------|----------|------------------|--------------|-------------|-------------|
| **llvm-cov** | Clang | Low | ✅ | ✅ | ✅ **Yes** |
| gcov | GCC | Medium | ✅ (via lcov) | ✅ | ⚠️ If using GCC |
| grcov | Clang/GCC | Medium | ✅ | ✅ | ⚠️ Cross-platform |
| kcov | Any | Low | ✅ | ✅ | ⚠️ Less accurate |
| Xcode | Clang | High | ✅ | ⚠️ | ❌ Requires Xcode |

---

## Integration with Existing Infrastructure

### Compatibility with Address Sanitizer

Coverage and ASan can be used together:

```cmake
# Both can be enabled simultaneously
if(ENABLE_ASAN AND ENABLE_COVERAGE)
    # ASan flags
    target_compile_options(${target_name} PRIVATE -fsanitize=address)
    # Coverage flags
    target_compile_options(${target_name} PRIVATE
        -fprofile-instr-generate
        -fcoverage-mapping
    )
endif()
```

**Note:** There may be a slight performance impact when both are enabled, but it's generally acceptable for test builds.

### Integration with Test Scripts

The existing `build_tests_macos.sh` script can be extended:

```bash
# Usage options:
./build_tests_macos.sh                    # Normal build
./build_tests_macos.sh --coverage         # With coverage
./build_tests_macos.sh --no-asan          # Without ASan
./build_tests_macos.sh --coverage --no-asan  # Coverage only
```

### CI/CD Integration

For continuous integration, coverage can be:

1. **Generated automatically** after test runs
2. **Uploaded to services** like Codecov, Coveralls
3. **Enforced with thresholds** (fail if coverage < X%)

Example CI integration:
```bash
# In CI script
cmake -DENABLE_COVERAGE=ON ...
cmake --build .
ctest --output-on-failure
./scripts/generate_coverage_macos.sh

# Upload to Codecov (if using)
bash <(curl -s https://codecov.io/bash) -f coverage/coverage.profdata
```

---

## Coverage Metrics Explained

### Line Coverage
- **What it measures:** Percentage of executable lines that were executed
- **Example:** 80% means 80 out of 100 executable lines were run
- **Use case:** Basic coverage metric, easiest to understand

### Function Coverage
- **What it measures:** Percentage of functions that were called
- **Example:** 90% means 90 out of 100 functions were called at least once
- **Use case:** Ensures all functions have at least some testing

### Branch Coverage
- **What it measures:** Percentage of branches (if/else, switch cases) that were taken
- **Example:** 75% means 3 out of 4 branches in if/else statements were tested
- **Use case:** More thorough than line coverage, catches untested conditions

### Region Coverage
- **What it measures:** Coverage of code regions (may include partial line coverage)
- **Example:** Shows which parts of a line were executed
- **Use case:** Most detailed coverage metric

---

## Best Practices

### 1. Coverage Thresholds

Set minimum coverage requirements:
- **Critical code:** 90%+ coverage
- **Core functionality:** 80%+ coverage
- **Utility code:** 70%+ coverage
- **Overall project:** 75%+ coverage

### 2. Exclude External Code

Don't measure coverage for:
- External libraries (`external/` directory)
- Test files themselves (`tests/` directory)
- Generated code
- Platform-specific stubs

Use `-ignore-filename-regex` in llvm-cov commands.

### 3. Focus on Quality, Not Quantity

- **High coverage ≠ good tests:** 100% coverage with weak tests is worse than 80% with strong tests
- **Focus on critical paths:** Ensure error handling, edge cases, and critical algorithms are well-tested
- **Use coverage to find gaps:** Coverage reports help identify untested code, not prove code is correct

### 4. Regular Coverage Runs

- Run coverage as part of regular development workflow
- Track coverage trends over time
- Set up CI to enforce coverage thresholds

### 5. Coverage Reports in PRs

- Include coverage reports in pull request reviews
- Show coverage changes (increase/decrease)
- Require coverage to not decrease for critical modules

---

## Troubleshooting

### Issue: No .profraw files generated

**Solution:**
- Ensure `-fprofile-instr-generate` is in both compile and link flags
- Check that tests actually ran (coverage files are created during execution)
- Verify `ENABLE_COVERAGE=ON` was set during CMake configuration

### Issue: Coverage shows 0% for all files

**Solution:**
- Ensure source files are compiled with coverage flags
- Check that test executables are instrumented (use `llvm-cov show` to verify)
- Verify source file paths match between build and coverage report

### Issue: External code included in coverage

**Solution:**
- Use `-ignore-filename-regex` to exclude directories:
  ```bash
  -ignore-filename-regex="external/|tests/|build/"
  ```

### Issue: Coverage report is too large

**Solution:**
- Focus on specific files: `llvm-cov show -object=target_file.cpp`
- Use summary mode: `llvm-cov report` instead of `show`
- Exclude more directories from report

---

## Next Steps

1. **Implement llvm-cov integration** (recommended approach)
   - Add CMake coverage option
   - Create coverage generation script
   - Update build script

2. **Test the implementation**
   - Build tests with coverage enabled
   - Run tests and generate report
   - Verify coverage data is accurate

3. **Set coverage baselines**
   - Run initial coverage report
   - Document current coverage percentages
   - Set target coverage goals

4. **Integrate into workflow**
   - Add coverage to CI/CD (if applicable)
   - Create documentation for team
   - Set up regular coverage reviews

5. **Optional: Coverage badges**
   - Integrate with coverage service (Codecov, Coveralls)
   - Add coverage badge to README
   - Track coverage trends

---

## References

- **llvm-cov documentation:** https://llvm.org/docs/CommandGuide/llvm-cov.html
- **Clang Source-based Code Coverage:** https://clang.llvm.org/docs/SourceBasedCodeCoverage.html
- **CMake Code Coverage:** Various community modules available
- **gcov documentation:** https://gcc.gnu.org/onlinedocs/gcc/Gcov.html
- **grcov:** https://github.com/mozilla/grcov

---

## Conclusion

**Recommended solution:** Use **llvm-cov** with Clang coverage flags. It's the most straightforward, well-integrated solution for macOS C++ projects using CMake and Clang.

The implementation requires:
1. Adding coverage flags to CMake (similar to existing ASan support)
2. Creating a coverage report generation script
3. Updating the build script to support coverage option

This provides comprehensive coverage metrics with minimal setup and no additional dependencies.

