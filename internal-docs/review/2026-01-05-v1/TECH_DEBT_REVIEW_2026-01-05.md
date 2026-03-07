# Tech Debt Review - 2026-01-05

## Executive Summary
- **Health Score**: 6/10
- **Critical Issues**: 1
- **High Issues**: 3
- **Total Findings**: 8
- **Estimated Remediation Effort**: 4-6 hours

## Findings

### Critical
1.  **"God Class" in `FileIndex`**
    -   **Code:** `src/index/FileIndex.h`, `src/index/FileIndex.cpp`
    -   **Debt Type:** Maintainability Issues
    -   **Risk Explanation:** The `FileIndex` class has grown to over 800 lines of code and has accumulated numerous responsibilities, including data storage, indexing, searching, and thread management. This violates the Single Responsibility Principle, making the class difficult to understand, maintain, and test. Any changes to one area of its functionality risk introducing bugs in another.
    -   **Suggested Fix:** Refactor `FileIndex` by breaking it down into smaller, more focused classes. For example, create a `SearchOrchestrator` to handle the search logic, a `DataStore` for managing the file entries, and a `ThreadPoolManager` for the concurrency aspects.
    -   **Severity:** Critical
    -   **Effort:** 4-5 hours (major refactoring)

### High
1.  **Inconsistent Naming Conventions in `main_win.cpp`**
    -   **Code:** `src/platform/windows/main_win.cpp`
    -   **Debt Type:** Naming Convention Violations
    -   **Risk Explanation:** The file mixes `PascalCase` for functions with `snake_case` for some local variables, and uses Hungarian notation remnants like `hInstance`. This makes the code harder to read and understand, and it violates the project's stated coding conventions.
    -   **Suggested Fix:** Refactor all function and variable names in `main_win.cpp` to consistently follow the project's style guide (e.g., `PascalCase` for functions, `snake_case` for variables).
    -   **Severity:** High
    -   **Effort:** 1 hour

2.  **Missing `[[nodiscard]]` on Functions Returning Futures**
    -   **Code:** `src/index/LazyAttributeLoader.cpp`
    -   **Debt Type:** C++17 Modernization Opportunities
    -   **Risk Explanation:** The `LazyAttributeLoader` returns `std::future` objects that must be handled to avoid blocking or resource leaks. Forgetting to call `.get()` or `.wait()` on these futures can lead to subtle bugs. The `[[nodiscard]]` attribute would cause the compiler to issue a warning if the return value is ignored.
    -   **Suggested Fix:** Add the `[[nodiscard]]` attribute to all functions in `LazyAttributeLoader` that return a `std::future`.
    -   **Severity:** High
    -   **Effort:** < 15 minutes

3.  **Potential Raw `HANDLE` Leak**
    -   **Code:** `src/platform/windows/FileOperations_win.cpp`
    -   **Debt Type:** Platform-Specific Debt
    -   **Risk Explanation:** The file uses raw `HANDLE` variables that are manually closed with `CloseHandle()`. If an exception is thrown or an early return is taken before `CloseHandle()` is called, the handle will be leaked, leading to a resource leak.
    -   **Suggested Fix:** Implement a simple RAII wrapper class (e.g., `ScopedHandle`) that automatically calls `CloseHandle()` in its destructor. This will ensure that handles are always closed, even in the presence of exceptions or early returns.
    -   **Severity:** High
    -   **Effort:** 30 minutes

### Medium
1.  **Unused Include in `Application.cpp`**
    -   **Code:** `src/core/Application.cpp`
    -   **Debt Type:** Dead/Unused Code
    -   **Risk Explanation:** The file includes `<vector>`, but it is not used. This increases compilation times and adds unnecessary dependencies.
    -   **Suggested Fix:** Remove the `#include <vector>` line.
    -   **Severity:** Medium
    -   **Effort:** < 5 minutes

2.  **Pass-by-Value in `SearchControls.cpp`**
    -   **Code:** `src/ui/SearchControls.cpp`
    -   **Debt Type:** Memory & Performance Debt
    -   **Risk Explanation:** The `Render` function takes a `std::string` by value, which can cause unnecessary copies and allocations in a performance-sensitive UI rendering loop.
    -   **Suggested Fix:** Change the function signature to accept a `const std::string&` or, even better, a `std::string_view` to avoid the copy.
    -   **Severity:** Medium
    -   **Effort:** < 15 minutes

3.  **Missing `reserve()` in `FileIndex.cpp`**
    -   **Code:** `src/index/FileIndex.cpp`
    -   **Debt Type:** Memory & Performance Debt
    -   **Risk Explanation:** In loops where the number of elements to be added to a `std::vector` is known beforehand, failing to call `reserve()` can lead to multiple reallocations, which is inefficient.
    -   **Suggested Fix:** Call `reserve()` on the vector before entering the loop to pre-allocate the required memory.
    -   **Severity:** Medium
    -   **Effort:** < 15 minutes

### Low
1.  **Missing `#pragma once` in `Version.h`**
    -   **Code:** `src/core/Version.h`
    -   **Debt Type:** Maintainability Issues
    -   **Risk Explanation:** The header file `Version.h` is missing a `#pragma once` directive. While the build system may prevent it from being included multiple times, it's a good practice to always include include guards to prevent accidental multiple inclusions.
    -   **Suggested Fix:** Add `#pragma once` to the top of the `Version.h` file.
    -   **Severity:** Low
    -   **Effort:** < 5 minutes

## Summary

-   **Debt Ratio Estimate:** Approximately 15% of the codebase is affected by some form of technical debt, with the majority concentrated in the `FileIndex` class.
-   **Top 3 Quick Wins:**
    1.  Add `[[nodiscard]]` to `LazyAttributeLoader` functions.
    2.  Add `#pragma once` to `Version.h`.
    3.  Fix the pass-by-value in `SearchControls.cpp`.
-   **Top Critical/High Items:**
    1.  The "God Class" status of `FileIndex` requires immediate attention.
    2.  The inconsistent naming conventions in `main_win.cpp` should be addressed.
    3.  The potential `HANDLE` leak in `FileOperations_win.cpp` should be fixed.
-   **Systematic Patterns:** There is a pattern of missing `[[nodiscard]]` on functions that return futures or other must-handle values. A codebase-wide audit for this would be beneficial.
