#!/bin/bash
# Generate code coverage report for macOS tests
# Usage: ./scripts/generate_coverage_macos.sh [--show-missing] [--per-binary]
#
# Prerequisites:
#   1. Tests must be built with -DENABLE_COVERAGE=ON (e.g. ./scripts/build_tests_macos.sh --coverage)
#   2. Tests must have been run (to generate .profraw files).
#      For app/FindHelper coverage, run: ./scripts/build_tests_macos.sh --coverage --run-imgui-tests
#      This script does not launch the app; it only merges existing .profraw and generates the report.
#
# Options:
#   --show-missing    Show list of source files not in coverage report
#   --per-binary      Per-executable coverage with LCOV merge to avoid llvm-cov
#                    \"functions have mismatched data\" warnings. Requires lcov and genhtml.
#
# Output:
#   - coverage/coverage_summary.txt: Text summary of coverage
#   - coverage/html/index.html: HTML coverage report
#   - coverage/missing_files.txt: List of files not in coverage (if --show-missing)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build_tests"
COVERAGE_DIR="$PROJECT_ROOT/coverage"

# Constant for separator line (used multiple times)
SEPARATOR_LINE="=========================================="

SHOW_MISSING="OFF"
PER_BINARY_MODE="OFF"

for arg in "$@"; do
    case "$arg" in
        --show-missing)
            SHOW_MISSING="ON"
            ;;
        --per-binary)
            PER_BINARY_MODE="ON"
            ;;
        *)
            echo "ERROR: Unknown option: $arg" >&2
            echo "Usage: $0 [--show-missing] [--per-binary]" >&2
            exit 1
            ;;
    esac
done

echo "$SEPARATOR_LINE"
echo "Generating Code Coverage Report"
echo "=========================================="
echo "Project root: $PROJECT_ROOT"
echo "Build directory: $BUILD_DIR"
echo "Coverage directory: $COVERAGE_DIR"
echo ""

# Check if coverage was enabled
if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    echo "ERROR: Build directory not found: $BUILD_DIR" >&2
    echo "Build tests first: ./scripts/build_tests_macos.sh --coverage" >&2
    exit 1
fi

if ! grep -q "ENABLE_COVERAGE:BOOL=ON" "$BUILD_DIR/CMakeCache.txt" 2>/dev/null; then
    echo "ERROR: Coverage was not enabled during build." >&2
    echo "Rebuild with: ./scripts/build_tests_macos.sh --coverage" >&2
    exit 1
fi

# Detect compiler (Clang or GCC)
# Try multiple methods: check for coverage files first, then check CMakeCache.txt
COMPILER_ID=""

# Method 1: Check for coverage files (most reliable)
if find "$BUILD_DIR" -name "*.profraw" -type f 2>/dev/null | grep -q .; then
    COMPILER_ID="Clang"
elif find "$BUILD_DIR" -name "*.gcda" -type f 2>/dev/null | grep -q .; then
    COMPILER_ID="GNU"
fi

