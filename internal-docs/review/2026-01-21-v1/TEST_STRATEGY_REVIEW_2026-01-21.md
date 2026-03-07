# Test Strategy Audit - 2026-01-21

## Executive Summary
- **Test Health Score**: 4/10
- **Coverage Estimate**: ~50% line coverage, with significantly lower branch coverage.
- **Critical Gaps**: `Application` and `ApplicationLogic`, `UsnMonitor`, and most platform-specific code.

The project's test suite provides a reasonable foundation of unit tests for core data structures and algorithms. However, it suffers from major gaps in integration and concurrency testing. Critical components like the main application logic and the `UsnMonitor` are almost entirely untested. The lack of a comprehensive mocking strategy for platform APIs makes it difficult to test platform-specific code in isolation.

## Findings

### Critical
1.  **Untested Core Application Logic**
    - **Component:** `src/core/Application.cpp`, `src/core/ApplicationLogic.cpp`
    - **Issue Type:** Coverage Analysis / Missing Test Files
    - **Gap Description:** There are no tests for the `Application` or `ApplicationLogic` classes, which are responsible for the main event loop, UI orchestration, and overall application state management.
    - **Risk:** High-impact bugs in the core application lifecycle, state transitions, or UI integration can go undetected. Regressions are highly likely when refactoring these classes.
    - **Suggested Action:** Create an integration test suite for `ApplicationLogic` that uses a mock UI and a mock `FileIndex` to test the core application behavior without a live GUI.
    - **Priority:** Critical
    - **Effort:** L

2.  **No Tests for `UsnMonitor`**
    - **Component:** `src/usn/UsnMonitor.cpp`
    - **Issue Type:** Coverage Analysis / Missing Test Files
    - **Gap Description:** The `UsnMonitor`, a critical component that interacts directly with the Windows USN Journal, has no unit or integration tests.
    - **Risk:** Bugs in the USN record parsing, handle management, or threading logic could lead to silent data corruption, crashes, or resource leaks. This component runs with elevated privileges, amplifying the risk.
    - **Suggested Action:**
        - Create unit tests that feed mock USN record data to the parsing logic.
        - Develop an integration test that uses a temporary, programmatically created VHD (Virtual Hard Disk) on Windows to create a controlled environment for testing interactions with a real USN Journal.
    - **Priority:** Critical
    - **Effort:** L

### High
1.  **Lack of Concurrency Testing**
    - **Component:** `src/search/ParallelSearchEngine.cpp`, `src/index/FileIndex.cpp`
    - **Issue Type:** Threading & Concurrency Testing / Race Condition Tests
    - **Gap Description:** While there are tests for the search functionality, they do not adequately simulate high-concurrency scenarios. There are no stress tests that involve rapid, overlapping search and index modification operations.
    - **Risk:** Race conditions, deadlocks, and other concurrency-related bugs may exist, leading to intermittent and hard-to-reproduce crashes or data corruption.
    - **Suggested Action:**
        - Integrate Thread Sanitizer (TSAN) into the build process to automatically detect data races.
        - Create a suite of stress tests that spawn multiple threads to perform concurrent searches, insertions, and deletions on the `FileIndex`.
    - **Priority:** High
    - **Effort:** M

2.  **Untested Platform-Specific Code**
    - **Component:** `src/platform/windows/`, `src/platform/linux/`
    - **Issue Type:** Coverage Analysis / Platform-specific files untested
    - **Gap Description:** Most of the platform-specific code, such as `FileOperations_win.cpp` and `FontUtils_linux.cpp`, is not covered by tests.
    - **Risk:** Platform-specific bugs will only be found through manual testing on each OS, which is slow and error-prone.
    - **Suggested Action:** Abstract platform dependencies behind interfaces (e.g., `IFileOperations`, `IFontProvider`) and use mocking or fakes to test the platform-agnostic code. For the platform-specific implementations, create targeted integration tests that run only on the respective platforms.
    - **Priority:** High
    - **Effort:** L

### Medium
1.  **Low Branch Coverage in Error Paths**
    - **Component:** Throughout the test suite.
    - **Issue Type:** Coverage Gaps by Type / Error handling paths never exercised
    - **Gap Description:** The existing tests primarily focus on "happy path" scenarios. Error conditions, such as file-not-found, access-denied, or out-of-memory, are rarely tested.
    - **Risk:** The application may not handle errors gracefully, leading to crashes, resource leaks, or undefined behavior when encountering unexpected conditions.
    - **Suggested Action:** For critical functions, add specific test cases that simulate failure conditions. This may require using dependency injection to mock components that can be made to fail on demand.
    - **Priority:** Medium
    - **Effort:** M

## Quick Wins
1.  **Add a simple unit test for a utility function:** Pick a simple, untested utility function from `StringUtils.h` or `PathUtils.h` and add a basic unit test for it. This is an easy way to start improving coverage.
2.  **Enable Sanitizers in CI:** Modify the CMake configuration to add build targets that compile and run the tests with Address Sanitizer (ASAN) and Thread Sanitizer (TSAN). This can immediately uncover existing memory and threading bugs.
3.  **Use `doctest` for simple tests:** The project already uses `doctest` for some tests. This is a lightweight framework that is well-suited for adding small, focused tests for utility functions.

## Recommended Testing Strategy
- **Unit Tests (60%):** Focus on pure, logic-based components like `PathPatternMatcher`, `StringUtils`, and the individual components of a refactored `FileIndex`.
- **Integration Tests (35%):** Test the interaction between components, such as `ApplicationLogic` with a mock `IFileIndex`, and `UsnMonitor` with a test VHD.
- **End-to-End (E2E) Tests (5%):** A small suite of E2E tests could be created using a UI testing framework (like `imgui_test_engine`) to verify the entire application flow. This is a long-term goal.

## Infrastructure Improvements
1.  **CI Integration:** The CI pipeline should be configured to run all tests on all supported platforms for every pull request.
2.  **Coverage Reporting:** Integrate `gcov/lcov` or a similar tool into the CI pipeline to generate and track code coverage reports over time. Set a target to gradually increase coverage to >80%.
3.  **Sanitizer Builds:** Create dedicated CI jobs that run the test suite with ASAN and TSAN enabled to catch memory and threading bugs automatically.
