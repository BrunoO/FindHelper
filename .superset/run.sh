#!/bin/bash
# Superset Run-button script for FindHelper (USN_WINDOWS).
# Builds the app target (incremental) and launches the GUI.
#
# FindHelper is a native GUI app - it opens no network ports, so parallel
# workspaces never collide and no per-workspace port allocation is needed.
# Each workspace builds into its own worktree-local build/ directory.
set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKTREE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$WORKTREE_ROOT"

# --- Pick the CMake preset for this platform --------------------------------
case "$(uname -s)" in
    Darwin) PRESET="macos-debug" ;;
    Linux)  PRESET="linux-release" ;;
    MINGW*|MSYS*|CYGWIN*)
        echo "On Windows, use the Visual Studio presets directly (docs/guides/building/)"
        echo "or run from a Developer prompt: cmake --preset windows-debug && cmake --build --preset windows-debug"
        exit 1
        ;;
    *) echo "Unsupported platform: $(uname -s)"; exit 1 ;;
esac
echo "=== FindHelper run ($PRESET) ==="

# --- Safety net: make sure submodules exist (setup normally does this) ------
if [ ! -f external/imgui/imgui.h ]; then
    echo "--- Submodules missing, initializing ---"
    git submodule update --init --jobs 4
fi

# --- Configure (only when the cache is missing or generator changed) --------
if [ ! -f build/CMakeCache.txt ]; then
    echo "--- Configuring (first run downloads/fetches GLFW and can take a minute) ---"
    cmake --preset "$PRESET"
fi

# --- Build the application target -------------------------------------------
echo "--- Building find_helper (incremental) ---"
cmake --build --preset "$PRESET" --target find_helper

# --- Launch -------------------------------------------------------------------
case "$(uname -s)" in
    Darwin) BIN="$WORKTREE_ROOT/build/FindHelper.app/Contents/MacOS/FindHelper" ;;
    Linux)  BIN="$WORKTREE_ROOT/build/FindHelper" ;;
esac

if [ ! -x "$BIN" ]; then
    echo "ERROR: expected executable not found: $BIN"
    exit 1
fi

echo "--- Launching $BIN ---"
exec "$BIN"
