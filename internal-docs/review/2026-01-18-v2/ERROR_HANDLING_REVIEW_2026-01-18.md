# Error Handling Review - 2026-01-18

## Executive Summary
- **Reliability Score**: 4/10
- **Critical Issues**: 1
- **High Issues**: 3
- **Total Findings**: 10
- **Estimated Remediation Effort**: 10-15 hours

## Findings

### Critical
1.  **Swallowed Exceptions in Core Monitoring Threads**
    *   **Location**: `src/usn/UsnMonitor.cpp`, `src/core/Application.cpp`
    *   **Error Type**: Exception Safety / Logging Completeness
    *   **Failure Scenario**: An unknown or unexpected exception (e.g., `std::bad_alloc`, a third-party library exception) is thrown from within the main loop of `UsnMonitor` or `Application::Run`.
    *   **Current Behavior**: The top-level `catch(...)` block catches the exception but only logs a generic error message. The thread or application then terminates without attempting a graceful shutdown or providing any diagnostic information (like a stack trace). This can lead to silent, unrecoverable failures.
    *   **Expected Behavior**: The application should attempt to capture a stack trace, log detailed diagnostic information, save any pending data, and then terminate gracefully. For a long-running service, it should ideally be restarted by a watchdog process.
    *   **Fix**:
        -   Implement a platform-specific mechanism to capture stack traces on unhandled exceptions.
        -   The top-level exception handler should call this mechanism to log as much information as possible before terminating.
        -   Ensure that critical state (like settings) is saved before exiting.
    *   **Severity**: Critical

### High
1.  **Missing `GetLastError()` Capture After Windows API Failures**
    *   **Location**: `src/platform/windows/FileOperations_win.cpp`, `src/usn/UsnMonitor.cpp`
    *   **Error Type**: Windows API Error Handling
    *   **Failure Scenario**: A Windows API function like `CreateFile` or `DeviceIoControl` fails and returns `INVALID_HANDLE_VALUE` or `FALSE`.
    *   **Current Behavior**: The code checks the return value, but often logs a generic "failed to..." message without immediately calling `GetLastError()` to retrieve and log the specific reason for the failure. This makes debugging incredibly difficult.
    *   **Expected Behavior**: Immediately after a failing API call, `GetLastError()` should be called to get the Win32 error code, which should then be formatted into a human-readable string and included in the log message.
    *   **Fix**: Create a helper function that takes a Windows API error code, formats it using `FormatMessage`, and logs it. Call this helper in all error paths for Windows API calls.
    *   **Severity**: High

2.  **No Graceful Degradation for Indexing Failures**
    *   **Location**: `src/crawler/FolderCrawler.cpp`
    *   **Error Type**: Graceful Degradation
    *   **Failure Scenario**: The `FolderCrawler` encounters a single file or directory that it cannot access due to a permissions error.
    *   **Current Behavior**: The error is logged, but the entire indexing process for that subdirectory may halt, leading to an incomplete index.
    *   **Expected Behavior**: The crawler should skip the inaccessible file/directory, log the error with the specific path, and continue indexing the rest of the file system. The application should maintain a list of skipped paths to show to the user.
    *   **Fix**: Wrap the file/directory processing logic in a `try-catch` block and, on failure, log the error and continue the loop.
    *   **Severity**: High

3.  **Raw `HANDLE`s without RAII Wrappers**
    *   **Location**: `src/usn/UsnMonitor.cpp`
    *   **Error Type**: RAII Completeness
    *   **Failure Scenario**: An exception is thrown after a `HANDLE` is opened but before the corresponding `CloseHandle` is called.
    *   **Current Behavior**: The `CloseHandle` call is skipped, and the handle is leaked. This can lead to resource exhaustion over time.
    *   **Expected Behavior**: The `HANDLE` should be wrapped in an RAII class (like `std::unique_ptr` with a custom deleter or a dedicated `ScopedHandle` class) that automatically calls `CloseHandle` in its destructor, ensuring that the handle is closed even if an exception is thrown.
    *   **Fix**: Implement a `ScopedHandle` class and use it for all Windows `HANDLE`s.
    *   **Severity**: High

