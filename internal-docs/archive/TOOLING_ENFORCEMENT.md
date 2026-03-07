# Tooling Enforcement for Naming Conventions

This document describes how to use `clang-format` and `clang-tidy` to enforce the naming conventions specified in `CXX17_NAMING_CONVENTIONS.md`.

## Overview

The project includes two configuration files:
- **`.clang-format`**: Enforces code formatting (indentation, spacing, line breaks, etc.)
- **`.clang-tidy`**: Enforces naming conventions and other code quality checks

## Prerequisites

### Installing clang-format and clang-tidy

**On Windows:**
- Install via Visual Studio Installer (C++ Clang Tools for Windows)
- Or download from: https://releases.llvm.org/
- Or use Chocolatey: `choco install llvm`

**On macOS:**
- Install via Homebrew: `brew install llvm`
- Or use Xcode Command Line Tools (may have older versions)

**On Linux:**
- Install via package manager: `sudo apt-get install clang-format clang-tidy` (Ubuntu/Debian)
- Or: `sudo yum install clang-tools-extra` (RHEL/CentOS)

### Verifying Installation

```bash
clang-format --version
clang-tidy --version
```

## Using clang-format

### Format a Single File

```bash
# Preview changes (dry-run)
clang-format file.cpp

# Format in-place
clang-format -i file.cpp

# Format a header file
clang-format -i file.h
```

### Format Multiple Files

```bash
# Format all C++ files in the project (excluding external/)
find . -name "*.cpp" -o -name "*.h" | grep -v external | xargs clang-format -i

# Format specific directories
clang-format -i src/*.cpp src/*.h
```

### Integration with Editors

**Visual Studio Code:**
- Install the "C/C++" extension
- Enable format on save: `"editor.formatOnSave": true`
- Set default formatter: `"C_Cpp.clang_format_style": "file"`

**Visual Studio:**
- Tools → Options → Text Editor → C/C++ → Code Style → Formatting
- Enable "Use clang-format" and set to "Use .clang-format file"

**CLion:**
- Settings → Editor → Code Style → C/C++
- Set "Use clang-format" and configure path to `.clang-format`

**Vim/Neovim:**
- Use plugins like `vim-clang-format` or `nvim-lspconfig` with clangd

## Using clang-tidy

### Check a Single File

```bash
# Basic check (requires compilation database)
clang-tidy file.cpp

# Specify include paths manually
clang-tidy file.cpp -- -I./include -I./external/imgui

# Check with specific checks only
clang-tidy file.cpp -checks='readability-identifier-naming'
```

### Generate Compilation Database

For best results, clang-tidy needs a compilation database (`compile_commands.json`):

**Using CMake:**
```bash
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON .
```

**Using Bear (Linux/macOS):**
```bash
bear -- make
```

**Using compiledb (Python):**
```bash
pip install compiledb
compiledb make
```

**Manual creation:**
Create `compile_commands.json` in the project root:
```json
[
  {
    "directory": "/path/to/project",
    "command": "cl /EHsc /std:c++17 /I./external/imgui main_gui.cpp",
    "file": "main_gui.cpp"
  }
]
```

### Check Multiple Files

```bash
# Check all C++ files
find . -name "*.cpp" -o -name "*.h" | grep -v external | xargs clang-tidy

# With compilation database
clang-tidy $(find . -name "*.cpp" | grep -v external)
```

### Apply Fixes Automatically

```bash
# Apply automatic fixes (where possible)
clang-tidy file.cpp -fix

# Apply fixes to multiple files
find . -name "*.cpp" | grep -v external | xargs clang-tidy -fix
```

**Note:** clang-tidy can automatically fix some issues (like formatting), but naming convention violations typically require manual fixes.

## CI/CD Integration

### GitHub Actions Example

```yaml
name: Code Quality

on: [push, pull_request]

jobs:
  format-check:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      - name: Install clang-format
        run: sudo apt-get update && sudo apt-get install -y clang-format
      - name: Check formatting
        run: |
          find . -name "*.cpp" -o -name "*.h" | grep -v external | xargs clang-format --dry-run --Werror

  tidy-check:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      - name: Install clang-tidy
        run: sudo apt-get update && sudo apt-get install -y clang-tidy
      - name: Run clang-tidy
        run: |
          find . -name "*.cpp" | grep -v external | xargs clang-tidy
```

## Limitations

### Naming Convention Checks

clang-tidy's `readability-identifier-naming` check has some limitations:

1. **Trailing Underscores**: Cannot automatically enforce `snake_case_` for member variables. This must be done manually or via a custom check.

2. **Prefixes**: Cannot automatically enforce prefixes like:
   - `g_` for global variables
   - `k` for constants (e.g., `kPascalCase`)

3. **Context-Aware Naming**: Some naming rules depend on context (e.g., Windows API macros should remain `UPPER_SNAKE_CASE`), which clang-tidy may flag incorrectly.

### Workarounds

1. **Manual Review**: Use clang-tidy as a guide, but review flagged items manually.

2. **Suppression Comments**: For false positives:
   ```cpp
   // NOLINTNEXTLINE(readability-identifier-naming)
   HWND hWnd;  // Windows API convention
   ```

3. **Custom Checks**: Consider writing custom clang-tidy checks for project-specific rules.

## Best Practices

1. **Run Before Committing**: Format and check code before committing:
   ```bash
   # Format
   find . -name "*.cpp" -o -name "*.h" | grep -v external | xargs clang-format -i
   
   # Check (if you have compile_commands.json)
   find . -name "*.cpp" | grep -v external | xargs clang-tidy
   ```

2. **Incremental Adoption**: Start with formatting, then gradually enable more clang-tidy checks.

3. **Editor Integration**: Set up format-on-save in your editor to maintain consistency.

4. **Team Alignment**: Ensure all team members use the same tool versions and configurations.

## Troubleshooting

### clang-tidy Can't Find Headers

```bash
# Specify include paths
clang-tidy file.cpp -- -I./include -I./external/imgui -std=c++17
```

### False Positives

Add suppression comments or adjust `.clang-tidy` configuration to disable specific checks for certain patterns.

### Performance

For large codebases, consider:
- Running clang-tidy on changed files only
- Using parallel execution: `clang-tidy -p . file1.cpp file2.cpp ...`
- Excluding external dependencies (already configured in `.clang-tidy`)

## References

- [clang-format Documentation](https://clang.llvm.org/docs/ClangFormat.html)
- [clang-tidy Documentation](https://clang.llvm.org/extra/clang-tidy/)
- [readability-identifier-naming Check](https://clang.llvm.org/extra/clang-tidy/checks/readability/identifier-naming.html)
- Project Naming Conventions: `CXX17_NAMING_CONVENTIONS.md`
