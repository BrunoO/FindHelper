# Rationale for Linux Build Fixes (2026-01-18-v2)

This document outlines the issues found during the Linux build and test verification process and the solutions implemented to resolve them.

## 1. Issue: AVX2 Linker Errors in `find_helper` Executable

*   **Description:** The Linux build failed during the linking phase of the `find_helper` executable with "target specific option mismatch" errors.
*   **Root Cause Analysis:** The errors occurred because some source files (e.g., `StringSearchAVX2.cpp`, `LoadBalancingStrategy.cpp`) are compiled with the `-mavx2` flag to enable AVX2 intrinsics. However, the main `find_helper` executable target did not have this flag enabled for x86_64 Linux builds. When the linker attempted to combine these object files and perform link-time optimizations (LTO), it detected an inconsistency in the compiler options, leading to a build failure.
*   **Solution Implemented:** The `-mavx2` flag was added to the `target_compile_options` for the `find_helper` target within the `CMakeLists.txt` file, specifically for x86_64 Linux builds in both Debug and Release configurations. This ensures that the entire executable is compiled and linked with consistent AVX2 support.
*   **Verification:** After applying the fix, the `find_helper` executable compiled and linked successfully.

## 2. Issue: AVX2 Linker Errors in `settings_tests` Executable

*   **Description:** Similar to the `find_helper` issue, the `settings_tests` test executable failed to link with the same "target specific option mismatch" errors.
*   **Root Cause Analysis:** The root cause was identical. The `settings_tests` target links against object libraries (like `test_core_obj`) that contain code compiled with `-mavx2`. The test executable target itself was missing this flag, causing a linker mismatch.
*   **Solution Implemented:** The `CMakeLists.txt` file was modified to include an architecture check for the `settings_tests` target on Linux. If the architecture is x86_64, the `-mavx2` flag is now added to the `target_compile_options` for the test executable.
*   **Verification:** After the change, the `settings_tests` executable compiled and linked without errors, and all tests passed.
