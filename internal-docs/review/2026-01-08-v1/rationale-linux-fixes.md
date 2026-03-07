# Rationale for Linux Build Fixes - 2026-01-08

This document outlines the issues found and fixes applied during the Linux build and test verification process.

## 1. Build Failure: Deleted Copy Constructor in `AppBootstrap_linux.cpp`

### Issue Description
The initial build attempt on Linux failed with a compilation error related to a deleted copy constructor for the `AppBootstrapResultLinux` struct. The error occurred in the `HandleInitializationException` function, which was attempting to return the `AppBootstrapResultLinux` object by value.

### Root Cause Analysis
The `AppBootstrapResultLinux` struct inherits from `AppBootstrapResultBase`, which has a deleted copy constructor. This deletion is propagated to the derived class, preventing it from being copied. The `HandleInitializationException` function was incorrectly designed to return this non-copyable struct by value, which is not allowed by the compiler.

### Solution Implemented
The fix involved two main changes:
1.  **Modified `HandleInitializationException`:** The return type of the function was changed from `AppBootstrapResultLinux` to `void`. The function now modifies the `result` object passed by reference and does not return anything.
2.  **Updated Call Sites:** All call sites to `HandleInitializationException` were updated to reflect the new `void` return type. Instead of `return HandleInitializationException(...)`, the code was changed to `HandleInitializationException(...); return result;`.

This change ensures that the non-copyable `AppBootstrapResultLinux` object is never returned by value, resolving the compilation error.

### Verification Steps
After applying the fix, the project was recompiled, and the build succeeded.

## 2. Linker Errors in Test Executables

### Issue Description
After fixing the initial build error, a subsequent build failed with linker errors in two test executables: `lazy_attribute_loader_tests` and `parallel_search_engine_tests`.

### Root Cause Analysis
1.  **`lazy_attribute_loader_tests`:** This test was missing a link to the `ThreadUtils_linux.cpp` source file, which provides the implementation for the `SetThreadName` function on Linux.
2.  **`parallel_search_engine_tests`:** This test was missing a link to the `TestHelpers.cpp` source file, which provides common helper functions used by many of the tests.

### Solution Implemented
The `CMakeLists.txt` file was modified to include the missing source files in the respective `add_executable` commands for the failing test targets.

### Verification Steps
After updating the `CMakeLists.txt` file, the project was rebuilt, and all test executables linked successfully. A full run of the test suite confirmed that all tests passed.
