# Test Strategy Audit - 2026-02-20

## Executive Summary
- **Test Health Score**: 7/10
- **Coverage Estimate**: 65% (Line coverage)
- **Critical Gaps**: 2
- **Flakiness Assessment**: Low (tests are primarily functional and deterministic)
- **Estimated Remediation Effort**: 40 hours

## Findings

### 1. Missing Test Coverage for Critical Platform Components
- **Component**: `src/usn/UsnMonitor.cpp`
- **Issue Type**: Coverage Analysis
- **Gap Description**: The entire USN monitoring logic, which is the primary real-time indexing mechanism on Windows, has zero unit or integration tests.
- **Risk**: High risk of regressions in file system monitoring, which could lead to an inconsistent index or application crashes.
- **Suggested Action**: Implement integration tests using a mock volume or a temporary test directory on Windows.
- **Priority**: Critical
- **Effort**: Large

### 2. Lack of Threading/Concurrency Stress Tests
- **Component**: `src/search/ParallelSearchEngine.cpp`, `src/index/FileIndex.cpp`
- **Issue Type**: Threading & Concurrency Testing
- **Gap Description**: While there are unit tests for parallel search, there are no stress tests that verify thread safety under high contention or during simultaneous index updates and searches.
- **Risk**: Race conditions or deadlocks that only appear under load.
- **Suggested Action**: Add stress tests that spawn multiple search and update threads simultaneously.
- **Priority**: High
- **Effort**: Medium

### 3. Missing Error Path Testing
- **Component**: All modules
- **Issue Type**: Error & Edge Case Testing
- **Gap Description**: Most tests focus on "happy paths". Failure modes (e.g., disk full, permission denied, invalid regex) are rarely tested.
- **Risk**: Unexpected behavior or crashes when system-level errors occur.
- **Suggested Action**: Add negative test cases for all major I/O operations and input parsers.
- **Priority**: Medium
- **Effort**: Medium

### 4. Framework Mismatch in Documentation
- **Component**: `docs/prompts/test-strategy-audit.md`
- **Issue Type**: Infrastructure
- **Gap Description**: The audit prompt references GoogleTest (gtest), but the project actually uses `doctest`.
- **Risk**: Confusion for new developers or automated tools.
- **Suggested Action**: Update the prompt and any developer documentation to correctly reflect the use of `doctest`.
- **Priority**: Low
- **Effort**: Small

## Summary

- **Test Health Score**: 7/10. The project has a good foundation of unit tests for utility classes and search logic. The use of `doctest` is efficient and well-integrated.
- **Coverage Estimate**: 65%. While core utilities are well-covered, major platform-specific components (USN) and UI logic are largely untested.
- **Critical Gaps**: `UsnMonitor` and high-load concurrency testing.
- **Quick Wins**:
  - Add unit tests for `UsnRecordUtils.cpp` (safe parsing logic).
  - Add negative test cases for `StdRegexUtils`.
- **Recommended Testing Strategy**:
  - **Unit**: Continue using `doctest` for all new logic.
  - **Integration**: Develop a platform-specific integration test suite for `UsnMonitor`.
  - **Performance**: Automate the existing benchmarks and track results over time.
- **Infrastructure Improvements**:
  - Integrate `TSAN` (Thread Sanitizer) into the test build.
  - Generate and track coverage reports in CI.
