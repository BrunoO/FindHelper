# Technical Debt Review - 2026-02-14

## Executive Summary
- **Health Score**: 6/10
- **Critical Issues**: 0
- **High Issues**: 2
- **Total Findings**: 5
- **Estimated Remediation Effort**: 10 hours

## Findings


### High
1. **Naming Convention Violations in `FileEntry`**
   - **Code**: `src/index/FileIndexStorage.h:struct FileEntry`
   - **Debt type**: Naming Convention Violations
   - **Risk**: Inconsistency with project style guide (`snake_case_` for members). Harder for automated tools and new developers to follow.
   - **Suggested fix**: Rename `parentID` to `parent_id_`, `isDirectory` to `is_directory_`, etc.
   - **Severity**: High
   - **Effort**: 20 lines, 1 hour

2. **God Object: `Application.cpp`**
   - **Code**: `src/core/Application.cpp`
   - **Debt type**: Maintainability Issues
   - **Risk**: Violates SRP. Extremely difficult to test and maintain. Tightly couples UI, Logic, and IO.
   - **Suggested fix**: Extract Window management, Event handling, and Search coordination into separate classes.
   - **Severity**: High
   - **Effort**: 500+ lines, 16 hours

### Medium
1. **Unbounded Regex Cache**
   - **Code**: `src/utils/StdRegexUtils.h:class ThreadLocalRegexCache`
   - **Debt type**: Memory & Performance Debt
   - **Risk**: Potential memory leak in long-running sessions if many unique regex patterns are used.
   - **Suggested fix**: Implement LRU eviction for the cache.
   - **Severity**: Medium
   - **Effort**: 50 lines, 2 hours


## Quick Wins
- Fix naming in `FileEntry`
- Implement LRU for Regex cache

## Recommended Actions
1. Refactor `Application.cpp` to separate concerns.
2. Standardize naming across all core structs.
