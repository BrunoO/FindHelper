# Test Strategy Review - 2026-01-05

## Executive Summary
- **Test Health Score**: 6/10
- **Coverage Estimate**: ~70% line coverage, ~50% branch coverage.
- **Critical Gaps**:
    -   Lack of integration tests for file system operations.
    -   No concurrency testing for thread-safe classes.
    -   Platform-specific code is largely untested.
- **Flakiness Assessment**: The test suite appears to be stable, with no obvious signs of flakiness.
- **Recommended Testing Strategy**:
    -   **Unit Tests**: Continue to write unit tests for all new business logic.
    -   **Integration Tests**: Add a new suite of integration tests that interact with the real file system.
    -   **Concurrency Tests**: Create a separate suite of stress tests for the multi-threaded components.

## Findings

### 1. Coverage Analysis

-   **Component:** `src/platform/`
-   **Issue Type:** Missing Test Files
-   **Gap Description:** The platform-specific code in the `platform` directory (e.g., `FileOperations_win.cpp`, `FontUtils_linux.cpp`) is almost entirely untested.
-   **Risk:** Platform-specific bugs could be missed, leading to a degraded user experience on Windows, macOS, or Linux.
-   **Suggested Action:** Create a new suite of integration tests that run on each platform and verify the correctness of the platform-specific code.
-   **Priority:** High
-   **Effort:** L (> 4hrs)

-   **Component:** `src/index/FileIndex.cpp`
-   **Issue Type:** Coverage Gaps by Type
-   **Gap Description:** The error handling paths in the `FileIndex` class are not well-tested. For example, there are no tests for how the system behaves when it encounters a file it doesn't have permission to read.
-   **Risk:** Unhandled errors could lead to crashes or data corruption.
-   **Suggested Action:** Add unit tests that use mocking to simulate file system errors and verify that the `FileIndex` class handles them gracefully.
-   **Priority:** High
-   **Effort:** M (1-4hrs)

### 2. Missing Test Types

-   **Component:** `src/index/FileIndex.cpp`, `src/search/SearchWorker.cpp`
-   **Issue Type:** Missing Concurrency Tests
-   **Gap Description:** The thread-safe classes are not tested under concurrent load. The existing tests are single-threaded and do not expose potential data races or deadlocks.
-   **Risk:** Concurrency-related bugs are notoriously difficult to reproduce and debug in production.
-   **Suggested Action:** Create a new suite of stress tests that spawn multiple threads and hammer on the `FileIndex` and `SearchWorker` classes. Use the Thread Sanitizer (TSan) to detect data races.
-   **Priority:** Critical
-   **Effort:** L (> 4hrs)

-   **Component:** `src/crawler/FolderCrawler.cpp`
-   **Issue Type:** Missing Integration Tests
-   **Gap Description:** There are no integration tests that verify the `FolderCrawler`'s interaction with the real file system. All the current tests use mocks.
-   **Risk:** The mocks may not accurately represent the behavior of the real file system, leading to bugs that are only discovered in production.
-   **Suggested Action:** Create a suite of integration tests that run against a temporary directory on the file system. These tests would create a known set of files and directories and then verify that the `FolderCrawler` indexes them correctly.
-   **Priority:** High
-   **Effort:** M (1-4hrs)

### 3. Test Quality

-   **Component:** `tests/FileIndexSearchStrategyTests.cpp`
-   **Issue Type:** Test Smell Detection
-   **Gap Description:** Some of the tests in this file are very long and contain multiple, unrelated assertions. This makes them difficult to read and maintain.
-   **Risk:** When a test fails, it's hard to determine which assertion failed and what the root cause is.
-   **Suggested Action:** Refactor these large tests into smaller, more focused tests, each with a single, clear purpose.
-   **Priority:** Low
-   **Effort:** S (< 1hr)

### 4. Test Infrastructure

-   **Component:** CMake build system
-   **Issue Type:** Build Integration
-   **Gap Description:** The Address Sanitizer (ASan) and Thread Sanitizer (TSan) are available but not enabled by default in the CMake build.
-   **Risk:** Memory errors and data races may go undetected.
-   **Suggested Action:** Enable ASan and TSan by default for all test builds. Provide a CMake option to disable them if necessary.
-   -**Priority:** Medium
-   **Effort:** S (< 1hr)
