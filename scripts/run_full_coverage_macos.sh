#!/bin/bash
# One-command full code coverage on macOS: build with coverage, run unit tests and (if built with
# ImGui Test Engine) FindHelper ImGui tests, then generate the report.
#
# Usage: ./scripts/run_full_coverage_macos.sh [OPTIONS]
#
# Options are passed to generate_coverage_macos.sh (e.g. --show-missing).
#
# FindHelper is always run with --show-metrics when ImGui tests run (build_tests_macos.sh and
# generate_coverage_macos.sh), so Metrics window coverage is included.
#
# For ImGui test engine coverage, configure with -DENABLE_IMGUI_TEST_ENGINE=ON before running
# (e.g. run once: cmake -DENABLE_IMGUI_TEST_ENGINE=ON ... in build_tests, or add that option
# to your CMake configure step).

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

"$SCRIPT_DIR/build_tests_macos.sh" --coverage
"$SCRIPT_DIR/generate_coverage_macos.sh" "$@"
