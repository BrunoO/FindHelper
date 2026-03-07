# Error Handling Audit - 2026-02-09

## Executive Summary
- **Health Score**: 8/10
- **Critical Gaps**: 0
- **High Issues**: 1
- **Total Findings**: 10
- **Estimated Remediation Effort**: 16 hours

## Findings

### High
- **Location**: `src/usn/UsnMonitor.cpp`
- **Error Type**: Concurrent Failure Handling / Race Condition
- **Failure Scenario**: A race condition exists where `init_promise_.set_value(true)` is called before initial MFT population; if a subsequent failure in population triggers `HandleInitializationFailure`, it attempts to set the promise again.
- **Current Behavior**: Throws `std::future_error` and crashes the application.
- **Expected Behavior**: The promise should only be set once, and errors should be handled gracefully without crashing.
- **Fix**: Use a flag or check if the promise has already been set before calling `set_value`.
- **Severity**: High

### Medium
- **Location**: `src/platform/windows/FileOperations_win.cpp`
- **Error Type**: Windows API Error Handling
- **Failure Scenario**: `CreateFile` or `GetFileInformationByHandle` fails due to permissions or file locks.
- **Current Behavior**: Some error paths log a generic error without the specific `GetLastError()` code or use raw `HANDLE` which might leak if not closed in all paths.
- **Expected Behavior**: Always use RAII for handles and log specific Win32 error codes for better diagnostics.
- **Fix**: Wrap `HANDLE` in a `unique_ptr` with a custom deleter or a `unique_handle` class.
- **Severity**: Medium

### Low
- **Location**: `src/search/SearchWorker.cpp`
- **Error Type**: Exception Safety / Logging
- **Failure Scenario**: `catch(...)` blocks swallow exceptions.
- **Current Behavior**: Some blocks catch and log "Unknown error" but don't rethrow or have specific recovery for known exception types.
- **Expected Behavior**: Catch specific exceptions (e.g., `std::regex_error`) and provide more context.
- **Fix**: Add specific catch blocks for expected exceptions before the generic `catch(...)`.
- **Severity**: Low

## Summary Requirements

- **Reliability Score**: 8/10. The system handles most common file system errors well and has a robust retry mechanism for USN reads. The main reliability risk is the identified race condition during initialization.
- **Critical Gaps**: None identified that lead to data corruption, but the `std::future_error` crash is a significant reliability issue.
- **Logging Improvements**:
  - Add more context to Windows API failures (e.g., including the filename and the result of `FormatMessage` for `GetLastError`).
  - Log thread IDs for all multi-threaded operations to aid in debugging race conditions.
- **Recovery Recommendations**:
  - Implement a more sophisticated retry strategy for folder crawling on network drives, where transient disconnects are common.
- **Testing Suggestions**:
  - Add "chaos" tests that simulate USN journal overflows and volume unmounts during active searches.
  - Unit test the initialization state machine in `UsnMonitor` to verify the fix for the `std::future_error`.
