<#
.SYNOPSIS
    Run Duplo duplicate code detection on USN_WINDOWS codebase

.DESCRIPTION
    This script finds all C++ source files in the project (excluding external
    dependencies and build artifacts) and runs Duplo to detect duplicate code blocks.
    It generates both text and JSON output reports.

.PARAMETER MinLines
    Minimum number of lines for duplicate detection (default: 15)

.PARAMETER DuploBin
    Path to Duplo executable (default: searches in PATH)

.EXAMPLE
    .\scripts\run_duplo.ps1
    Run Duplo with default settings (minimum 15 lines)

.EXAMPLE
    .\scripts\run_duplo.ps1 -MinLines 20
    Run Duplo with minimum 20 lines for duplicate detection

.EXAMPLE
    .\scripts\run_duplo.ps1 -DuploBin "C:\Tools\duplo.exe"
    Run Duplo with custom executable path

.NOTES
    Requirements:
    - Duplo executable must be in PATH or specified via -DuploBin
    - Download from: https://github.com/dlidstrom/Duplo/releases
#>

param(
    [int]$MinLines = 15,
    [string]$DuploBin = "duplo.exe"
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent $ScriptDir

# Check if Duplo is available
if (-not (Get-Command $DuploBin -ErrorAction SilentlyContinue)) {
    Write-Host "ERROR: Duplo not found. Install from: https://github.com/dlidstrom/Duplo/releases" -ForegroundColor Red
    Write-Host ""
    Write-Host "Installation options:"
    Write-Host "  1. Download Windows binary and add to PATH"
    Write-Host "  2. Place binary in project root and use: -DuploBin `$PWD\duplo.exe"
    Write-Host ""
    exit 1
}

$OutputFile = Join-Path $ProjectRoot "duplo_report.txt"
$JsonOutput = Join-Path $ProjectRoot "duplo_report.json"

Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "Running Duplo duplicate code detection" -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "Project root: $ProjectRoot"
Write-Host "Duplo binary: $DuploBin"
Write-Host "Minimum duplicate lines: $MinLines"
Write-Host "Output: $OutputFile"
Write-Host "JSON output: $JsonOutput"
Write-Host ""

# Find all C++ source files (exclude external dependencies and build artifacts)
Write-Host "Scanning for C++ source files..." -ForegroundColor Yellow

$SourceFiles = Get-ChildItem -Path $ProjectRoot -Recurse -Include *.cpp,*.h,*.hpp `
    | Where-Object { 
        $_.FullName -notmatch "\\external\\" -and
        $_.FullName -notmatch "\\build" -and
        $_.FullName -notmatch "\\build_" -and
        $_.FullName -notmatch "\\coverage\\" -and
        $_.FullName -notmatch "\\.git\\"
    } | ForEach-Object { $_.FullName }

if ($SourceFiles.Count -eq 0) {
    Write-Host "ERROR: No C++ source files found!" -ForegroundColor Red
    exit 1
}

Write-Host "Found $($SourceFiles.Count) C++ source files" -ForegroundColor Green
Write-Host ""

# Write file list to temporary file
$TempFile = [System.IO.Path]::GetTempFileName()
$SourceFiles | Out-File -FilePath $TempFile -Encoding ASCII

try {
    # Run Duplo with text output
    Write-Host "Running Duplo analysis (text output)..." -ForegroundColor Yellow
    & $DuploBin -ml $MinLines -ip $TempFile $OutputFile
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: Duplo analysis failed!" -ForegroundColor Red
        exit 1
    }
    
    # Run Duplo with JSON output
    Write-Host "Running Duplo analysis (JSON output)..." -ForegroundColor Yellow
    & $DuploBin -ml $MinLines -ip -json $JsonOutput $TempFile
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "WARNING: Duplo JSON output generation failed (text report still available)" -ForegroundColor Yellow
    }
    
    Write-Host ""
    Write-Host "==========================================" -ForegroundColor Cyan
    Write-Host "Duplo analysis complete!" -ForegroundColor Cyan
    Write-Host "==========================================" -ForegroundColor Cyan
    Write-Host "Text report: $OutputFile"
    Write-Host "JSON report: $JsonOutput"
    Write-Host ""
    
    # Check if duplicates were found
    if (Test-Path $OutputFile -PathType Leaf) {
        $FileContent = Get-Content $OutputFile -Raw
        if ($FileContent -and $FileContent.Trim().Length -gt 0) {
            # Check if file contains duplicate entries (lines starting with file paths)
            $DuplicateLines = Get-Content $OutputFile | Where-Object { $_ -match "^[A-Za-z]:\\|^\.\\" }
            if ($DuplicateLines.Count -gt 0) {
                Write-Host "⚠️  Duplicate code blocks detected!" -ForegroundColor Yellow
                Write-Host "   Review the report and consider refactoring duplicated code."
                Write-Host ""
                Write-Host "   To view the report:"
                Write-Host "     Get-Content $OutputFile"
                Write-Host ""
            } else {
                Write-Host "✅ No duplicate code blocks found" -ForegroundColor Green
            }
        } else {
            Write-Host "✅ No duplicate code blocks found" -ForegroundColor Green
        }
    } else {
        Write-Host "✅ No duplicate code blocks found" -ForegroundColor Green
    }
    
    Write-Host ""
    Write-Host "Next steps:" -ForegroundColor Cyan
    Write-Host "  1. Review the duplicate code blocks in the report"
    Write-Host "  2. Identify refactoring opportunities"
    Write-Host "  3. Extract common code into shared functions/utilities"
    Write-Host "  4. Re-run this script to verify improvements"
    Write-Host ""
    
} finally {
    # Clean up temporary file
    if (Test-Path $TempFile) {
        Remove-Item $TempFile -Force
    }
}

