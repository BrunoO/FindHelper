#!/bin/bash

# Run Duplo duplicate code detection on USN_WINDOWS codebase
#
# This script finds all C++ source files in the project (excluding external
# dependencies and build artifacts) and runs Duplo to detect duplicate code blocks.
#
# Usage:
#   ./scripts/run_duplo.sh                      # Default: minimum 15 lines
#   DUPLO_MIN_LINES=20 ./scripts/run_duplo.sh   # Custom minimum lines
#   DUPLO_BIN=/path/to/duplo ./scripts/run_duplo.sh  # Custom Duplo location
#
# Requirements:
#   - Duplo executable must be in PATH or specified via DUPLO_BIN
#   - Download from: https://github.com/dlidstrom/Duplo/releases

set -e  # Exit on error

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Duplo executable path (adjust based on installation)
DUPLO_BIN="${DUPLO_BIN:-duplo}"

# Check if Duplo is available
if ! command -v "$DUPLO_BIN" &> /dev/null; then
    echo "ERROR: Duplo not found. Install from: https://github.com/dlidstrom/Duplo/releases" >&2
    echo "" >&2
    echo "Installation options:" >&2
    echo "  1. Download binary and add to PATH" >&2
    echo "  2. Place binary in project root and set: DUPLO_BIN=\$PWD/duplo" >&2
    echo "  3. macOS: Check if available via Homebrew" >&2
    echo "" >&2
    exit 1
fi

# Output files
OUTPUT_FILE="${PROJECT_ROOT}/duplo_report.txt"
JSON_OUTPUT="${PROJECT_ROOT}/duplo_report.json"

# Minimum lines for duplicate detection (configurable)
MIN_LINES="${DUPLO_MIN_LINES:-15}"

echo "=========================================="
echo "Running Duplo duplicate code detection"
echo "=========================================="
echo "Project root: $PROJECT_ROOT"
echo "Duplo binary: $DUPLO_BIN"
echo "Minimum duplicate lines: $MIN_LINES"
echo "Output: $OUTPUT_FILE"
echo "JSON output: $JSON_OUTPUT"
echo ""

# Find all C++ source files (exclude external dependencies and build artifacts)
cd "$PROJECT_ROOT"

echo "Scanning for C++ source files..."
SOURCE_FILES=$(find . -type f \( -iname "*.cpp" -o -iname "*.h" -o -iname "*.hpp" \) \
    ! -path "./external/*" \
    ! -path "./build/*" \
    ! -path "./build_*/*" \
    ! -path "./coverage/*" \
    ! -path "./.git/*" \
    2>/dev/null)

if [[ -z "$SOURCE_FILES" ]]; then
    echo "ERROR: No C++ source files found!" >&2
    exit 1
fi

FILE_COUNT=$(echo "$SOURCE_FILES" | wc -l | tr -d ' ')
echo "Found $FILE_COUNT C++ source files"
echo ""

# Run Duplo with text output
echo "Running Duplo analysis (text output)..."
# Duplo returns non-zero exit code when no duplicates are found, which is OK
# We check if the output file was created to verify the run was successful
echo "$SOURCE_FILES" | "$DUPLO_BIN" -ml "$MIN_LINES" -ip - "$OUTPUT_FILE" || true

# Check if output file was created (even if empty, it means duplo ran successfully)
if [[ ! -f "$OUTPUT_FILE" ]]; then
    echo "ERROR: Duplo analysis failed - output file not created!" >&2
    exit 1
fi

# Run Duplo with JSON output
echo "Running Duplo analysis (JSON output)..."
# Duplo returns non-zero exit code when no duplicates are found, which is OK
echo "$SOURCE_FILES" | "$DUPLO_BIN" -ml "$MIN_LINES" -ip -json "$JSON_OUTPUT" - || true

# Check if JSON output file was created
if [[ ! -f "$JSON_OUTPUT" ]]; then
    echo "WARNING: Duplo JSON output generation failed (text report still available)" >&2
fi

echo ""
echo "=========================================="
echo "Duplo analysis complete!"
echo "=========================================="
echo "Text report: $OUTPUT_FILE"
echo "JSON report: $JSON_OUTPUT"
echo ""

# Check if duplicates were found
if [[ -s "$OUTPUT_FILE" ]]; then
    DUPLICATE_COUNT=$(grep -c "^[A-Za-z]" "$OUTPUT_FILE" 2>/dev/null || echo "0")
    if [[ "$DUPLICATE_COUNT" -gt 0 ]]; then
        echo "⚠️  Duplicate code blocks detected!"
        echo "   Review the report and consider refactoring duplicated code."
        echo ""
        echo "   To view the report:"
        echo "     cat $OUTPUT_FILE"
        echo ""
    else
        echo "✅ No duplicate code blocks found (or report is empty)"
    fi
else
    echo "✅ No duplicate code blocks found"
fi

echo ""
echo "Next steps:"
echo "  1. Review the duplicate code blocks in the report"
echo "  2. Identify refactoring opportunities"
echo "  3. Extract common code into shared functions/utilities"
echo "  4. Re-run this script to verify improvements"
echo ""

