# Pre-commit Hook for Code Quality Checks

This directory contains a pre-commit hook script that runs `clang-tidy` on staged C++ files to catch code quality issues, including const correctness violations.

## Installation

### Option 1: Copy the script (recommended)
```bash
cp scripts/pre-commit-clang-tidy.sh .git/hooks/pre-commit
chmod +x .git/hooks/pre-commit
```

### Option 2: Create a symlink
```bash
ln -s ../../scripts/pre-commit-clang-tidy.sh .git/hooks/pre-commit
chmod +x .git/hooks/pre-commit
```

## What It Does

The pre-commit hook:
1. Checks if `clang-tidy` is available (skips if not installed)
2. Uses the **same config and build dir** as `scripts/run_clang_tidy.sh`:
   - **Config:** `--config-file=<project>/.clang-tidy` (project root `.clang-tidy`)
   - **Compile DB:** `build/compile_commands.json`, or `build_coverage/`, or root `compile_commands.json`
3. Runs `clang-tidy` on all staged C++ files (`.cpp`, `.h`, `.hpp`, `.cxx`, `.cc`)
4. Blocks the commit if any clang-tidy warning or error is found (same checks as analysis)

This keeps pre-commit and the analysis script **consistent**: the same warnings are reported and measured.

## Requirements

- `clang-tidy` must be installed and available in PATH
- `.clang-tidy` must exist at project root
- `compile_commands.json` must exist in `build/`, `build_coverage/`, or project root (from CMake)

## Bypassing the Hook

If you need to bypass the hook (not recommended):
```bash
git commit --no-verify
```

## Manual Testing

You can test the hook manually:
```bash
# Stage some files
git add src/some_file.cpp

# Run the hook manually
.git/hooks/pre-commit
```

## Troubleshooting

### clang-tidy not found
Install clang-tidy:
- **macOS**: `brew install llvm` (adds clang-tidy)
- **Linux**: `sudo apt-get install clang-tidy` (Ubuntu/Debian)
- **Windows**: Install LLVM from https://llvm.org/builds/

### compile_commands.json not found
Generate it with CMake (same as for the analysis script):
```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
# This creates build/compile_commands.json; the hook uses -p build automatically.
# Optional: symlink to project root if you run clang-tidy from root without -p:
#   ln -s build/compile_commands.json .
```

### Hook is too slow
The hook only checks staged files, so it should be fast. If it's slow:
- Make sure you're only staging the files you changed
- Consider running clang-tidy manually before committing

## Related Documentation

- `.clang-tidy` - Main clang-tidy configuration
- `AGENTS.md` - Const correctness rules and guidelines
- `internal-docs/analysis/code-quality/SONAR_ISSUES_2026-01-13_FIXES.md` - Analysis of const correctness issues
