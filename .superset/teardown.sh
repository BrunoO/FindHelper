#!/bin/bash
# Superset workspace teardown for FindHelper (USN_WINDOWS).
# Runs when the workspace is deleted. Setup starts no services or servers;
# the only long-running process a workspace can own is the FindHelper app
# launched by .superset/run.sh, so we stop that if it is still running.
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKTREE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${SUPERSET_WORKSPACE_PATH:-$WORKTREE_ROOT}/build/"

echo "=== FindHelper workspace teardown ==="

# Kill only processes whose executable path lives inside THIS workspace's
# build/ directory (other workspaces' FindHelper instances are untouched).
# pgrep -f matches the full command line; quote the pattern because worktree
# paths may contain spaces.
PIDS="$(pgrep -f -- "$BUILD_DIR" || true)"
if [ -n "$PIDS" ]; then
    # Double-check each candidate really is the FindHelper binary (on macOS
    # `ps -o comm=` prints the full executable path, so compare the basename).
    for pid in $PIDS; do
        CMD="$(ps -p "$pid" -o comm= 2>/dev/null || true)"
        if [ "${CMD##*/}" = "FindHelper" ]; then
            echo "Stopping FindHelper (pid $pid) for this workspace"
            kill "$pid" 2>/dev/null || true
        fi
    done
    # Grace period, then force-kill anything still alive.
    sleep 2
    for pid in $PIDS; do
        if ps -p "$pid" >/dev/null 2>&1; then
            CMD="$(ps -p "$pid" -o comm= 2>/dev/null || true)"
            if [ "${CMD##*/}" = "FindHelper" ]; then
                echo "Force-stopping FindHelper (pid $pid)"
                kill -9 "$pid" 2>/dev/null || true
            fi
        fi
    done
else
    echo "No FindHelper process running from this workspace"
fi

# The worktree (including build/, settings.json, imgui.ini copies) is removed
# by Superset right after teardown, so no further cleanup is needed.
echo "=== Teardown complete ==="
