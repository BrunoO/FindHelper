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

# The build compiles through Apple clang (often via ccache) and emits a PCH per
# target. Homebrew clang-tidy is a different clang major version and rejects that
# PCH ("uses an older format that is no longer supported"), which makes every
# PCH-using translation unit fail to analyze. Those diagnostics are filtered out
# below, so without this step the hook reports success having checked nothing.
# scripts/make_clang_tidy_compile_db.py writes a PCH-free copy for clang-tidy.
# Required, not best-effort: the unstripped database still references the PCH,
# so every PCH-using translation unit fails to analyze and the filtered output
# below reports success having checked nothing. Degrading quietly here is the
# exact failure this step exists to prevent.
CLANG_TIDY_DB="$BUILD_DIR"
PCH_DB_SCRIPT="${PROJECT_ROOT}/scripts/make_clang_tidy_compile_db.py"
if [[ ! -f "$PCH_DB_SCRIPT" ]]; then
  echo -e "${RED}Error: $PCH_DB_SCRIPT not found.${NC}" >&2
  echo "Cannot produce a PCH-free compile database; refusing to check." >&2
  exit 1
fi
if ! command -v python3 &> /dev/null; then
  echo -e "${RED}Error: python3 not found.${NC}" >&2
  echo "It generates the PCH-free compile database and filters clang-tidy output." >&2
  exit 1
fi
# The generator merges every available build database, so no --db is passed.
if ! FILTERED_DB_DIR=$(python3 "$PCH_DB_SCRIPT" 2>/dev/null); then
  echo -e "${RED}Error: $PCH_DB_SCRIPT failed.${NC}" >&2
  echo "Run it directly for the reason: python3 $PCH_DB_SCRIPT" >&2
  exit 1
fi
if [[ ! -f "$FILTERED_DB_DIR/compile_commands.json" ]]; then
  echo -e "${RED}Error: PCH-stripped compile db not produced (empty output).${NC}" >&2
  echo "Run: python3 $PCH_DB_SCRIPT" >&2
  exit 1
fi
CLANG_TIDY_DB="$FILTERED_DB_DIR"

# Get list of staged C++ files
STAGED_FILES=$(git diff --cached --name-only --diff-filter=ACM | grep -E '\.(cpp|h|hpp|cxx|cc)$' || true)

if [ -z "$STAGED_FILES" ]; then
  # No C++ files staged, nothing to check
  exit 0
fi

# Run C++ and MSVC code quality guardrails on staged files (covers all files, including Windows-only headers/sources)
# The script itself is optional, but python3 is not: it is already required above
# to produce the compile database, so re-checking for it here would be dead code.
GUARDRAILS_SCRIPT="${PROJECT_ROOT}/scripts/check_cpp_guardrails.py"
if [[ -f "$GUARDRAILS_SCRIPT" ]]; then
  echo "Checking C++ and MSVC code quality guardrails on staged files..."
  if ! python3 "$GUARDRAILS_SCRIPT" --files $STAGED_FILES; then
    echo -e "${RED}C++ / MSVC guardrail violations detected in staged files.${NC}"
    echo "Please resolve or suppress (// NOSONAR(rule-id) or // NOLINT) before committing."
    exit 1
  fi
  echo ""
fi

echo "Running clang-tidy on staged C++ files..."
echo ""

# Track if any issues were found
ISSUES_FOUND=0
# Staged sources this hook could not analyze at all. Reported at the end so a
# partially-covered run never reads as a clean pass.
SKIPPED_FILES=()

# Diagnostics that mean clang-tidy failed to parse the translation unit, as
# opposed to a finding in the code. Defined in the shared file so this hook and
# run_clang_tidy.sh cannot drift apart in what they consider a broken toolchain.
TOOLCHAIN_RE_FILE="$PROJECT_ROOT/scripts/clang_tidy_toolchain_re.sh"
if [[ -f "$TOOLCHAIN_RE_FILE" ]]; then
    # shellcheck source=scripts/clang_tidy_toolchain_re.sh
    source "$TOOLCHAIN_RE_FILE"
else
    echo -e "${RED}✗ Error: $TOOLCHAIN_RE_FILE not found.${NC}" >&2
    echo "Cannot tell a toolchain failure from a finding; refusing to run." >&2
    exit 1
fi

