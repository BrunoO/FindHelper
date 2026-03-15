#!/bin/bash
# Build and run tests on macOS for USN_WINDOWS project
# This allows running tests locally on macOS without Windows build environment

set -eo pipefail  # Exit on error; pipeline fails if any command in it fails

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_DIR="$PROJECT_ROOT/build_tests"

# ASan is ON by default (catches memory bugs like use-after-free). TSAN and ASAN are mutually exclusive.
ENABLE_ASAN="ON"
ENABLE_TSAN="OFF"
ENABLE_COVERAGE="OFF"
ENABLE_COVERAGE_ALL_SOURCES="OFF"
BUILD_TYPE="Debug"  # Debug for debugging, Release for faster builds
USE_NINJA="ON"      # Use Ninja generator for faster builds (if available)
FORCE_RECONFIGURE="OFF"  # Force CMake reconfiguration (default: skip if cache exists)
# ImGui Test Engine (FindHelper --run-imgui-tests-and-exit) is costly; run only when requested
RUN_IMGUI_TEST_ENGINE="OFF"

# Show help message
show_help() {
    cat << EOF
Usage: $0 [OPTIONS]

Build and run tests on macOS for USN_WINDOWS project.

Options:
    --no-asan          Disable Address Sanitizer (faster build, but won't catch memory bugs)
    --tsan             Enable Thread Sanitizer (data-race detection; disables ASAN, macOS/Linux only)
    --coverage         Enable code coverage (generates .profraw files for coverage reports)
    --all-sources      Enable comprehensive coverage (includes all source files, requires --coverage)
    --release          Use Release build type (faster builds, less debugging info)
    --no-ninja         Use Make instead of Ninja (if Ninja causes issues)
    --reconfigure      Force CMake reconfiguration (skip cache check)
    --run-imgui-tests  Run ImGui Test Engine (FindHelper --run-imgui-tests-and-exit); costly, off by default
    --help, -h         Show this help message

Examples:
    $0                          # Build and run doctest unit tests only (no ImGui test engine)
    $0 --run-imgui-tests        # Also run ImGui Test Engine (smoke, regression, ui_windows, etc.)
    $0 --no-asan                # Build without Address Sanitizer (faster)
    $0 --tsan                   # Build with Thread Sanitizer (data-race detection)
    $0 --coverage               # Build with code coverage enabled
    $0 --coverage --run-imgui-tests  # Coverage + ImGui test engine
EOF
    exit 0
    return 0
}

# Parse command line arguments
for arg in "$@"; do
    case "$arg" in
        --help|-h)
            show_help
            ;;
        --no-asan)
            ENABLE_ASAN="OFF"
            ;;
        --tsan)
            ENABLE_TSAN="ON"
            ENABLE_ASAN="OFF"
            ;;
        --coverage)
            ENABLE_COVERAGE="ON"
            ;;
        --all-sources)
            ENABLE_COVERAGE_ALL_SOURCES="ON"
            # All sources mode requires coverage
            ENABLE_COVERAGE="ON"
            ;;
        --release)
            BUILD_TYPE="Release"
            ;;
        --no-ninja)
            USE_NINJA="OFF"
            ;;
        --reconfigure)
            FORCE_RECONFIGURE="ON"
            ;;
        --run-imgui-tests)
            RUN_IMGUI_TEST_ENGINE="ON"
            ;;
        *)
            echo "ERROR: Unknown option: $arg" >&2
            echo "Use --help for usage information" >&2
            exit 1
            ;;
    esac
done

if [[ "$ENABLE_ASAN" == "OFF" && "$ENABLE_TSAN" == "OFF" ]]; then
    echo "Address Sanitizer DISABLED (faster build, but won't catch memory bugs)"
fi
if [[ "$ENABLE_TSAN" == "ON" ]]; then
    echo "Thread Sanitizer ENABLED (data-race detection; ASAN disabled)"
fi

if [[ "$ENABLE_COVERAGE" == "ON" ]]; then
    echo "Code coverage ENABLED (generates .profraw files for coverage reports)"
