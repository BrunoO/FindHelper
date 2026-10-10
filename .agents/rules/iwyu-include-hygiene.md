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
2. **Preserve facade / wrapper headers**:
   - Wrapped third-party headers (e.g. `utils/CTrackWrapper.h`) must be preserved. Use `// IWYU pragma: export` or mapping rules in `scripts/iwyu.imp` if IWYU tries to bypass them.
3. **Alphabetical include ordering**:
   - Ensure headers remain sorted alphabetically by ASCII order (`'A'–'Z'` before `'a'–'z'`) after adding or reorganizing includes, per `AGENTS.md`.
4. **Verification**:
   - Always run the full test suite (`./scripts/build_tests_macos.sh --no-asan`) after modifying includes.
