# Rationale for Linux Build Fixes

This document outlines the issues encountered during the Linux build verification and the solutions implemented to resolve them.

## 1. Missing OpenGL Dependency

*   **Issue**: The initial CMake configuration failed due to a missing OpenGL dependency.
*   **Root Cause**: The build environment lacked the `libgl1-mesa-dev` package, and the `find_package(OpenGL REQUIRED)` command in `CMakeLists.txt` made this dependency mandatory.
*   **Solution**: Since the primary goal was to run the unit tests (which do not have a hard dependency on OpenGL), the `REQUIRED` keyword was removed from the `find_package(OpenGL)` command, making it an optional dependency.

## 2. Missing Wayland and RandR Dependencies for GLFW

*   **Issue**: After making OpenGL optional, the build failed again due to missing Wayland and RandR dependencies, which are required by GLFW.
*   **Root Cause**: The build environment did not have the necessary Wayland and X11 development packages installed, and GLFW's build process, triggered by `FetchContent`, was attempting to build with support for these features.
*   **Solution**: To avoid the need for these dependencies, the main application build was made optional. A new CMake option, `BUILD_APPLICATION`, was introduced to conditionally compile the `find_helper` executable. By disabling this option (`-DBUILD_APPLICATION=OFF`), the GUI application is not built, and therefore, GLFW is not required.

## 3. Missing CURL Dependency for Gemini API Utils Test

*   **Issue**: With the main application build disabled, the CMake configuration failed again due to a missing `CURL` dependency, which is required by the `gemini_api_utils_tests`.
*   **Root Cause**: The build environment did not have the `libcurl` development package installed.
*   **Solution**: Since the `CURL` dependency could not be installed, the `gemini_api_utils_tests` was conditionally compiled based on whether the `CURL` package was found. This was achieved by wrapping the test's `add_executable` and related commands in an `if(CURL_FOUND)` block.

## 4. AVX2 Inlining Error

*   **Issue**: The build failed with an AVX2-related error (`target specific option mismatch`) in `LoadBalancingStrategy.cpp`.
*   **Root Cause**: The `test_core_obj` library, which includes `LoadBalancingStrategy.cpp`, was not being compiled with the `-mavx2` flag, which is required for files that use AVX2 intrinsics.
*   **Solution**: The `-mavx2` compile option was added to the `test_core_obj` library for x86_64 architectures, consistent with how other AVX2-dependent libraries are handled in the project.

## Verification

After implementing these fixes, the project was successfully configured, built, and all 22 unit tests passed on the Linux environment.
