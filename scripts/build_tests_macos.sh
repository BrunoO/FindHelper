#!/bin/bash
# Build and run tests on macOS for USN_WINDOWS project
# This allows running tests locally on macOS without Windows build environment

set -eo pipefail  # Exit on error; pipeline fails if any command in it fails

# Internal worker invocation: execute a single test in parallel, format output, and log
if [[ "$1" == "--internal-worker" ]]; then
    _res_dir="$2"
    _cov="$3"
    _target="$4"
    shift 4
    _extra_args="$*"

    _exe="./$_target"
    if [[ ! -f "$_exe" ]]; then
        exit 0
    fi

    _log="$_res_dir/${_target}.log"
    _t0=$(python3 -c "import time; print(time.time())" 2>/dev/null || date +%s)

    if [[ "$_cov" == "ON" ]]; then
        if LLVM_PROFILE_FILE="${_target}.profraw" "$_exe" $_extra_args > "$_log" 2>&1; then
            _t1=$(python3 -c "import time; print(time.time())" 2>/dev/null || date +%s)
            _d=$(python3 -c "print(f'{float($_t1)-float($_t0):.2f}')" 2>/dev/null || echo "0.00")
            printf "  \033[32m[PASS]\033[0m %-45s (%ss)\n" "$_target" "$_d"
            exit 0
        else
            _exit_code=$?
            _t1=$(python3 -c "import time; print(time.time())" 2>/dev/null || date +%s)
            _d=$(python3 -c "print(f'{float($_t1)-float($_t0):.2f}')" 2>/dev/null || echo "0.00")
            printf "  \033[31m[FAIL]\033[0m %-45s (%ss, exit %d)\n" "$_target" "$_d" "$_exit_code"
            touch "$_res_dir/${_target}.failed"
            exit "$_exit_code"
        fi
    else
        if "$_exe" $_extra_args > "$_log" 2>&1; then
            _t1=$(python3 -c "import time; print(time.time())" 2>/dev/null || date +%s)
            _d=$(python3 -c "print(f'{float($_t1)-float($_t0):.2f}')" 2>/dev/null || echo "0.00")
            printf "  \033[32m[PASS]\033[0m %-45s (%ss)\n" "$_target" "$_d"
            exit 0
        else
            _exit_code=$?
            _t1=$(python3 -c "import time; print(time.time())" 2>/dev/null || date +%s)
            _d=$(python3 -c "print(f'{float($_t1)-float($_t0):.2f}')" 2>/dev/null || echo "0.00")
            printf "  \033[31m[FAIL]\033[0m %-45s (%ss, exit %d)\n" "$_target" "$_d" "$_exit_code"
            touch "$_res_dir/${_target}.failed"
            exit "$_exit_code"
        fi
    fi
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_DIR="$PROJECT_ROOT/build_tests"

# ASan is ON by default (catches memory bugs like use-after-free). TSAN and ASAN are mutually exclusive.
ENABLE_ASAN="ON"
ENABLE_TSAN="OFF"
# UBSan is OFF by default (catches undefined behavior; composes with ASan and TSan).
ENABLE_UBSAN="OFF"
ENABLE_COVERAGE="OFF"
ENABLE_COVERAGE_ALL_SOURCES="OFF"
BUILD_TYPE="Debug"  # Debug for debugging, Release for faster builds
USE_NINJA="ON"      # Use Ninja generator for faster builds (if available)
FORCE_RECONFIGURE="OFF"  # Force CMake reconfiguration (default: skip if cache exists)
# ImGui Test Engine (FindHelper --run-imgui-tests-and-exit) is costly; run only when requested
RUN_IMGUI_TEST_ENGINE="OFF"

# Execution mode options
RUN_SMOKE="OFF"
SPECIFIED_TARGETS=()
PARALLEL_JOBS=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
SKIP_BENCHMARKS="OFF"
RUN_FULL_BENCHMARKS="OFF"
EXTRA_TEST_ARGS=""

# Curated core targets for fast smoke test runs
SMOKE_TARGETS=(
    "string_search_tests"
    "fuzzy_search_tests"
    "path_utils_tests"
    "path_pattern_matcher_tests"
    "parallel_search_engine_tests"
    "index_operations_tests"
    "search_controller_tests"
    "gui_state_tests"
    "incremental_search_state_tests"
    "settings_tests"
    "raw_mft_record_parser_tests"
    "mft_runlist_decoder_tests"
    "usn_journal_queue_tests"
)

