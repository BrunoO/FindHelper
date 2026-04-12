#!/bin/bash
# Run clang-tidy on the entire project and generate a comprehensive warnings report
#
# Usage:
#   ./scripts/run_clang_tidy.sh [options]
#
# Options:
#   --output FILE     Save output to file (default: clang-tidy-report-YYYY-MM-DD.txt)
#   --summary-only    Show only summary statistics
#   --no-summary      Don't show summary, only warnings
#   --quiet           Suppress progress messages
#   --jobs N          Run up to N clang-tidy processes in parallel (default: auto-detect cores)
#
# Examples:
#   ./scripts/run_clang_tidy.sh
#   ./scripts/run_clang_tidy.sh --output my-report.txt
#   ./scripts/run_clang_tidy.sh --summary-only

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Get project root
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

# Parse arguments
OUTPUT_FILE=""
SUMMARY_ONLY=false
NO_SUMMARY=false
QUIET=false
JOBS=""

while [[ $# -gt 0 ]]; do
    case $1 in
        --output)
            OUTPUT_FILE="$2"
            shift 2
            ;;
        --summary-only)
            SUMMARY_ONLY=true
            shift
            ;;
        --no-summary)
            NO_SUMMARY=true
            shift
            ;;
        --quiet)
            QUIET=true
            shift
            ;;
        --jobs)
            JOBS="$2"
            shift 2
            ;;
        --help|-h)
            echo "Usage: $0 [options]"
            echo ""
            echo "Options:"
            echo "  --output FILE     Save output to file"
            echo "  --summary-only    Show only summary statistics"
            echo "  --no-summary      Don't show summary, only warnings"
            echo "  --quiet           Suppress progress messages"
            echo "  --jobs N          Run up to N clang-tidy processes in parallel (default: auto)"
            echo "  --help, -h        Show this help message"
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            echo "Use --help for usage information" >&2
            exit 1
            ;;
    esac
done

# Default jobs: use all cores (nproc on Linux, sysctl on macOS), cap at 4 if detection fails
if [[ -z "$JOBS" ]]; then
    if command -v nproc >/dev/null 2>&1; then
        JOBS=$(nproc 2>/dev/null || echo "4")
    elif [[ "$OSTYPE" == "darwin"* ]] && command -v sysctl >/dev/null 2>&1; then
        JOBS=$(sysctl -n hw.ncpu 2>/dev/null || echo "4")
    else
        JOBS="4"
    fi
fi

# Determine clang-tidy path (use wrapper script if available, otherwise find directly)
if [[ -f "$PROJECT_ROOT/scripts/clang-tidy-wrapper.sh" ]]; then
    CLANG_TIDY_CMD="$PROJECT_ROOT/scripts/clang-tidy-wrapper.sh"
elif [[ -f "/opt/homebrew/opt/llvm/bin/clang-tidy" ]]; then
    CLANG_TIDY_CMD="/opt/homebrew/opt/llvm/bin/clang-tidy"
elif [[ -f "/usr/local/opt/llvm/bin/clang-tidy" ]]; then
    CLANG_TIDY_CMD="/usr/local/opt/llvm/bin/clang-tidy"
elif command -v clang-tidy >/dev/null 2>&1; then
    CLANG_TIDY_CMD="clang-tidy"
else
    echo -e "${RED}✗ Error: clang-tidy not found${NC}" >&2
    echo "" >&2
    echo "Installation options:" >&2
    echo "  1. Via Homebrew: brew install llvm" >&2
    echo "  2. Via system package manager" >&2
    echo "" >&2
    echo "After installation, verify with: clang-tidy --version" >&2
    exit 1
fi

# Check if compile_commands.json exists
if [[ ! -f "$PROJECT_ROOT/compile_commands.json" ]] && [[ ! -f "$PROJECT_ROOT/build/compile_commands.json" ]]; then
    echo -e "${YELLOW}⚠ Warning: compile_commands.json not found${NC}" >&2
    echo "" >&2
    echo "Generate it with:" >&2
    echo "  cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON" >&2
    echo "" >&2
    echo "Or create a symlink:" >&2
    echo "  ln -sf build/compile_commands.json compile_commands.json" >&2
    exit 1
fi

# Determine compile commands location
if [[ -f "$PROJECT_ROOT/compile_commands.json" ]]; then
    COMPILE_DB="$PROJECT_ROOT"
