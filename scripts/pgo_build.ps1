#Requires -Version 5.1
<#
.SYNOPSIS
    Runs PGO (Profile-Guided Optimization) phases for the FindHelper Windows build.

.DESCRIPTION
    Use this script from a Visual Studio Developer PowerShell (or with MSVC in PATH).
    PGO requires: Phase 1 (instrumented build) -> training run -> merge .pgc -> Phase 2 (optimized build).

.PARAMETER Phase
    Which step to run:
    - Phase1    : Configure and build instrumented executable (generates .pgc when run).
    - RunTrain  : Launch the instrumented app for training (you use the app, then close it).
    - Merge     : Merge .pgc files into FindHelper.pgd (run after training).
    - Phase2    : Reconfigure (so CMake sees .pgd) and build optimized executable.
    - Full      : Phase1, then prompt to run training, then Merge, then Phase2 (interactive).

.PARAMETER BuildDir
    CMake build directory (default: build).

.PARAMETER CleanFirst
    If set, run --clean-first on build (useful after source changes between phases).

.EXAMPLE
    .\scripts\pgo_build.ps1 -Phase Phase1
    .\scripts\pgo_build.ps1 -Phase RunTrain
    .\scripts\pgo_build.ps1 -Phase Merge
    .\scripts\pgo_build.ps1 -Phase Phase2
.EXAMPLE
    .\scripts\pgo_build.ps1 -Phase Full
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Phase1', 'RunTrain', 'Merge', 'Phase2', 'Full')]
    [string] $Phase,

    [Parameter(Mandatory = $false)]
    [string] $BuildDir = "build",

    [Parameter(Mandatory = $false)]
    [switch] $CleanFirst
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path (Join-Path $ProjectRoot "CMakeLists.txt"))) {
    $ProjectRoot = (Get-Location).Path
}
if (-not (Test-Path (Join-Path $ProjectRoot "CMakeLists.txt"))) {
    Write-Error "Project root not found (no CMakeLists.txt). Run from repo root or scripts folder."
}
Set-Location $ProjectRoot

$ReleaseDir = Join-Path $BuildDir "Release"
$ExePath    = Join-Path $ReleaseDir "FindHelper.exe"
$PgdPath    = Join-Path $ReleaseDir "FindHelper.pgd"

function Write-Step { param([string]$Message) Write-Host "`n===> $Message" -ForegroundColor Cyan }
function Write-Ok    { param([string]$Message) Write-Host "    $Message" -ForegroundColor Green }
function Write-Warn  { param([string]$Message) Write-Host "    $Message" -ForegroundColor Yellow }

function Do-Phase1 {
    Write-Step "PGO Phase 1: Configure and build instrumented executable"
    # If .pgd already exists, CMake will do Phase 2 (optimized) and the build won't generate .pgc when run
    if (Test-Path $PgdPath) {
        Write-Warn "FindHelper.pgd already exists in $ReleaseDir. CMake will configure for Phase 2 (optimized), not Phase 1 (instrumented)."
        Write-Warn "To collect new profile data: delete or move $PgdPath, then re-run Phase1."
        $confirm = Read-Host "Continue anyway? [y/N]"
        if ($confirm -ne "y" -and $confirm -ne "Y") { return }
    }
    $cmakeArgs = @("-S", ".", "-B", $BuildDir, "-DENABLE_PGO=ON", "-DCMAKE_BUILD_TYPE=Release")
    & cmake @cmakeArgs
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }
    $buildArgs = @("--build", $BuildDir, "--config", "Release")
    if ($CleanFirst) { $buildArgs += "--clean-first" }
    & cmake @buildArgs
    if ($LASTEXITCODE -ne 0) { throw "Build failed." }
    Write-Ok "Phase 1 done. Run the app to collect profile data, then run -Phase Merge and -Phase Phase2."
}

function Do-RunTrain {
    Write-Step "Launching instrumented executable for training"
    if (-not (Test-Path $ExePath)) {
        Write-Error "Executable not found: $ExePath. Run Phase1 first."
    }
    Write-Warn "Use the app (searches, filters, etc.), then close it to flush .pgc files."
    # .pgc files are written to the process current working directory (MSVC behavior). Run from ReleaseDir so they go to build\Release.
    Push-Location $ReleaseDir
    try {
        & .\FindHelper.exe
    } finally {
        Pop-Location
    }
    Write-Ok "Training run finished. .pgc files (if any) are in $ReleaseDir"
}

