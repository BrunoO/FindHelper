# Tech Debt Review - 2026-01-17

## Executive Summary
- **Health Score**: 6/10
- **Critical Issues**: 1
- **High Issues**: 2
- **Total Findings**: 5
- **Estimated Remediation Effort**: 12 hours

## Findings

### Critical
- **"God Class" - `Application`**: The `Application` class (`src/core/Application.h`, `src/core/Application.cpp`) has over 600 lines of code and is responsible for too many things, including the main loop, UI rendering, and application logic. This violates the Single Responsibility Principle and makes the class difficult to maintain and test.
  - **Risk**: High maintenance cost, difficult to test, high coupling.
  - **Suggested fix**: Refactor the `Application` class into smaller, more focused classes. For example, create a `UIRenderer` class to handle all UI rendering, a `MainLoop` class to manage the main application loop, and an `ApplicationLogic` class to handle the core application logic.
  - **Severity**: Critical
  - **Effort**: 8 hours

### High
- **"God Class" - `FileIndex`**: The `FileIndex` class (`src/index/FileIndex.h`, `src/index/FileIndex.cpp`) is responsible for data storage, searching, and index maintenance. This is another example of a "God Class" that violates the Single Responsibility Principle.
  - **Risk**: High maintenance cost, difficult to test, high coupling.
  - **Suggested fix**: Refactor the `FileIndex` class into smaller, more focused classes. For example, create a `FileIndexStorage` class to handle data storage, a `Searcher` class to handle searching, and an `IndexMaintainer` class to handle index maintenance.
  - **Severity**: High
  - **Effort**: 2 hours

- **"God Class" - `ParallelSearchEngine`**: The `ParallelSearchEngine` class (`src/search/ParallelSearchEngine.h`, `src/search/ParallelSearchEngine.cpp`) is responsible for parallelizing the search process. While this is a single responsibility, the class is very large and complex, making it difficult to understand and maintain.
  - **Risk**: High maintenance cost, difficult to test, high coupling.
  - **Suggested fix**: Refactor the `ParallelSearchEngine` class to simplify its design. For example, consider using a thread pool to manage the worker threads and a simpler mechanism for distributing the search work.
  - **Severity**: High
  - **Effort**: 2 hours

### Medium
- **C++17 Modernization Opportunities**: There are several opportunities to modernize the codebase by using C++17 features. For example, `std::string_view` could be used instead of `const std::string&` for function parameters, and `[[nodiscard]]` could be used to mark functions that return important values.
  - **Risk**: Missed opportunities for performance improvements and better code clarity.
  - **Suggested fix**: Systematically review the codebase and apply C++17 features where appropriate.
  - **Severity**: Medium
  - **Effort**: 1 hour

### Low
- **Naming Convention Violations**: There are several instances of naming convention violations in the codebase. For example, some member variables are missing the trailing underscore.
  - **Risk**: Inconsistent code style, which can make the code harder to read and understand.
  - **Suggested fix**: Systematically review the codebase and fix all naming convention violations.
  - **Severity**: Low
  - **Effort**: 1 hour

## Quick Wins
- Add `[[nodiscard]]` to functions that return important values.
- Replace `const std::string&` with `std.string_view` where appropriate.
- Fix naming convention violations.

## Recommended Actions
1. Refactor the `Application` class.
2. Refactor the `FileIndex` class.
3. Refactor the `ParallelSearchEngine` class.
4. Modernize the codebase with C++17 features.
5. Fix naming convention violations.