### Medium
1.  **Inconsistent Error Propagation**
    *   **Location**: Across the codebase.
    *   **Error Type**: Error Propagation
    *   **Failure Scenario**: A low-level function fails.
    *   **Current Behavior**: Some functions return `bool`, others return `HRESULT`, and some throw exceptions. There is no consistent strategy, which makes the code harder to reason about. Low-level error details (like the original Win32 error code) are often lost.
    *   **Expected Behavior**: A consistent error handling strategy should be used. For example, use exceptions for exceptional circumstances and return `std::optional` or a `Result` type for expected failures. Error types should preserve the original error code and context.
    *   **Fix**: Refactor the error handling to use a consistent strategy. A good candidate would be a custom `Error` class that can store an error code, a message, and a stack trace.
    *   **Severity**: Medium

2.  **Log Messages Missing Context**
    *   **Location**: Across the codebase.
    *   **Error Type**: Logging Completeness
    *   **Failure Scenario**: An error occurs when processing a file.
    *   **Current Behavior**: A log message might say "Failed to open file", but not include the file path or the error code.
    *   **Expected Behavior**: All log messages related to errors should include as much context as possible, such as file paths, user input, and relevant state variables.
    *   **Fix**: Review all logging statements and add the necessary context.
    *   **Severity**: Medium

3.  **No Automatic Retries for Transient Errors**
    *   **Location**: `src/usn/UsnMonitor.cpp`
    *   **Error Type**: Recovery Strategies
    *   **Failure Scenario**: Access to the USN Journal fails due to a temporary condition, such as the volume being briefly unavailable.
    *   **Current Behavior**: The operation fails, and the monitor may stop.
    *   **Expected Behavior**: The application should attempt to retry the operation a few times with an exponential backoff before giving up.
    *   **Fix**: Implement a retry mechanism for critical operations that can fail due to transient conditions.
    *   **Severity**: Medium

### Low
1.  **Destructors That Might Throw**
    *   **Location**: `src/core/Application.cpp`
    *   **Error Type**: Exception Safety
    *   **Failure Scenario**: The `Application` destructor calls `index_builder_->Stop()`, which could potentially throw an exception.
    *   **Current Behavior**: If `Stop()` throws, the exception would propagate out of the destructor, leading to `std::terminate`.
    *   **Expected Behavior**: Destructors should be `noexcept`. Any operations that can fail should be handled within the destructor and logged.
    *   **Fix**: Mark the destructor `noexcept` and wrap the call to `Stop()` in a `try-catch` block.
    *   **Severity**: Low

2.  **Empty `catch` Blocks**
    *   **Location**: `tests/` directory.
    *   **Error Type**: Exception Path Testing
    *   **Failure Scenario**: An exception is thrown in a test.
    *   **Current Behavior**: Some tests have empty `catch` blocks, which can hide legitimate failures.
    *   **Expected Behavior**: Test failures should be reported. If an exception is expected, it should be caught and asserted on.
    *   **Fix**: Replace empty `catch` blocks with `FAIL()` macros or specific exception assertions.
    *   **Severity**: Low

3.  **Error Messages Not User-Friendly**
    *   **Location**: `src/ui/Popups.cpp`
    *   **Error Type**: User Communication
    *   **Failure Scenario**: An error is displayed to the user.
    *   **Current Behavior**: The error messages are often technical and not helpful to a non-technical user (e.g., "Failed with error 0x80070005").
    *   **Expected Behavior**: Error messages should be clear, concise, and provide actionable advice to the user.
    *   **Fix**: Create a mapping of common error codes to user-friendly messages.
    *   **Severity**: Low

## Summary Requirements
- **Reliability Score**: 4/10. The application lacks robustness in several key areas. The swallowed exceptions in core threads are a critical issue that could lead to silent failures in production. The error handling for Windows APIs is inadequate, making the application difficult to debug.
- **Critical Gaps**:
    -   Silent failures due to swallowed exceptions.
    -   Lack of RAII for Windows handles.
    -   No graceful degradation for partial indexing failures.
- **Logging Improvements**:
    -   Always log the specific error code (`GetLastError()`) for failed Windows API calls.
    -   Include context (file paths, etc.) in all error messages.
    -   Capture and log a stack trace on unhandled exceptions.
- **Recovery Recommendations**:
    -   Implement a retry mechanism with exponential backoff for transient errors.
    -   For the `UsnMonitor`, if the volume handle is lost, the application should periodically try to re-establish it.
- **Testing Suggestions**:
    -   Add tests that simulate Windows API failures to verify that error paths are handled correctly.
    -   Add tests for file system errors (e.g., permission denied) to ensure the crawler can recover gracefully.
    -   Use fault injection to test exception safety.
