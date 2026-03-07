# Tech Debt Review - 2026-01-08

## Executive Summary
- **Health Score**: 6/10
- **Critical Issues**: 0
- **High Issues**: 2
- **Total Findings**: 5
- **Estimated Remediation Effort**: 8-12 hours

## Findings

### High

**1. God Class: `FileIndex`**
- **Code**: `src/index/FileIndex.h`, `src/index/FileIndex.cpp`
- **Debt Type**: Maintainability Issues (God Class)
- **Risk Explanation**: The `FileIndex` class has grown to over 800 lines of code and has accumulated numerous responsibilities, including data storage, indexing, searching, thread pool management, and load balancing strategy. This violates the Single Responsibility Principle, making the class difficult to understand, maintain, and test. Changes in one area of functionality have a high risk of impacting unrelated areas.
- **Suggested Fix**: Refactor `FileIndex` by extracting distinct responsibilities into smaller, more focused classes. For example:
    - `IndexStorage`: Manages the raw data structures for file entries.
    - `SearchOrchestrator`: Manages the execution of searches, including thread pool interaction and load balancing.
    - `IndexBuilder`: Handles the initial population and updating of the index.
- **Severity**: High
- **Effort**: 6-8 hours (significant refactoring)

**2. God Class: `PathPatternMatcher`**
- **Code**: `src/path/PathPatternMatcher.h`, `src/path/PathPatternMatcher.cpp`
- **Debt Type**: Maintainability Issues (God Class)
- **Risk Explanation**: The `PathPatternMatcher` class is responsible for compiling patterns, generating a state machine, and performing the actual matching. This combination of responsibilities in a single class makes it complex and difficult to modify.
- **Suggested Fix**: Split the `PathPatternMatcher` into two or three smaller classes:
    - `PathPatternCompiler`: Responsible for taking a string pattern and compiling it into an intermediate representation.
    - `PathPatternStateMachine`: Represents the compiled pattern and contains the logic for matching.
- **Severity**: High
- **Effort**: 2-4 hours

### Medium

**1. Pass-by-value of `std::string`**
- **Code**: `src/index/IndexFromFilePopulator.h`
  ```cpp
  bool populate_index_from_file(const std::string file_path, FileIndex& file_index);
  ```
- **Debt Type**: Memory & Performance Debt
- **Risk Explanation**: The `populate_index_from_file` function accepts its `file_path` argument by value, causing an unnecessary copy of the string every time the function is called. For long file paths, this can result in performance overhead due to memory allocation.
- **Suggested Fix**: Change the function signature to accept the file path by `const std::string&` to avoid the copy.
  ```cpp
  bool populate_index_from_file(const std::string& file_path, FileIndex& file_index);
  ```
- **Severity**: Medium
- **Effort**: < 15 minutes

**2. Missing `[[nodiscard]]` on Functions Returning Important Values**
- **Code**: `src/index/FileIndex.h`
  ```cpp
  bool IsCaseSensitive() const;
  ```
- **Debt Type**: C++17 Modernization Opportunities
- **Risk Explanation**: The `IsCaseSensitive` function returns a boolean value that is intended to be used by the caller. Without the `[[nodiscard]]` attribute, the compiler will not warn if the return value is accidentally ignored, which could lead to incorrect search behavior.
- **Suggested Fix**: Add the `[[nodiscard]]` attribute to the function signature to ensure that the compiler issues a warning if the return value is ignored.
  ```cpp
  [[nodiscard]] bool IsCaseSensitive() const;
  ```
- **Severity**: Medium
- **Effort**: < 15 minutes

### Low

**1. Naming Convention Violation: Member Variable**
- **Code**: `src/search/SearchContext.h`
  ```cpp
  std::shared_ptr<PathPatternMatcher> pathMatcher;
  ```
- **Debt Type**: Naming Convention Violations
- **Risk Explanation**: The member variable `pathMatcher` does not have a trailing underscore, which violates the project's naming conventions. This can make it more difficult to distinguish between member variables and local variables.
- **Suggested Fix**: Rename the member variable to `pathMatcher_` to adhere to the project's coding standards.
  ```cpp
  std::shared_ptr<PathPatternMatcher> pathMatcher_;
  ```
- **Severity**: Low
- **Effort**: < 15 minutes

## Summary

- **Debt Ratio Estimate**: Approximately 10% of the core logic is affected by the God Class issues, which have a high impact on maintainability.
- **Top 3 "Quick Wins"**:
  1. Fix pass-by-value of `std::string` in `populate_index_from_file`.
  2. Add `[[nodiscard]]` to `IsCaseSensitive`.
  3. Fix naming convention violation for `pathMatcher` in `SearchContext`.
- **Top Critical/High Items**:
  1. Refactor the `FileIndex` God Class.
  2. Refactor the `PathPatternMatcher` God Class.
