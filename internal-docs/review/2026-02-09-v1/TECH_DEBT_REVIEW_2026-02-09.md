# Tech Debt Review - 2026-02-09

## Executive Summary
- **Health Score**: 6/10
- **Critical Issues**: 1
- **High Issues**: 3
- **Total Findings**: ~20
- **Estimated Remediation Effort**: 40 hours

## Findings

### Critical
- **ReDoS Vulnerability in `rs:` searches**: (src/utils/RegexAliases.h)
  - **Debt type**: Potential Bugs and Logic Errors / Security
  - **Risk explanation**: User-provided regex via the `rs:` prefix is processed by `std::regex` or `boost::regex`. Both use backtracking engines vulnerable to catastrophic backtracking. A maliciously crafted regex (e.g., `(a+)+$`) can cause the search thread to hang indefinitely, consuming 100% CPU.
  - **Suggested fix**: Integrate a linear-time regex engine like **RE2** for user-provided patterns, or implement strict complexity limits/timeouts on the backtracking engine.
  - **Severity**: Critical
  - **Effort**: 8 hours (Integrate RE2)

### High
- **Widespread Naming Convention Violations**: (Project-wide, e.g., src/index/FileIndexStorage.h, src/search/SearchTypes.h)
  - **Debt type**: Naming Convention Violations
  - **Risk explanation**: Extensive use of `camelCase` for member variables and functions (e.g., `parentID`, `isDirectory`, `fileSize`) violates the project's `snake_case_` and `PascalCase` rules. This is currently suppressed by ~1600 `NOLINT` directives, increasing noise and hiding potential real issues.
  - **Suggested fix**: Perform a systematic refactor to `snake_case_` for members and `PascalCase` for methods.
  - **Severity**: High
  - **Effort**: 24 hours (Systematic refactor)

- **Tight Coupling in `Application.cpp` (God Object)**: (src/core/Application.h)
  - **Debt type**: Maintainability Issues
  - **Risk explanation**: The `Application` class acts as a central hub for too many responsibilities (UI, life-cycle, indexing, searching). It has 23+ fields and coordinates many components directly.
  - **Suggested fix**: Extract concerns into dedicated manager classes (e.g., `SearchManager`, `IndexManager`, `WindowCoordinator`).
  - **Severity**: High
  - **Effort**: 16 hours

### Medium
- **Redundant `NOLINT` and `NOSONAR` directives**: (Project-wide)
  - **Debt type**: Maintainability Issues
  - **Risk explanation**: Many `NOLINT` directives reference checks that are globally disabled in `.clang-tidy`. This clutters the code and makes it harder to see relevant suppressions.
  - **Suggested fix**: Cleanup `NOLINT` directives using `scripts/analyze_clang_tidy_warnings.py` results.
  - **Severity**: Medium
  - **Effort**: 4 hours

- **AI Search Logic mixed with UI**: (src/ui/SearchInputs.cpp)
  - **Debt type**: Maintainability Issues / SRP
  - **Risk explanation**: Gemini API handling logic (waiting for futures, processing results) is embedded within the ImGui rendering functions.
  - **Suggested fix**: Move AI logic to a dedicated service or `ApplicationLogic`.
  - **Severity**: Medium
  - **Effort**: 4 hours

### Low
- **Missing `[[nodiscard]]` on fallible methods**: (Project-wide)
  - **Debt type**: C++17 Modernization
  - **Risk explanation**: Methods returning success/failure bools or important pointers (e.g., `FileIndexStorage::GetEntry`) lack `[[nodiscard]]`.
  - **Suggested fix**: Add `[[nodiscard]]` to all pure getters and success-indicating returns.
  - **Severity**: Low
  - **Effort**: 2 hours

## Quick Wins
1. **Remove redundant `NOLINT`** for globally disabled checks.
2. **Add `[[nodiscard]]`** to key retrieval methods in `FileIndex`.
3. **Fix `Simplified UI` setting override** in `SearchInputs.cpp` to at least show a tooltip explaining the forced state.

## Recommended Actions
1. **Immediate**: Integrate RE2 for `rs:` regex patterns to prevent ReDoS.
2. **Short-term**: Refactor `FileEntry` members to `snake_case_` to remove the bulk of naming violations.
3. **Long-term**: Decompose the `Application` class to improve modularity and testability.
