# Duplo Integration Research
**Date:** 2025-12-30  
**Tool:** [Duplo](https://github.com/dlidstrom/Duplo) - Duplicate Code Detection  
**Purpose:** Research integration options for duplicate code detection in USN_WINDOWS toolchain

---

## Executive Summary

**Duplo** is a high-performance duplicate code detection tool that can identify duplicated code blocks across various source code formats. It supports C++, C#, Java, Ada, and can analyze arbitrary text files.

**Key Features:**
- Language-aware analysis (removes comments, preprocessor directives)
- High performance (~10,000+ lines/second, single-threaded)
- Multi-threaded support (`-j` option)
- Multiple output formats (text, JSON, XML with stylesheet)
- Cross-platform (Windows, macOS, Linux binaries available)

**Integration Recommendation:**
- **Option 1 (Recommended):** Standalone script for manual/CI runs
- **Option 2:** CMake custom target for developer convenience
- **Option 3:** CI/CD integration (GitHub Actions, etc.)

---

## Duplo Overview

### What Duplo Does

Duplo detects duplicate code blocks by:
1. **Normalizing code** - Removes comments, preprocessor directives, whitespace
2. **Tokenizing** - Breaks code into tokens for comparison
3. **Finding matches** - Identifies blocks of code that appear multiple times
4. **Reporting** - Outputs duplicate locations with line numbers

### Performance Characteristics

From Duplo's documentation:
- **Single-threaded:** ~10,000+ lines/second
- **Multi-threaded:** Near-linear improvement with `-j` option
- **Example:** Quake2 (266 files, 102,740 LOC) processed in ~9 seconds

### Supported Languages

- **C++** (`.cpp`, `.h`, `.hpp`, `.cxx`, `.cc`)
- **C#** (`.cs`)
- **Java** (`.java`)
- **Ada** (`.ada`, `.adb`, `.ads`)
- **Generic text** (any file format, but without language-specific normalization)

---

## Integration Options

### Option 1: Standalone Script (Recommended)

**Pros:**
- Simple to implement and maintain
- Easy to run manually or in CI/CD
- No CMake complexity
- Works across all platforms

**Cons:**
- Requires manual Duplo installation
- Not integrated into build system

**Implementation:**

Create `scripts/run_duplo.sh` (macOS/Linux) and `scripts/run_duplo.ps1` (Windows):

```bash
#!/bin/bash
# scripts/run_duplo.sh
# Run Duplo duplicate code detection on USN_WINDOWS codebase

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Duplo executable path (adjust based on installation)
DUPLO_BIN="${DUPLO_BIN:-duplo}"

# Check if Duplo is available
if ! command -v "$DUPLO_BIN" &> /dev/null; then
    echo "ERROR: Duplo not found. Install from: https://github.com/dlidstrom/Duplo/releases"
    echo "  macOS: brew install duplo (if available) or download binary"
    echo "  Linux: Download from releases page"
    exit 1
fi

# Output file
OUTPUT_FILE="${PROJECT_ROOT}/duplo_report.txt"
JSON_OUTPUT="${PROJECT_ROOT}/duplo_report.json"

# Minimum lines for duplicate detection (configurable)
MIN_LINES="${DUPLO_MIN_LINES:-15}"

echo "=========================================="
echo "Running Duplo duplicate code detection"
echo "=========================================="
echo "Project root: $PROJECT_ROOT"
echo "Minimum duplicate lines: $MIN_LINES"
echo "Output: $OUTPUT_FILE"
echo ""

# Find all C++ source files (exclude external dependencies)
cd "$PROJECT_ROOT"
find . -type f \( -iname "*.cpp" -o -iname "*.h" -o -iname "*.hpp" \) \
    ! -path "./external/*" \
    ! -path "./build/*" \
    ! -path "./build_*/*" \
    ! -path "./coverage/*" \
    ! -path "./.git/*" \
    | "$DUPLO_BIN" -ml "$MIN_LINES" -ip - "$OUTPUT_FILE"

# Also generate JSON output for programmatic processing
find . -type f \( -iname "*.cpp" -o -iname "*.h" -o -iname "*.hpp" \) \
    ! -path "./external/*" \
    ! -path "./build/*" \
    ! -path "./build_*/*" \
    ! -path "./coverage/*" \
    ! -path "./.git/*" \
    | "$DUPLO_BIN" -ml "$MIN_LINES" -ip -json "$JSON_OUTPUT" -

if [ $? -eq 0 ]; then
    echo ""
    echo "=========================================="
    echo "Duplo analysis complete!"
    echo "=========================================="
    echo "Text report: $OUTPUT_FILE"
    echo "JSON report: $JSON_OUTPUT"
    echo ""
    echo "Review the report and refactor duplicated code blocks."
else
    echo "ERROR: Duplo analysis failed!"
    exit 1
fi
```

**Windows PowerShell version:**

```powershell
# scripts/run_duplo.ps1
# Run Duplo duplicate code detection on USN_WINDOWS codebase

param(
    [int]$MinLines = 15,
    [string]$DuploBin = "duplo.exe"
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent $ScriptDir

# Check if Duplo is available
if (-not (Get-Command $DuploBin -ErrorAction SilentlyContinue)) {
    Write-Host "ERROR: Duplo not found. Install from: https://github.com/dlidstrom/Duplo/releases"
    Write-Host "  Download Windows binary and add to PATH"
    exit 1
}

$OutputFile = Join-Path $ProjectRoot "duplo_report.txt"
$JsonOutput = Join-Path $ProjectRoot "duplo_report.json"

Write-Host "=========================================="
Write-Host "Running Duplo duplicate code detection"
Write-Host "=========================================="
Write-Host "Project root: $ProjectRoot"
Write-Host "Minimum duplicate lines: $MinLines"
Write-Host "Output: $OutputFile"
Write-Host ""

# Find all C++ source files (exclude external dependencies)
$SourceFiles = Get-ChildItem -Path $ProjectRoot -Recurse -Include *.cpp,*.h,*.hpp `
    | Where-Object { 
        $_.FullName -notmatch "\\external\\" -and
        $_.FullName -notmatch "\\build" -and
        $_.FullName -notmatch "\\build_" -and
        $_.FullName -notmatch "\\coverage\\" -and
        $_.FullName -notmatch "\\.git\\"
    } | ForEach-Object { $_.FullName }

# Write file list to temporary file
$TempFile = [System.IO.Path]::GetTempFileName()
$SourceFiles | Out-File -FilePath $TempFile -Encoding ASCII

# Run Duplo
& $DuploBin -ml $MinLines -ip $TempFile $OutputFile

# Generate JSON output
& $DuploBin -ml $MinLines -ip -json $JsonOutput $TempFile

# Clean up
Remove-Item $TempFile

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "=========================================="
    Write-Host "Duplo analysis complete!"
    Write-Host "=========================================="
    Write-Host "Text report: $OutputFile"
    Write-Host "JSON report: $JsonOutput"
    Write-Host ""
    Write-Host "Review the report and refactor duplicated code blocks."
} else {
    Write-Host "ERROR: Duplo analysis failed!"
    exit 1
}
```

**Usage:**

```bash
# macOS/Linux
./scripts/run_duplo.sh

