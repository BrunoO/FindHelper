# Rationale for Linux Build Fixes (2026-01-12 v1)

## 1. Issue: Linker Error for XScreenSaver Extension

### Description of Issue
The Linux build failed during the linking phase with "undefined reference" errors for `XScreenSaverQueryExtension`, `XScreenSaverAllocInfo`, and `XScreenSaverQueryInfo`.

### Root Cause Analysis
These functions are part of the XScreenSaver extension library (`libXss`). The `CMakeLists.txt` file was attempting to find this library to support system idle detection on Linux, but it was using the incorrect library name (`XScrnSaver` instead of `Xss`). The `find_library` command was also configured with `QUIET`, so it failed silently without stopping the configuration process, leading to a linker error later in the build.

### Solution Implemented
1.  **Corrected Library Name:** The library name in `CMakeLists.txt` was changed from `XScrnSaver` to `Xss`.
2.  **Made Dependency Required:** The `find_library` command was changed from `QUIET` to `REQUIRED`. This ensures that if `libXss` (or its development package `libxss-dev`) is missing, the CMake configuration will fail immediately with a clear error message, rather than allowing the build to proceed to a linker failure.

### Verification Steps
1.  Applied the changes to `CMakeLists.txt`.
2.  Ensured the `libxss-dev` package was installed.
3.  Performed a clean build by removing the `build/` directory.
4.  Re-ran the CMake configuration and build commands. The project compiled and linked successfully.
5.  Ran the test suite (`ctest`) to confirm all tests still pass.

The build is now successful and all tests pass, verifying the fix.