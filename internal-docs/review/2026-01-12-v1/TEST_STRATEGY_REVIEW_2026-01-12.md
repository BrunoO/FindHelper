# Test Strategy Audit - 2026-01-12

## Executive Summary
- **Test Health Score**: 6/10
- **Coverage Estimate**: Approximately 60% line coverage for core logic, but < 20% for UI, platform-specific code, and error handling paths.
- **Critical Gaps**:
    1.  **UI Components Untested**: There is no testing for any of the ImGui-based UI components in the `ui/` directory.
    2.  **Platform-Specific Logic Untested**: The `UsnMonitor` on Windows and platform-specific file operations have no dedicated unit or integration tests.
    3.  **Concurrency Not Tested**: Thread safety in classes like `FileIndex` and `ThreadPool` is assumed but not verified with stress tests.
- **Recommended Strategy**: Focus on adding integration tests for platform-specific and UI-adjacent logic. Introduce concurrency stress tests for core data structures.

## Findings

### Critical
1.  **Component**: `src/ui/` directory
    -   **Issue Type**: Coverage Analysis / Missing Test Files
    -   **Gap Description**: The entire UI layer, which includes complex logic for rendering, state management, and user interaction, is completely untested.
    -   **Risk**: High risk of regressions in UI behavior. Bugs in UI logic (e.g., incorrect state transitions, mishandled user input) will not be caught by automated tests.
    -   **Suggested Action**: While direct unit testing of ImGui components is difficult, the logic within them can be tested. Refactor UI components to separate the logic (e.g., in `ApplicationLogic` or a dedicated UI state manager) from the rendering. The logic can then be unit-tested without a graphical environment.
    -   **Priority**: Critical
    -   **Effort**: L

2.  **Component**: `src/usn/UsnMonitor.cpp`
    -   **Issue Type**: Coverage Analysis / Missing Test Files
    -   **Gap Description**: The `UsnMonitor`, a critical, platform-specific, and high-privilege component, has no unit or integration tests. Its correctness is only verified through manual, end-to-end testing.
    -   **Risk**: Regressions in USN record parsing, volume handle management, or threading logic can lead to silent failures or crashes.
    -   **Suggested Action**: Create an integration test suite for `UsnMonitor`. This suite would need to run with elevated privileges and would involve creating a temporary file, making changes to it, and verifying that the `UsnMonitor` correctly captures and processes the corresponding USN records.
    -   **Priority**: Critical
    -   **Effort**: L

### High
1.  **Component**: `src/index/FileIndex.cpp`
    -   **Issue Type**: Threading & Concurrency Testing / Race Condition Tests
    -   **Gap Description**: `FileIndex` is designed to be thread-safe, with a `shared_mutex` protecting its data. However, there are no tests that specifically try to provoke race conditions by calling its methods concurrently from multiple threads.
    -   **Risk**: Subtle race conditions or deadlocks may exist that are not apparent in single-threaded testing.
    -   **Suggested Action**: Create a stress test that spawns multiple threads to perform simultaneous read (search) and write (add/remove file) operations on a single `FileIndex` instance. Enable TSAN (Thread Sanitizer) for this test target to automatically detect data races.
    -   **Priority**: High
    -   **Effort**: M

2.  **Component**: `tests/` directory
    -   **Issue Type**: Test Quality / Test Independence Issues
    -   **Gap Description**: Several test files (`FileIndexSearchStrategyTests.cpp`, etc.) create and delete files on the actual file system in the current working directory. This makes the tests dependent on the environment, slower, and potentially flaky if there are permission issues or leftover files from previous runs.
    -   **Risk**: Tests can fail for reasons unrelated to the code under test.
    -   **Suggested Action**: Abstract file system operations behind an interface (`IFileSystem`) and use a mock implementation (`MockFileSystem`) for tests. For integration tests that require a real file system, use a dedicated temporary directory that is reliably created and cleaned up before and after each test run.
    -   **Priority**: High
    -   **Effort**: M

### Medium
1.  **Component**: All test files.
    -   **Issue Type**: Test Organization / Duplicate Setup Code
    -   **Gap Description**: Many test files repeat the same boilerplate code to set up a `FileIndex` with a set of test files.
    -   **Risk**: High maintenance burden. A change to the `FileIndex` API requires updating dozens of locations.
    -   **Suggested Action**: Create a test fixture (e.g., `FileIndexTest`) or a test helper library (`TestIndexBuilder`) to encapsulate the creation and population of a `FileIndex` instance for tests.
    -   **Priority**: Medium
    -   **Effort**: S

2.  **Component**: `tests/GeminiApiUtilsTests.cpp`
    -   **Issue Type**: Mock & Stub Strategy / Isolation
    -   **Gap Description**: The tests for `GeminiApiUtils` do not mock the underlying HTTP client. They either test helper functions in isolation or would perform real network requests if the main function were called.
    -   **Risk**: Tests are slow, flaky (dependent on network), and cannot cover error cases like network timeouts or 500 server errors.
    -   **Suggested Action**: Abstract the HTTP client behind an `IHttpClient` interface. The production code will use a concrete `CurlHttpClient` or `WinHttpClient`, while tests can use a `MockHttpClient` that returns canned responses.
    -   **Priority**: Medium
    -   **Effort**: M

### Infrastructure Improvements
-   **Enable Sanitizers**: The CMake build should have options to easily enable ASAN (Address Sanitizer) and TSAN (Thread Sanitizer) for test builds. This is invaluable for catching memory errors and race conditions. The current build enables ASAN but not TSAN.
-   **CI Coverage Reports**: The CI pipeline should be configured to run `gcov`/`lcov` (or an equivalent) after the test suite runs and upload the coverage report (e.g., to Codecov or a similar service). This will make coverage gaps visible and track trends over time.
-   **Categorize Tests**: The `ctest` configuration should be updated to categorize tests into "unit", "integration", and "performance". This will allow developers to run faster subsets of tests locally.