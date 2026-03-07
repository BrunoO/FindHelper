# Profile-Guided Optimization (PGO) Setup Guide

## Overview

Profile-Guided Optimization (PGO) allows the compiler to optimize your code based on actual runtime behavior. This typically provides 10-20% performance improvement.

## How PGO Works

1. **Instrumented Build**: Compile with profiling instrumentation
2. **Training Run**: Execute the application with typical workloads
3. **Profile Collection**: Collect data about function calls, branches, etc.
4. **Optimized Build**: Recompile using profile data for better optimization

## MSVC PGO Setup

### Step 1: Enable PGO in CMakeLists.txt

The CMakeLists.txt has been updated with PGO support. To use it:

```powershell
# Configure with PGO enabled
cmake -B build -DENABLE_PGO=ON

# Build instrumented version (first pass)
cmake --build build --config Release
```

### Step 2: Training Run

Run your application with typical search scenarios. **Important:** MSVC writes `.pgc` files to the **current working directory** of the process, not next to the executable. Run the app from `build\Release` (or use `scripts\pgo_build.ps1 -Phase RunTrain`) so `.pgc` files are created in `build\Release` where the merge step expects them.

```powershell
# Run from build\Release so .pgc files are written there
cd build\Release
.\FindHelper.exe
# Perform typical searches, then close the app to flush .pgc files
cd ..\..

# Or use the script (it runs the exe from build\Release):
# .\scripts\pgo_build.ps1 -Phase RunTrain
```

**Note**: The `.pgc` files are named `FindHelper!1.pgc`, `FindHelper!2.pgc`, etc. (one per run). They are created in the **current working directory** when the instrumented app exits. If you run the exe from the project root, `.pgc` files will be in the project root and the merge step (which looks in `build\Release`) will not find them.

### Step 3: Merge Profile Data

```powershell
# Navigate to the Release directory
cd build\Release

# First, verify .pgc files exist
dir FindHelper*.pgc

# Merge all .pgc files into .pgd with verbose output
# /summary = Show per-function statistics
# /detail = Show detailed flow graph coverage information
# This helps verify profile data quality and diagnose issues
pgomgr /merge /summary /detail FindHelper*.pgc FindHelper.pgd

# Alternative: If you have a single .pgc file
pgomgr /merge /summary /detail FindHelper.pgc FindHelper.pgd

# Verify the .pgd file was created and is not empty
dir FindHelper.pgd

# Check file size (should be > 0 bytes)
# If file is 0 bytes, the merge failed - check pgomgr output for errors

# Return to project root
cd ..\..
```

**Note**: 
- If `pgomgr` is not found, it's part of the MSVC toolchain. Make sure you're running from a Visual Studio Developer Command Prompt, or add the MSVC bin directory to your PATH.
- The `/summary` and `/detail` flags provide verbose output to verify profile data quality
- If you see warnings during merge, the profile data may be incomplete or corrupted

### Step 4: Optimized Build

```powershell
# Rebuild with profile data
cmake --build build --config Release
# The build system will automatically use the .pgd file
```

## CMake Integration

The CMakeLists.txt now includes PGO support with automatic phase detection:

```cmake
option(ENABLE_PGO "Enable Profile-Guided Optimization" OFF)

if(ENABLE_PGO)
    # Automatically detects if .pgd file exists
    # Phase 1: Instrumented build (if no .pgd file)
    # Phase 2: Optimized build (if .pgd file exists)
endif()
```

**How it works**:
- **First build**: Creates instrumented executable (generates `.pgc` files when run)
- **After training**: Merge `.pgc` files into `.pgd` file
- **Second build**: Automatically detects `.pgd` and uses it for optimization

## Benefits

- **10-20% overall performance improvement**
- **Better branch prediction** (optimized for actual branches)
- **Optimal code layout** (hot code grouped together)
- **Smarter inlining** (based on actual call frequency)

## Workflow

1. **Development**: Build normally (PGO off)
2. **Release Preparation**: Enable PGO, build instrumented version
3. **Training**: Run with typical workloads
4. **Final Build**: Rebuild with profile data
5. **Deploy**: Use optimized version

## Troubleshooting

### PG0188 Warning: Profile data not found

**Symptom**: Linker warning `PG0188: no .pgc files found`

**Causes**:
- `.pgd` file doesn't exist or is empty
- `.pgd` file path not specified correctly
- Profile data wasn't merged properly

**Solution**:
1. Verify `.pgd` file exists: `dir build\Release\FindHelper.pgd`
2. Check file size (should be > 0 bytes)
3. Re-merge profile data with verbose output:
   ```powershell
   cd build\Release
   pgomgr /merge /summary /detail FindHelper*.pgc FindHelper.pgd
   ```
4. The CMakeLists.txt now includes `/PGD:FindHelper.pgd` linker flag to explicitly specify the profile path

### C4961 Warning: Profile data issues

**Symptom**: Compiler warning `C4961: no profile data was merged into ...`

**Causes**:
- Profile data mismatch between instrumented and optimized builds
- `.pgd` file is corrupted or incomplete
- Source code changed between Phase 1 and Phase 2 builds

**Solution**:
1. Clean and rebuild Phase 1 (instrumented):
   ```powershell
   cmake --build build --config Release --clean-first
   ```
2. Re-run training to generate fresh `.pgc` files
3. Re-merge profile data:
   ```powershell
   cd build\Release
   pgomgr /merge /summary /detail FindHelper*.pgc FindHelper.pgd
   ```
4. Reconfigure and rebuild Phase 2:
   ```powershell
   cmake -S . -B build -DENABLE_PGO=ON
   cmake --build build --config Release
   ```

### Verifying PGO is Working

**Check linker output**:
- Look for messages about profile data being loaded
- With `/VERBOSE` linker flag, you'll see detailed PGO information
- No PG0188 or C4961 warnings indicates successful PGO

**Check pgomgr output**:
- Use `/summary` to see per-function statistics
- Use `/detail` to see flow graph coverage
- High coverage numbers indicate good profile data

**Performance verification**:
- Compare performance before/after PGO
- Expect 10-20% improvement in typical scenarios
- Hot code paths should show better optimization

## Notes

- PGO requires two build passes (instrumented + optimized)
- Training should use realistic workloads
- Profile data is CPU-specific (rebuild for different CPUs if needed)
- Always use verbose pgomgr options (`/summary /detail`) to verify profile data quality
- The linker now uses `/VERBOSE` flag for detailed PGO diagnostics