# Show help message
show_help() {
    cat << EOF
Usage: $0 [OPTIONS] [-- <test-args>]

Build and run tests on macOS for USN_WINDOWS project.

Execution Scope Options:
    --smoke, -s         Run fast smoke tests only (~14 core unit test targets, <2s run time)
    --target, -t NAME   Build and run specific test target(s) (e.g. -t gui_state_tests or -t t1,t2)
    --jobs, -j N        Number of parallel test jobs (default: number of CPU cores)
    --skip-benchmarks   Skip benchmarks during test execution
    --with-benchmarks   Run full benchmarks including the 706k-path Linux filesystem hotpath/extension

Build & Sanitizer Options:
    --no-asan           Disable Address Sanitizer (faster build, but won't catch memory bugs)
    --tsan              Enable Thread Sanitizer (data-race detection; disables ASAN, macOS/Linux only)
    --ubsan             Enable UndefinedBehavior Sanitizer (UB detection; composes with ASan/TSan)
    --coverage          Enable code coverage (generates .profraw files for coverage reports)
    --all-sources       Enable comprehensive coverage (includes all source files, requires --coverage)
    --release           Use Release build type (faster builds, less debugging info)
    --no-ninja          Use Make instead of Ninja (if Ninja causes issues)
    --reconfigure       Force CMake reconfiguration (skip cache check)
    --run-imgui-tests   Run ImGui Test Engine (FindHelper --run-imgui-tests-and-exit); costly, off by default
    --help, -h          Show this help message

Examples:
    $0                          # Build and run all unit tests in parallel (fast benchmark smoke check)
    $0 --smoke                  # Fast smoke test suite (<2s run time)
    $0 --target gui_state_tests # Build and run a single test target directly in foreground
    $0 -t gui_state_tests -- -tc="RemapSelection*" # Single test with doctest test-case filter
    $0 --skip-benchmarks        # Run all unit tests in parallel, skipping all benchmarks
    $0 --with-benchmarks        # Run all unit tests and full heavy search benchmarks (706k paths)
    $0 --no-asan                # Build without Address Sanitizer (faster compile)
    $0 --tsan                   # Build with Thread Sanitizer (data-race detection)
    $0 --coverage               # Build with code coverage enabled
EOF
    exit 0
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --help|-h)
            show_help
            ;;
        --smoke|-s)
            RUN_SMOKE="ON"
            shift
            ;;
        --target|-t)
            if [[ $# -lt 2 || "$2" == --* ]]; then
                echo "ERROR: --target requires a target name" >&2
                exit 1
            fi
            IFS=',' read -r -a _t_list <<< "$2"
            for _t in "${_t_list[@]}"; do
                SPECIFIED_TARGETS+=("$_t")
            done
            shift 2
            ;;
        --target=*)
            _raw="${1#*=}"
            IFS=',' read -r -a _t_list <<< "$_raw"
            for _t in "${_t_list[@]}"; do
                SPECIFIED_TARGETS+=("$_t")
            done
            shift
            ;;
        --jobs|-j)
            if [[ $# -lt 2 || "$2" == --* ]]; then
                echo "ERROR: --jobs requires an integer" >&2
                exit 1
            fi
            PARALLEL_JOBS="$2"
            shift 2
            ;;
        --jobs=*)
            PARALLEL_JOBS="${1#*=}"
            shift
            ;;
        --skip-benchmarks|--no-benchmarks)
            SKIP_BENCHMARKS="ON"
            shift
            ;;
        --with-benchmarks|--full-benchmarks)
            RUN_FULL_BENCHMARKS="ON"
            shift
            ;;
        --no-asan)
            ENABLE_ASAN="OFF"
            shift
            ;;
        --tsan)
            ENABLE_TSAN="ON"
            ENABLE_ASAN="OFF"
            shift
            ;;
        --ubsan)
            ENABLE_UBSAN="ON"
            shift
            ;;
        --coverage)
            ENABLE_COVERAGE="ON"
            shift
            ;;
        --all-sources)
            ENABLE_COVERAGE_ALL_SOURCES="ON"
            ENABLE_COVERAGE="ON"
            shift
            ;;
        --release)
            BUILD_TYPE="Release"
            shift
            ;;
        --no-ninja)
            USE_NINJA="OFF"
            shift
            ;;
        --reconfigure)
            FORCE_RECONFIGURE="ON"
            shift
            ;;
        --run-imgui-tests)
            RUN_IMGUI_TEST_ENGINE="ON"
            shift
            ;;
        --)
            shift
            EXTRA_TEST_ARGS="$*"
            break
            ;;
        *)
            echo "ERROR: Unknown option: $1" >&2
            echo "Use --help for usage information" >&2
            exit 1
            ;;
    esac
