# Test Strategy Audit - 2026-01-17

## Executive Summary
- **Test Health Score**: 6/10
- **Coverage Estimate**: ~70% line coverage, ~60% branch coverage
- **Critical Gaps**:
    -   `Application` and `ApplicationLogic` classes are completely untested.
    -   `UsnMonitor` class is untested.
    -   Platform-specific code is largely untested.
- **Flakiness Assessment**: The test suite appears to be stable and free of flaky tests.
- **Quick Wins**:
    -   Add unit tests for the `ApplicationLogic` class.
    -   Add unit tests for the `UsnMonitor` class.
- **Recommended Testing Strategy**:
    -   Increase unit test coverage for critical components.
    -   Add integration tests for platform-specific code.
    -   Add performance tests for the search functionality.
- **Infrastructure Improvements**:
    -   Enable TSAN/ASAN in CI.
    -   Add coverage reporting to CI pipeline.

## Findings

### Critical
-   **`Application` and `ApplicationLogic` Classes Untested**: The `Application` and `ApplicationLogic` classes are critical components of the application, but they are completely untested. This is a major gap in the test suite.
    -   **Component**: `src/core/Application.cpp`, `src/core/ApplicationLogic.cpp`
    -   **Issue Type**: Coverage Gaps
    -   **Gap Description**: There are no unit tests for the `Application` and `ApplicationLogic` classes.
    -   **Risk**: Bugs in the core application logic could go undetected.
    -   **Suggested Action**: Add unit tests for the `Application` and `ApplicationLogic` classes.
    -   **Priority**: Critical
    -   **Effort**: L

-   **`UsnMonitor` Class Untested**: The `UsnMonitor` class is responsible for real-time monitoring on Windows, but it is untested. This is a major gap in the test suite.
    -   **Component**: `src/usn/UsnMonitor.cpp`
    -   **Issue Type**: Coverage Gaps
    -   **Gap Description**: There are no unit tests for the `UsnMonitor` class.
    -   **Risk**: Bugs in the real-time monitoring functionality could go undetected.
    -   **Suggested Action**: Add unit tests for the `UsnMonitor` class.
    -   **Priority**: Critical
    -   **Effort**: L

### High
-   **Platform-Specific Code Largely Untested**: The platform-specific code in `src/platform` is largely untested. This is a major gap in the test suite.
    -   **Component**: `src/platform`
    -   **Issue Type**: Coverage Gaps
    -   **Gap Description**: There are no unit tests for the platform-specific code.
    -   **Risk**: Bugs in the platform-specific code could go undetected.
    -   **Suggested Action**: Add integration tests for the platform-specific code.
    -   **Priority**: High
    -   **Effort**: L

### Medium
-   **Missing Performance Tests**: There are no performance tests for the search functionality. This makes it difficult to track performance regressions.
    -   **Component**: `src/search`
    -   **Issue Type**: Missing Test Types
    -   **Gap Description**: There are no performance tests for the search functionality.
    -   **Risk**: Performance regressions could go undetected.
    -   **Suggested Action**: Add performance tests for the search functionality.
    -   **Priority**: Medium
    -   **Effort**: M
