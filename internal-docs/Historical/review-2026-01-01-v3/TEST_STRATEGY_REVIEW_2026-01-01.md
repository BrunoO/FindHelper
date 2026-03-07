# Test Strategy Audit - 2026-01-01

## Test Health Score: 6/10

The project has a solid foundation of unit tests for its core components, with good use of a mocking strategy for the `ISearchableIndex` interface. However, the overall test strategy has some significant gaps, particularly in the areas of integration testing, platform-specific testing, and concurrency testing. The current test suite provides a reasonable level of confidence in the core logic, but it is not sufficient to guarantee the reliability of the application as a whole.

## Coverage Estimate

*   **Line/Branch Coverage**: Estimated to be around 60-70% for the core logic, but much lower for the UI and platform-specific code.

## Critical Gaps

1.  **Lack of Integration Tests**: There are no integration tests that verify the interaction between the main components, such as the `UsnMonitor`, `FileIndex`, and `ParallelSearchEngine`.
2.  **Missing Platform-Specific Tests**: The platform-specific code (e.g., `UsnMonitor.cpp` for Windows, `FileOperations_linux.cpp` for Linux) is not adequately tested.
3.  **No Concurrency Tests**: There are no tests that specifically target the thread-safety of the application. The existing tests run in a single-threaded environment and do not exercise the locking mechanisms under concurrent access.

## Findings

### 1. Coverage Analysis

*   **Component**: `UsnMonitor.cpp`
*   **Issue Type**: Missing Test Files
*   **Gap Description**: There is no test file for the `UsnMonitor` class, which is a critical component for the application's real-time monitoring functionality on Windows.
*   **Risk**: Bugs in the USN journal monitoring logic, such as race conditions or resource leaks, could go undetected.
*   **Suggested Action**: Create a new test file, `UsnMonitorTests.cpp`, and add unit tests for the `UsnMonitor` class. This will likely require creating a mock of the Windows API for USN journal operations.
*   **Priority**: Critical
*   **Effort**: L

### 2. Test Quality

*   **Component**: `ParallelSearchEngineTests.cpp`
*   **Issue Type**: Test Independence Issues
*   **Gap Description**: The tests in this file use a hardcoded path (`C:\\...`) which is not platform-agnostic. This will cause the tests to fail on non-Windows platforms.
*   **Risk**: The tests are not portable and will fail on Linux and macOS.
*   **Suggested Action**: Replace the hardcoded paths with platform-agnostic paths, or use a test fixture to create temporary files and directories for testing.
*   **Priority**: High
*   **Effort**: S

### 3. Missing Test Types

*   **Component**: Entire application
*   **Issue Type**: Missing Integration Tests
*   **Gap Description**: The test suite is composed almost entirely of unit tests. There are no integration tests that verify the correct interaction between the different components of the application.
*   **Risk**: Bugs that only manifest when multiple components are working together will be missed.
*   **Suggested Action**: Create a new integration test suite that launches the application and interacts with it as a user would. This could involve using a framework for UI automation.
*   **Priority**: High
*   **Effort**: L

*   **Component**: `ParallelSearchEngine`
*   **Issue Type**: Missing Concurrency Tests
*   **Gap Description**: The `ParallelSearchEngine` is a multi-threaded component, but there are no tests that specifically exercise its thread-safety.
*   **Risk**: Race conditions or deadlocks could exist in the code and would not be caught by the current test suite.
*   **Suggested Action**: Create a new test case that spawns multiple threads to perform searches concurrently. Use a thread sanitizer (like TSAN) to help detect race conditions.
*   **Priority**: High
*   **Effort**: M

## Recommended Testing Strategy

*   **Unit Tests**: Continue to write unit tests for all new classes and methods. Aim for a line coverage of at least 80% for the core logic.
*   **Integration Tests**: Create a separate integration test suite that focuses on the interaction between the main components. This suite should be run as part of the CI pipeline.
*   **Platform-Specific Tests**: Create a separate test suite for each platform that tests the platform-specific code. These tests should be run on their respective platforms in the CI pipeline.
*   **Concurrency Tests**: Add a suite of stress tests that specifically target the multi-threaded aspects of the application. These tests should be run with a thread sanitizer enabled.

## Infrastructure Improvements

*   **Enable TSAN/ASAN in CI**: Configure the CI pipeline to run the concurrency tests with the Thread Sanitizer (TSAN) and Address Sanitizer (ASAN) enabled. This will help to automatically detect race conditions and memory errors.
*   **Add Coverage Reporting to CI**: Configure the CI pipeline to generate and publish code coverage reports. This will provide visibility into the parts of the codebase that are not well-tested.
