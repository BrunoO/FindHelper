---
description: On macOS, validate C++/CMake changes with scripts/build_tests_macos.sh only
globs: "**/*.cpp,**/*.h,**/*.hpp,CMakeLists.txt,**/CMakeLists.txt"
alwaysApply: false
paths:
  - "**/*.cpp"
  - "**/*.h"
  - "**/*.hpp"
  - "CMakeLists.txt"
  - "**/CMakeLists.txt"
---

# macOS build and test (only allowed entrypoint)

On macOS, any change to C++ source, headers, or CMake **must** be validated by running the cross-platform test suite.

## Required

- **Command (macOS only):** `scripts/build_tests_macos.sh`
- **Scope:** When changing `*.cpp`, `*.c`, `*.h`, `*.hpp`, `*.cxx`, `*.cc`, `CMakeLists.txt`, or scripts that affect builds/tests.
- **Exception:** Pure documentation-only changes (e.g. `*.md` or comment-only) may skip this step.

## Fast Workflow for AI Agents

To avoid long build/test iteration times, agents should choose the appropriate scope:

| Purpose | Command | Typical Duration |
| :--- | :--- | :--- |
| **Component iteration** | `./scripts/build_tests_macos.sh --target <name>` | **~0.2s** (incremental) |
| **Doctest test case filter** | `./scripts/build_tests_macos.sh --target <name> -- -tc="Pattern*"` | **~0.2s** |
| **Core regression sanity** | `./scripts/build_tests_macos.sh --smoke` | **~3s** (incremental) |
| **All unit tests** | `./scripts/build_tests_macos.sh --skip-benchmarks` | **~11s** (parallel) |
| **Full suite + bench smoke**| `./scripts/build_tests_macos.sh` | **~50s** |
| **Full suite + 706k corpus**| `./scripts/build_tests_macos.sh --with-benchmarks` | Full regression run |

## Do not

Do **not** invoke `cmake`, `make`, `clang++`, or other build tools directly. This script is the **only** allowed build/test entrypoint on macOS. When targeting a single test, use `--target <name>`, never direct binary invocation.

## Agent note

When modifying code under macOS, **state in your response** whether `scripts/build_tests_macos.sh` was run (including flags used, e.g. `--smoke` or `--target <name>`) and whether it passed.

**Reference:** AGENTS.md § Build & Test Commands.
