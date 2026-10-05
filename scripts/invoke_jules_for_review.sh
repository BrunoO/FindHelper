#!/bin/bash
# Script to call Jules for Linux build verification and comprehensive review
# Usage: ./scripts/call_jules.sh

set -euo pipefail  # Exit on error, undefined vars, pipe failures

# Check if jules is available
if ! command -v jules &> /dev/null; then
    echo "Error: jules command not found. Please install jules first." >&2
    exit 1
fi

# Check if we're in the project root (look for CMakeLists.txt)
if [[ ! -f "CMakeLists.txt" ]]; then
    echo "Error: This script must be run from the project root directory." >&2
    exit 1
fi

# Check if master-orchestrator.md exists
if [[ ! -f "internal-docs/prompts/master-orchestrator.md" ]]; then
    echo "Warning: internal-docs/prompts/master-orchestrator.md not found. Review may fail." >&2
fi

# Determine GitHub repo slug (owner/name) from origin remote for Jules
origin_url=$(git remote get-url origin 2>/dev/null || true)
repo_slug=""
if [[ -n "$origin_url" ]]; then
    # Support both HTTPS and SSH GitHub URLs
    if [[ "$origin_url" =~ github\.com[:/]+([^/]+/[^/.]+)(\.git)?$ ]]; then
        repo_slug="${BASH_REMATCH[1]}"
    fi
fi

if [[ -z "$repo_slug" ]]; then
    echo "Error: Unable to determine GitHub repo slug from 'origin' remote. Please ensure it points to GitHub (e.g. https://github.com/OWNER/REPO.git)." >&2
    exit 1
fi

# Determine today's date for the review directory
TODAY=$(date +%Y-%m-%d)

# Find the highest version number for today's date
# Check both active reviews (internal-docs/review/) and historical reviews (internal-docs/Historical/review/)
MAX_VERSION=0
for review_base in "internal-docs/review" "internal-docs/Historical/review"; do
    if [[ -d "$review_base" ]]; then
        for dir in ${review_base}/${TODAY}-v*; do
            if [[ -d "$dir" ]]; then
                # Extract version number from directory name (e.g., "2026-01-01-v5" -> "5")
                VERSION=$(basename "$dir" | sed -n "s/${TODAY}-v\([0-9]*\)/\1/p")
                if [[ -n "$VERSION" && "$VERSION" -gt "$MAX_VERSION" ]]; then
                    MAX_VERSION=$VERSION
                fi
            fi
        done
    fi
done
# Also check for review directories directly in Historical (e.g., review-2026-01-01-v3)
if [[ -d "internal-docs/Historical" ]]; then
    for dir in internal-docs/Historical/review-${TODAY}-v*; do
        if [[ -d "$dir" ]]; then
            # Extract version number from directory name (e.g., "review-2026-01-01-v3" -> "3")
            VERSION=$(basename "$dir" | sed -n "s/review-${TODAY}-v\([0-9]*\)/\1/p")
            if [[ -n "$VERSION" && "$VERSION" -gt "$MAX_VERSION" ]]; then
                MAX_VERSION=$VERSION
            fi
        fi
    done
fi

# Increment version for this execution
NEXT_VERSION=$((MAX_VERSION + 1))
REVIEW_DIR="internal-docs/review/${TODAY}-v${NEXT_VERSION}"

# Create review directory
mkdir -p "$REVIEW_DIR"

echo "Starting Jules session for Linux build verification and comprehensive review..."
echo "Review documents will be stored in: $REVIEW_DIR"

jules remote new --repo "$repo_slug" --session "${TODAY}-v${NEXT_VERSION} - Linux Build Verification and Comprehensive Review

## Context
This is a cross-platform project (Windows primary, macOS/Linux secondary targets). You are working on Linux build verification and comprehensive code review.

## Task 1: Linux Build and Test Verification (Priority: CRITICAL)

1. **Read the Linux build documentation**: Review BUILDING_ON_LINUX.md for dependencies and build instructions
2. **Verify build process**:
   - Check that all required dependencies are documented
   - Verify CMake configuration works: \`cmake -S . -B build -DCMAKE_BUILD_TYPE=Release\`
   - Verify compilation succeeds: \`cmake --build build --config Release\`
   - Verify all targets build successfully
3. **Verify test execution**:
   - Build tests: \`cmake --build build --config Release\` (tests are built by default)
   - Run tests: \`cd build && ctest --output-on-failure\`
   - Verify all test executables run successfully
   - Check for any test failures or platform-specific issues
4. **If issues are found**:
   - Fix the issues following project coding conventions (see workspace rules)
   - Re-verify build and tests after fixes
   - Document all fixes in \`$REVIEW_DIR/rationale-linux-fixes.md\` with:
     - Description of each issue found
     - Root cause analysis
     - Solution implemented
     - Verification steps taken

## Task 2: Comprehensive Code Review (Priority: HIGH)

1. **Follow the master orchestrator**: Execute the comprehensive review following instructions in \`internal-docs/prompts/master-orchestrator.md\`
2. **Generate all review reports** as specified in the orchestrator
3. **Store all documents** in: \`$REVIEW_DIR\`
4. **Include a summary** linking to all generated reports

## Important Notes

- **Cross-platform project**: Main dev is macOS, primary target is Windows, secondary is Linux
- **Follow workspace rules**: See project guidelines for coding conventions, naming, Windows-specific rules (e.g., (std::min), (std::max))
- **Test requirements**: On macOS, changes to C++/CMake must be validated with \`scripts/build_tests_macos.sh\`
- **Documentation**: All fixes must be documented with rationale

## Expected Outcomes

1. ✅ Linux build succeeds without errors
2. ✅ All tests pass on Linux
3. ✅ All fixes documented in rationale-linux-fixes.md (if any fixes were needed)
4. ✅ Comprehensive review reports generated in $REVIEW_DIR
5. ✅ Review summary document created"