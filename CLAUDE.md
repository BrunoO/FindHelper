# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**FindHelper** is a cross-platform file search application (C++17, CMake) with a Dear ImGui GUI. It indexes a folder using the Windows USN journal (or a recursive crawl on macOS/Linux), then searches by name/pattern/extension with size/date filters. An optional Gemini API integration translates natural-language queries into search configs.

- **Primary development platform:** macOS
- **Primary deployment target:** Windows (DirectX 11 + GLFW)
- **Secondary targets:** macOS (Metal + GLFW), Linux (OpenGL 3 + GLFW)

## Build Commands

### First-time setup
```bash
git submodule update --init --recursive
```

### macOS (primary development)
```bash
# Configure & build (Release)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_IMGUI_TEST_ENGINE=OFF
cmake --build build --config Release

# Configure & build (Debug)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_IMGUI_TEST_ENGINE=OFF
cmake --build build --config Debug
```

### Windows (from Developer Command Prompt)
```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

### Run the app
```bash
# macOS
open build/FindHelper.app
# or directly:
build/FindHelper.app/Contents/MacOS/FindHelper

# Windows
build\Release\FindHelper.exe
```

## Running Tests

```bash
# macOS/Linux - build and run all tests
scripts/build_tests_macos.sh

# Or manually:
cmake --build build --config Release
ctest --test-dir build --output-on-failure

# Windows - must specify config
ctest --test-dir build -C Release --output-on-failure

# Convenience target (all platforms)
cmake --build build --target run_tests
```

Tests use [doctest](https://github.com/doctest/doctest). To skip building tests: `cmake -S . -B build -DBUILD_TESTS=OFF`.

### ImGui Test Engine (macOS, in-process UI tests)
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_IMGUI_TEST_ENGINE=ON
cmake --build build --config Release
# Run with a test dataset:
build/FindHelper.app/Contents/MacOS/FindHelper --index-from-file=tests/data/std-linux-filesystem.txt
```

## Key CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_TESTS` | `ON` | Build unit tests |
| `ENABLE_IMGUI_TEST_ENGINE` | `OFF` | In-process UI tests (macOS) |
| `FAST_LIBS_BOOST` | `OFF` | Use Boost (unordered_map, regex, lockfree) |
| `ENABLE_MFT_METADATA_READING` | `OFF` | Read size/mod time from MFT on initial populate (Windows) |
| `ENABLE_PGO` | `OFF` | Profile-Guided Optimization (Windows) |

## Code Quality Tools

```bash
# clang-tidy on changed files (pre-commit hook)
scripts/pre-commit-clang-tidy.sh

# Full clang-tidy run
scripts/run_clang_tidy.sh

# Detect class/struct forward-declaration mismatches (MSVC C4099)
python3 scripts/find_class_struct_mismatches.py

# Coverage (macOS)
scripts/run_full_coverage_macos.sh

# Sonar scan
scripts/run_sonar_scanner.sh
```

A pre-commit hook runs clang-tidy automatically; see `scripts/README_PRE_COMMIT.md`.

## Architecture

### Source Layout (`src/`)

| Directory | Responsibility |
|-----------|---------------|
| `core/` | App bootstrap, `Application`, `ApplicationLogic`, `Settings`, CLI args |
| `index/` | `FileIndex` (facade), `FileIndexStorage`, `PathStorage`, `LazyAttributeLoader`, `InitialIndexPopulator`, `FileIndexMaintenance` |
| `search/` | `ParallelSearchEngine`, `SearchController`, `SearchWorker`, `StreamingResultsCollector`, search context/filters |
| `usn/` | Windows USN journal monitor (`UsnMonitor`, `UsnRecordUtils`) and `WindowsIndexBuilder` |
| `crawler/` | Cross-platform directory crawl for initial index population |
| `ui/` | All ImGui UI panels: `UIRenderer`, `ResultsTable`, `FilterPanel`, `SearchInputs`, `SettingsWindow`, etc. |
| `gui/` | `GuiState`, `RendererInterface`, `UIActions` (state shared between UI and logic) |
| `filters/` | Size and time filter types and utilities |
| `path/` | Path utilities, pattern matcher, regex, string search |
| `utils/` | `StringUtils`, `FileSystemUtils`, logging, etc. |
| `api/` | Gemini API integration (`GeminiApiUtils`) |
| `platform/` | Per-platform implementations: `windows/`, `macos/`, `linux/`; embedded fonts; `FileOperations.h` |

### FileIndex Component Architecture

`FileIndex` is a facade coordinating:
- **`PathStorage`** — Structure of Arrays (SoA) for path data; zero-copy access
- **`FileIndexStorage`** — `FileEntry` storage, string pool, directory cache
- **`PathBuilder`** — Stateless path computation
- **`LazyAttributeLoader`** — On-demand file size/mod-time I/O with caching
- **`ParallelSearchEngine`** — Multi-threaded search orchestration and load balancing

All components interact with `FileIndex` through the `ISearchableIndex` interface (no `friend` classes).

### Search Data Flow

1. User types in `SearchInputs` → `SearchController` builds a `SearchContext`
2. `ParallelSearchEngine` distributes work across `SearchWorker` threads
3. Workers stream results into `StreamingResultsCollector`
4. `GuiState` holds the current results; `ResultsTable` renders them
5. On Windows, `UsnMonitor` pushes real-time file-system changes to `FileIndexMaintenance`

### Threading Model

- **Main thread:** ImGui render loop + GUI state updates
- **Search thread pool:** `SearchThreadPool` / `SearchThreadPoolManager` coordinate workers
- `FileIndex` operations are protected; reads are designed for high concurrency
- `UsnMonitor` runs its own reader/processor threads (Windows only)