# Method 2: Check CMakeCache.txt if still unknown
if [[ -z "$COMPILER_ID" && -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    # Try various CMake cache formats
    if grep -qiE "CMAKE_CXX_COMPILER_ID.*Clang|CMAKE_CXX_COMPILER_ID.*AppleClang" "$BUILD_DIR/CMakeCache.txt" 2>/dev/null; then
        COMPILER_ID="Clang"
    elif grep -qiE "CMAKE_CXX_COMPILER_ID.*GNU|CMAKE_CXX_COMPILER_ID.*GCC" "$BUILD_DIR/CMakeCache.txt" 2>/dev/null; then
        COMPILER_ID="GNU"
    fi
fi

# Method 3: Check if llvm-cov is available (indicates Clang toolchain)
if [[ -z "$COMPILER_ID" ]] && command -v llvm-cov >/dev/null 2>&1; then
    COMPILER_ID="Clang"
fi

# If still unknown, default to Clang on macOS (most common)
if [[ -z "$COMPILER_ID" ]]; then
    if [[ "$OSTYPE" == "darwin"* ]]; then
        COMPILER_ID="Clang"
        echo "WARNING: Could not detect compiler, defaulting to Clang (typical for macOS)" >&2
    else
        echo "ERROR: Could not detect compiler type. Only Clang and GCC are supported." >&2
        exit 1
    fi
fi

echo "Detected compiler: $COMPILER_ID"
echo ""

# Create coverage output directory
mkdir -p "$COVERAGE_DIR"

if [[ "$COMPILER_ID" == "Clang" ]]; then
    # Clang uses .profraw files
    # Find llvm-profdata and llvm-cov using xcrun (standard on macOS)
    LLVM_PROFDATA=$(xcrun --find llvm-profdata 2>/dev/null || which llvm-profdata 2>/dev/null || echo "")
    LLVM_COV=$(xcrun --find llvm-cov 2>/dev/null || which llvm-cov 2>/dev/null || echo "")
    
    if [[ -z "$LLVM_PROFDATA" || -z "$LLVM_COV" ]]; then
        echo "ERROR: llvm-profdata or llvm-cov not found." >&2
        echo "Install Xcode Command Line Tools: xcode-select --install" >&2
        exit 1
    fi

    # Do not run FindHelper here. App coverage is produced by build_tests_macos.sh when you run:
    #   ./scripts/build_tests_macos.sh --coverage --run-imgui-tests
    # This script only merges existing .profraw and generates the report (no separate app launch).

    echo "Looking for .profraw files..."
    # Find all .profraw files recursively in build directory
    PROFRAW_FILES=$(find "$BUILD_DIR" -name "*.profraw" -type f 2>/dev/null | sort || true)
    
    if [[ -z "$PROFRAW_FILES" ]]; then
        echo "ERROR: No .profraw files found. Did you run the tests?" >&2
        echo "Run tests first: ./scripts/build_tests_macos.sh --coverage" >&2
        echo "" >&2
        echo "Note: Each test executable should generate its own .profraw file." >&2
        echo "If you see only 'default.profraw', tests may have been run without coverage enabled." >&2
        exit 1
    fi
    
    echo "Found .profraw files..."
    # Count files for display (handle case where find returns single line)
    PROFRAW_COUNT=$(echo "$PROFRAW_FILES" | grep -c . || echo "0")
    echo "  Found $PROFRAW_COUNT .profraw file(s):"
    echo "$PROFRAW_FILES" | sed 's|^|    |'
    echo ""
    
    # Find test executables (must match TEST_TARGETS in build_tests_macos.sh so coverage is cumulative)
    TEST_EXECUTABLES=(
        "$BUILD_DIR/string_search_tests"
        "$BUILD_DIR/fuzzy_search_tests"
        "$BUILD_DIR/cpu_features_tests"
        "$BUILD_DIR/string_search_avx2_tests"
        "$BUILD_DIR/string_search_neon_tests"
        "$BUILD_DIR/path_utils_tests"
        "$BUILD_DIR/path_pattern_matcher_tests"
        "$BUILD_DIR/path_pattern_integration_tests"
        "$BUILD_DIR/simple_regex_tests"
        "$BUILD_DIR/search_pattern_utils_tests"
        "$BUILD_DIR/export_search_results_service_tests"
        "$BUILD_DIR/regex_generator_utils_tests"
        "$BUILD_DIR/string_utils_tests"
        "$BUILD_DIR/file_system_utils_tests"
        "$BUILD_DIR/lazy_attribute_loader_tests"
        "$BUILD_DIR/parallel_search_engine_tests"
        "$BUILD_DIR/time_filter_utils_tests"
        "$BUILD_DIR/gui_state_tests"
        "$BUILD_DIR/file_index_search_strategy_tests"
        "$BUILD_DIR/streaming_results_collector_tests"
        "$BUILD_DIR/gemini_api_utils_tests"
        "$BUILD_DIR/settings_tests"
        "$BUILD_DIR/search_context_tests"
        "$BUILD_DIR/index_operations_tests"
        "$BUILD_DIR/path_operations_tests"
        "$BUILD_DIR/directory_resolver_tests"
        "$BUILD_DIR/file_index_maintenance_tests"
        "$BUILD_DIR/std_regex_utils_tests"
        "$BUILD_DIR/search_result_sort_tests"
        "$BUILD_DIR/total_size_computation_tests"
        "$BUILD_DIR/incremental_search_state_tests"
    )
    
    # Add comprehensive coverage executable if it exists
    if [[ -f "$BUILD_DIR/coverage_all_sources" ]]; then
        TEST_EXECUTABLES+=("$BUILD_DIR/coverage_all_sources")
        echo "  Including comprehensive coverage executable (all source files)"
    fi

    # Add FindHelper app if present (ImGui test engine coverage)
    if [[ -f "$BUILD_DIR/FindHelper.app/Contents/MacOS/FindHelper" ]]; then
        TEST_EXECUTABLES+=("$BUILD_DIR/FindHelper.app/Contents/MacOS/FindHelper")
        echo "  Including FindHelper app (ImGui test engine coverage)"
    fi
    
    # Filter to only existing executables
    EXISTING_EXECUTABLES=()
    for exe in "${TEST_EXECUTABLES[@]}"; do
        if [[ -f "$exe" ]]; then
            EXISTING_EXECUTABLES+=("$exe")
        fi
    done
    
    if [[ ${#EXISTING_EXECUTABLES[@]} -eq 0 ]]; then
        echo "ERROR: No test executables found in $BUILD_DIR" >&2
        echo "Build tests first: ./scripts/build_tests_macos.sh --coverage" >&2
        exit 1
    fi

    # Per-binary LCOV mode: generate coverage per executable and merge at LCOV level
    if [[ "$PER_BINARY_MODE" == "ON" ]]; then
        echo "Per-binary LCOV mode enabled (--per-binary)."
        echo "  and merging at LCOV level. Requires lcov and genhtml (e.g. brew install lcov)."
        echo ""

        LCOV_BIN=$(which lcov 2>/dev/null || echo "")
        GENHTML_BIN=$(which genhtml 2>/dev/null || echo "")

        if [[ -z "$LCOV_BIN" || -z "$GENHTML_BIN" ]]; then
            echo "WARNING: lcov or genhtml not found in PATH." >&2
            echo "         Falling back to single merged llvm-cov report (mismatched data warnings" >&2
            echo "         may still occur). To enable per-binary LCOV mode, install:" >&2
            echo "           brew install lcov" >&2
            echo ""
            PER_BINARY_MODE="OFF"
        fi
    fi

    if [[ "$PER_BINARY_MODE" == "ON" ]]; then
        echo "Generating per-binary coverage and merging with LCOV..."

        # Clean any previous LCOV artifacts
        rm -f "$COVERAGE_DIR"/coverage_*.profdata \
              "$COVERAGE_DIR"/coverage_*.json \
              "$COVERAGE_DIR"/coverage_*.info \
              "$COVERAGE_DIR/coverage.info"

        PER_BINARY_INFO_FILES=()

        for exe in "${EXISTING_EXECUTABLES[@]}"; do
            exe_name="$(basename "$exe")"

            # Map executable to its .profraw files:
            # - Unit tests and coverage_all_sources: <name>.profraw in build dir
            # - FindHelper: FindHelper_*.profraw in build dir
            profraw_pattern=""
            if [[ "$exe" == *"FindHelper.app/Contents/MacOS/FindHelper" ]]; then
                profraw_pattern="$BUILD_DIR/FindHelper_*.profraw"
            else
                profraw_pattern="$BUILD_DIR/${exe_name}.profraw"
            fi

            # shellcheck disable=SC2086
            raw_matches=( $profraw_pattern )
            # Keep only existing files (globs may leave literal path when file is missing)
            profraw_matches=()
            for f in "${raw_matches[@]}"; do
                if [[ -f "$f" ]]; then
                    profraw_matches+=("$f")
                fi
            done
            if [[ ${#profraw_matches[@]} -eq 0 ]]; then
                echo "  Skipping $exe_name (no .profraw files found for pattern: $profraw_pattern)"
                continue
            fi

            echo "  Processing $exe_name with ${#profraw_matches[@]} .profraw file(s)..."

            per_profdata="$COVERAGE_DIR/coverage_${exe_name}.profdata"
            # shellcheck disable=SC2068
            "$LLVM_PROFDATA" merge -sparse ${profraw_matches[@]} -o "$per_profdata"

            if [[ ! -f "$per_profdata" ]]; then
                echo "    ERROR: Failed to create profdata for $exe_name, skipping." >&2
                continue
            fi

            per_info="$COVERAGE_DIR/coverage_${exe_name}.info"
            "$LLVM_COV" export \
                -instr-profile="$per_profdata" \
                -ignore-filename-regex="external/|tests/|build/" \
                -format=lcov \
                "$exe" > "$per_info" 2>/dev/null || {
                echo "    WARNING: llvm-cov export failed for $exe_name, skipping." >&2
                rm -f "$per_info"
                continue
            }

            if [[ -f "$per_info" && -s "$per_info" ]]; then
                PER_BINARY_INFO_FILES+=("$per_info")
            fi
        done

        if [[ ${#PER_BINARY_INFO_FILES[@]} -eq 0 ]]; then
            echo "ERROR: No per-binary LCOV .info files were generated. Cannot build merged report." >&2
            exit 1
        fi

        MERGED_INFO="$COVERAGE_DIR/coverage.info"

        # Start with the first .info file, then iteratively merge the rest.
        cp "${PER_BINARY_INFO_FILES[0]}" "$MERGED_INFO"

        if [[ ${#PER_BINARY_INFO_FILES[@]} -gt 1 ]]; then
            for ((i = 1; i < ${#PER_BINARY_INFO_FILES[@]}; ++i)); do
                next_info="${PER_BINARY_INFO_FILES[$i]}"
                tmp_info="$COVERAGE_DIR/coverage_tmp.info"
                # Ignore \"inconsistent\" / \"corrupt\" / \"unsupported\" errors that can appear
                # when combining multiple tracefiles; we prefer merged coverage over strict checks.
                "$LCOV_BIN" --quiet \
                    --ignore-errors inconsistent,corrupt,unsupported \
                    --add-tracefile "$MERGED_INFO" \
                    --add-tracefile "$next_info" \
                    --output-file "$tmp_info" >/dev/null 2>&1 || {
                    echo "WARNING: lcov failed while merging $next_info; keeping previous state." >&2
                    rm -f "$tmp_info"
                    continue
                }
                mv "$tmp_info" "$MERGED_INFO"
            done
        fi

        echo "  Merged LCOV data written to: $MERGED_INFO"

        # Generate summary using lcov
        "$LCOV_BIN" --ignore-errors inconsistent,corrupt,unsupported --summary "$MERGED_INFO" > "$COVERAGE_DIR/coverage_summary.txt" 2>&1 || {
            echo "WARNING: Failed to generate LCOV summary (coverage_summary.txt)." >&2
        }

        if [[ -f "$COVERAGE_DIR/coverage_summary.txt" ]]; then
            echo ""
            echo "Coverage Summary (LCOV):"
            echo "------------------------"
            cat "$COVERAGE_DIR/coverage_summary.txt"
            echo ""
        fi

        # Generate HTML report via genhtml
        echo "Generating HTML report from merged LCOV data..."
        rm -rf "$COVERAGE_DIR/html"
        "$GENHTML_BIN" "$MERGED_INFO" \
            --output-directory "$COVERAGE_DIR/html" \
            --title "USN_WINDOWS Coverage (Per-binary merged)" \
            --quiet >/dev/null 2>&1 || {
            echo "WARNING: genhtml failed to generate HTML report." >&2
        }

        # Handle --show-missing using merged LCOV data
        if [[ "$SHOW_MISSING" == "ON" ]]; then
            echo ""
            echo "Analyzing missing files (from merged LCOV data)..."

            # Extract covered source files from LCOV info
            if [[ -f "$MERGED_INFO" ]]; then
                grep '^SF:' "$MERGED_INFO" | \
                    sed 's|^SF:||' | \
                    sed "s|^$PROJECT_ROOT/||" | \
                    sort -u > "$COVERAGE_DIR/covered_files.txt" 2>/dev/null || true
            fi

            # Find all project source files in src/ directory
            find "$PROJECT_ROOT/src" -name "*.cpp" -o -name "*.h" -o -name "*.mm" 2>/dev/null | \
                grep -v "^$PROJECT_ROOT/external" | \
                grep -v "^$PROJECT_ROOT/build" | \
                sed "s|^$PROJECT_ROOT/||" | \
                sort > "$COVERAGE_DIR/all_project_files.txt" 2>/dev/null || true

            # Find missing files (present in project but not in coverage)
            if [[ -f "$COVERAGE_DIR/covered_files.txt" && -f "$COVERAGE_DIR/all_project_files.txt" ]]; then
                comm -23 "$COVERAGE_DIR/all_project_files.txt" "$COVERAGE_DIR/covered_files.txt" > \
                    "$COVERAGE_DIR/missing_files.txt" 2>/dev/null || true
                
                MISSING_COUNT=$(wc -l < "$COVERAGE_DIR/missing_files.txt" 2>/dev/null | tr -d ' ' || echo "0")
                if [[ "$MISSING_COUNT" -gt 0 ]]; then
                    echo "  Found $MISSING_COUNT source file(s) not in coverage report:"
                    echo "  (These files are not compiled into any test executable)"
                    echo ""
                    head -20 "$COVERAGE_DIR/missing_files.txt" | sed 's/^/    /'
                    if [[ "$MISSING_COUNT" -gt 20 ]]; then
                        echo "    ... and $((MISSING_COUNT - 20)) more"
                    fi
                    echo ""
                    echo "  Full list: $COVERAGE_DIR/missing_files.txt"
                    echo ""
                    echo "  Note: Files not compiled into test executables won't appear in coverage."
                    echo "  To include them, add them to a test executable in CMakeLists.txt"
                else
                    echo "  All project source files are in the coverage report!"
                fi
            fi
        fi
    else
        # Existing single-merge llvm-cov behavior (may report \"functions have mismatched data\")
        "$LLVM_PROFDATA" merge -sparse $PROFRAW_FILES -o "$COVERAGE_DIR/coverage.profdata"
        
        if [[ ! -f "$COVERAGE_DIR/coverage.profdata" ]]; then
            echo "ERROR: Failed to merge coverage data" >&2
            exit 1
        fi
        
        echo "  Coverage data merged successfully"
        echo ""
        
        echo "Generating coverage summary..."
        echo "  Using ${#EXISTING_EXECUTABLES[@]} test executable(s)..."
        
        # Use -object flag for each executable to ensure all are included
        # Build the command with all executables properly quoted
        LLVM_COV_CMD=("$LLVM_COV" report \
            -instr-profile="$COVERAGE_DIR/coverage.profdata" \
            -ignore-filename-regex="external/|tests/|build/")
        
        # Add each executable with -object flag (ensures proper aggregation)
        for exe in "${EXISTING_EXECUTABLES[@]}"; do
            LLVM_COV_CMD+=(-object "$exe")
        done
        
        "${LLVM_COV_CMD[@]}" > "$COVERAGE_DIR/coverage_summary.txt" 2>&1 || {
            echo "WARNING: Some issues generating summary (this may be normal)" >&2
        }
        
        if [[ -f "$COVERAGE_DIR/coverage_summary.txt" ]]; then
            echo ""
            echo "Coverage Summary:"
            echo "----------------"
            cat "$COVERAGE_DIR/coverage_summary.txt"
            echo ""
        fi
        
        # Generate HTML report
        echo "Generating HTML report..."
        # Build the command with all executables properly quoted
        LLVM_COV_SHOW_CMD=("$LLVM_COV" show \
            -instr-profile="$COVERAGE_DIR/coverage.profdata" \
            -ignore-filename-regex="external/|tests/" \
            -format=html \
            -output-dir="$COVERAGE_DIR/html" \
            -show-line-counts-or-regions \
            -show-branches=count)
        
        # Add each executable with -object flag
        for exe in "${EXISTING_EXECUTABLES[@]}"; do
            LLVM_COV_SHOW_CMD+=(-object "$exe")
        done
        
        "${LLVM_COV_SHOW_CMD[@]}" > /dev/null 2>&1 || {
            echo "WARNING: Some issues generating HTML report (this may be normal)" >&2
        }
        
        # Check for --show-missing flag
        if [[ "$SHOW_MISSING" == "ON" ]]; then
            echo ""
            echo "Analyzing missing files..."
            
            # Extract covered files from summary
            if [[ -f "$COVERAGE_DIR/coverage_summary.txt" ]]; then
                grep -E "^[A-Za-z].*\.(cpp|h)" "$COVERAGE_DIR/coverage_summary.txt" | \
                    awk '{print $1}' | sort > "$COVERAGE_DIR/covered_files.txt" 2>/dev/null || true
            fi
            
            # Find all project source files in src/ directory
            find "$PROJECT_ROOT/src" -name "*.cpp" -o -name "*.h" -o -name "*.mm" 2>/dev/null | \
                grep -v "^$PROJECT_ROOT/external" | \
                grep -v "^$PROJECT_ROOT/build" | \
                sed "s|^$PROJECT_ROOT/||" | \
                sort > "$COVERAGE_DIR/all_project_files.txt" 2>/dev/null || true
            
            # Find missing files
            if [[ -f "$COVERAGE_DIR/covered_files.txt" && -f "$COVERAGE_DIR/all_project_files.txt" ]]; then
                comm -23 "$COVERAGE_DIR/all_project_files.txt" "$COVERAGE_DIR/covered_files.txt" > \
                    "$COVERAGE_DIR/missing_files.txt" 2>/dev/null || true
                
                MISSING_COUNT=$(wc -l < "$COVERAGE_DIR/missing_files.txt" 2>/dev/null | tr -d ' ' || echo "0")
                if [[ "$MISSING_COUNT" -gt 0 ]]; then
                    echo "  Found $MISSING_COUNT source file(s) not in coverage report:"
                    echo "  (These files are not compiled into any test executable)"
                    echo ""
                    head -20 "$COVERAGE_DIR/missing_files.txt" | sed 's/^/    /'
                    if [[ "$MISSING_COUNT" -gt 20 ]]; then
                        echo "    ... and $((MISSING_COUNT - 20)) more"
                    fi
                    echo ""
                    echo "  Full list: $COVERAGE_DIR/missing_files.txt"
                    echo ""
                    echo "  Note: Files not compiled into test executables won't appear in coverage."
                    echo "  To include them, add them to a test executable in CMakeLists.txt"
                else
                    echo "  All project source files are in the coverage report!"
                fi
            fi
        fi
    fi
    
elif [[ "$COMPILER_ID" == "GNU" ]]; then
    # GCC uses .gcda files
    echo "Looking for .gcda files..."
    GCDA_FILES=$(find "$BUILD_DIR" -name "*.gcda" 2>/dev/null || true)
    
    if [[ -z "$GCDA_FILES" ]]; then
        echo "ERROR: No .gcda files found. Did you run the tests?" >&2
        echo "Run tests first: cd $BUILD_DIR && ctest --output-on-failure" >&2
        exit 1
    fi
    
    echo "Found .gcda files"
    echo ""
    echo "GCC coverage support requires lcov for HTML reports."
    echo "For now, generating summary with gcov..."
    echo ""
    echo "To generate HTML reports with GCC:"
    echo "  1. Install lcov: brew install lcov"
    echo "  2. Run: lcov --capture --directory $BUILD_DIR --output-file $COVERAGE_DIR/coverage.info"
    echo "  3. Run: genhtml $COVERAGE_DIR/coverage.info --output-directory $COVERAGE_DIR/html"
    echo ""
    echo "Note: Clang (llvm-cov) is recommended for better macOS integration."
fi

echo ""
echo "$SEPARATOR_LINE"
echo "Coverage report generated!"
echo "$SEPARATOR_LINE"
if [[ "$COMPILER_ID" == "Clang" ]]; then
    echo "Summary: $COVERAGE_DIR/coverage_summary.txt"
    echo "HTML:    $COVERAGE_DIR/html/index.html"
    echo ""
    echo "Open HTML report: open $COVERAGE_DIR/html/index.html"
elif [[ "$COMPILER_ID" == "GNU" ]]; then
    echo "Coverage data: $BUILD_DIR (look for .gcda files)"
    echo "Use lcov to generate HTML reports (see instructions above)"
fi
echo ""

