# Error Handling Review - 2026-01-21

## Executive Summary
- **Reliability Score**: 3/10
- **Critical Gaps**: Swallowed exceptions in core monitoring threads, missing RAII for critical resources.
- **Logging Improvements**: Error logs lack sufficient context (e.g., file paths, Win32 error codes) to be actionable.

The application's error handling is dangerously inadequate for a long-running system process. The most critical issue is the use of `catch(...)` blocks in core threads that swallow unknown exceptions, potentially leading to silent, unrecoverable failures. Resource management is inconsistent, with a mix of manual `CloseHandle` calls and a lack of comprehensive RAII, creating a high risk of handle and memory leaks on error paths.

## Findings

### Critical
1.  **Swallowed Exceptions in Core Monitoring Threads**
    - **Location:** `src/usn/UsnMonitor.cpp`
    - **Error Type:** Exception Safety / `catch(...)` swallowing exceptions
    - **Failure Scenario:** An unexpected C++ exception (e.g., `std::bad_alloc`) or a structured exception (e.g., access violation) is thrown from within the `UsnMonitor`'s main loop.
    - **Current Behavior:** The `catch(...)` block catches the exception, logs a generic "Unknown error" message, and the thread continues or terminates, potentially leaving the index in a corrupt state without any detailed diagnostic information.
    - **Expected Behavior:** The exception should be caught, logged with as much detail as possible (including a stack trace if possible), and the application should enter a safe shutdown state. The user should be notified of the critical failure.
    - **Fix:** Replace `catch(...)` with specific catch blocks for known exception types. If a catch-all is necessary, it should log detailed diagnostics and terminate the application gracefully.
    - **Severity:** Critical

2.  **Missing RAII for `HANDLE` in `UsnMonitor`**
    - **Location:** `src/usn/UsnMonitor.h`
    - **Error Type:** Resource Cleanup / Handle Leaks
    - **Failure Scenario:** An exception is thrown in the `UsnMonitor` constructor after the volume handle (`hVolume_`) has been successfully opened but before the constructor completes.
    - **Current Behavior:** The `UsnMonitor` destructor is never called, so `CloseHandle(hVolume_)` is never invoked, leading to a handle leak.
    - **Expected Behavior:** The volume handle should be automatically closed when the `UsnMonitor` object goes out of scope, regardless of how the scope is exited (normal return or exception).
    - **Fix:** Wrap the `HANDLE` in a dedicated RAII class (e.g., `ScopedHandle`) that calls `CloseHandle` in its destructor.
    - **Severity:** Critical

### High
1.  **Inconsistent `GetLastError()` Usage**
    - **Location:** `src/platform/windows/FileOperations_win.cpp` and other Windows-specific files.
    - **Error Type:** Windows API Error Handling / Missing Checks
    - **Failure Scenario:** A Windows API function like `CreateFile` fails and returns `INVALID_HANDLE_VALUE`. The calling code checks for the invalid handle but fails to immediately call `GetLastError()` to retrieve the specific reason for the failure.
    - **Current Behavior:** The application knows that the operation failed but has no information about why (e.g., "Access Denied" vs. "File Not Found"). Error logs are generic and not actionable.
    - **Expected Behavior:** Immediately after a failed API call, `GetLastError()` should be called to retrieve the Win32 error code. This code should be converted to a human-readable string and included in the log message.
    - **Fix:** Create a utility function that wraps Windows API calls, checks for failure, and throws an exception containing the formatted error message from `GetLastError()`.
    - **Severity:** High

2.  **Silent Failures in Search**
    - **Location:** `src/search/ParallelSearchEngine.cpp`
    - **Error Type:** Graceful Degradation / Silent Failures
    - **Failure Scenario:** A worker thread in the `ParallelSearchEngine` encounters an unexpected error (e.g., a filesystem error when trying to access a file's attributes).
    - **Current Behavior:** The error is not effectively propagated. The search might return partial or no results without any indication to the user that a problem occurred.
    - **Expected Behavior:** The error should be caught within the worker thread, packaged, and propagated back to the main thread. The `future` should contain the exception, and the calling code should handle it, log the error, and potentially notify the user.
    - **Fix:** Use `std::promise` and `std::future` to propagate exceptions from worker threads. The main thread should have a `try-catch` block around `future.get()` to handle exceptions from the search tasks.
    - **Severity:** High

## Recovery Recommendations
1.  **Implement an application-wide unhandled exception filter** using `SetUnhandledExceptionFilter` on Windows to log crashes and generate minidumps for post-mortem debugging.
2.  For transient file system errors (e.g., sharing violations), implement a **retry mechanism with exponential backoff** in the file operation utilities.
3.  The `UsnMonitor` should have a **reconnection logic** to attempt to re-open the volume handle if it becomes invalid (e.g., due to a volume being dismounted).

## Testing Suggestions
1.  **Create unit tests that simulate Windows API failures** using mocking or dependency injection to verify that error handling paths are correctly executed.
2.  **Add tests that throw exceptions** from within search worker threads to ensure they are correctly propagated and handled.
3.  **Develop a test that rapidly mounts and unmounts a test volume** to verify the robustness of the `UsnMonitor`'s handle management and recovery logic.