# One mktemp per run: a hardcoded /tmp path lets concurrent commits interleave
# their output and report each other's findings.
CLANG_OUTPUT_FILE=$(mktemp)
FILTERED_FILE=$(mktemp)
CLASSIFIED_FILE=$(mktemp)
CHANGED_LINES_FILE=$(mktemp)
trap 'rm -f "$CLANG_OUTPUT_FILE" "$FILTERED_FILE" "$CLASSIFIED_FILE" "$CHANGED_LINES_FILE"' EXIT

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
  if [[ "$OSTYPE" == "darwin"* ]] && [[ "$FILE" == src/usn/UsnRecordUtils.cpp ]]; then
    continue
  fi
  if [[ "$OSTYPE" == "darwin"* ]] && [[ "$FILE" == src/index/InitialIndexPopulator.cpp ]]; then
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

  # Skip source files not in compile_commands.json (e.g. app-only GUI sources when analyzing
  # with the test db). Recorded and reported at the end rather than dropped silently.
  # Membership is resolved through the same helper that builds the database, because CMake
  # stores absolute paths there and a plain grep for the repo-relative path never matches.
  if [[ "$FILE" == *.cpp ]]; then
    if ! python3 "${PROJECT_ROOT}/scripts/make_clang_tidy_compile_db.py" --has "$FILE" > /dev/null 2>&1; then
      echo -e "${YELLOW}Skipping $FILE (not in compilation database; clang-tidy cannot analyze it).${NC}"
      SKIPPED_FILES+=("$FILE")
      continue
    fi
  fi
  
  echo "Checking $FILE..."
  
  # Same invocation as run_clang_tidy.sh: no --config-file (auto-discovered), same -p, same init-statement filter
  FILTER_SCRIPT="${PROJECT_ROOT}/scripts/filter_clang_tidy_init_statements.py"
  if [[ -f "$FILTER_SCRIPT" ]]; then
    CLANG_OUTPUT=$($CLANG_TIDY_CMD -p "$CLANG_TIDY_DB" "$FILE" --quiet 2>&1 | python3 "$FILTER_SCRIPT")
  else
    CLANG_OUTPUT=$($CLANG_TIDY_CMD -p "$CLANG_TIDY_DB" "$FILE" --quiet 2>&1)
  fi

  # A translation unit clang-tidy could not parse is a broken check, not a pass.
  if echo "$CLANG_OUTPUT" | is_toolchain_failure; then
    echo -e "${RED}clang-tidy could not analyze $FILE (compile database or toolchain problem):${NC}"
    echo "$CLANG_OUTPUT" | grep -E "$TOOLCHAIN_FAILURE_RE" | head -5
    echo ""
    print_toolchain_failure_help < <(echo "$CLANG_OUTPUT")
    exit 1
  fi
  
  # Collect issues: warnings and errors, excluding llvmlibc-*
  echo "$CLANG_OUTPUT" | \
     grep -v "llvmlibc-" | \
     grep -E "(readability-non-const-parameter|warning:|error:)" > "$CLANG_OUTPUT_FILE" || true

  # On macOS: ignore [clang-diagnostic-error] when deciding pass/fail so that
  # compilation errors from other-platform code (e.g. Windows-only headers)
  # do not block commits. Those diagnostics are expected when analyzing
  # cross-platform code on a single OS. Toolchain failures are caught above and
  # never reach this filter.
  if [[ "$OSTYPE" == "darwin"* ]]; then
    grep -v "\[clang-diagnostic-error\]" "$CLANG_OUTPUT_FILE" > "$FILTERED_FILE" || true
    mv "$FILTERED_FILE" "$CLANG_OUTPUT_FILE"
  fi

  # Ignore [misc-header-include-cycle] from third-party curl (same as run_clang_tidy.sh)
  # so that committing files that include curl (e.g. GeminiApiHttp_linux.cpp) does not fail.
  awk '/\[misc-header-include-cycle\]/ && /curl/ {next} 1' "$CLANG_OUTPUT_FILE" > "$FILTERED_FILE" || true
  mv "$FILTERED_FILE" "$CLANG_OUTPUT_FILE"

  if [[ -s "$CLANG_OUTPUT_FILE" ]]; then
    # Split findings by whether they sit on a line this commit actually changed.
    # Failing on all of them would block every future commit that touches a file
    # with pre-existing findings, which makes the hook noise rather than signal.
    git diff --cached -U0 -- "$FILE" | awk '
      /^@@/ {
        plus = $3
        sub(/^\+/, "", plus)
        split(plus, cd, ",")
        start = cd[1] + 0
        count = (cd[2] == "" ? 1 : cd[2] + 0)
        for (i = 0; i < count; i++) changed[start + i] = 1
      }
      END { for (l in changed) print l }
    ' | sort -n > "$CHANGED_LINES_FILE"

    awk -F: -v changed="$CHANGED_LINES_FILE" '
      BEGIN { while ((getline l < changed) > 0) ischanged[l] = 1 }
      /: (warning|error):/ {
        print (($2 + 0) in ischanged ? "NEW" : "OLD") "\t" $0
      }
    ' "$CLANG_OUTPUT_FILE" > "$CLASSIFIED_FILE"

    NEW_COUNT=$(grep -c "^NEW" "$CLASSIFIED_FILE" || true)
    OLD_COUNT=$(grep -c "^OLD" "$CLASSIFIED_FILE" || true)

    if [ "${NEW_COUNT:-0}" -gt 0 ]; then
      echo -e "${RED}Issues introduced by this commit in $FILE:${NC}"
      grep "^NEW" "$CLASSIFIED_FILE" | cut -f2-
      echo ""
      ISSUES_FOUND=1
    fi

    if [ "${OLD_COUNT:-0}" -gt 0 ]; then
      echo -e "${YELLOW}Pre-existing issues in $FILE (not introduced here, not blocking):${NC}"
      grep "^OLD" "$CLASSIFIED_FILE" | cut -f2-
      echo ""
    fi
  fi
done

if [ ${#SKIPPED_FILES[@]} -gt 0 ]; then
  echo -e "${YELLOW}clang-tidy did not analyze ${#SKIPPED_FILES[@]} staged file(s):${NC}"
  printf '  %s\n' "${SKIPPED_FILES[@]}"
  echo -e "${YELLOW}They are not part of the configured build, so they were neither${NC}"
  echo -e "${YELLOW}checked nor cleared. To cover them, point the hook at a build that has them:${NC}"
  echo "  cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
  echo ""
fi

if [ $ISSUES_FOUND -eq 1 ]; then
  echo -e "${RED}clang-tidy found issues in staged files.${NC}"
  echo ""
  echo "Please fix the issues above before committing."
  echo "You can run clang-tidy manually (same compile database as this hook):"
  echo "  python3 scripts/make_clang_tidy_compile_db.py"
  echo "  $CLANG_TIDY_CMD -p \"\$(python3 scripts/make_clang_tidy_compile_db.py)\" <file>"
  echo ""
  echo "To bypass this check (not recommended), use:"
  echo "  git commit --no-verify"
  exit 1
fi

echo -e "${GREEN}All clang-tidy checks passed!${NC}"
exit 0
