# Clang-Tidy Usage Guide

This guide provides instructions for running and maintaining `clang-tidy` on the FindHelper project.

---

## Prerequisites

### macOS
Install LLVM via Homebrew:
```bash
brew install llvm python3
```
Homebrew's `clang-tidy` binary is typically located at `/opt/homebrew/opt/llvm/bin/clang-tidy` (Apple Silicon) or `/usr/local/opt/llvm/bin/clang-tidy` (Intel).

### Linux (Ubuntu/Debian)
```bash
sudo apt-get update
sudo apt-get install -y clang-tidy python3
```

---

## Recommended Workflows

### 1. Pre-Commit Hook (Daily Development)
The fastest way to ensure code quality is the pre-commit hook, which inspects only staged C++ files:
```bash
# Run manually:
scripts/pre-commit-clang-tidy.sh

# Or install as git hook:
ln -sf ../../scripts/pre-commit-clang-tidy.sh .git/hooks/pre-commit
```
The hook automatically:
- Checks out only staged `.cpp`, `.h`, `.mm` files.
- Generates a PCH-stripped compile database via `scripts/make_clang_tidy_compile_db.py`.
- Runs `clang-tidy` via `scripts/clang-tidy-wrapper.sh` with correct SDK includes.
- Filters out false positives using `scripts/filter_clang_tidy_init_statements.py`.

### 2. Full Project Scan
To analyze all project source files in parallel and generate a date-stamped report:
```bash
# Run full scan (uses all CPU cores by default):
./scripts/run_clang_tidy.sh

# Options:
./scripts/run_clang_tidy.sh --jobs 8             # Specify concurrency
./scripts/run_clang_tidy.sh --output report.txt  # Custom output file
./scripts/run_clang_tidy.sh --summary-only       # Show summary metrics only
```

### 3. Single-File Analysis
For targeted inspection of an individual file:
```bash
# 1. Ensure PCH-stripped database is up to date:
python3 scripts/make_clang_tidy_compile_db.py

# 2. Run wrapper on file:
scripts/clang-tidy-wrapper.sh -p .cache/clang-tidy-db src/path/PathUtils.cpp
```

---

## Architecture & Configuration

### Compilation Database & Precompiled Headers (PCH)
- CMake exports compile flags when building with `CMAKE_EXPORT_COMPILE_COMMANDS=ON` (enabled by default).
- On macOS, Apple Clang creates Precompiled Headers (`.pch`). Because Homebrew's upstream LLVM `clang-tidy` cannot deserialize Apple Clang's AST format, running `clang-tidy` directly on `build/compile_commands.json` produces PCH format mismatch errors.
- `scripts/make_clang_tidy_compile_db.py` strips `-include-pch` flags and writes a clean database to `.cache/clang-tidy-db/compile_commands.json`.

### Configuration Hierarchy
1. **Root [`.clang-tidy`](../../.clang-tidy)**:
   - Enforces C++17 modern practices and project conventions (see [`docs/standards/CXX17_NAMING_CONVENTIONS.md`](../standards/CXX17_NAMING_CONVENTIONS.md)).
   - Explicitly enables targeted check families (`bugprone-*`, `cert-*`, `concurrency-*`, `cppcoreguidelines-*`, `google-*`, `hicpp-*`, `llvm-*`, `misc-*`, `modernize-*`, `performance-*`, `portability-*`, `readability-*`).
   - Disables non-applicable checks (e.g. `abseil-*`, `android-*`, `fuchsia-*`, `llvmlibc-*`) and heavy path-sensitive analyzer checks (`clang-analyzer-*`, handled separately by `scan-build`).
2. **Test Suite [`tests/.clang-tidy`](../../tests/.clang-tidy)**:
   - Uses `InheritParentConfig: true`.
   - Suppresses unavoidable doctest macro false positives (`cert-err33-c`, `readability-function-cognitive-complexity`, `readability-magic-numbers`, `bugprone-empty-catch`).

---

## Suppressions & In-Code Rules
- To suppress a false positive on a specific line, use `// NOLINT(<check-name>)` on the same line with justification:
  ```cpp
  auto* ptr = static_cast<char*>(buf);  // NOLINT(cppcoreguidelines-pro-type-cstyle-cast) - C API buffer handoff
  ```
- Do not use bare `// NOLINT` without check name.
- SonarQube suppressions should use `// NOSONAR(rule-id)`.
