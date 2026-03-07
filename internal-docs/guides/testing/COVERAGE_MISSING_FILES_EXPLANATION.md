# Why Some Files Are Missing from Coverage Reports

## Answer to Your Question

**Q: Not all files of the project are included in the report, is this because they are not touched by current tests?**

**A: Yes, exactly!** Files are missing because they are **not compiled into any test executable**. llvm-cov can only report coverage for files that are:
1. Compiled into test executables (with coverage flags)
2. Actually executed during test runs

**Q: Is there an option to include them nevertheless?**

**A: Yes!** We've implemented a solution that allows you to include all source files in the coverage report.

## The Solution

### Option 1: Use `--show-missing` Flag (Identify Missing Files)

```bash
./scripts/generate_coverage_macos.sh --show-missing
```

This will:
- Generate the normal coverage report
- List all source files **not** in the coverage report
- Save the list to `coverage/missing_files.txt`

**Use this to:** Identify which files are missing and need tests.

### Option 2: Comprehensive Coverage Mode (Include All Files)

```bash
# Build with comprehensive coverage (includes all source files)
./scripts/build_tests_macos.sh --coverage --all-sources

# Generate coverage report (will now include all files)
./scripts/generate_coverage_macos.sh
```

This will:
- Create a `coverage_all_sources` test executable that includes all cross-platform source files
- All files will appear in the coverage report
- Files not executed by tests will show **0% coverage** (better than missing!)

## How It Works

### Why Files Are Missing

**llvm-cov limitation:** It can only report on files that are compiled into executables with coverage instrumentation. If a file isn't compiled into any test executable, it has no coverage data and won't appear in the report.

**Current situation:**
- **23 files** in coverage report (compiled into test executables)
- **~71 files** missing (not compiled into any test executable)

**Missing files include:**
- UI components (`ui/*.cpp`) - not in test executables
- Application entry points (`Application.cpp`, `main_*.cpp`) - not in tests
- Platform-specific files (`*_win.cpp`, `*_mac.mm`) - platform-specific
- Windows-specific components (`UsnMonitor.cpp`, `DirectXManager.cpp`) - Windows-only

### Comprehensive Coverage Mode

When you use `--all-sources`, a special test executable is created that includes:
- All cross-platform core files
- All UI components
- Platform-specific files (based on current platform)

**Benefits:**
- All files appear in coverage report
- Untested files show 0% coverage (clearly visible)
- Easy to identify what needs testing

**Limitations:**
- Some files may not compile (entry points, Windows-only code on macOS)
- Some files have dependencies that may cause compilation issues
- Platform-specific files only included on matching platform

## Example Output

### Without `--all-sources`:
```
Coverage Summary:
----------------
Filename                      Regions    Cover
CpuFeatures.cpp                    26    26.92%
FileIndex.cpp                    549    33.88%
...
TOTAL                           2664    53.08%
```
(Only 23 files shown)

### With `--all-sources`:
```
Coverage Summary:
----------------
Filename                      Regions    Cover
Application.cpp                   XXX     0.00%  ← Now visible!
ApplicationLogic.cpp             XXX     0.00%  ← Now visible!
CpuFeatures.cpp                    26    26.92%
FileIndex.cpp                    549    33.88%
ui/FilterPanel.cpp                XXX     0.00%  ← Now visible!
...
TOTAL                            XXXX    XX.XX%
```
(All files shown, untested ones at 0%)

## Recommendations

### For Development
1. **Use `--show-missing`** regularly to identify untested files
2. **Add tests** for missing files as you work on them
3. **Use `--all-sources`** when you want a complete picture

### For CI/CD
- Use `--all-sources` to get comprehensive coverage reports
- Set coverage thresholds (e.g., fail if coverage < 70%)
- Track coverage trends over time

## Technical Details

### How llvm-cov Works

1. **Compilation:** Files must be compiled with `-fprofile-instr-generate -fcoverage-mapping`
2. **Execution:** Test executables run and generate `.profraw` files
3. **Merging:** `llvm-profdata merge` combines all `.profraw` files
4. **Reporting:** `llvm-cov show/report` reads coverage data from executables

**Key point:** Coverage data is embedded in the executable's binary. If a file isn't in the binary, there's no coverage data for it.

### Comprehensive Test Executable

The `coverage_all_sources` executable:
- Includes all cross-platform source files
- Doesn't need to run (just needs to compile)
- Provides coverage mapping for all included files
- Files executed by other tests show actual coverage
- Files not executed show 0% coverage

## Summary

- **Missing files = not compiled into test executables**
- **Solution = use `--all-sources` flag to include all files**
- **Result = all files appear in report (0% if untested)**

Use `--show-missing` to identify what's missing, and `--all-sources` to include everything in the report!

