# Error Handling Audit - 2026-02-20

## Executive Summary
- **Reliability Score**: 6/10
- **Critical Gaps**: 1
- **High Issues**: 2
- **Total Findings**: 8
- **Estimated Remediation Effort**: 24 hours

## Findings

### 1. Missing Exception Handling in `WorkerThread`
- **Location**: `src/search/SearchWorker.cpp:607`, `SearchWorker::WorkerThread()`
- **Error Type**: Exception Safety
- **Failure Scenario**: Any exception thrown during search execution (e.g., `std::bad_alloc` during result vector growth, or regex compilation error in `ExecuteSearch`).
- **Current Behavior**: The background thread terminates with an unhandled exception, causing the entire application to crash (`std::terminate`).
- **Expected Behavior**: Exceptions should be caught, logged, and the worker state reset so it can handle subsequent search requests. The error should be surfaced to the UI.
- **Fix**: Wrap `ExecuteSearch(params)` in a `try-catch` block.
- **Severity**: Critical

### 2. Inconsistent Windows API Error Checking
- **Location**: `src/usn/UsnMonitor.cpp`
- **Error Type**: Windows API Error Handling
- **Failure Scenario**: `DeviceIoControl` or `ReadDirectoryChangesW` failing due to transient volume issues.
- **Current Behavior**: Some calls check return values but don't always capture `GetLastError()` immediately or log enough context.
- **Expected Behavior**: Every Windows API call should be checked, and on failure, `GetLastError()` should be logged along with the operation name and relevant parameters (e.g., volume path).
- **Fix**: Use a helper macro or function for Windows API calls that automatically logs `GetLastError()` on failure.
- **Severity**: High

### 3. Potential for Silently Dropped Search Results
- **Location**: `src/search/SearchWorker.cpp:635`
- **Error Type**: Graceful Degradation
- **Failure Scenario**: A new search is requested while the worker is finalizing the previous search.
- **Current Behavior**: The results of the current search might be silently discarded if `search_requested_` is true when the lock is acquired.
- **Expected Behavior**: Clearer state transition logic to ensure results are either delivered or explicitly cancelled.
- **Fix**: Refine the state machine in `SearchWorker`.
- **Severity**: Medium

### 4. Raw Handle usage in `UsnMonitor`
- **Location**: `src/usn/UsnMonitor.cpp`
- **Error Type**: Resource Cleanup
- **Failure Scenario**: Exception thrown between `CreateFileW` and wrapping the handle.
- **Current Behavior**: Leak of the volume handle.
- **Expected Behavior**: Immediate wrapping in `ScopedHandle`.
- **Fix**: Use `ScopedHandle` for all volume and event handles.
- **Severity**: Medium

## Summary

- **Reliability Score**: 6/10. While the core logic is robust, the lack of top-level exception handling in background threads is a significant risk.
- **Critical Gaps**: Unhandled exceptions in `SearchWorker::WorkerThread` and potentially other background threads (e.g., `UsnMonitor` threads) can crash the application.
- **Logging Improvements**:
  - Log `GetLastError()` for all Windows API failures.
  - Include more context in search error logs (e.g., which filter failed).
- **Recovery Recommendations**:
  - Implement a retry mechanism for transient USN journal read failures.
  - Ensure background threads can recover from exceptions and stay alive.
- **Testing Suggestions**:
  - Add "chaos tests" that simulate file system errors during active searches.
  - Unit test search with invalid regex patterns to ensure they don't crash.