fi

if [[ "$ENABLE_COVERAGE_ALL_SOURCES" == "ON" ]]; then
    echo "Comprehensive coverage ENABLED (includes all source files in coverage report)"
fi

if [[ "$BUILD_TYPE" == "Release" ]]; then
    echo "Build type: Release (faster builds, less debugging info)"
fi

echo "=========================================="
echo "Building tests for macOS"
echo "=========================================="
echo "Project root: $PROJECT_ROOT"
echo "Build directory: $BUILD_DIR"
echo "Build type: $BUILD_TYPE"
echo "Address Sanitizer: $ENABLE_ASAN"
echo "Thread Sanitizer: $ENABLE_TSAN"
echo "Code Coverage: $ENABLE_COVERAGE"
echo "All Sources Coverage: $ENABLE_COVERAGE_ALL_SOURCES"
echo "Run ImGui Test Engine: $RUN_IMGUI_TEST_ENGINE"
echo ""

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Check if we should skip CMake reconfiguration
# Reconfigure if cache is missing, or if key options (coverage, ASan, build type) don't match
NEED_RECONFIGURE="YES"
if [[ "$FORCE_RECONFIGURE" == "OFF" ]] && [[ -f "CMakeCache.txt" ]]; then
    CACHE_COVERAGE=$(grep "ENABLE_COVERAGE:BOOL=" CMakeCache.txt 2>/dev/null | cut -d= -f2) || true
    CACHE_ASAN=$(grep "ENABLE_ASAN:BOOL=" CMakeCache.txt 2>/dev/null | cut -d= -f2) || true
    CACHE_TSAN=$(grep "ENABLE_TSAN:BOOL=" CMakeCache.txt 2>/dev/null | cut -d= -f2) || true
    CACHE_BUILD_TYPE=$(grep "CMAKE_BUILD_TYPE:" CMakeCache.txt 2>/dev/null | head -1 | cut -d= -f2) || true
    CACHE_IMGUI_TEST_ENGINE=$(grep "ENABLE_IMGUI_TEST_ENGINE:BOOL=" CMakeCache.txt 2>/dev/null | cut -d= -f2) || true
    CACHE_COVERAGE=${CACHE_COVERAGE:-OFF}
    CACHE_ASAN=${CACHE_ASAN:-OFF}
    CACHE_TSAN=${CACHE_TSAN:-OFF}
    CACHE_BUILD_TYPE=${CACHE_BUILD_TYPE:-Debug}
    CACHE_IMGUI_TEST_ENGINE=${CACHE_IMGUI_TEST_ENGINE:-OFF}
    if grep -q "BUILD_TESTS:BOOL=ON" CMakeCache.txt 2>/dev/null && \
       grep -q "CROSS_PLATFORM_TESTS:BOOL=ON" CMakeCache.txt 2>/dev/null && \
       [[ "$CACHE_COVERAGE" == "$ENABLE_COVERAGE" ]] && \
       [[ "$CACHE_ASAN" == "$ENABLE_ASAN" ]] && \
       [[ "$CACHE_TSAN" == "$ENABLE_TSAN" ]] && \
       [[ "$CACHE_BUILD_TYPE" == "$BUILD_TYPE" ]] && \
       [[ "$CACHE_IMGUI_TEST_ENGINE" == "$RUN_IMGUI_TEST_ENGINE" ]]; then
        NEED_RECONFIGURE="NO"
        echo "Using existing CMake configuration (use --reconfigure to force reconfiguration)"
    else
        echo "CMake cache does not match requested options (e.g. coverage=$ENABLE_COVERAGE, ASan=$ENABLE_ASAN, TSan=$ENABLE_TSAN, ImGui test engine=$RUN_IMGUI_TEST_ENGINE), reconfiguring..."
    fi
fi

# Determine generator (Ninja is faster than Make)
CMAKE_GENERATOR=""
if [[ "$USE_NINJA" == "ON" ]]; then
    if command -v ninja &> /dev/null; then
        CMAKE_GENERATOR="-GNinja"
        echo "Using Ninja generator (faster builds)"
    else
        echo "Warning: Ninja not found, using default generator (install with 'brew install ninja' for faster builds)"
    fi