# Windows
.\scripts\run_duplo.ps1

# Custom minimum lines
DUPLO_MIN_LINES=20 ./scripts/run_duplo.sh
```

---

### Option 2: CMake Custom Target

**Pros:**
- Integrated into build system
- Easy to run: `cmake --build . --target duplo`
- Can be part of CI/CD builds

**Cons:**
- Requires Duplo to be installed/available
- CMake configuration complexity
- May not work if Duplo isn't found

**Implementation:**

Add to `CMakeLists.txt`:

```cmake
# --- Duplo Integration (Optional) ---
option(ENABLE_DUPLO "Enable Duplo duplicate code detection" OFF)

if(ENABLE_DUPLO)
    # Find Duplo executable
    find_program(DUPLO_EXECUTABLE
        NAMES duplo duplo.exe
        PATHS
            ${CMAKE_SOURCE_DIR}/tools/duplo
            ${CMAKE_SOURCE_DIR}/external/duplo
            /usr/local/bin
            /usr/bin
        DOC "Path to Duplo executable"
    )
    
    if(DUPLO_EXECUTABLE)
        message(STATUS "Duplo found: ${DUPLO_EXECUTABLE}")
        
        # Minimum lines for duplicate detection
        set(DUPLO_MIN_LINES 15 CACHE STRING "Minimum lines for duplicate detection")
        
        # Output files
        set(DUPLO_OUTPUT_TXT "${CMAKE_BINARY_DIR}/duplo_report.txt")
        set(DUPLO_OUTPUT_JSON "${CMAKE_BINARY_DIR}/duplo_report.json")
        
        # Collect source files (exclude external dependencies)
        file(GLOB_RECURSE DUPLO_SOURCE_FILES
            "${CMAKE_SOURCE_DIR}/*.cpp"
            "${CMAKE_SOURCE_DIR}/*.h"
            "${CMAKE_SOURCE_DIR}/*.hpp"
        )
        
        # Filter out external dependencies and build directories
        list(FILTER DUPLO_SOURCE_FILES EXCLUDE REGEX ".*/external/.*")
        list(FILTER DUPLO_SOURCE_FILES EXCLUDE REGEX ".*/build.*")
        list(FILTER DUPLO_SOURCE_FILES EXCLUDE REGEX ".*/coverage/.*")
        list(FILTER DUPLO_SOURCE_FILES EXCLUDE REGEX ".*/\\.git/.*")
        
        # Create file list
        set(DUPLO_FILE_LIST "${CMAKE_BINARY_DIR}/duplo_files.txt")
        file(WRITE "${DUPLO_FILE_LIST}" "")
        foreach(file ${DUPLO_SOURCE_FILES})
            file(APPEND "${DUPLO_FILE_LIST}" "${file}\n")
        endforeach()
        
        # Custom target for text output
        add_custom_target(duplo
            COMMAND ${DUPLO_EXECUTABLE} -ml ${DUPLO_MIN_LINES} -ip ${DUPLO_FILE_LIST} ${DUPLO_OUTPUT_TXT}
            COMMAND ${CMAKE_COMMAND} -E echo "Duplo report generated: ${DUPLO_OUTPUT_TXT}"
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Running Duplo duplicate code detection..."
            VERBATIM
        )
        
        # Custom target for JSON output
        add_custom_target(duplo_json
            COMMAND ${DUPLO_EXECUTABLE} -ml ${DUPLO_MIN_LINES} -ip -json ${DUPLO_OUTPUT_JSON} ${DUPLO_FILE_LIST}
            COMMAND ${CMAKE_COMMAND} -E echo "Duplo JSON report generated: ${DUPLO_OUTPUT_JSON}"
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Running Duplo duplicate code detection (JSON output)..."
            VERBATIM
        )
        
        message(STATUS "  Run: cmake --build . --target duplo")
        message(STATUS "  Or:  cmake --build . --target duplo_json")
    else()
        message(WARNING "Duplo not found. Install from: https://github.com/dlidstrom/Duplo/releases")
        message(WARNING "  Set DUPLO_EXECUTABLE to path of duplo executable")
        message(WARNING "  Or disable with: -DENABLE_DUPLO=OFF")
    endif()
