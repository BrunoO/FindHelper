---
description: Static analysis on macOS - scan-build, clang analyzer, Sonar (allowed entrypoints only)
globs: "**/*.cpp,**/*.h,**/*.hpp,**/*.mm"
alwaysApply: false
paths:
  - "**/*.cpp"
  - "**/*.h"
  - "**/*.hpp"
  - "**/*.mm"
---

# Static analysis (macOS)

When asked to statically analyze C++/CMake changes, use the project entrypoints; do not
hand-roll cmake/clang analyzer toolchains from scratch.

## Allowed entrypoints

```bash
# Clang Static Analyzer over the whole macOS build (clang --analyze via scan-build)
./scripts/run_scan_build.sh                     # build dir build_scan, results under the output dir
./scripts/run_scan_build.sh <build_dir> <out>   # custom dirs

# clang-tidy with the repo's .clang-tidy config and compile_commands.json
scripts/clang-tidy-wrapper.sh -p . <files...>   # same invocation as the pre-commit hook
#   also: cmake --build <build> --target clang-tidy (when the target exists in that build dir)

# Open SonarCloud issues (verify against current code before fixing — snapshot lags HEAD)
./scripts/fetch_sonar_results.sh --open-only
```

## Per-file clang analyzer (`clang++ --analyze`)

For targeted analysis of a few files (faster than a full scan-build run):

```bash
clang++ --analyze -Xanalyzer -analyzer-output=text \
  -std=c++17 -Isrc -Ibuild_tests \
  src/<file>.cpp
```

Notes:
- Linux-only / Windows-only TUs that the macOS build does not compile
  (`src/platform/linux/*`, `src/usn/UsnMonitor.cpp`, `src/usn/*_win.cpp`) cannot be
  analyzed on macOS: `__linux__`-gated headers and `<windows.h>` are unavailable.
  State this limitation instead of forcing partial analysis; rerun on the target
  platform (MSVC `/analyze`, clang-tidy on Windows) for real coverage.
- `clang++ -fsyntax-only` with the test-build include set is the quick compile check
  for Linux-only files; add `-include vector` for headers relying on transitive
  includes.
- Do not add fake `windows.h` stub directories to force analysis of Windows-only files —
  the results are unreliable; rely on the platform CI.

## Pre-commit hook interplay

The pre-commit hook blocks on clang-tidy warnings for staged files; scan-build/analyzer
findings from the entrypoints above are advisory and not hook-enforced.
