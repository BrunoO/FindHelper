#!/bin/bash
# Run include-what-you-use (IWYU) on the project sources
#
# Usage:
#   ./scripts/run_iwyu.sh [options] [files/directories...]
#
# Options:
#   --fix              Apply IWYU suggested changes automatically via fix_includes.py
#   --dry-run          Show unified diff of proposed changes without modifying files
#   --output FILE      Save raw IWYU output to file
#   --jobs, -j N       Run up to N parallel IWYU processes (default: CPU cores)
#   --mapping FILE     Specify an additional IWYU mapping file
#   --help, -h         Show this help message
#
# Examples:
#   ./scripts/run_iwyu.sh                              # Analyze all files in src/
#   ./scripts/run_iwyu.sh src/path/PathUtils.cpp       # Analyze a single file
#   ./scripts/run_iwyu.sh --dry-run                    # Preview diff of all proposed changes
#   ./scripts/run_iwyu.sh --fix src/filters/           # Auto-fix headers in src/filters/
#   ./scripts/run_iwyu.sh --output iwyu_report.txt     # Save full report to file

set -eo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

FIX=false
DRY_RUN=false
OUTPUT_FILE=""
JOBS=""
USER_MAPPING=""
CUSTOM_BUILD_DIR=""
SOURCES=()

while [[ $# -gt 0 ]]; do
    case $1 in
        --fix)
            FIX=true
            shift
            ;;
        --dry-run)
            DRY_RUN=true
            shift
            ;;
        --output)
            OUTPUT_FILE="$2"
            shift 2
            ;;
        -p|--build-dir)
            CUSTOM_BUILD_DIR="$2"
            shift 2
            ;;
        --jobs|-j)
            JOBS="$2"
            shift 2
            ;;
        --mapping)
            USER_MAPPING="$2"
            shift 2
            ;;
        --help|-h)
            cat << EOF
Usage: $0 [options] [files/directories...]

Run include-what-you-use (IWYU) on project sources.

Options:
  --fix              Apply IWYU suggested changes automatically via fix_includes.py
  --dry-run          Show unified diff of proposed changes without modifying files
  --output FILE      Save raw IWYU output to file
  --jobs, -j N       Run up to N parallel processes (default: CPU cores)
  --mapping FILE     Specify an additional IWYU mapping file
  --help, -h         Show this help message

Examples:
  $0                                # Analyze all files in src/
  $0 src/path/PathUtils.cpp         # Analyze a single file
  $0 --dry-run                      # Preview diff of all proposed changes
  $0 --fix src/filters/             # Auto-fix headers in src/filters/
  $0 --output iwyu_report.txt       # Save report to file
EOF
            exit 0
            ;;
        -*)
            echo -e "${RED}Error: Unknown option $1${NC}" >&2
            echo "Use --help for usage information" >&2
            exit 1
            ;;
        *)
            SOURCES+=("$1")
            shift
            ;;
    esac
done

