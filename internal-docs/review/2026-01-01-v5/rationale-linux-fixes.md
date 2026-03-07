# Rationale for Linux Build Fixes - 2026-01-01

This document outlines the issues found and fixes applied during the Linux build and test verification process.

## 1. Build Failure due to Unrecognized Compiler Flag

### Description of Issue
The initial compilation attempt on Linux failed with the following error:
```
cc1plus: error: unrecognized argument to ‘-flto=’ option: ‘full’
```

### Root Cause Analysis
The `CMakeLists.txt` file contained the compiler option `-flto=full` for Release builds on Linux. While this is a valid flag for recent versions of Clang (commonly used on macOS) to enable full Link-Time Optimization, it is not supported by the version of GCC being used in the Linux build environment. The equivalent flag for GCC is simply `-flto`.

### Solution Implemented
The `CMakeLists.txt` file was modified to use the correct flag for GCC.

**File:** `CMakeLists.txt`
**Change:**
```diff
--- a/CMakeLists.txt
+++ b/CMakeLists.txt
@@ -493,7 +493,7 @@
         $<$<CONFIG:Debug>:-g -O0 -Winline>
         $<$<CONFIG:Release>:
             -O3                    # Maximum optimization (more aggressive than -O2)
-            -flto=full             # Link-Time Optimization (cross-module optimization)
+            -flto                  # Link-Time Optimization (cross-module optimization)
             -fdata-sections        # Enable dead code elimination
             -ffunction-sections    # Enable dead code elimination
             -funroll-loops         # Loop unrolling for hot loops

```

### Verification Steps
After applying the fix, the following steps were taken to verify the solution:
1.  The project was re-configured using `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`.
2.  The project was compiled using `cmake --build build --config Release`.
3.  The build completed successfully without any errors.
4.  All tests were executed and passed using `cd build && ctest --output-on-failure`.