done

if [[ "$RUN_SMOKE" == "ON" && ${#SPECIFIED_TARGETS[@]} -gt 0 ]]; then
    echo "ERROR: Cannot specify both --smoke and --target" >&2
    exit 1
fi

if [[ "$ENABLE_ASAN" == "OFF" && "$ENABLE_TSAN" == "OFF" ]]; then
    echo "Address Sanitizer DISABLED (faster build, but won't catch memory bugs)"
fi
if [[ "$ENABLE_TSAN" == "ON" ]]; then
    echo "Thread Sanitizer ENABLED (data-race detection; ASAN disabled)"
fi
if [[ "$ENABLE_UBSAN" == "ON" ]]; then
    echo "UndefinedBehavior Sanitizer ENABLED (undefined behavior detection)"
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

# Single source of truth: scripts/test_targets.txt (one name per line).
TEST_TARGETS=()
while IFS= read -r line; do
    [[ -n "$line" ]] && TEST_TARGETS+=("$line")
done < "${SCRIPT_DIR}/test_targets.txt"

# Validate specified targets if any
if [[ ${#SPECIFIED_TARGETS[@]} -gt 0 ]]; then
    for _target in "${SPECIFIED_TARGETS[@]}"; do
        _found="NO"
        for _valid in "${TEST_TARGETS[@]}" "coverage_all_sources" "find_helper"; do
            if [[ "$_target" == "$_valid" ]]; then
                _found="YES"
                break
            fi
        done
        if [[ "$_found" == "NO" ]]; then
            echo "ERROR: Unknown test target: '$_target'" >&2
            echo "See scripts/test_targets.txt for valid target names." >&2
            exit 1
        fi
    done
fi

# Determine active target list
ACTIVE_TARGETS=()
if [[ ${#SPECIFIED_TARGETS[@]} -gt 0 ]]; then
    ACTIVE_TARGETS=("${SPECIFIED_TARGETS[@]}")
    echo "Target filter active: ${ACTIVE_TARGETS[*]}"
elif [[ "$RUN_SMOKE" == "ON" ]]; then
    ACTIVE_TARGETS=("${SMOKE_TARGETS[@]}")
    echo "Smoke test mode active (${#ACTIVE_TARGETS[@]} core targets)"
else
    ACTIVE_TARGETS=("${TEST_TARGETS[@]}")
fi

echo "=========================================="
echo "Building tests for macOS"
echo "=========================================="
echo "Project root: $PROJECT_ROOT"
echo "Build directory: $BUILD_DIR"
echo "Build type: $BUILD_TYPE"
echo "Address Sanitizer: $ENABLE_ASAN"
echo "Thread Sanitizer: $ENABLE_TSAN"
echo "UndefinedBehavior Sanitizer: $ENABLE_UBSAN"
echo "Code Coverage: $ENABLE_COVERAGE"
echo "All Sources Coverage: $ENABLE_COVERAGE_ALL_SOURCES"
echo "Run ImGui Test Engine: $RUN_IMGUI_TEST_ENGINE"
echo "Parallel Test Jobs: $PARALLEL_JOBS"
echo ""

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Application target (FindHelper.app) is only needed when running ImGui test engine
BUILD_APPLICATION="OFF"
if [[ "$RUN_IMGUI_TEST_ENGINE" == "ON" ]]; then
    BUILD_APPLICATION="ON"
fi

# Check if we should skip CMake reconfiguration
NEED_RECONFIGURE="YES"
if [[ "$FORCE_RECONFIGURE" == "OFF" ]] && [[ -f "CMakeCache.txt" ]]; then
    CACHE_COVERAGE=$(grep "ENABLE_COVERAGE:BOOL=" CMakeCache.txt 2>/dev/null | cut -d= -f2) || true
    CACHE_ASAN=$(grep "ENABLE_ASAN:BOOL=" CMakeCache.txt 2>/dev/null | cut -d= -f2) || true
    CACHE_TSAN=$(grep "ENABLE_TSAN:BOOL=" CMakeCache.txt 2>/dev/null | cut -d= -f2) || true
    CACHE_UBSAN=$(grep "ENABLE_UBSAN:BOOL=" CMakeCache.txt 2>/dev/null | cut -d= -f2) || true
    CACHE_BUILD_TYPE=$(grep "CMAKE_BUILD_TYPE:" CMakeCache.txt 2>/dev/null | head -1 | cut -d= -f2) || true
    CACHE_IMGUI_TEST_ENGINE=$(grep "ENABLE_IMGUI_TEST_ENGINE:BOOL=" CMakeCache.txt 2>/dev/null | cut -d= -f2) || true
    CACHE_BUILD_APP=$(grep "BUILD_APPLICATION:BOOL=" CMakeCache.txt 2>/dev/null | cut -d= -f2) || true
    CACHE_COVERAGE=${CACHE_COVERAGE:-OFF}
    CACHE_ASAN=${CACHE_ASAN:-OFF}
    CACHE_TSAN=${CACHE_TSAN:-OFF}
    CACHE_UBSAN=${CACHE_UBSAN:-OFF}
    CACHE_BUILD_TYPE=${CACHE_BUILD_TYPE:-Debug}
    CACHE_IMGUI_TEST_ENGINE=${CACHE_IMGUI_TEST_ENGINE:-OFF}
    CACHE_BUILD_APP=${CACHE_BUILD_APP:-ON}
    if grep -q "BUILD_TESTS:BOOL=ON" CMakeCache.txt 2>/dev/null && \
       [[ "$CACHE_BUILD_APP" == "$BUILD_APPLICATION" ]] && \
       [[ "$CACHE_COVERAGE" == "$ENABLE_COVERAGE" ]] && \
       [[ "$CACHE_ASAN" == "$ENABLE_ASAN" ]] && \
       [[ "$CACHE_TSAN" == "$ENABLE_TSAN" ]] && \
       [[ "$CACHE_UBSAN" == "$ENABLE_UBSAN" ]] && \
       [[ "$CACHE_BUILD_TYPE" == "$BUILD_TYPE" ]] && \
       [[ "$CACHE_IMGUI_TEST_ENGINE" == "$RUN_IMGUI_TEST_ENGINE" ]]; then
        NEED_RECONFIGURE="NO"
        echo "Using existing CMake configuration (use --reconfigure to force reconfiguration)"
    else
        echo "CMake cache does not match requested options, reconfiguring..."
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

# Configure CMake if needed
if [[ "$NEED_RECONFIGURE" == "YES" ]]; then
    echo "Configuring CMake for tests..."
    CMAKE_ARGS=(
        "$PROJECT_ROOT"
        $CMAKE_GENERATOR
        -DBUILD_TESTS=ON
        -DBUILD_APPLICATION=$BUILD_APPLICATION
        -DENABLE_ASAN=$ENABLE_ASAN
        -DENABLE_TSAN=$ENABLE_TSAN
        -DENABLE_UBSAN=$ENABLE_UBSAN
        -DENABLE_COVERAGE=$ENABLE_COVERAGE
        -DENABLE_COVERAGE_ALL_SOURCES=$ENABLE_COVERAGE_ALL_SOURCES
        -DENABLE_IMGUI_TEST_ENGINE=$RUN_IMGUI_TEST_ENGINE
        -DCMAKE_BUILD_TYPE=$BUILD_TYPE
        -DCMAKE_CXX_STANDARD=17
    )
    cmake "${CMAKE_ARGS[@]}"
else
    echo "Skipping CMake configuration (using existing cache)"
fi

echo ""
echo "Building targets..."

# Build targets (only active targets when --target or --smoke specified)
N_CPU=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
if [[ ${#SPECIFIED_TARGETS[@]} -gt 0 || "$RUN_SMOKE" == "ON" ]]; then
    echo "Building ${#ACTIVE_TARGETS[@]} target(s):"
    for target in "${ACTIVE_TARGETS[@]}"; do
        echo "  - $target"
    done
    cmake --build . --target "${ACTIVE_TARGETS[@]}" -j"$N_CPU"
else
    echo "Building all tests..."
    cmake --build . -j"$N_CPU"
    if [[ "$ENABLE_COVERAGE_ALL_SOURCES" == "ON" ]]; then
        cmake --build . --target coverage_all_sources -j"$N_CPU"
    fi
fi

echo ""
echo "=========================================="
echo "Running tests..."
echo "=========================================="
echo ""

TEST_FAILURES=0

# Helper function to run a test sequentially with coverage support
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

# CASE 1: Single target specified -> run directly in foreground for interactive doctest output
if [[ ${#ACTIVE_TARGETS[@]} -eq 1 ]]; then
    single_target="${ACTIVE_TARGETS[0]}"
    if [[ "$single_target" == "search_benchmark" ]]; then
        run_test_with_coverage "$single_target" "$single_target" \
            "--index $PROJECT_ROOT/tests/benchmark_index_sample.txt --config $PROJECT_ROOT/tests/benchmark_config_sample.json $EXTRA_TEST_ARGS"
        if [[ "$RUN_FULL_BENCHMARKS" == "ON" ]]; then
            STD_LINUX_INDEX="$PROJECT_ROOT/tests/data/std-linux-filesystem.txt"
            STD_LINUX_BENCH_CONFIG="$PROJECT_ROOT/tests/benchmark_config_std_linux_filename_hotpath.json"
            if [[ -f "$STD_LINUX_INDEX" && -f "$STD_LINUX_BENCH_CONFIG" ]]; then
                run_test_with_coverage "search_benchmark_std_linux_filename" "$single_target" \
                    "--index $STD_LINUX_INDEX --config $STD_LINUX_BENCH_CONFIG --iterations 1 --warmup 0"
            fi
            STD_LINUX_EXT_CONFIG="$PROJECT_ROOT/tests/benchmark_config_extension_only.json"
            if [[ -f "$STD_LINUX_INDEX" && -f "$STD_LINUX_EXT_CONFIG" ]]; then
                run_test_with_coverage "search_benchmark_std_linux_extension" "$single_target" \
                    "--index $STD_LINUX_INDEX --config $STD_LINUX_EXT_CONFIG --iterations 1 --warmup 0"
            fi
        fi
    else
        run_test_with_coverage "$single_target" "$single_target" "$EXTRA_TEST_ARGS"
    fi

# CASE 2: Multiple targets -> run unit tests in parallel, then benchmarks sequentially
else
    UNIT_TESTS=()
    BENCHMARK_TARGETS=()

    for target in "${ACTIVE_TARGETS[@]}"; do
        if [[ "$target" == *"benchmark"* ]]; then
            if [[ "$SKIP_BENCHMARKS" != "ON" && "$RUN_SMOKE" != "ON" ]]; then
                BENCHMARK_TARGETS+=("$target")
            fi
        else
            UNIT_TESTS+=("$target")
        fi
    done

    # Run unit tests in parallel
    if [[ ${#UNIT_TESTS[@]} -gt 0 ]]; then
        echo "Running ${#UNIT_TESTS[@]} unit test targets in parallel ($PARALLEL_JOBS jobs)..."
        TMP_RESULTS_DIR="$BUILD_DIR/.test_run_tmp_$$"
        rm -rf "$TMP_RESULTS_DIR"
        mkdir -p "$TMP_RESULTS_DIR"
        trap 'rm -rf "$TMP_RESULTS_DIR"' EXIT

        # Pass targets through xargs worker
        printf "%s\n" "${UNIT_TESTS[@]}" | \
            xargs -n 1 -P "$PARALLEL_JOBS" "$SCRIPT_DIR/build_tests_macos.sh" --internal-worker "$TMP_RESULTS_DIR" "$ENABLE_COVERAGE"

        # Check for failures and report
        for fail_file in "$TMP_RESULTS_DIR"/*.failed; do
            if [[ -f "$fail_file" ]]; then
                TEST_FAILURES=$((TEST_FAILURES + 1))
                failed_name="$(basename "$fail_file" .failed)"
                echo ""
                echo "=========================================="
                echo "FAILURE: $failed_name"
                echo "=========================================="
                if [[ -f "$TMP_RESULTS_DIR/${failed_name}.log" ]]; then
                    cat "$TMP_RESULTS_DIR/${failed_name}.log"
                fi
                echo "=========================================="
            fi
        done

        rm -rf "$TMP_RESULTS_DIR"
        trap - EXIT
    fi

    # Run benchmarks sequentially (if any)
    if [[ ${#BENCHMARK_TARGETS[@]} -gt 0 ]]; then
        echo ""
        echo "Running benchmarks sequentially..."
        for target in "${BENCHMARK_TARGETS[@]}"; do
            if [[ "$target" == "search_benchmark" ]]; then
                run_test_with_coverage "$target" "$target" \
                    "--index $PROJECT_ROOT/tests/benchmark_index_sample.txt --config $PROJECT_ROOT/tests/benchmark_config_sample.json"
                if [[ "$RUN_FULL_BENCHMARKS" == "ON" ]]; then
                    STD_LINUX_INDEX="$PROJECT_ROOT/tests/data/std-linux-filesystem.txt"
                    STD_LINUX_BENCH_CONFIG="$PROJECT_ROOT/tests/benchmark_config_std_linux_filename_hotpath.json"
                    if [[ -f "$STD_LINUX_INDEX" && -f "$STD_LINUX_BENCH_CONFIG" ]]; then
                        run_test_with_coverage "search_benchmark_std_linux_filename" "$target" \
                            "--index $STD_LINUX_INDEX --config $STD_LINUX_BENCH_CONFIG --iterations 1 --warmup 0"
                    fi
                    STD_LINUX_EXT_CONFIG="$PROJECT_ROOT/tests/benchmark_config_extension_only.json"
                    if [[ -f "$STD_LINUX_INDEX" && -f "$STD_LINUX_EXT_CONFIG" ]]; then
                        run_test_with_coverage "search_benchmark_std_linux_extension" "$target" \
                            "--index $STD_LINUX_INDEX --config $STD_LINUX_EXT_CONFIG --iterations 1 --warmup 0"
                    fi
                else
                    echo "  (Note: Skipping heavy 706k-path Linux benchmarks; use --with-benchmarks to run them)"
                fi
            else
                run_test_with_coverage "$target" "$target"
            fi
            echo ""
        done
    elif [[ "$SKIP_BENCHMARKS" == "ON" ]]; then
        echo "Skipping benchmarks (--skip-benchmarks enabled)."
    fi
fi

# Run comprehensive coverage executable if enabled and not in smoke/single target mode
if [[ "$ENABLE_COVERAGE_ALL_SOURCES" == "ON" && "$RUN_SMOKE" != "ON" && ${#SPECIFIED_TARGETS[@]} -eq 0 ]]; then
    run_test_with_coverage "coverage_all_sources" "coverage_all_sources"
    echo ""
fi

# Run FindHelper with ImGui Test Engine only when --run-imgui-tests is set
FINDHELPER_EXE="$BUILD_DIR/FindHelper.app/Contents/MacOS/FindHelper"
IMGUI_TEST_ENGINE_ON=$(grep -E "ENABLE_IMGUI_TEST_ENGINE:BOOL=ON" "$BUILD_DIR/CMakeCache.txt" 2>/dev/null || true)
if [[ "$RUN_IMGUI_TEST_ENGINE" == "ON" && -x "$FINDHELPER_EXE" && -n "$IMGUI_TEST_ENGINE_ON" && "$RUN_SMOKE" != "ON" && ${#SPECIFIED_TARGETS[@]} -eq 0 ]]; then
    echo "Running FindHelper (ImGui tests then exit) with --show-metrics..."
    INDEX_FILE="$PROJECT_ROOT/tests/data/std-linux-filesystem.txt"
    if [[ ! -f "$INDEX_FILE" ]]; then
        echo "  Note: $INDEX_FILE not found; regression tests may fail (other tests still run)."
    fi
    export IMGUI_TEST_INDEX_FOLDER="$PROJECT_ROOT/tests/data/fixture"
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
elif [[ -x "$FINDHELPER_EXE" && -n "$IMGUI_TEST_ENGINE_ON" && "$RUN_SMOKE" != "ON" && ${#SPECIFIED_TARGETS[@]} -eq 0 ]]; then
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
