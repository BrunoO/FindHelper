# Search Benchmark Guide

This guide details the standalone `search_benchmark` harness and the runner script `scripts/run_search_benchmark.sh`.

---

## 1. Overview

`search_benchmark` evaluates search query latency and correctness across a pre-populated `FileIndex`. It provides:
- High-precision search timing (excluding harness allocation / result ID collection overhead).
- Statistical dispersion analysis (standard deviation `stddev_ms` and coefficient of variation `cv_percent`).
- Equivalence verification (`--verify`) between candidate-indexed paths and full sequential scans, including diff diagnostics on false positives/negatives.
- Automated A/B baseline comparison (`--compare`) computing latency deltas and speedup factors directly.
- Regression gating (`--max-regression-pct` and `--fail-on-hit-mismatch`).
- Automated directory configuration batching (`--config-dir`).
- Clean machine parsing modes (`--quiet` / `-q` with `--format json` or `--format csv`).

---

## 2. Building the Benchmark

### macOS / Linux
```bash
# Build as part of test suite
./scripts/build_tests_macos.sh --no-asan

# Or build the target directly with CMake
cmake --build build_tests --target search_benchmark
```

The resulting executable is located at `build_tests/search_benchmark` (or `build/search_benchmark`).

---

## 3. Quick Start

### Using the Runner Script
The easiest way to execute benchmarks is through `scripts/run_search_benchmark.sh`:

```bash
# Run a single config against the standard Linux filesystem corpus
./scripts/run_search_benchmark.sh --config tests/benchmark_config_sample.json

# Record baseline results to JSON
./scripts/run_search_benchmark.sh --config tests/benchmark_config_sample.json --record-baseline baseline.json

# Compare candidate performance against baseline with 10% regression tolerance
./scripts/run_search_benchmark.sh --config tests/benchmark_config_sample.json --compare baseline.json --max-regression-pct 10.0

# Batch run an entire directory of test configurations
./scripts/run_search_benchmark.sh --config-dir tests/benchmark_configs/ --verify
```

---

## 4. CLI Reference

```text
Usage: search_benchmark --index PATH (--config SPEC | --config-dir DIR)... [options]
```

### Core Options
| Flag | Description |
|---|---|
| `--index <path>` | Required. Text file containing one file path per line. |
| `--config <spec>` | JSON config file path or inline JSON string (repeatable). |
| `--config-dir <dir>` | Directory containing `*.json` configuration files (sorted alphabetically). |
| `--iterations <N>` | Number of timed measurement iterations (default: 5). |
| `--warmup <M>` | Number of untimed warmup iterations before measurement (default: 1). |
| `--threads <N>` | Number of worker threads: `-1` or `0` for auto-detect (default: -1). |
| `--config-inline` | Treat `--config` arguments strictly as literal JSON, bypassing file path sniffing. |

### Comparison & Regression Gating
| Flag | Description |
|---|---|
| `--compare <file>` | Compare run statistics against a baseline JSON output file. |
| `--max-regression-pct <P>` | Exit with code 1 if average latency regresses by more than `P`% vs baseline. |
| `--fail-on-hit-mismatch` | Exit with code 1 if candidate hit count differs from baseline. |
| `--expect <N>` | Assert exactly `N` matches for a single `--config` run (exits 1 on mismatch). |

### Diagnostics & Output
| Flag | Description |
|---|---|
| `--verify` | Force scan fallback vs normal indexed path; diff ID sets and print false positive/negative path samples on mismatch. |
| `--measure-merge-sort` | Run an extra search measuring merge and sort latency breakdown. |
| `--format <mode>` | Output mode: `text` (default prose), `csv`, or `json`. |
| `--quiet`, `-q` | Silence prose headers and progress lines for pure machine parsing. |

---

## 5. JSON Configuration Schema

Benchmark queries are configured using JSON. The JSON contract supports the standard `search_config` format:

```json
{
  "version": "1.0",
  "expected_count": 1486,
  "search_config": {
    "filename": "controller",
    "path": "pp:src/search/**",
    "extensions": ["cpp", "h"],
    "folders_only": false,
    "files_only": true,
    "case_sensitive": false,
    "time_filter": "None",
    "size_filter": "None"
  }
}
```

### Supported Fields
- `filename`: Filename substring, glob, or path pattern (`pp:...`).
- `path`: Path query (e.g., `pp:**/tests/**`).
- `extensions`: Array of extension strings (without leading dot).
- `folders_only`: Boolean (default: `false`). If true, filters for directories only.
- `files_only`: Boolean (default: `false`). If true, filters for files only.
- `case_sensitive`: Boolean (default: `false`).
- `expected_count`: Optional unsigned integer. When set, each configuration asserts its own match count in multi-config runs.