elif [[ -f "$PROJECT_ROOT/build/compile_commands.json" ]]; then
    COMPILE_DB="$PROJECT_ROOT/build"
else
    echo -e "${RED}✗ Error: Could not find compile_commands.json${NC}" >&2
    exit 1
fi

# Set default output file if not specified
if [[ -z "$OUTPUT_FILE" ]]; then
    TODAY=$(date +%Y-%m-%d)
    OUTPUT_FILE="$PROJECT_ROOT/clang-tidy-report-${TODAY}.txt"
fi

# Show header
if [[ "$QUIET" == false ]]; then
    echo -e "${GREEN}=== clang-tidy Static Analysis ===${NC}"
    echo "Project: $PROJECT_ROOT"
    echo "clang-tidy: $CLANG_TIDY_CMD"
    echo "Compile DB: $COMPILE_DB"
    echo ""
fi

# Show clang-tidy version
if [[ "$QUIET" == false ]]; then
    echo -e "${GREEN}✓ Found clang-tidy${NC}"
    "$CLANG_TIDY_CMD" --version 2>/dev/null | head -1 || echo "  (version check failed)"
    echo ""
fi

# Find all source files
if [[ "$QUIET" == false ]]; then
    echo -e "${BLUE}Finding source files...${NC}"
fi

SOURCE_FILES=$(find src -type f \( -name "*.cpp" -o -name "*.h" \) ! -path "*/external/*" ! -name "Embedded*" | sort)
FILE_COUNT=$(echo "$SOURCE_FILES" | grep -c . || echo "0")

if [[ "$FILE_COUNT" -eq 0 ]]; then
    echo -e "${RED}✗ Error: No source files found${NC}" >&2
    exit 1
fi

if [[ "$QUIET" == false ]]; then
    echo -e "${GREEN}✓ Found $FILE_COUNT source files${NC}"
    echo "Jobs: $JOBS (parallel)"
    echo ""
    echo -e "${BLUE}Running clang-tidy (this may take a while)...${NC}"
fi

# Create temporary file for raw output
TEMP_OUTPUT=$(mktemp)
trap "rm -f '$TEMP_OUTPUT'" EXIT

# Run clang-tidy on all files in parallel (xargs -P, one file per invocation with -n 1)
# Redirect stderr to stdout to capture all output
# Filter out C++17 init-statement false positives for cppcoreguidelines-init-variables
FILTER_SCRIPT="$PROJECT_ROOT/scripts/filter_clang_tidy_init_statements.py"
if [[ -f "$FILTER_SCRIPT" ]]; then
    echo "$SOURCE_FILES" | xargs -P "$JOBS" -n 1 "$CLANG_TIDY_CMD" -p "$COMPILE_DB" 2>&1 | \
        python3 "$FILTER_SCRIPT" | tee "$TEMP_OUTPUT" > /dev/null || true
else
    echo "$SOURCE_FILES" | xargs -P "$JOBS" -n 1 "$CLANG_TIDY_CMD" -p "$COMPILE_DB" 2>&1 | tee "$TEMP_OUTPUT" > /dev/null || true
fi

# Filter out processing messages and extract warnings/errors
WARNINGS=$(grep -E "warning:|error:" "$TEMP_OUTPUT" | grep -v "Processing file" | grep -v "warnings and" | grep -v "Error while processing" || true)

# On macOS: exclude [clang-diagnostic-error] from report so cross-platform compilation
# errors (e.g. missing Windows/Linux headers) do not inflate the count. ExcludeHeaderFilterRegex
# only applies to diagnostics *in* headers matching the regex; these errors are reported in
# .cpp/.h paths (e.g. Application.cpp, DirectXManager.h), so they are not filtered by .clang-tidy.
# Filtering here keeps the report focused on actionable clang-tidy issues.
if [[ "$OSTYPE" == "darwin"* ]]; then
  EXCLUDED_ERRORS=$(echo "$WARNINGS" | grep -c "\[clang-diagnostic-error\]" || echo "0")
  WARNINGS=$(echo "$WARNINGS" | grep -v "\[clang-diagnostic-error\]" || true)
else
  EXCLUDED_ERRORS="0"
fi