else()
    message(STATUS "Duplo disabled (use -DENABLE_DUPLO=ON to enable)")
endif()
```

**Usage:**

```bash
# Configure with Duplo enabled
cmake -S . -B build -DENABLE_DUPLO=ON

# Run Duplo analysis
cmake --build build --target duplo

# Or JSON output
cmake --build build --target duplo_json
```

---

### Option 3: CI/CD Integration

**GitHub Actions Example:**

```yaml
name: Code Quality

on:
  push:
    branches: [ main, develop ]
  pull_request:
    branches: [ main, develop ]
  schedule:
    - cron: '0 0 * * 0'  # Weekly on Sunday

jobs:
  duplo:
    name: Duplicate Code Detection
    runs-on: ${{ matrix.os }}
    strategy:
      matrix:
        os: [ubuntu-latest, windows-latest, macos-latest]
    
    steps:
      - name: Checkout code
        uses: actions/checkout@v4
      
      - name: Download Duplo
        shell: bash
        run: |
          if [[ "${{ runner.os }}" == "Linux" ]]; then
            curl -L -o duplo.zip https://github.com/dlidstrom/Duplo/releases/latest/download/duplo-linux.zip
            unzip duplo.zip
            chmod +x duplo
            sudo mv duplo /usr/local/bin/
          elif [[ "${{ runner.os }}" == "macOS" ]]; then
            curl -L -o duplo.zip https://github.com/dlidstrom/Duplo/releases/latest/download/duplo-macos.zip
            unzip duplo.zip
            chmod +x duplo
            sudo mv duplo /usr/local/bin/
          else  # Windows
            curl -L -o duplo.zip https://github.com/dlidstrom/Duplo/releases/latest/download/duplo-windows.zip
            Expand-Archive duplo.zip -DestinationPath .
            Move-Item duplo/duplo.exe C:\Windows\System32\duplo.exe
          fi
      
      - name: Run Duplo
        shell: bash
        run: |
          find . -type f \( -iname "*.cpp" -o -iname "*.h" -o -iname "*.hpp" \) \
            ! -path "./external/*" \
            ! -path "./build/*" \
            ! -path "./.git/*" \
            | duplo -ml 15 -ip - duplo_report.txt
      
      - name: Upload Duplo Report
        uses: actions/upload-artifact@v3
        if: always()
        with:
          name: duplo-report-${{ matrix.os }}
          path: duplo_report.txt
      
      - name: Check for Duplicates
        shell: bash
        run: |
          if [ -s duplo_report.txt ]; then
            echo "⚠️ Duplicate code blocks detected!"
            cat duplo_report.txt
            # Optionally fail the build if duplicates exceed threshold
            # exit 1
          else
            echo "✅ No duplicate code blocks found"
          fi