fi

# Define test targets in an array (must match CMakeLists.txt TEST_DEPENDENCIES / add_test)
# Optional: gemini_api_utils_tests is only built when CURL is found; run loop skips if missing
TEST_TARGETS=(
    "string_search_tests"
    "fuzzy_search_tests"
    "cpu_features_tests"
    "string_search_avx2_tests"
    "string_search_neon_tests"
    "path_utils_tests"
    "path_pattern_matcher_tests"
    "path_pattern_integration_tests"
    "simple_regex_tests"
    "search_pattern_utils_tests"
    "export_search_results_service_tests"
    "regex_generator_utils_tests"
    "string_utils_tests"
    "file_system_utils_tests"
    "lazy_attribute_loader_tests"
    "parallel_search_engine_tests"
    "time_filter_utils_tests"
    "gui_state_tests"
    "file_index_search_strategy_tests"
    "streaming_results_collector_tests"
    "gemini_api_utils_tests"
    "settings_tests"
    "search_context_tests"
    "index_operations_tests"
    "path_operations_tests"
    "directory_resolver_tests"
    "file_index_maintenance_tests"
    "std_regex_utils_tests"
    "search_result_sort_tests"
    "total_size_computation_tests"
    "incremental_search_state_tests"
    "path_pattern_benchmark"
    "search_benchmark"
)

# Configure CMake if needed
if [[ "$NEED_RECONFIGURE" == "YES" ]]; then
    echo "Configuring CMake for tests..."
    CMAKE_ARGS=(
        "$PROJECT_ROOT"
        $CMAKE_GENERATOR
        -DBUILD_TESTS=ON
        -DCROSS_PLATFORM_TESTS=ON
        -DENABLE_ASAN=$ENABLE_ASAN
        -DENABLE_TSAN=$ENABLE_TSAN
        -DENABLE_COVERAGE=$ENABLE_COVERAGE
        -DENABLE_COVERAGE_ALL_SOURCES=$ENABLE_COVERAGE_ALL_SOURCES
        -DENABLE_IMGUI_TEST_ENGINE=$RUN_IMGUI_TEST_ENGINE
        -DCMAKE_BUILD_TYPE=$BUILD_TYPE
        -DCMAKE_CXX_STANDARD=17
    )
    cmake "${CMAKE_ARGS[@]}"
    # Note: No need for manual error check - set -e handles it
else
    echo "Skipping CMake configuration (using existing cache)"
fi

echo ""
echo "Building tests..."
echo "Available test targets:"
for target in "${TEST_TARGETS[@]}"; do
    echo "  - $target"
done
echo ""

# Build all test executables at once (more efficient and proper error handling)
# set -e will exit if build fails
cmake --build . -j$(sysctl -n hw.ncpu)

# Build comprehensive coverage executable if enabled
if [[ "$ENABLE_COVERAGE_ALL_SOURCES" == "ON" ]]; then
    cmake --build . --target coverage_all_sources -j$(sysctl -n hw.ncpu)
fi

echo ""
echo "=========================================="
echo "Running tests..."
echo "=========================================="
echo ""

# Track test failures
TEST_FAILURES=0

# Helper function to run a test with coverage support
# Skips if executable is missing (e.g. gemini_api_utils_tests when CURL not found)
# Optional third argument: extra CLI args (e.g. for search_benchmark --index ... --config ...)
run_test_with_coverage() {
    local test_name=$1
    local test_exe=$2
    local extra_args=${3:-}

    if [[ ! -f "./$test_exe" ]]; then
        echo "Skipping $test_name (executable not built, e.g. optional dependency)"
        return 0
    fi

    echo "Running $test_name..."
    if [[ "$ENABLE_COVERAGE" == "ON" ]]; then
        # Set unique profile file for each test to avoid overwriting
        if LLVM_PROFILE_FILE="${test_name}.profraw" ./"$test_exe" $extra_args; then
            return 0
        else
            TEST_FAILURES=$((TEST_FAILURES + 1))
            return 1
        fi
    else
        if ./"$test_exe" $extra_args; then
            return 0
        else
            TEST_FAILURES=$((TEST_FAILURES + 1))
            return 1
        fi
    fi
}

