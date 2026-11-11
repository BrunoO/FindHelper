#!/usr/bin/env bash
#
# run_search_benchmark.sh — Convenient runner for FindHelper search_benchmark
#
# Provides a high-level wrapper to record baselines, run A/B regression gates,
# and execute benchmarks across single configs or config directories.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Resolve benchmark binary
BENCH_EXE=""
if [[ -x "${PROJECT_ROOT}/build_tests/search_benchmark" ]]; then
  BENCH_EXE="${PROJECT_ROOT}/build_tests/search_benchmark"
elif [[ -x "${PROJECT_ROOT}/build/search_benchmark" ]]; then
  BENCH_EXE="${PROJECT_ROOT}/build/search_benchmark"
else
  echo "search_benchmark executable not found. Building via CMake..."
  if [[ -f "${PROJECT_ROOT}/build_tests/CMakeCache.txt" ]]; then
    cmake --build "${PROJECT_ROOT}/build_tests" --target search_benchmark -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"
    BENCH_EXE="${PROJECT_ROOT}/build_tests/search_benchmark"
  elif [[ -f "${PROJECT_ROOT}/build/CMakeCache.txt" ]]; then
    cmake --build "${PROJECT_ROOT}/build" --target search_benchmark -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"
    BENCH_EXE="${PROJECT_ROOT}/build/search_benchmark"
  else
    echo "Error: no configured build directory found in build_tests/ or build/." >&2
    echo "Please configure first: ./scripts/build_tests_macos.sh --no-asan" >&2
    exit 1
  fi
fi

# Default index
DEFAULT_INDEX="${PROJECT_ROOT}/tests/benchmark_index_sample.txt"
if [[ -f "${PROJECT_ROOT}/tests/data/std-linux-filesystem.txt" ]]; then
  DEFAULT_INDEX="${PROJECT_ROOT}/tests/data/std-linux-filesystem.txt"
fi

RECORD_BASELINE=""
INDEX_ARG=""
FORWARD_ARGS=()

show_usage() {
  cat <<EOF
Usage: ./scripts/run_search_benchmark.sh [options] [search_benchmark options]

High-level Options:
  --record-baseline FILE  Run benchmark suite in clean JSON format and save to FILE
  --compare FILE          Compare run against baseline JSON file
  --max-regression-pct P  Exit 1 if latency regressed by more than P% vs baseline
  --fail-on-hit-mismatch  Exit 1 if candidate hits differ from baseline
  --index FILE            Path to index file (default: $DEFAULT_INDEX)
  --verify                Run scan-vs-filter equivalence test
  --format MODE           Output format: text, csv, or json (default: text)
  --quiet, -q             Suppress prose headers and banners
  -h, --help              Show this help message

Examples:
  # Record baseline:
  ./scripts/run_search_benchmark.sh --config tests/benchmark_config_sample.json --record-baseline baseline.json

  # Compare against baseline with 10% regression tolerance:
  ./scripts/run_search_benchmark.sh --config tests/benchmark_config_sample.json --compare baseline.json --max-regression-pct 10.0

  # Batch execute all configs in a directory:
  ./scripts/run_search_benchmark.sh --config-dir tests/benchmark_configs/ --verify
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --record-baseline)
      if [[ $# -lt 2 ]]; then
        echo "Error: --record-baseline requires a target file" >&2
        exit 1
      fi
      RECORD_BASELINE="$2"
      shift 2
      ;;
    --index)
      if [[ $# -lt 2 ]]; then
        echo "Error: --index requires a file path" >&2
        exit 1
      fi
      INDEX_ARG="$2"
      shift 2
      ;;
    -h|--help)
      show_usage
      exit 0
      ;;
    *)
      FORWARD_ARGS+=("$1")
      shift
      ;;
  esac
done

FINAL_INDEX="${INDEX_ARG:-$DEFAULT_INDEX}"

if [[ -n "$RECORD_BASELINE" ]]; then
  echo "Recording benchmark baseline to: $RECORD_BASELINE (index: $FINAL_INDEX)..."
  "$BENCH_EXE" --index "$FINAL_INDEX" "${FORWARD_ARGS[@]}" --format json --quiet > "$RECORD_BASELINE"
  echo "Baseline recorded successfully ($(wc -l < "$RECORD_BASELINE" | tr -d ' ') lines)."
  exit 0
fi

# Run normal benchmark or compare
exec "$BENCH_EXE" --index "$FINAL_INDEX" "${FORWARD_ARGS[@]}"
