# clang-tidy Script Usage Guide

**Date:** 2026-01-17

## Overview

The `scripts/run_clang_tidy.sh` script provides an easy way to run clang-tidy on the entire project and generate comprehensive warnings and error reports.

## Quick Start

```bash
# Run clang-tidy on all source files and save report
./scripts/run_clang_tidy.sh

# View only summary statistics
./scripts/run_clang_tidy.sh --summary-only

# Save to custom file
./scripts/run_clang_tidy.sh --output my-report.txt
```

## Usage

### Basic Usage

```bash
./scripts/run_clang_tidy.sh
```

This will:
- Find all source files in `src/` (excluding `external/`)
- Run clang-tidy on each file
- Generate a report file: `clang-tidy-report-YYYY-MM-DD.txt`
- Display summary statistics
- Show top warning categories

### Options

| Option | Description |
|--------|-------------|
| `--output FILE` | Save output to specified file (default: `clang-tidy-report-YYYY-MM-DD.txt`) |
| `--summary-only` | Show only summary statistics, not detailed warnings |
| `--no-summary` | Don't show summary, only detailed warnings |
| `--quiet` | Suppress progress messages |
| `--help`, `-h` | Show help message |

### Examples

**Generate full report with default filename:**
```bash
./scripts/run_clang_tidy.sh
```

**Generate summary only (faster):**
```bash
./scripts/run_clang_tidy.sh --summary-only
```

**Save to custom location:**
```bash
./scripts/run_clang_tidy.sh --output docs/analysis/clang-tidy-report.txt
```

**Quiet mode (minimal output):**
```bash
./scripts/run_clang_tidy.sh --quiet
```

**View only warnings (no summary):**
```bash
./scripts/run_clang_tidy.sh --no-summary
```

## Report Format

The generated report includes:

1. **Header**: Date, project path, files analyzed
2. **Summary**: Total warnings, errors, and top categories
3. **Detailed Warnings**: Full list of all warnings and errors with file locations

### Example Output

```
================================================================================
clang-tidy Analysis Report
Generated: Mon Jan 17 14:30:00 PST 2026
Project: /Users/brunoorsier/dev/USN_WINDOWS
Files Analyzed: 182
================================================================================

SUMMARY
================================================================================
Total Warnings: 3133
Total Errors:   51
Total Issues:  3184

Top Warning Categories:
--------------------------------------------------------------------------------
   525  [readability-identifier-naming]
   350  [hicpp-uppercase-literal-suffix,readability-uppercase-literal-suffix]
   278  [cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers]
   ...
```

## Prerequisites

1. **clang-tidy installed**: 
   - macOS: `brew install llvm`
   - Linux: `sudo apt-get install clang-tidy`
   - Windows: Included with Visual Studio or install LLVM

2. **compile_commands.json**: 
   ```bash
   cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
   ln -sf build/compile_commands.json compile_commands.json
   ```

## Performance

- **Full analysis**: ~2-5 minutes (depends on system)
- **Summary only**: ~1-2 minutes (faster, less output)

The script processes 182 source files, so it may take a few minutes to complete.

## Integration

### Regular Checks

Run periodically to track code quality:

```bash
# Weekly check
./scripts/run_clang_tidy.sh --output docs/analysis/weekly-reports/clang-tidy-$(date +%Y-%m-%d).txt
```

### CI/CD Integration

For CI/CD, use summary-only mode:

```bash
./scripts/run_clang_tidy.sh --summary-only --quiet > clang-tidy-summary.txt
```

### Pre-commit Hook

The script can be used in pre-commit hooks (see `scripts/pre-commit-clang-tidy.sh` for staged files only).

## Troubleshooting

### clang-tidy not found

**Error:** `✗ Error: clang-tidy not found`

**Solution:**
- macOS: `brew install llvm`
- Linux: `sudo apt-get install clang-tidy`
- Verify: `clang-tidy --version`

### compile_commands.json not found

**Error:** `⚠ Warning: compile_commands.json not found`

**Solution:**
```bash
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
ln -sf build/compile_commands.json compile_commands.json
```

### Too many warnings

If the output is overwhelming, use `--summary-only` to see just the statistics:

```bash
./scripts/run_clang_tidy.sh --summary-only
```

## Related Documentation

- **Full Warnings List**: `docs/analysis/2026-01-17_CLANG_TIDY_WARNINGS_LIST.md`
- **Zero Warnings Plan**: `docs/analysis/2026-01-17_CLANG_TIDY_ZERO_WARNINGS_PLAN.md`
- **Clang-Tidy Guide**: `docs/analysis/CLANG_TIDY_GUIDE.md`
- **Clang-Tidy Configuration**: `.clang-tidy`

## Notes

- The script automatically uses the `clang-tidy-wrapper.sh` if available (for macOS system header fixes)
- Reports are saved with timestamps for tracking progress over time
- The script exits with code 0 even if warnings are found (check the report file for details)
- Processing messages are filtered out to keep output clean