# If no sources specified, default to src/
if [[ ${#SOURCES[@]} -eq 0 ]]; then
    SOURCES=("src/")
fi

# Detect CPU cores if jobs not specified
if [[ -z "$JOBS" ]]; then
    if command -v nproc >/dev/null 2>&1; then
        JOBS=$(nproc 2>/dev/null || echo "4")
    elif [[ "$OSTYPE" == "darwin"* ]] && command -v sysctl >/dev/null 2>&1; then
        JOBS=$(sysctl -n hw.ncpu 2>/dev/null || echo "4")
    else
        JOBS="4"
    fi
fi

# Locate include-what-you-use binary
IWYU_BIN=""
if command -v include-what-you-use >/dev/null 2>&1; then
    IWYU_BIN="$(command -v include-what-you-use)"
elif [[ -x "/opt/homebrew/bin/include-what-you-use" ]]; then
    IWYU_BIN="/opt/homebrew/bin/include-what-you-use"
elif [[ -x "/usr/local/bin/include-what-you-use" ]]; then
    IWYU_BIN="/usr/local/bin/include-what-you-use"
else
    echo -e "${RED}✗ Error: include-what-you-use not found in PATH${NC}" >&2
    echo "Install it with:" >&2
    echo "  brew install include-what-you-use    # macOS" >&2
    echo "  sudo apt install iwyu               # Ubuntu/Debian" >&2
    exit 1
fi

# Locate iwyu_tool.py driver
IWYU_TOOL=""
if command -v iwyu_tool.py >/dev/null 2>&1; then
    IWYU_TOOL="$(command -v iwyu_tool.py)"
elif [[ -x "/opt/homebrew/bin/iwyu_tool.py" ]]; then
    IWYU_TOOL="/opt/homebrew/bin/iwyu_tool.py"
elif [[ -x "/usr/local/bin/iwyu_tool.py" ]]; then
    IWYU_TOOL="/usr/local/bin/iwyu_tool.py"
else
    echo -e "${RED}✗ Error: iwyu_tool.py driver not found${NC}" >&2
    exit 1
fi

# Locate fix_includes.py if fix or dry-run requested
FIX_INCLUDES=""
if [[ "$FIX" == true || "$DRY_RUN" == true ]]; then
    if command -v fix_includes.py >/dev/null 2>&1; then
        FIX_INCLUDES="$(command -v fix_includes.py)"
    elif [[ -x "/opt/homebrew/bin/fix_includes.py" ]]; then
        FIX_INCLUDES="/opt/homebrew/bin/fix_includes.py"
    elif [[ -x "/usr/local/bin/fix_includes.py" ]]; then
        FIX_INCLUDES="/usr/local/bin/fix_includes.py"
    else
        echo -e "${RED}✗ Error: fix_includes.py not found (required for --fix / --dry-run)${NC}" >&2
        exit 1
    fi
fi

# Produce PCH-stripped compile database via make_clang_tidy_compile_db.py
PCH_DB_SCRIPT="$PROJECT_ROOT/scripts/make_clang_tidy_compile_db.py"
if [[ ! -f "$PCH_DB_SCRIPT" ]]; then
    echo -e "${RED}✗ Error: $PCH_DB_SCRIPT not found.${NC}" >&2
    exit 1
fi

DB_ARG=()
if [[ -n "$CUSTOM_BUILD_DIR" ]]; then
    if [[ -f "$CUSTOM_BUILD_DIR/compile_commands.json" ]]; then
        DB_ARG=("--db" "$CUSTOM_BUILD_DIR/compile_commands.json")
    elif [[ -f "$CUSTOM_BUILD_DIR" ]]; then
        DB_ARG=("--db" "$CUSTOM_BUILD_DIR")
    else
        echo -e "${RED}✗ Error: Compile DB not found in $CUSTOM_BUILD_DIR${NC}" >&2
        exit 1
    fi
else
    # Check if local compile_commands exists in this tree; if not and we are in a worktree, check main tree
    if [[ ! -f "$PROJECT_ROOT/compile_commands.json" ]] && \
       [[ ! -f "$PROJECT_ROOT/build/compile_commands.json" ]] && \
       [[ ! -f "$PROJECT_ROOT/build_tests/compile_commands.json" ]]; then
        COMMON_GIT_DIR="$(git rev-parse --git-common-dir 2>/dev/null || true)"
        if [[ -n "$COMMON_GIT_DIR" ]]; then
            MAIN_REPO="$(cd "$COMMON_GIT_DIR/.." && pwd)"
            for candidate in "$MAIN_REPO/build_tests/compile_commands.json" "$MAIN_REPO/build/compile_commands.json" "$MAIN_REPO/compile_commands.json"; do
                if [[ -f "$candidate" ]]; then
                    DB_ARG=("--db" "$candidate")
                    break
                fi
            done
        fi
    fi
fi

if ! FILTERED_DB_DIR=$(python3 "$PCH_DB_SCRIPT" "${DB_ARG[@]}" 2>/dev/null); then
    # If python script failed because no compile DB exists, advise user
    echo -e "${YELLOW}⚠ Warning: compile_commands.json not found${NC}" >&2
    echo "Generate it first with:" >&2
    echo "  ./scripts/build_tests_macos.sh --no-asan" >&2
    echo "or:" >&2
    echo "  cmake -B build_tests -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DENABLE_PCH=OFF" >&2
    exit 1
fi

if [[ ! -f "$FILTERED_DB_DIR/compile_commands.json" ]]; then
    echo -e "${RED}✗ PCH-stripped compile DB not found at $FILTERED_DB_DIR${NC}" >&2
    exit 1
fi

COMPILE_DB="$FILTERED_DB_DIR"

# Assemble extra arguments for include-what-you-use
IWYU_EXTRA_ARGS=("-Xiwyu" "--cxx17ns")

# macOS sysroot detection (required for Homebrew LLVM / IWYU to locate system headers)
if [[ "$OSTYPE" == "darwin"* ]]; then
    if [[ -n "$SDKROOT" && -d "$SDKROOT" ]]; then
        IWYU_EXTRA_ARGS+=("-isysroot" "$SDKROOT")
    elif command -v xcrun >/dev/null 2>&1; then
        OSX_SDK="$(xcrun --show-sdk-path 2>/dev/null || true)"
        if [[ -n "$OSX_SDK" && -d "$OSX_SDK" ]]; then
            IWYU_EXTRA_ARGS+=("-isysroot" "$OSX_SDK")
        fi
    fi
fi

# Default project mapping file
DEFAULT_MAPPING="$PROJECT_ROOT/scripts/iwyu.imp"
if [[ -f "$DEFAULT_MAPPING" ]]; then
    IWYU_EXTRA_ARGS+=("-Xiwyu" "--mapping_file=$DEFAULT_MAPPING")
fi

# Optional user mapping file
if [[ -n "$USER_MAPPING" ]]; then
    if [[ -f "$USER_MAPPING" ]]; then
        IWYU_EXTRA_ARGS+=("-Xiwyu" "--mapping_file=$USER_MAPPING")
    else
        echo -e "${RED}✗ Error: Mapping file not found: $USER_MAPPING${NC}" >&2
        exit 1
    fi
fi

echo -e "${GREEN}=== include-what-you-use (IWYU) ===${NC}"
echo "Driver:       $IWYU_TOOL"
echo "Binary:       $IWYU_BIN"
echo "Compile DB:   $COMPILE_DB"
echo "Target(s):    ${SOURCES[*]}"
echo "Concurrency:  $JOBS jobs"
if [[ "$FIX" == true ]]; then
    echo "Mode:         AUTO-FIX (--fix)"
elif [[ "$DRY_RUN" == true ]]; then
    echo "Mode:         DRY-RUN (--dry-run)"
else
    echo "Mode:         REPORT"
fi
echo ""

# Run iwyu_tool.py
TMP_OUTPUT=$(mktemp)
trap 'rm -f "$TMP_OUTPUT"' EXIT

set +e
python3 "$IWYU_TOOL" -p "$COMPILE_DB" -j "$JOBS" "${SOURCES[@]}" -- "${IWYU_EXTRA_ARGS[@]}" > "$TMP_OUTPUT" 2>&1
IWYU_EXIT=$?
set -e

# If output file requested, save a copy
if [[ -n "$OUTPUT_FILE" ]]; then
    cp "$TMP_OUTPUT" "$OUTPUT_FILE"
    echo -e "${BLUE}Saved raw output to: $OUTPUT_FILE${NC}"
fi

# Handle --fix or --dry-run
if [[ "$FIX" == true ]]; then
    echo -e "${YELLOW}Applying include fixes via fix_includes.py...${NC}"
    python3 "$FIX_INCLUDES" < "$TMP_OUTPUT"
elif [[ "$DRY_RUN" == true ]]; then
    echo -e "${YELLOW}Proposed include modifications (diff):${NC}"
    python3 "$FIX_INCLUDES" --dry_run < "$TMP_OUTPUT" || true
else
    cat "$TMP_OUTPUT"
fi

echo ""
echo -e "${GREEN}✓ IWYU analysis complete.${NC}"
exit 0
