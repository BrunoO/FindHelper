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

## Do not

Do **not** invoke `cmake`, `make`, `clang++`, or other build tools directly. This script is the **only** allowed build/test entrypoint on macOS.

## Agent note

When modifying code under macOS, **state in your response** whether `scripts/build_tests_macos.sh` was run and whether it passed.

**Reference:** AGENTS.md § macOS Test Requirement (Build-Tests Exception).
