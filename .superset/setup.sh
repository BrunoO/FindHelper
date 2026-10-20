#!/bin/bash
# Superset workspace setup for FindHelper (USN_WINDOWS).
# Runs in a fresh worktree on workspace creation.
# Idempotent and fast (no compilation here; the Run button builds on demand).
set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKTREE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$WORKTREE_ROOT"

echo "=== FindHelper workspace setup ==="
echo "Worktree: $WORKTREE_ROOT"

# --- 1. Resolve the main checkout (root repository) -------------------------
# Superset sets SUPERSET_ROOT_PATH; fall back to the common git dir when the
# script is run outside Superset (e.g. manual testing).
ROOT_PATH="${SUPERSET_ROOT_PATH:-}"
if [ -z "$ROOT_PATH" ]; then
    COMMON_DIR="$(git -C "$WORKTREE_ROOT" rev-parse --git-common-dir 2>/dev/null || true)"
    if [ -n "$COMMON_DIR" ]; then
        case "$COMMON_DIR" in
            /*) ROOT_PATH="$(cd "$COMMON_DIR/.." && pwd)" ;;
            *)  ROOT_PATH="$(cd "$WORKTREE_ROOT/$COMMON_DIR/.." && pwd)" ;;
        esac
    fi
fi
echo "Main checkout: ${ROOT_PATH:-<unknown>}"

# --- 2. Git submodules (the C++ "dependency install") -----------------------
# New worktrees check out external/{imgui,doctest,nlohmann_json,freetype,
# imgui_test_engine} as empty directories; CMake needs their sources.
# `git submodule update --init` only clones what is missing, so re-runs are fast.
echo "--- Initializing submodules (if needed) ---"
git submodule update --init --jobs 4

# external/glfw is NOT tracked: find_or_fetch_glfw() in CMakeLists.txt resolves
# GLFW via find_package / brew / FetchContent at configure time. Nothing to do.

# --- 3. Copy untracked local files from the main checkout -------------------
# These are gitignored (so absent from new worktrees) but give the developer
# their app settings, saved searches and UI layout:
#   settings.json  - FindHelper app settings / saved searches
#   imgui.ini      - ImGui window layout
#   .env / .env.local - environment overrides (not used today; future-proof)
if [ -n "$ROOT_PATH" ] && [ "$ROOT_PATH" != "$WORKTREE_ROOT" ]; then
    echo "--- Copying untracked local files from main checkout ---"
    for name in settings.json imgui.ini .env .env.local; do
        if [ -f "$ROOT_PATH/$name" ] && [ ! -f "$WORKTREE_ROOT/$name" ]; then
            cp "$ROOT_PATH/$name" "$WORKTREE_ROOT/$name"
            echo "  copied $name"
        elif [ -f "$WORKTREE_ROOT/$name" ]; then
            echo "  kept existing $name"
        else
            echo "  $name not present in main checkout, skipping"
        fi
    done
fi

# --- 4. Toolchain sanity check (warn, never fail) ----------------------------
echo "--- Toolchain check ---"
if command -v cmake >/dev/null 2>&1; then
    echo "  cmake: $(cmake --version | head -1)"
else
    echo "  WARNING: cmake not found - install it (brew install cmake) before using Run."
fi
if command -v ninja >/dev/null 2>&1; then
    echo "  ninja: $(ninja --version)"
else
    echo "  WARNING: ninja not found - CMake will fall back to Make (brew install ninja for faster builds)."
fi

echo "=== Setup complete ==="