```

**GitLab CI Example:**

```yaml
duplo:
  stage: test
  image: ubuntu:latest
  before_script:
    - apt-get update && apt-get install -y curl unzip
    - curl -L -o duplo.zip https://github.com/dlidstrom/Duplo/releases/latest/download/duplo-linux.zip
    - unzip duplo.zip && chmod +x duplo && mv duplo /usr/local/bin/
  script:
    - find . -type f \( -iname "*.cpp" -o -iname "*.h" -o -iname "*.hpp" \) ! -path "./external/*" ! -path "./build/*" | duplo -ml 15 -ip - duplo_report.txt
    - |
      if [ -s duplo_report.txt ]; then
        echo "⚠️ Duplicate code blocks detected!"
        cat duplo_report.txt
      else
        echo "✅ No duplicate code blocks found"
      fi
  artifacts:
    paths:
      - duplo_report.txt
    expire_in: 1 week
```

---

## Duplo Command-Line Options

### Key Options

- **`-ml <n>`** - Minimum lines for duplicate detection (default: 3)
- **`-ip`** - Ignore preprocessor directives
- **`-json <file>`** - Output JSON format
- **`-xml <file>`** - Output XML format (with stylesheet for browser viewing)
- **`-j <n>`** - Use multiple threads (performance improvement)

### Example Commands

```bash
# Basic usage (minimum 15 lines)
find . -name "*.cpp" -o -name "*.h" | duplo -ml 15 -ip - report.txt

# JSON output for programmatic processing
find . -name "*.cpp" -o -name "*.h" | duplo -ml 15 -ip -json report.json -

# Multi-threaded (faster for large codebases)
find . -name "*.cpp" -o -name "*.h" | duplo -ml 15 -ip -j 4 - report.txt

# XML output with stylesheet (for browser viewing)
find . -name "*.cpp" -o -name "*.h" | duplo -ml 15 -ip -xml report.xml -
```

---

## Installation Options

### Option 1: Pre-built Binaries (Recommended)

Download from [Duplo Releases](https://github.com/dlidstrom/Duplo/releases):

- **macOS:** `duplo-macos.zip`
- **Linux:** `duplo-linux.zip`
- **Windows:** `duplo-windows.zip`

Extract and add to PATH, or place in project `tools/duplo/` directory.

### Option 2: Build from Source

Duplo uses CMake, so it can be built from source:

```bash
git clone https://github.com/dlidstrom/Duplo.git
cd Duplo
mkdir build && cd build
cmake ..
make  # or cmake --build . on Windows
```

### Option 3: Package Managers

- **macOS:** May be available via Homebrew (check: `brew search duplo`)
- **Linux:** Check distribution package repositories
- **Windows:** Use pre-built binary or build from source

---

## Integration with Existing Toolchain

### Relationship to Other Tools

**Complementary Tools:**
- **clang-tidy** - Static analysis (code quality, style)
- **Code coverage** - Test coverage analysis
- **Address Sanitizer** - Runtime memory error detection
- **Duplo** - Duplicate code detection (this tool)

**Workflow Integration:**

1. **Pre-commit:** Run Duplo manually before committing
2. **CI/CD:** Run Duplo in CI pipeline (weekly or on PR)
3. **Code Review:** Review Duplo reports as part of PR process
4. **Refactoring:** Use Duplo reports to identify refactoring opportunities

### Recommended Workflow

```bash
# 1. Run Duplo before major refactoring
./scripts/run_duplo.sh

# 2. Review report
cat duplo_report.txt