# Run all tests using the TEST_TARGETS array
for target in "${TEST_TARGETS[@]}"; do
    if [[ "$target" == "search_benchmark" ]]; then
        run_test_with_coverage "$target" "$target" \
            "--index $PROJECT_ROOT/tests/benchmark_index_sample.txt --config $PROJECT_ROOT/tests/benchmark_config_sample.json"
    else
        run_test_with_coverage "$target" "$target"
    fi
    echo ""
done

# Run comprehensive coverage executable if enabled
if [[ "$ENABLE_COVERAGE_ALL_SOURCES" == "ON" ]]; then
    run_test_with_coverage "coverage_all_sources" "coverage_all_sources"
    echo ""
fi

# Run FindHelper with ImGui Test Engine only when --run-imgui-tests is set (costly; skip by default for quick pre-commit check)
FINDHELPER_EXE="$BUILD_DIR/FindHelper.app/Contents/MacOS/FindHelper"
IMGUI_TEST_ENGINE_ON=$(grep -E "ENABLE_IMGUI_TEST_ENGINE:BOOL=ON" "$BUILD_DIR/CMakeCache.txt" 2>/dev/null || true)
if [[ "$RUN_IMGUI_TEST_ENGINE" == "ON" && -x "$FINDHELPER_EXE" && -n "$IMGUI_TEST_ENGINE_ON" ]]; then
    echo "Running FindHelper (ImGui tests then exit) with --show-metrics..."
    INDEX_FILE="$PROJECT_ROOT/tests/data/std-linux-filesystem.txt"
    if [[ ! -f "$INDEX_FILE" ]]; then
        echo "  Note: $INDEX_FILE not found; regression tests may fail (other tests still run)."
    fi
    # Absolute path for index_configuration test (select_folder_and_start_indexing) so it passes regardless of CWD
    export IMGUI_TEST_INDEX_FOLDER="$PROJECT_ROOT/tests/data"
    if [[ "$ENABLE_COVERAGE" == "ON" ]]; then
        if (cd "$PROJECT_ROOT" && LLVM_PROFILE_FILE="$BUILD_DIR/FindHelper_%p.profraw" "$FINDHELPER_EXE" \
            --run-imgui-tests-and-exit \
            --show-metrics \
            --index-from-file=tests/data/std-linux-filesystem.txt); then
            : "FindHelper ImGui tests passed"
        else
            TEST_FAILURES=$((TEST_FAILURES + 1))
        fi
    else
        if (cd "$PROJECT_ROOT" && "$FINDHELPER_EXE" \
            --run-imgui-tests-and-exit \
            --show-metrics \
            --index-from-file=tests/data/std-linux-filesystem.txt); then
            : "FindHelper ImGui tests passed"
        else
            TEST_FAILURES=$((TEST_FAILURES + 1))
        fi
    fi
    echo ""
elif [[ -x "$FINDHELPER_EXE" && -n "$IMGUI_TEST_ENGINE_ON" ]]; then
    echo "Skipping ImGui Test Engine (use --run-imgui-tests to run it)."
    echo ""
fi

echo ""
echo "=========================================="
if [[ $TEST_FAILURES -eq 0 ]]; then
    echo "All tests completed successfully!"
else
    echo "ERROR: $TEST_FAILURES test(s) failed!" >&2
    exit 1
fi
echo "=========================================="

# If coverage was enabled, remind user how to generate report
if [[ "$ENABLE_COVERAGE" == "ON" ]]; then
    echo ""
    echo "Coverage data has been generated (.profraw files)"
    echo "To generate coverage report, run:"
    echo "  ./scripts/generate_coverage_macos.sh"
    echo ""
fi
