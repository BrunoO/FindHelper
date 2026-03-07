# Test Strategy Review - 2026-01-18

## Executive Summary
- **Test Health Score**: 5/10
- **Coverage Estimate**: ~60% line coverage, ~40% branch coverage.
- **Critical Gaps**: `Application`, `ApplicationLogic`, `UsnMonitor`, and most platform-specific code are completely untested.
- **Flakiness Assessment**: The test suite appears to be stable, with no obvious signs of flakiness.
- **Estimated Remediation Effort**: 30-40 hours

## Findings

### Critical
1.  **Core Application Logic is Untested**
    *   **Component**: `src/core/Application.cpp`, `src/core/ApplicationLogic.cpp`
    *   **Issue Type**: Coverage Analysis
    *   **Gap Description**: There are no unit tests for the `Application` or `ApplicationLogic` classes, which are the heart of the application. This is the single largest gap in the test suite.
    *   **Risk**: Bugs in the main application loop, state management, or UI orchestration logic would not be caught by any automated tests.
    *   **Suggested Action**: Refactor `Application` and `ApplicationLogic` to allow for dependency injection (see Architecture Review). Then, write unit tests that mock the dependencies and verify the core logic.
    *   **Priority**: Critical
    *   **Effort**: L

2.  **No Tests for `UsnMonitor`**
    *   **Component**: `src/usn/UsnMonitor.cpp`
    *   **Issue Type**: Coverage Analysis
    *   **Gap Description**: The `UsnMonitor`, a critical component for real-time indexing on Windows, has no unit or integration tests.
    *   **Risk**: Bugs in the USN Journal parsing, handle management, or threading logic could lead to data corruption, missed file updates, or crashes. These would be very difficult to debug without tests.
    *   **Suggested Action**:
        -   **Unit Tests**: Abstract the Windows API dependencies behind an interface and write unit tests that simulate various USN record types and error conditions.
        -   **Integration Tests**: Create a test that runs against a real (temporary) volume to verify the end-to-end functionality. This test should be marked as slow and run in a dedicated CI environment.
    *   **Priority**: Critical
    *   **Effort**: L

### High
1.  **Platform-Specific Code is Untested**
    *   **Component**: `src/platform/` directory
    *   **Issue Type**: Coverage Analysis
    *   **Gap Description**: The majority of the platform-specific code (e.g., `FileOperations_win.cpp`, `AppBootstrap_mac.mm`) is not covered by any tests.
    *   **Risk**: Platform-specific bugs will only be found through manual testing, which is slow and error-prone.
    *   **Suggested Action**: For each platform-specific implementation, write a corresponding set of tests. Where possible, these tests should be driven by a common, platform-agnostic interface.
    *   **Priority**: High
    *   **Effort**: L

2.  **No Integration Tests for Component Interactions**
    *   **Component**: End-to-end search workflow.
    *   **Issue Type**: Missing Test Types
    *   **Gap Description**: The test suite consists almost entirely of unit tests. There are no integration tests that verify the interactions between major components like `SearchController`, `SearchWorker`, and `FileIndex`.
    *   **Risk**: The unit tests for each component might pass, but the system could fail as a whole due to incorrect interactions or assumptions between components.
    *   **Suggested Action**: Create a suite of integration tests that set up a small, in-memory `FileIndex`, trigger a search via the `SearchController`, and verify that the correct results are returned.
    *   **Priority**: High
    *   **Effort**: M

3.  **No Performance or Benchmark Tests**
    *   **Component**: Search and indexing performance.
    *   **Issue Type**: Missing Test Types
    *   **Gap Description**: There are no automated performance tests to track search speed or indexing time.
    *   **Risk**: Performance regressions could be introduced without being noticed until a user complains.
    *   **Suggested Action**: Create a set of benchmark tests using a framework like Google Benchmark. These tests should measure the performance of key operations (e.g., search with a specific query, indexing a directory) and be run periodically to track performance over time.
    *   **Priority**: High
    *   **Effort**: M

