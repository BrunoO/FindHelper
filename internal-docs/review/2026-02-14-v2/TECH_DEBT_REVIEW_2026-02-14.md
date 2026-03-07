# Tech Debt Review - 2026-02-14

## Executive Summary
- **Health Score**: 6/10
- **Critical Issues**: 1
- **High Issues**: 3
- **Total Findings**: 12
- **Estimated Remediation Effort**: 40 hours

## Findings

### Critical
- **Naming Convention Violations and Suppressions in `SearchTypes.h`**:
  - **Quote**: `mutable uint64_t fileSize = 0; // NOLINT(readability-identifier-naming)`
  - **Debt type**: Naming Convention
  - **Risk explanation**: Widespread use of `camelCase` for member variables and `PascalCase` for local variables/parameters violates the project's `snake_case_` and `snake_case` rules. The use of `NOLINT` suppresses warnings rather than fixing the debt, making the codebase inconsistent.
  - **Suggested fix**: Refactor `SearchResult`, `SearchResultData`, and `SearchParams` to use `snake_case_` for members and remove `NOLINT` suppressions.
  - **Severity**: Critical
  - **Effort**: 16 hours (due to widespread usage in UI and Search logic)

### High
- **God Object: `Application.cpp`**:
  - **Location**: `src/core/Application.cpp` (812 lines)
  - **Debt type**: Maintainability (God Class)
  - **Risk explanation**: Handles lifecycle, windowing, UI orchestration, and core logic. Violates SRP.
  - **Suggested fix**: Decompose into `WindowManager`, `LifecycleManager`, and `UIOrchestrator`.
  - **Severity**: High
  - **Effort**: 12 hours

- **Complexity in `PathPatternMatcher.cpp`**:
  - **Location**: `src/path/PathPatternMatcher.cpp` (1499 lines)
  - **Debt type**: Maintainability
  - **Risk explanation**: Extremely long file handling complex regex and pattern matching logic. Hard to maintain and test.
  - **Suggested fix**: Split into `RegexMatcher`, `WildcardMatcher`, and `PatternParser`.
  - **Severity**: High
  - **Effort**: 16 hours

### Medium
- **`std::string_view` Opportunities**:
  - **Quote**: `std::string ResultsTable::TruncatePathAtBeginning(const std::string& path, float max_width)`
  - **Debt type**: C++17 Modernization
  - **Risk explanation**: Passing `const std::string&` for read-only path truncation causes unnecessary allocations if called with string literals or substrings.
  - **Suggested fix**: Use `std::string_view`.
  - **Severity**: Medium
  - **Effort**: 2 hours

### Low
- **Missing `[[nodiscard]]`**:
  - **Debt type**: C++17 Modernization
  - **Risk explanation**: Many utility functions returning success/failure or important values (like `PathUtils`) lack `[[nodiscard]]`.
  - **Severity**: Low
  - **Effort**: 4 hours

## Quick Wins
- Add `[[nodiscard]]` to `PathUtils` functions.
- Convert `ResultsTable::TruncatePathAtBeginning` to use `std::string_view`.
- Remove unused includes identified by `clang-tidy`.

## Recommended Actions
1. **Immediate**: Resolve the naming convention debt in `SearchTypes.h` as it affects all new search-related features.
2. **Short-term**: Refactor `Application.cpp` to separate concerns.
3. **Long-term**: Modularize `PathPatternMatcher.cpp`.

---
**Debt ratio estimate**: ~15% of codebase affected by naming or structural debt.
