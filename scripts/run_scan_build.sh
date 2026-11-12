#!/bin/bash
# Run scan-build static analysis on the USN_WINDOWS project
#
# This script runs the Clang Static Analyzer (scan-build) on the project
# to find deep bugs like memory leaks, null pointer dereferences, etc.
#
# Usage:
#   ./scripts/run_scan_build.sh [build_directory] [output_directory]
#
# Examples:
#   ./scripts/run_scan_build.sh                    # Uses default: build_scan, scan-results
#   ./scripts/run_scan_build.sh build scan-out     # Custom directories

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Get project root
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

# Default directories
BUILD_DIR="${1:-build_scan}"
OUTPUT_DIR="${2:-scan-results}"

echo -e "${GREEN}=== scan-build Static Analysis ===${NC}"
echo "Project: $PROJECT_ROOT"
echo "Build directory: $BUILD_DIR"
echo "Output directory: $OUTPUT_DIR"
echo ""

# Check if scan-build is available
if ! command -v scan-build &> /dev/null; then
    echo -e "${YELLOW}⚠ scan-build not found in PATH${NC}"
    echo ""
    echo "Installation options:"
    echo "  1. Via pip:    pip3 install scan-build"
    echo "  2. Via Homebrew: brew install llvm"
    echo ""
    echo "After installation, verify with: scan-build --version"
    exit 1
fi

# Show scan-build version
echo -e "${GREEN}✓ Found scan-build${NC}"
scan-build --version | head -1
echo ""

# Check if CMake is available
if ! command -v cmake &> /dev/null; then
    echo -e "${RED}✗ cmake not found${NC}"
    exit 1
fi

# Generate build directory with compile commands
echo -e "${GREEN}Generating CMake build configuration...${NC}"
cmake -B "$BUILD_DIR" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_STANDARD=17

# Check if compile_commands.json was generated
if [[ ! -f "$BUILD_DIR/compile_commands.json" ]]; then
    echo -e "${YELLOW}⚠ Warning: compile_commands.json not found${NC}"
    echo "   This may affect analysis quality"
fi

# Find clang analyzer (scan-build needs clang, not clang++)
CLANG_ANALYZER=""
if command -v clang &> /dev/null; then
    CLANG_ANALYZER="$(command -v clang)"
    echo -e "${GREEN}✓ Found clang analyzer: $CLANG_ANALYZER${NC}"
elif [[ -f "/opt/homebrew/opt/llvm/bin/clang" ]]; then
    CLANG_ANALYZER="/opt/homebrew/opt/llvm/bin/clang"
    echo -e "${GREEN}✓ Found clang analyzer: $CLANG_ANALYZER${NC}"
elif [[ -f "/usr/local/opt/llvm/bin/clang" ]]; then
    CLANG_ANALYZER="/usr/local/opt/llvm/bin/clang"
    echo -e "${GREEN}✓ Found clang analyzer: $CLANG_ANALYZER${NC}"
else
    echo -e "${YELLOW}⚠ Warning: clang not found${NC}"
    echo "   scan-build will try to auto-detect, but may fail"
    echo "   Install clang: brew install llvm"
fi

# Run scan-build
echo ""
echo -e "${GREEN}Running scan-build analysis...${NC}"
echo "This may take several minutes..."
echo ""

# Run scan-build with output directory
# Note: --use-analyzer expects clang (not clang++), or we can omit it for auto-detection
if [[ -n "$CLANG_ANALYZER" ]]; then
    scan-build -o "$OUTPUT_DIR" \
        --use-analyzer="$CLANG_ANALYZER" \
        cmake --build "$BUILD_DIR" -j$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
else
    # Let scan-build auto-detect (may work if clang is in PATH)
    scan-build -o "$OUTPUT_DIR" \
        cmake --build "$BUILD_DIR" -j$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
fi

# Check scan-build exit status
SCAN_BUILD_EXIT=$?

# Find the results directory
RESULTS_DIR=$(find "$OUTPUT_DIR" -name "index.html" -type f | head -1 | xargs dirname 2>/dev/null || echo "")

echo ""

# Check if scan-build reported "No bugs found"
if [[ $SCAN_BUILD_EXIT -eq 0 ]]; then
    if [[ -n "$RESULTS_DIR" && -f "$RESULTS_DIR/index.html" ]]; then
        # Bugs were found - show results
        echo -e "${YELLOW}⚠ Analysis found potential issues!${NC}"
        echo ""
        echo "Results available at:"
        echo "  $RESULTS_DIR/index.html"
        echo ""
        echo "Open in browser:"
        echo "  open \"$RESULTS_DIR/index.html\""
        echo ""
        echo "Review the issues and fix them as needed."
    else
        # No bugs found - this is good!
        echo -e "${GREEN}✓ Analysis complete - No bugs found!${NC}"
        echo ""
        echo "scan-build analyzed your code and found no issues."
        echo "This is a positive result - your code passed static analysis."
        echo ""
        echo "Note: scan-build removes empty result directories when no bugs are found."
    fi
else
    # scan-build failed
    echo -e "${RED}✗ Analysis failed${NC}"
    echo ""
    echo "Check the error messages above for details."
    echo "Common causes:"
    echo "  - Build errors (check CMake configuration)"
    echo "  - Missing dependencies"
    echo "  - Compiler errors"
    exit $SCAN_BUILD_EXIT
fi