### Medium
1.  **Error Handling Paths Are Not Tested**
    *   **Component**: Across the codebase.
    *   **Issue Type**: Error & Edge Case Testing
    *   **Gap Description**: The existing tests primarily focus on the "happy path". Error conditions, such as file not found, permission denied, or out-of-memory, are generally not tested.
    *   **Risk**: The error handling code is likely to be buggy, as it is not exercised by the tests. This could lead to crashes or unexpected behavior when errors occur.
    *   **Suggested Action**: For each component, add tests that simulate failure conditions and verify that the component behaves as expected (e.g., returns an error, logs a message).
    *   **Priority**: Medium
    *   **Effort**: M

2.  **Threading and Concurrency Are Not Tested**
    *   **Component**: `SearchWorker`, `ParallelSearchEngine`, `ThreadPool`.
    *   **Issue Type**: Threading & Concurrency Testing
    *   **Gap Description**: There are no tests that specifically target the thread safety of the application. The tests are all single-threaded.
    *   **Risk**: Race conditions, deadlocks, and other concurrency bugs could exist and would not be caught by the current test suite.
    *   **Suggested Action**:
        -   Integrate Thread Sanitizer (TSAN) into the build process to automatically detect data races.
        -   Write stress tests that execute the search and indexing code from multiple threads concurrently.
    *   **Priority**: Medium
    *   **Effort**: M

3.  **Low Branch Coverage**
    *   **Component**: Across the codebase.
    *   **Issue Type**: Coverage Analysis
    *   **Gap Description**: Many tests only cover the main execution path through a function, neglecting branches for error handling or different input conditions.
    *   **Risk**: Logic errors in less-frequently-used branches will not be detected.
    *   **Suggested Action**: When writing tests, aim for high branch coverage, not just line coverage. Use a coverage tool to identify untested branches.
    *   **Priority**: Medium
    *   **Effort**: M

### Low
1.  **Weak Assertions**
    *   **Component**: `tests/` directory.
    *   **Issue Type**: Test Quality
    *   **Gap Description**: Some tests use weak assertions like `EXPECT_TRUE(result)` instead of more specific assertions like `EXPECT_EQ(result, expected_value)`.
    *   **Risk**: The test might pass even if the function returns an incorrect value (as long as it's not `false` or `0`).
    *   **Suggested Action**: Review the existing tests and replace weak assertions with more specific ones.
    *   **Priority**: Low
    *   **Effort**: S

2.  **Duplicate Setup Code**
    *   **Component**: `tests/` directory.
    *   **Issue Type**: Test Organization
    *   **Gap Description**: Some test files have duplicate setup code that could be shared using a test fixture.
    *   **Risk**: This makes the tests harder to maintain.
    *   **Suggested Action**: Use test fixtures (classes that inherit from `::testing::Test`) to share setup and teardown logic.
    *   **Priority**: Low
    *   **Effort**: S

## Summary Requirements
- **Test Health Score**: 5/10. The project has a decent number of unit tests for its core data structures and algorithms, but it completely lacks coverage for the main application logic, platform-specific code, and component interactions.
- **Coverage Estimate**: Approximately 60% line coverage and 40% branch coverage.
- **Critical Gaps**: The most critical gaps are the lack of tests for `Application`, `ApplicationLogic`, `UsnMonitor`, and the platform-specific code.
- **Flakiness Assessment**: The test suite appears to be stable.
- **Quick Wins**:
    -   Add a simple test for a utility function that is currently uncovered.
    -   Replace a weak assertion with a more specific one.
    -   Refactor a test file to use a test fixture to reduce code duplication.
- **Recommended Testing Strategy**:
    -   **Unit Tests (70%)**: Continue to write unit tests for individual classes and functions. Focus on covering the critical gaps identified above.
    -   **Integration Tests (25%)**: Create a suite of integration tests that verify the interactions between components.
    -   **End-to-End (E2E) Tests (5%)**: Create a small number of E2E tests that run the actual application and verify its behavior from a user's perspective.
- **Infrastructure Improvements**:
    -   **CI**: The CI pipeline should be configured to run the tests on all three platforms (Windows, macOS, Linux).
    -   **Sanitizers**: Address Sanitizer (ASAN) and Thread Sanitizer (TSAN) should be enabled in the CI build to catch memory and concurrency bugs.
    -   **Coverage Reporting**: The CI pipeline should generate and publish a code coverage report for every build.
