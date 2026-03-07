# D9002 Warning Quick Fix

## Problem
```
cl : Command line warning D9002: ignoring unknown option '/GENPROFILE'
```

## Cause
You didn't specify Release configuration in BOTH the configure AND build steps.

## Solution

### ❌ WRONG (causes D9002):
```powershell
cmake -S . -B build -DENABLE_PGO=ON
cmake --build build --config Release
```

### ✅ CORRECT (no D9002):
```powershell
cmake -S . -B build -DENABLE_PGO=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

**Key difference:** Add `-DCMAKE_BUILD_TYPE=Release` in the configure step.

## Why?

- CMake's Visual Studio generator supports multiple configurations (Debug, Release, etc.)
- PGO flags only apply to Release builds via generator expressions
- If you don't specify `-DCMAKE_BUILD_TYPE=Release`, CMake might also prepare Debug configuration
- `/GENPROFILE` incorrectly passed to compiler instead of linker
- Debug tries to apply `/GENPROFILE` → D9002 warning
- Fix: Explicitly tell CMake "this is a Release build"

## Verification

After fixing, you should see in CMake output:
```
Current BUILD CONFIGURATION:
  CMAKE_BUILD_TYPE: Release
```

And NO D9002 warnings in build output.

## One-Line Reference

**Always use this exact command for PGO:**
```powershell
cmake -S . -B build -DENABLE_PGO=ON -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release
```

Or use the helper script (if you added it):
```powershell
.\scripts\pgo_build.bat
```

