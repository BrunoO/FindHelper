# Rationale for Linux Fixes - 2026-03-06-v1

This document describes the issues found and fixed during Linux build and test verification.

## 1. Missing Build Dependencies

### Issue
The initial CMake configuration failed because several required development libraries were missing from the environment.

### Root Cause Analysis
The Linux environment (Ubuntu 24.04) did not have the full set of development headers and tools required for the application's UI (OpenGL, GLFW, Wayland) and network features (libcurl).

### Solution
Installed the missing dependencies as specified in `BUILDING_ON_LINUX.md`:
- `libgl1-mesa-dev`, `libglu1-mesa-dev` (OpenGL)
- `libcurl4-openssl-dev` (libcurl)
- `libxss-dev` (X11 Screen Saver)
- `wayland-protocols`, `libwayland-dev`, `libwayland-bin` (Wayland)
- `libxkbcommon-dev`, `libxrandr-dev`, `libxinerama-dev`, `libxcursor-dev`, `libxi-dev` (X11/Wayland dependencies)

### Verification Steps
Re-ran CMake configuration and verified that all dependencies were found (with GLFW falling back to FetchContent as expected).

---

## 2. IncrementalSearchStateTests Failure

### Issue
The `IncrementalSearchStateTests` test suite failed with multiple errors, including `CHECK_EQ( 0, 1 )` and `CHECK( false )` for matching queries.

### Root Cause Analysis
The issue was in the test helper `MakeResult`. It created a `SearchResult` where the `fullPath` (a `std::string_view`) pointed to a temporary `std::string` object passed as an argument. By the time the `SearchResult` was used in the test, the temporary string had been destroyed, leaving the `string_view` dangling.

This resulted in undefined behavior; on Linux with GCC, it consistently failed to match because the memory was likely reused or cleared.

### Solution
Implemented a `ResultStore` struct in the test file to manage the lifetime of the path strings. `ResultStore::MakeResult` now stores the `std::string` in a `std::vector` that outlives the search operations, and the `SearchResult::fullPath` view points to these stable strings.

### Verification Steps
Built and ran the `incremental_search_state_tests` executable directly. All 15 test cases passed successfully.

---

## Summary of Results

- **Build Status**: ✅ Success (after dependency installation)
- **Test Status**: ✅ 27/27 tests passed
- **Platform**: Ubuntu 24.04 LTS (Noble Numbat)