## Critical Coding Rules (from AGENTS.md)

### Platform guards
- Never modify code inside `#ifdef _WIN32` / `#ifdef __APPLE__` / `#ifdef __linux__` to make it cross-platform. Refactor to a platform-agnostic abstraction with separate per-platform implementations instead.
- Always comment `#endif` with the matching condition: `#endif  // _WIN32` (two spaces before `//`).

### Windows/MSVC compatibility
- Use `(std::min)(a, b)` and `(std::max)(a, b)` — never bare `std::min`/`std::max` (conflicts with Windows.h macros).
- Use `strcpy_safe` / `strcat_safe` from `StringUtils.h` instead of `strncpy`/`strcpy`/`strcat`.
- Forward declarations must match the definition keyword (`struct` vs `class`); run `python3 scripts/find_class_struct_mismatches.py` when touching headers.
- Lambdas inside template functions **must** use explicit capture lists (`[&x, &y]`, not `[&]` or `[=]`). Implicit captures cause MSVC C2062/C2059/C2143 errors that cascade into STL headers.

### Modern C++17 style
- Prefer C++17 init-statements: `if (auto it = map.find(k); it != map.end()) { ... }`.
- Use `std::string_view` for read-only string parameters instead of `const std::string&`.
- Use braced return: `return {value};` not `return std::string(value);`.
- Use `auto` when the type is obvious from the right-hand side (iterators, casts, factory returns).
- Use `[[maybe_unused]]` or unnamed parameters for intentionally unused parameters.
- Use RAII / smart pointers; avoid `new`/`delete` and `malloc`/`free`.
- Apply `const` to **all** non-mutating local variables, not just parameters and member functions — result IDs, path strings, bool flags, lock guards (`misc-const-correctness`).
- Use explicit lambda captures everywhere (also required for correctness on MSVC).
- Avoid explicit `std::string(view)` construction when the assignment target already accepts `std::string_view`: use `.assign(view)` for member assignment and pass `view` directly to `insert_or_assign` / `emplace` (C++17 mapped-type construction handles the conversion).
- Never create a `std::string_view` from a ternary where one branch produces a temporary `std::string` — the view dangles immediately. Use a named `static` (or `static thread_local`) fallback string instead.
- Do not declare `static` local variables inside loop bodies (SonarQube S3010); declare them before the loop.
- This project targets **C++17**. The clang-tidy `llvm-use-ranges` check recommends `std::ranges::` algorithms which require C++20 — suppress with `// NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20` rather than applying the suggestion.

### Naming conventions
- Types (classes, structs, enums, type aliases): `PascalCase`
- Functions and methods: `PascalCase`
- Local variables and parameters: `snake_case`
- Member variables: `snake_case_` (trailing underscore)
- Constants / `constexpr`: `kPascalCase` or `ALL_CAPS`

### Include order
1. `#pragma once`
2. Standard library headers
3. Platform-specific system headers (`<windows.h>`, etc.) inside `#ifdef`
4. Third-party / project headers

All `#include` directives must appear in the top block of the file (Sonar S954). Use lowercase for all include paths (Sonar S3806).

Within each group, headers are sorted alphabetically by the `llvm-include-order` check using **ASCII order** (uppercase letters sort before lowercase: `'A'–'Z'` = 65–90 < `'a'–'z'` = 97–122). Concretely: `"TestHelpers.h"` sorts before `"crawler/..."` because `'T'` (84) < `'c'` (99).

### Test infrastructure
`tests/.clang-tidy` suppresses three false-positive checks that doctest makes unavoidable across the entire test directory:
- `cert-err33-c` — every `CHECK()`/`REQUIRE()` triggers this; the framework discards the return value by design.
- `readability-function-cognitive-complexity` — `TEST_CASE` macros generate anonymous functions that aggregate many assertions.
- `readability-magic-numbers` — expected values in assertions are intentionally literal.
- `readability-identifier-naming` — test infrastructure (fixtures, mocks) follows project `snake_case_` convention which conflicts with the check in some patterns.

Do **not** add per-line `// NOLINT(cert-err33-c)` in test files; the directory-level config already handles it.

## Adding Features

Prompt templates for structured AI-assisted development are in `internal-docs/prompts/` and `specs/`:
- `internal-docs/prompts/TaskmasterPrompt.md` — turn a goal into a concrete task
- `internal-docs/prompts/AGENT_STRICT_CONSTRAINTS.md` — quality guardrails to paste into every task
- `internal-docs/prompts/AGENT_IMPACT_ANALYSIS_REGRESSION_PREVENTION.md` — regression prevention checklist
- `specs/SPECIFICATION_DRIVEN_DEVELOPMENT_PROMPT.md` — write a spec before implementing

Full docs index: `docs/DOCUMENTATION_INDEX.md`.

## Documentation Placement

When creating or updating documentation, use the correct folder:

- **`docs/`** — external contributors only: build guides, architecture/design docs, coding standards, platform notes, tooling guides. Content must be stable and publicly useful.
- **`internal-docs/`** — maintainer-only: AI agent prompt templates (`internal-docs/prompts/`), dated analyses and performance reviews (`internal-docs/analysis/`), in-progress design reviews (`internal-docs/design/`), plans and checklists (`internal-docs/plans/`), historical/archived material.
- **`specs/`** — formal feature specifications written before implementation.

**Decision rule:** Would a first-time external contributor need this to build or contribute? Yes → `docs/`. No → `internal-docs/`.

Dated analysis documents (`YYYY-MM-DD_` prefix), task prompts, Sonar/clang-tidy result files, performance trackers, and AI agent workflow files always go in `internal-docs/`.
