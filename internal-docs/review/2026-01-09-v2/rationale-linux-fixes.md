# Rationale for Linux Build Fixes

This document outlines the issues found during the Linux build verification and the solutions implemented to resolve them.

## Issue 1: Redeclaration of `file_path` in `FileOperations_linux.cpp`

*   **Description:** The build failed with a "redeclaration of 'std::filesystem::__cxx11::path file_path'" error in `src/platform/linux/FileOperations_linux.cpp`.
*   **Root Cause Analysis:** The variable `file_path` was declared twice within the `DeleteFileToRecycleBin` function. This is a simple copy-paste error.
*   **Solution Implemented:** Removed the redundant declaration of `file_path`.
*   **Verification Steps:** The project was rebuilt, and the compilation error was resolved.

## Issue 2: Incorrect Compilation of macOS-specific File on Linux

*   **Description:** The build failed with an error in `src/platform/macos/ThreadUtils_mac.cpp` related to the `pthread_setname_np` function.
*   **Root Cause Analysis:** The `time_filter_utils_tests` target in `CMakeLists.txt` was incorrectly including macOS-specific source files (`ThreadUtils_mac.cpp` and `StringUtils_mac.cpp`) in all builds, regardless of the platform. The `pthread_setname_np` function has different signatures on macOS and Linux, which caused a compilation error when building on Linux.
*   **Solution Implemented:** Modified `CMakeLists.txt` to conditionally include the correct platform-specific source files for the `time_filter_utils_tests` target. This was done by creating a list of common sources and another list for platform-specific sources, and then adding them to the executable. This also improves the readability of the `CMakeLists.txt` file and follows the DRY principle.
*   **Verification Steps:** The project was rebuilt, and the compilation error was resolved. The build now completes successfully on Linux.