# 3. Refactor duplicated code blocks

# 4. Re-run to verify improvements
./scripts/run_duplo.sh
```

---

## Output Format

### Text Output Example

```
src\engine\geometry\simple\TorusGeometry.cpp(56)
src\engine\geometry\simple\SphereGeometry.cpp(54)
    pBuffer[currentIndex*size+3]=(i+1)/(float)subdsU;
    pBuffer[currentIndex*size+4]=j/(float)subdsV;
    currentIndex++;
    pPrimitiveBuffer->unlock();
```

### JSON Output Structure

```json
{
  "duplicates": [
    {
      "lines": 4,
      "files": [
        {
          "path": "src/file1.cpp",
          "start": 56,
          "end": 59
        },
        {
          "path": "src/file2.cpp",
          "start": 54,
          "end": 57
        }
      ],
      "code": "pBuffer[currentIndex*size+3]=(i+1)/(float)subdsU;\n..."
    }
  ]
}
```

---

## Thresholds and Configuration

### Recommended Settings

**Minimum Lines:**
- **15 lines** - Good balance (catches meaningful duplicates, avoids noise)
- **20 lines** - Stricter (only larger duplicates)
- **10 lines** - More sensitive (may catch false positives)

**File Exclusions:**
- `external/` - Third-party dependencies
- `build*/` - Build artifacts
- `coverage/` - Coverage reports
- `.git/` - Git metadata

**Language Support:**
- C++ files (`.cpp`, `.h`, `.hpp`) are automatically detected
- Use `-ip` flag to ignore preprocessor directives (recommended for C++)

---

## Performance Considerations

### Large Codebases

For large codebases (100,000+ LOC):
- Use `-j` option for multi-threading
- Run on subset of files initially
- Consider running during off-peak hours in CI

### Optimization Tips

1. **Exclude generated files** - Don't analyze build artifacts
2. **Use file lists** - Pre-generate file lists for faster runs
3. **Parallel execution** - Use `-j` option with number of CPU cores
4. **Incremental analysis** - Only analyze changed files in CI

---

## Recommendations

### Immediate Actions

1. **Create scripts** - Implement Option 1 (standalone scripts)
2. **Test on codebase** - Run initial analysis to establish baseline
3. **Review results** - Identify high-priority duplicates
4. **Document findings** - Track duplicate code blocks in technical debt

### Long-term Integration

1. **CMake target** - Add Option 2 for developer convenience
2. **CI/CD integration** - Add Option 3 for automated checks
3. **Threshold enforcement** - Fail CI if duplicates exceed threshold
4. **Regular reviews** - Schedule weekly/monthly duplicate reviews

### Best Practices

1. **Don't fail builds** - Use Duplo as advisory tool initially
2. **Focus on large duplicates** - Prioritize 20+ line duplicates
3. **Refactor incrementally** - Don't try to fix everything at once
4. **Track improvements** - Monitor duplicate count over time

---

## References

- **Duplo GitHub:** https://github.com/dlidstrom/Duplo
- **Duplo Releases:** https://github.com/dlidstrom/Duplo/releases
- **Duplo Documentation:** See README.md in repository
- **Algorithm Background:** Based on Duploc techniques (see Duplo README)

---

## Next Steps

1. **Review this document** - Validate integration approach
2. **Choose integration option** - Option 1 (scripts) recommended for start
3. **Implement scripts** - Create `scripts/run_duplo.sh` and `scripts/run_duplo.ps1`
4. **Test integration** - Run on codebase and review results
5. **Iterate** - Refine based on findings and team feedback

---

## Implementation Status

**Option 1 (Standalone Scripts):** ✅ **IMPLEMENTED**

Scripts created:
- `scripts/run_duplo.sh` - macOS/Linux script
- `scripts/run_duplo.ps1` - Windows PowerShell script

**Usage:**

```bash
# macOS/Linux
./scripts/run_duplo.sh

# Windows
.\scripts\run_duplo.ps1

# Custom minimum lines
DUPLO_MIN_LINES=20 ./scripts/run_duplo.sh
.\scripts\run_duplo.ps1 -MinLines 20
```

**Next Steps:**
1. Install Duplo binary (see Installation Options above)
2. Test scripts: `./scripts/run_duplo.sh`
3. Review initial duplicate code report
4. Consider implementing Option 2 (CMake target) for build integration

---

**Status:** Option 1 Implemented  
**Next Action:** Test scripts and review initial results

