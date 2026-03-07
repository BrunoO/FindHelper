# Technical Debt Review - 2026-01-13

## Executive Summary
- **Health Score**: 6/10
- **Critical Issues**: 1
- **High Issues**: 3
- **Total Findings**: 10
- **Estimated Remediation Effort**: 20 hours

## Findings

### Critical
- **God Class: `FileIndex`**
  - **Debt Type**: Maintainability Issues
  - **Location**: `src/index/FileIndex.h`, `src/index/FileIndex.cpp`
  - **Risk Explanation**: The `FileIndex` class has over 1000 lines of code and manages searching, data storage, filtering, and asynchronous operations. This violates the Single Responsibility Principle, making it extremely difficult to test, maintain, and refactor. A bug in one area (e.g., searching) can have unintended consequences in another (e.g., data storage).
  - **Suggested Fix**: Refactor `FileIndex` into smaller, more focused classes:
    - `FileIndexStorage`: Manages the underlying data structures (e.g., vectors of file entries).
    - `FileIndexSearcher`: Implements search algorithms against the storage.
    - `FileIndexUpdater`: Handles adding, removing, and updating entries.
  - **Severity**: Critical
  - **Effort**: 12 hours

### High
- **Inconsistent Naming Conventions**
  - **Debt Type**: Naming Convention Violations
  - **Location**: Multiple files, e.g., `g_search_worker` in `Application.cpp` (should be `g_search_worker_`), `start_monitoring` in `UsnMonitor.h` (should be `StartMonitoring`).
  - **Risk Explanation**: Inconsistent naming makes the code harder to read and understand, increasing the cognitive load on developers and leading to potential bugs when assumptions about naming are incorrect.
  - **Suggested Fix**: Apply a consistent naming scheme using `clang-format` or a similar tool, and manually correct any remaining inconsistencies.
  - **Severity**: High
  - **Effort**: 4 hours

- **Widespread Use of C-Style Arrays**
  - **Debt Type**: C++17 Modernization Opportunities
  - **Location**: Throughout the codebase, e.g., `char path[MAX_PATH];`.
  - **Risk Explanation**: C-style arrays are not type-safe, do not store their own size, and are prone to buffer overflows. They are a common source of security vulnerabilities.
  - **Suggested Fix**: Replace C-style arrays with `std::array` for fixed-size buffers and `std::vector` for dynamic arrays.
  - **Severity**: High
  - **Effort**: 6 hours

- **Duplicated Logic in Platform-Specific Code**
  - **Debt Type**: Aggressive DRY / Code Deduplication
  - **Location**: `src/platform/linux`, `src/platform/windows`
  - **Risk Explanation**: The platform-specific files for file operations contain significant amounts of duplicated logic for path manipulation and error handling. This means that a bug fix in one file may not be propagated to the others, leading to inconsistent behavior across platforms.
  - **Suggested Fix**: Extract the common logic into a platform-agnostic helper function or base class, and have the platform-specific implementations call into the shared code.
  - **Severity**: High
  - **Effort**: 8 hours

### Medium
- **Missing `[[nodiscard]]` on Functions Returning Status**
- **Use of `std::string` instead of `std::string_view` for Read-Only Parameters**
- **Unnecessary `shared_ptr` usage where `unique_ptr` would suffice**

### Low
- **Missing `#pragma once` in some headers**
- **Stale Doxygen comments**
- **Some member variables missing trailing underscore**

## Quick Wins
1.  **Fix Naming Conventions**: Use a script to automatically correct a large number of naming convention violations.
2.  **Add `[[nodiscard]]`**: Add the `[[nodiscard]]` attribute to functions that return a status or error code.
3.  **Add `#pragma once`**: Add `#pragma once` to all header files that are missing it.

## Recommended Actions
1.  **Refactor `FileIndex`**: This is the highest priority item and will have the largest impact on the codebase's maintainability.
2.  **Replace C-Style Arrays**: Address this to improve security and code safety.
3.  **Deduplicate Platform-Specific Logic**: This will reduce bugs and improve consistency across platforms.
