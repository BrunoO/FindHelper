---
description: Run include-what-you-use (IWYU) periodically to maintain clean header dependencies
globs: "**/*.cpp,**/*.h,**/*.hpp"
alwaysApply: false
paths:
  - "**/*.cpp"
  - "**/*.h"
  - "**/*.hpp"
---

# Include-What-You-Use (IWYU) Maintenance

To prevent header bloat, slow compile times, and hidden transitive include dependencies, agents should run IWYU checks periodically or when refactoring headers.

## When to Run

- **Periodically during maintenance / boy-scout runs**: Run full analysis across `src/` to catch accumulated header drift.
- **After substantial header refactoring**: Whenever moving, splitting, or forward-declaring types across headers.
- **Before adding new module dependencies**: Verify whether headers can be forward-declared or included only in `.cpp` files.

## Commands

```bash
# Preview proposed include changes across src/ (dry run)
./scripts/run_iwyu.sh --dry-run

# Run analysis on a specific directory or changed files
./scripts/run_iwyu.sh src/path/
./scripts/run_iwyu.sh src/path/PathUtils.cpp

# Run via CMake target in build_tests
cd build_tests && cmake --build . --target iwyu
```

## Guidelines for Applying IWYU Changes

1. **Never blindly apply `--fix`**:
   - IWYU runs on macOS and does not see `#ifdef _WIN32` blocks. Headers used exclusively in Windows or Linux platform blocks (such as `<windows.h>`, `FileTimeTypes.h`, `<cstring>` for `std::memcpy`) must NOT be removed.
   - Always preview with `--dry-run` first, or inspect git diff carefully after running.
   - **Mandatory for platform-only includes**: any `#include` whose symbols are used exclusively inside a non-host `#ifdef` block (`_WIN32`, `__linux__`, `__x86_64__`, etc.) must carry `// IWYU pragma: keep` plus a short reason comment, so IWYU and `fix_includes.py` never propose removing it again. Real regressions: `utils/Logger.h` + `<sstream>` dropped from `CpuFeatures.cpp` (broke Linux x86 CI) and `path/PathUtils.h` dropped from `AppBootstrapCommon.h` (broke Windows CI).
2. **Verify non-host code paths after `--fix`** — the macOS build does not compile them:
   - x86 Linux/macOS paths: cross-syntax-check the touched file (cheap, catches `__x86_64__` blocks):
     ```bash
     clang++ -isysroot $(xcrun --show-sdk-path) -arch x86_64 -std=c++17 -fsyntax-only \
       -DSTRING_SEARCH_AVX2_AVAILABLE=1 -I src <file.cpp>
     ```
   - `#ifdef _WIN32` paths: cannot be compiled on macOS — inspect each touched file's `_WIN32` blocks and confirm every symbol's declaring header is still included, or rely on the Windows CI build gate.
3. **Preserve facade / wrapper headers**:
   - Wrapped third-party headers (e.g. `utils/CTrackWrapper.h`) must be preserved. Use `// IWYU pragma: export` or mapping rules in `scripts/iwyu.imp` if IWYU tries to bypass them.
4. **Alphabetical include ordering**:
   - Ensure headers remain sorted alphabetically by ASCII order (`'A'–'Z'` before `'a'–'z'`) after adding or reorganizing includes, per `AGENTS.md`.
5. **Verification**:
   - Always run the full test suite (`./scripts/build_tests_macos.sh --no-asan`) after modifying includes.
