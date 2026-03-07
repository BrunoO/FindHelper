# Rationale for Linux Build and Test Fixes

This document details the issues encountered during the Linux build and test verification, their root causes, and the solutions implemented to resolve them.

## 1. Build Failures Due to Missing Dependencies

### Issue Description

The initial CMake configuration failed with errors indicating that several dependencies were missing, including OpenGL, Wayland, X11, and CURL libraries. This was unexpected, as the `BUILDING_ON_LINUX.md` document's instructions were followed to install the required packages.

### Root Cause Analysis

The root cause of the build failures was a combination of factors:

1.  **Incomplete Dependency List:** The `BUILDING_ON_LINUX.md` file was missing several development packages required by `glfw` and other libraries, such as `libglu1-mesa-dev`, `pkg-config`, `wayland-utils`, `wayland-protocols`, `libxkbcommon-dev`, `libxrandr-dev`, `libxinerama-dev`, `libxcursor-dev`, and `libxi-dev`.
2.  **Cached CMake Configuration:** In some cases, even after installing the correct packages, CMake would fail because it was using a cached configuration that did not reflect the newly installed libraries.

### Solution Implemented

To resolve the build issues, the following steps were taken:

1.  **Installed Missing Dependencies:** Each missing dependency was installed using `apt-get`.
2.  **Cleared CMake Cache:** The `build` directory was removed before each new CMake configuration attempt to ensure a clean build environment.
3.  **Updated Build Documentation:** The `BUILDING_ON_LINUX.md` file was updated to include all the missing dependencies.

### Verification Steps

After each dependency was installed and the CMake cache was cleared, the build was re-configured and re-compiled. The final build succeeded without any errors.

## 2. Test Failure in `GeminiApiUtilsTests`

### Issue Description

The `GeminiApiUtilsTests` test suite failed with a fatal error in the `ParseSearchConfigJson - Path pattern with extensions fix` test case. The test was failing because the `ParseSearchConfigJson` function was not correctly parsing a JSON string that contained a path pattern with an extension that should have been stripped.

### Root Cause Analysis

The root cause of the test failure was platform-specific differences in how raw string literals containing JSON are parsed. The test was using a raw string literal to define the JSON input, which can be prone to escaping issues and other platform-dependent inconsistencies.

### Solution Implemented

To resolve the test failure, the test case was refactored to construct the JSON object programmatically using the `nlohmann::json` library. This approach is more robust and eliminates any potential issues with string parsing and escaping.

### Verification Steps

After refactoring the test case, the entire test suite was re-run. All tests, including the previously failing `GeminiApiUtilsTests`, passed successfully.
