# Tech Debt Review - 2026-02-06

## Executive Summary
- **Health Score**: 7/10
- **Critical Issues**: 0
- **High Issues**: 2
- **Total Findings**: 8
- **Estimated Remediation Effort**: 12 hours

## Findings

### Critical
None found.

### High
1. **Redundant Exception Handling in SearchWorker.cpp**
   - **Location**: `src/search/SearchWorker.cpp:392-466`
   - **Debt Type**: C++ Technical Debt / Code Deduplication
   - **Risk**: High cognitive complexity and maintenance burden. Multiple catch blocks (10+) for different exception types perform identical logging and error-setting logic.
   - **Suggested Fix**: Use a helper function or catch `std::exception` once if the logic is truly identical.
   - **Effort**: 1 hour, ~50 lines removed.

2. **Tight Coupling in Application.cpp**
   - **Location**: `src/core/Application.cpp`
   - **Debt Type**: Maintainability Issues / God Class
   - **Risk**: The `Application` class has too many responsibilities (window management, rendering coordination, search management, index lifecycle). This makes unit testing difficult.
   - **Suggested Fix**: Refactor into smaller specialized controllers (e.g., `WindowController`, `IndexController`).
   - **Effort**: 6 hours, major refactoring.

### Medium
1. **Header-Heavy Architecture**
   - **Location**: `src/index/FileIndex.h`
   - **Debt Type**: Maintainability Issues
   - **Risk**: `FileIndex.h` includes many other headers and has many inline methods, leading to long compilation times and circular dependency risks.
   - **Suggested Fix**: Move more implementations to `.cpp` files and use PIMPL where appropriate.
   - **Effort**: 3 hours.

2. **Frequent use of NOLINT/NOSONAR**
   - **Location**: Multiple files (e.g., `SearchWorker.cpp`, `Logger.h`)
   - **Debt Type**: Maintainability Issues
   - **Risk**: Over-reliance on suppression comments can hide real issues and makes the code cluttered.
   - **Suggested Fix**: Address the root causes of static analysis warnings instead of suppressing them.
   - **Effort**: 2 hours.

### Low
1. **Pass-by-value of large types**
   - **Location**: `src/search/SearchWorker.cpp:353` (`std::vector<char> buffer`)
   - **Debt Type**: Memory & Performance Debt
   - **Risk**: Unnecessary copies of large buffers.
   - **Suggested Fix**: Use `std::move` or pass by reference where appropriate.
   - **Effort**: 0.5 hours.

2. **Missing [[nodiscard]]**
   - **Location**: Various functions returning error codes (e.g., `FileIndexStorage::UpdateFileSize`)
   - **Debt Type**: C++17 Modernization
   - **Risk**: Error codes might be ignored.
   - **Suggested Fix**: Add `[[nodiscard]]` to all relevant functions.
   - **Effort**: 0.5 hours.

## Quick Wins
1. **Consolidate exception handling in `SearchWorker.cpp`**: Replace redundant catch blocks with a single `catch(const std::exception&)` or a macro/helper.
2. **Add `[[nodiscard]]` to core index operations**: Ensure return values of `Insert`, `Remove`, `Rename` are not ignored.
3. **Remove redundant `NOLINT`**: Clean up suppressions that are no longer needed after recent refactorings.

## Recommended Actions
1. **Immediate**: Refactor `SearchWorker.cpp` exception handling.
2. **Short-term**: Decouple `Application` class by extracting `IndexController`.
3. **Long-term**: Reduce header dependencies in `FileIndex.h` by moving inline methods to `FileIndex.cpp`.
