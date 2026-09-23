# Clang-Tidy Usage Guide

This guide provides instructions for running `clang-tidy` on the FindHelper project.

## Prerequisites

Ensure you have the following installed (Ubuntu/Debian):

```bash
sudo apt-get update
sudo apt-get install -y clang-tidy libxss-dev
```

## Running Clang-Tidy

### 1. Generate Compilation Database

Clang-Tidy needs a `compile_commands.json` file to understand the build flags.

```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

### 2. Run on Project Files

To run `clang-tidy` on all project source files:

```bash
# Find all .cpp files in src (excluding external)
find src -name "*.cpp" -not -path "*/external/*" > project_files.txt

# Run clang-tidy
xargs clang-tidy -p build < project_files.txt
```

### 3. Working Command for Specific Files

If you encounter issues with system headers in the environment (e.g., "file not found" errors for standard headers), use the following workaround:

```bash
clang-tidy <file_path> --extra-arg="-nostdinc++" -- -std=c++17 -Isrc -Iexternal/nlohmann_json/single_include
```

## Troubleshooting

### Segmentation Faults
In some environments, `clang-tidy` may crash when processing complex C++ templates. If this happens:
- Try running it on individual files instead of all at once.
- Reduce the number of enabled checks temporarily.
- Ensure all dependencies (like `libxss-dev`) are installed.

### False Positives
We have configured `.clang-tidy` to minimize noise, but some false positives may still occur.
- **Naming**: The project uses `kPascalCase` for constants and `snake_case_` for member variables.
- **SonarQube**: The configuration is aligned with SonarQube rules. Use `// NOSONAR` to suppress violations in code when necessary.

---
*Date: January 16, 2025*
