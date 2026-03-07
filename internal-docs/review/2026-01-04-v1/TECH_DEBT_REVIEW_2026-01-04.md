# Tech Debt Review - 2026-01-04

## Executive Summary
- **Health Score**: 6/10
- **Critical Issues**: 1
- **High Issues**: 3
- **Total Findings**: 10
- **Estimated Remediation Effort**: 8 hours

## Findings

### Critical
1.  **God Class (`FileIndex`)**
    - **Code**: `FileIndex.h`, `FileIndex.cpp`
    - **Debt type**: Maintainability Issues
    - **Risk explanation**: The `FileIndex` class is a "God Class" with over 1000 lines of code and numerous responsibilities, including data storage, indexing, searching, and thread management. This makes it difficult to understand, maintain, and test.
    - **Suggested fix**: Refactor `FileIndex` into smaller, more focused classes. For example, create a `Searcher` class to handle search logic, an `Indexer` class for indexing, and a `DataManager` class for data storage.
    - **Severity**: Critical
    - **Effort**: 4 hours

### High
1.  **Inconsistent Naming Conventions**
    - **Code**: Throughout the codebase.
    - **Debt type**: Naming Convention Violations
    - **Risk explanation**: The codebase inconsistently applies the specified naming conventions. For example, some member variables are missing the trailing underscore, and some functions use snake_case instead of PascalCase. This makes the code harder to read and understand.
    - **Suggested fix**: Perform a codebase-wide search and replace to enforce the naming conventions. Use a linter to prevent future violations.
    - **Severity**: High
    - **Effort**: 1 hour

2.  **Missing `[[nodiscard]]`**
    - **Code**: `PathUtils.h`, `StringUtils.h`, and others.
    - **Debt type**: C++17 Modernization Opportunities
    - **Risk explanation**: Many functions that return a value that should be checked (e.g., error codes, new objects) are missing the `[[nodiscard]]` attribute. This can lead to subtle bugs if the return value is ignored.
    - **Suggested fix**: Add `[[nodiscard]]` to all functions whose return value should not be ignored.
    - **Severity**: High
    - **Effort**: 1 hour

3.  **Potential for Unnecessary Copies**
    - **Code**: Various files.
    - **Debt type**: Memory & Performance Debt
    - **Risk explanation**: Some functions take `std::string` by value when `std::string_view` would be more efficient, especially in performance-critical code paths. This can lead to unnecessary memory allocations and copies.
    - **Suggested fix**: Replace `const std::string&` with `std::string_view` in function parameters where the function only needs to read the string.
    - **Severity**: High
    - **Effort**: 1 hour

### Medium
1.  **Unused Includes**
    - **Code**: Various files.
    - **Debt type**: Dead/Unused Code
    - **Risk explanation**: Many files have unused `#include` directives. This increases build times and can make it harder to understand the dependencies of a file.
    - **Suggested fix**: Use a tool like `iwyu` (Include What You Use) to identify and remove unused includes.
    - **Severity**: Medium
    - **Effort**: 30 minutes

2.  **Missing `reserve()` calls**
    - **Code**: `FileIndex.cpp` and others.
    - **Debt type**: Memory & Performance Debt
    - **Risk explanation**: Some loops that add a known number of elements to a `std::vector` are missing a call to `reserve()` beforehand. This can lead to multiple reallocations and degrade performance.
    - **Suggested fix**: Add a call to `reserve()` before any loop that adds a known number of elements to a `std::vector`.
    - **Severity**: Medium
    - **Effort**: 30 minutes

3.  **Redundant Locking**
    - **Code**: `SearchWorker.cpp`
    - **Debt type**: C++ Technical Debt
    - **Risk explanation**: The mutex in `SearchWorker` is held for longer than necessary, potentially causing contention.
    - **Suggested fix**: Reduce the scope of the lock to only protect the critical section.
    - **Severity**: Medium
    - **Effort**: 15 minutes

### Low
1.  **Missing `#pragma once`**
    - **Code**: `StringUtils.h`
    - **Debt type**: Maintainability Issues
    - **Risk explanation**: The `StringUtils.h` header is missing a `#pragma once` directive, which can lead to multiple inclusion issues.
    - **Suggested fix**: Add `#pragma once` to the top of `StringUtils.h`.
    - **Severity**: Low
    - **Effort**: 5 minutes

2.  **Use of raw pointers where smart pointers are more appropriate**
    - **Code**: `FileIndex.cpp`
    - **Debt type**: C++ Technical Debt
    - **Risk explanation**: `FileIndex` uses raw pointers for some members, which could be managed by `std::unique_ptr` or `std::shared_ptr` to improve safety and clarity.
    - **Suggested fix**: Replace raw pointers with the appropriate smart pointer.
    - **Severity**: Low
    - **Effort**: 15 minutes

3.  **Missing Doxygen comments**
    - **Code**: `FileIndex.h` and others.
    - **Debt type**: Maintainability Issues
    - **Risk explanation**: Many public functions are missing Doxygen comments, making it harder for other developers to understand how to use them.
    - **Suggested fix**: Add Doxygen comments to all public functions.
    - **Severity**: Low
    - **Effort**: 30 minutes

## Quick Wins
1.  Add `#pragma once` to `StringUtils.h`.
2.  Fix inconsistent naming in `FileIndex.h`.
3.  Add `[[nodiscard]]` to `PathUtils::GetExtension`.

## Recommended Actions
1.  Refactor the `FileIndex` class.
2.  Perform a codebase-wide naming convention fix.
3.  Add `[[nodiscard]]` to all appropriate functions.
4.  Replace `const std::string&` with `std::string_view` where possible.