function Do-Merge {
    Write-Step "Merging .pgc files into FindHelper.pgd"
    $pgcFiles = Get-ChildItem -Path $ReleaseDir -Filter "FindHelper*.pgc" -ErrorAction SilentlyContinue
    if (-not $pgcFiles -or $pgcFiles.Count -eq 0) {
        $inRoot = Get-ChildItem -Path $ProjectRoot -Filter "FindHelper*.pgc" -ErrorAction SilentlyContinue
        if ($inRoot -and $inRoot.Count -gt 0) {
            Write-Error "No .pgc files in $ReleaseDir, but found $($inRoot.Count) in project root. PGO writes .pgc to the app's current directory. Re-run -Phase RunTrain (this script runs the exe from $ReleaseDir so .pgc go there)."
        } else {
            Write-Error "No FindHelper*.pgc files in $ReleaseDir. Ensure Phase1 built the instrumented exe (no existing .pgd), then run -Phase RunTrain and close the app to flush .pgc."
        }
    }
    $pgcList = ($pgcFiles | ForEach-Object { $_.Name }) -join " "
    Push-Location $ReleaseDir
    try {
        & pgomgr /merge /summary /detail FindHelper*.pgc FindHelper.pgd
        if ($LASTEXITCODE -ne 0) { throw "pgomgr merge failed." }
        if (-not (Test-Path "FindHelper.pgd")) { throw "FindHelper.pgd was not created." }
        $size = (Get-Item "FindHelper.pgd").Length
        if ($size -eq 0) { throw "FindHelper.pgd is empty. Merge may have failed." }
        Write-Ok "FindHelper.pgd created ($size bytes). Run -Phase Phase2 to build optimized executable."
    } finally {
        Pop-Location
    }
}

function Do-Phase2 {
    Write-Step "PGO Phase 2: Reconfigure and build optimized executable"
    if (-not (Test-Path $PgdPath)) {
        Write-Error "Profile database not found: $PgdPath. Run Merge first."
    }
    $size = (Get-Item $PgdPath).Length
    if ($size -eq 0) { Write-Error "FindHelper.pgd is empty. Re-run training and Merge." }
    Write-Ok "Using profile: $PgdPath ($size bytes)"
    $cmakeArgs = @("-S", ".", "-B", $BuildDir, "-DENABLE_PGO=ON", "-DCMAKE_BUILD_TYPE=Release")
    & cmake @cmakeArgs
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }
    $buildArgs = @("--build", $BuildDir, "--config", "Release")
    if ($CleanFirst) { $buildArgs += "--clean-first" }
    & cmake @buildArgs
    if ($LASTEXITCODE -ne 0) { throw "Build failed." }
    Write-Ok "Phase 2 done. FindHelper.exe in $ReleaseDir is now PGO-optimized."
}

function Do-Full {
    Do-Phase1
    Write-Host ""
    $run = Read-Host "Run training now? (launch app, use it, then close) [Y/n]"
    if ($run -ne "n" -and $run -ne "N") {
        Do-RunTrain
    } else {
        Write-Warn "Skipped training. Run: .\scripts\pgo_build.ps1 -Phase RunTrain"
    }
    Write-Host ""
    $merge = Read-Host "Merge .pgc into .pgd and build Phase 2? [Y/n]"
    if ($merge -ne "n" -and $merge -ne "N") {
        Do-Merge
        Do-Phase2
    } else {
        Write-Warn "Skipped. Run: .\scripts\pgo_build.ps1 -Phase Merge; then -Phase Phase2"
    }
}

# Ensure pgomgr is available when needed
if ($Phase -in 'Merge', 'Full') {
    $pgomgrCmd = Get-Command pgomgr -ErrorAction SilentlyContinue
    if (-not $pgomgrCmd) {
        Write-Warn "pgomgr not in PATH. Use 'Developer PowerShell for VS' or run from VS Developer Command Prompt."
    }
}

switch ($Phase) {
    Phase1   { Do-Phase1 }
    RunTrain { Do-RunTrain }
    Merge    { Do-Merge }
    Phase2   { Do-Phase2 }
    Full     { Do-Full }
}