# Exclude [misc-header-include-cycle] from third-party curl (paths/messages containing "curl").
# ExcludeHeaderFilterRegex in .clang-tidy may not filter these depending on path format/clang-tidy
# behavior; filtering here ensures they do not appear in the report.
EXCLUDED_CURL_CYCLES=$(echo "$WARNINGS" | grep "\[misc-header-include-cycle\]" | grep -c "curl" || echo "0")
WARNINGS=$(echo "$WARNINGS" | awk '/\[misc-header-include-cycle\]/ && /curl/ {next} 1' || true)

# Count warnings and errors (strip to digits so arithmetic never fails)
WARNING_COUNT=$(echo "$WARNINGS" | grep -c "warning:" 2>/dev/null || echo "0")
ERROR_COUNT=$(echo "$WARNINGS" | grep -c "error:" 2>/dev/null || echo "0")
WARNING_COUNT=${WARNING_COUNT//[^0-9]/}
ERROR_COUNT=${ERROR_COUNT//[^0-9]/}
WARNING_COUNT=${WARNING_COUNT:-0}
ERROR_COUNT=${ERROR_COUNT:-0}
TOTAL_COUNT=$((WARNING_COUNT + ERROR_COUNT))

# Generate summary statistics
SUMMARY_STATS=$(echo "$WARNINGS" | grep -oE "\[[^]]+\]" | sort | uniq -c | sort -rn || true)

# Generate output
{
    echo "================================================================================"
    echo "clang-tidy Analysis Report"
    echo "Generated: $(date)"
    echo "Project: $PROJECT_ROOT"
    echo "Files Analyzed: $FILE_COUNT"
    echo "================================================================================"
    echo ""
    
    if [[ "$NO_SUMMARY" == false ]]; then
        echo "SUMMARY"
        echo "================================================================================"
        echo "Total Warnings: $WARNING_COUNT"
        echo "Total Errors:   $ERROR_COUNT"
        echo "Total Issues:   $TOTAL_COUNT"
        EXCLUDED_ERRORS_NUM=${EXCLUDED_ERRORS//[^0-9]/}
        EXCLUDED_CURL_NUM=${EXCLUDED_CURL_CYCLES//[^0-9]/}
        if [[ -n "$EXCLUDED_ERRORS_NUM" ]] && [[ "$EXCLUDED_ERRORS_NUM" -gt 0 ]]; then
            echo "(Excluded: $EXCLUDED_ERRORS_NUM [clang-diagnostic-error] — cross-platform compilation errors, not actionable on this host)"
        fi
        if [[ -n "$EXCLUDED_CURL_NUM" ]] && [[ "$EXCLUDED_CURL_NUM" -gt 0 ]]; then
            echo "(Excluded: $EXCLUDED_CURL_NUM [misc-header-include-cycle] — third-party curl headers)"
        fi
        echo ""
        
        if [[ -n "$SUMMARY_STATS" ]]; then
            echo "Top Warning Categories:"
            echo "--------------------------------------------------------------------------------"
            echo "$SUMMARY_STATS" | head -20 | while read -r count check; do
                printf "  %4s  %s\n" "$count" "$check"
            done
            echo ""
        fi
    fi
    
    if [[ "$SUMMARY_ONLY" == false ]]; then
        echo "================================================================================"
        echo "DETAILED WARNINGS AND ERRORS"
        echo "================================================================================"
        echo ""
        
        if [[ -n "$WARNINGS" ]]; then
            echo "$WARNINGS"
        else
            echo "No warnings or errors found!"
        fi
    fi
} > "$OUTPUT_FILE"

# Display results
if [[ "$QUIET" == false ]]; then
    echo ""
    echo -e "${GREEN}✓ Analysis complete${NC}"
    echo ""
    echo "Summary:"
    echo "  Warnings: $WARNING_COUNT"
    echo "  Errors:   $ERROR_COUNT"
    echo "  Total:    $TOTAL_COUNT"
    echo ""
    
    if [[ "$SUMMARY_ONLY" == false ]]; then
        echo -e "${BLUE}Full report saved to:${NC} $OUTPUT_FILE"
        echo ""
        echo "Top warning categories:"
        echo "$SUMMARY_STATS" | head -10 | while read -r count check; do
            printf "  ${YELLOW}%4s${NC}  %s\n" "$count" "$check"
        done
    else
        echo -e "${BLUE}Summary saved to:${NC} $OUTPUT_FILE"
    fi
    echo ""
fi

# Exit with non-zero if there are warnings/errors
if [[ $TOTAL_COUNT -gt 0 ]]; then
    exit 0  # Return 0 even with warnings (user can check the file)
else
    exit 0
fi
