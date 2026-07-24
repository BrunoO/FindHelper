#!/bin/bash
#
# Pre-commit hook to run clang-tidy on staged C++ files
# Checks for const correctness and other code quality issues
#
# Usage: Copy or symlink this script to .git/hooks/pre-commit
#   ln -s ../../scripts/pre-commit-clang-tidy.sh .git/hooks/pre-commit
#
# Or install using:
#   cp scripts/pre-commit-clang-tidy.sh .git/hooks/pre-commit
#   chmod +x .git/hooks/pre-commit

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Project root
PROJECT_ROOT=$(git rev-parse --show-toplevel 2>/dev/null || echo ".")
cd "$PROJECT_ROOT"

# Same clang-tidy command as run_clang_tidy.sh (so pre-commit and full run give same result per file)
if [[ -f "$PROJECT_ROOT/scripts/clang-tidy-wrapper.sh" ]]; then
  CLANG_TIDY_CMD="$PROJECT_ROOT/scripts/clang-tidy-wrapper.sh"
elif [[ -f "/opt/homebrew/opt/llvm/bin/clang-tidy" ]]; then
  CLANG_TIDY_CMD="/opt/homebrew/opt/llvm/bin/clang-tidy"
elif [[ -f "/usr/local/opt/llvm/bin/clang-tidy" ]]; then
  CLANG_TIDY_CMD="/usr/local/opt/llvm/bin/clang-tidy"
elif command -v clang-tidy &> /dev/null; then
  CLANG_TIDY_CMD="clang-tidy"
else
  echo -e "${YELLOW}Warning: clang-tidy not found. Skipping pre-commit checks.${NC}"
  echo "Install clang-tidy to enable automatic code quality checks."
  exit 0
fi

# Same compile_commands.json order as run_clang_tidy.sh: prefer root, then build/
if [ -f "${PROJECT_ROOT}/compile_commands.json" ]; then
  BUILD_DIR="${PROJECT_ROOT}"
elif [ -f "${PROJECT_ROOT}/build/compile_commands.json" ]; then
  BUILD_DIR="${PROJECT_ROOT}/build"
elif [ -f "${PROJECT_ROOT}/build_coverage/compile_commands.json" ]; then
  BUILD_DIR="${PROJECT_ROOT}/build_coverage"
else
  echo -e "${YELLOW}Warning: compile_commands.json not found.${NC}"
  echo "Run: cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
  echo "Or symlink: ln -s build/compile_commands.json ."
  echo "Skipping clang-tidy checks for this commit."
  exit 0
fi

if [ ! -f "${PROJECT_ROOT}/.clang-tidy" ]; then
  echo -e "${YELLOW}Warning: .clang-tidy not found. Skipping pre-commit checks.${NC}"
  exit 0
fi

# Get list of staged C++ files
STAGED_FILES=$(git diff --cached --name-only --diff-filter=ACM | grep -E '\.(cpp|h|hpp|cxx|cc)$' || true)

if [ -z "$STAGED_FILES" ]; then
  # No C++ files staged, nothing to check
  exit 0
fi

echo "Running clang-tidy on staged C++ files..."
echo ""

# Track if any issues were found
ISSUES_FOUND=0

# Run clang-tidy on each staged file
for FILE in $STAGED_FILES; do
  # Skip external dependencies
  if [[ "$FILE" == external/* ]]; then
    continue
  fi

  # Skip generated embedded data (Embedded*.cpp / Embedded*.h) - tool-generated, very large
  if [[ "$(basename "$FILE")" == Embedded* ]]; then
    continue
  fi

  # Skip Windows-only sources on macOS (Windows headers not available)
  if [[ "$OSTYPE" == "darwin"* ]] && [[ "$FILE" == src/usn/UsnMonitor.cpp ]]; then
    continue
  fi
  if [[ "$OSTYPE" == "darwin"* ]] && [[ "$FILE" == src/platform/windows/* ]]; then
    continue
  fi
  if [[ "$OSTYPE" == "darwin"* ]] && [[ "$FILE" == src/platform/linux/* ]]; then
    continue
  fi
  
  # Skip if file doesn't exist (might be deleted)
  if [ ! -f "$FILE" ]; then
    continue
  fi
  
  echo "Checking $FILE..."
  
  # Same invocation as run_clang_tidy.sh: no --config-file (auto-discovered), same -p, same init-statement filter
  FILTER_SCRIPT="${PROJECT_ROOT}/scripts/filter_clang_tidy_init_statements.py"
  if [[ -f "$FILTER_SCRIPT" ]]; then
    CLANG_OUTPUT=$($CLANG_TIDY_CMD -p "$BUILD_DIR" "$FILE" --quiet 2>&1 | python3 "$FILTER_SCRIPT")
  else
    CLANG_OUTPUT=$($CLANG_TIDY_CMD -p "$BUILD_DIR" "$FILE" --quiet 2>&1)
  fi
  
  # Collect issues: warnings and errors, excluding llvmlibc-*
  echo "$CLANG_OUTPUT" | \
     grep -v "llvmlibc-" | \
     grep -E "(readability-non-const-parameter|warning:|error:)" > /tmp/clang-tidy-output.txt || true

  # On macOS: ignore [clang-diagnostic-error] when deciding pass/fail so that
  # compilation errors from other-platform code (e.g. Windows-only headers)
  # do not block commits. Those diagnostics are expected when analyzing
  # cross-platform code on a single OS.
  if [[ "$OSTYPE" == "darwin"* ]]; then
    grep -v "\[clang-diagnostic-error\]" /tmp/clang-tidy-output.txt > /tmp/clang-tidy-filtered.txt || true
    mv /tmp/clang-tidy-filtered.txt /tmp/clang-tidy-output.txt
  fi

  # Ignore [misc-header-include-cycle] from third-party curl (same as run_clang_tidy.sh)
  # so that committing files that include curl (e.g. GeminiApiHttp_linux.cpp) does not fail.
  awk '/\[misc-header-include-cycle\]/ && /curl/ {next} 1' /tmp/clang-tidy-output.txt > /tmp/clang-tidy-filtered.txt || true
  mv /tmp/clang-tidy-filtered.txt /tmp/clang-tidy-output.txt

  if [[ -s /tmp/clang-tidy-output.txt ]]; then
    echo -e "${RED}Issues found in $FILE:${NC}"
    cat /tmp/clang-tidy-output.txt
    echo ""
    ISSUES_FOUND=1
  fi
done

# Clean up temp file
rm -f /tmp/clang-tidy-output.txt

if [ $ISSUES_FOUND -eq 1 ]; then
  echo -e "${RED}clang-tidy found issues in staged files.${NC}"
  echo ""
  echo "Please fix the issues above before committing."
  echo "You can run clang-tidy manually (same as full run):"
  echo "  $CLANG_TIDY_CMD -p $BUILD_DIR <file>"
  echo ""
  echo "To bypass this check (not recommended), use:"
  echo "  git commit --no-verify"
  exit 1
fi

echo -e "${GREEN}All clang-tidy checks passed!${NC}"
exit 0
