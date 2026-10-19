---
description: Safe CMakeLists.txt changes (patterns, PGO, test targets)
globs: "CMakeLists.txt,**/CMakeLists.txt"
alwaysApply: false
paths:
  - "CMakeLists.txt"
  - "**/CMakeLists.txt"
---

# Modifying CMakeLists.txt safely

When adding or changing sources or tests:

## Do

- **Mirror existing patterns** — same style, indentation, ordering as nearby entries. Group related files.
- **Add inside existing blocks** — do not move or duplicate `if(MSVC)`, `if(BUILD_TESTS)`, `if(ENABLE_PGO)`; add entries inside the correct block.
- **Leave PGO flags unchanged** — do not change `/GL`, `/Gy`, `/GENPROFILE`, `/USEPROFILE`, `/LTCG:*` when adding files.

## Test targets sharing code with main app

If a test target compiles source files that are also in the main executable (e.g. `FileIndex.cpp`), the test target **must** use the same PGO compiler flags as the main target (to avoid LNK1269). Use standard linker flags (e.g. `/LTCG /OPT:REF /OPT:ICF`), not PGO linker flags. See `file_index_search_strategy_tests` in CMakeLists.txt for the pattern.

## After editing

Run CMake configure and build on Windows to confirm no config/link errors. Keep changes minimal and describe in the commit message.

Full rules: AGENTS.md § Modifying CMakeLists.txt Safely.
