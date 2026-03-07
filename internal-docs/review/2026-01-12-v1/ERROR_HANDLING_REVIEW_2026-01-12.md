# Error Handling Review - 2026-01-12

## Executive Summary
- **Reliability Score**: 5/10
- **Critical Gaps**: The application has several critical error handling gaps, including the potential for silent failures in core components and resource leaks on error paths. Exception safety is inconsistent.
- **Logging Improvements**: Logs often lack context (e.g., which file path caused an error). Critical failures are sometimes logged at the `INFO` level.
- **Recovery Recommendations**: The application lacks robust recovery mechanisms for transient errors, especially in file system operations.

## Findings

### Critical
1.  **Error Type**: Exception Safety / `catch(...)` swallowing exceptions
    -   **Location**: `src/usn/UsnMonitor.cpp` (in `ReaderThread` and `ProcessingThread`)
    -   **Failure Scenario**: An unknown or unexpected exception (e.g., `std::bad_alloc`, a third-party library exception) is thrown from within one of the core monitoring threads.
    -   **Current Behavior**: The `catch(...)` block catches the exception and logs "An unknown error occurred," but the thread is allowed to terminate silently. This leaves the `UsnMonitor` in a "zombie" state where it appears to be running but is no longer processing records, leading to a silent failure of the real-time monitoring feature.
    -   **Expected Behavior**: A critical, unrecoverable error in a core thread should at a minimum be logged as a `FATAL` error. Ideally, it should trigger a controlled shutdown of the application or, at the very least, clearly signal to the rest of the application that the monitor is no longer functional.
    -   **Fix**: Replace the generic `catch(...)` with more specific exception handlers. If a `catch(...)` must be used as a last resort, it should log a fatal error and call a function to gracefully shut down the application or the monitoring service.
    -   **Severity**: Critical. This can lead to silent data loss (missed real-time updates) with no indication to the user that something is wrong.

### High
1.  **Error Type**: Windows API Error Handling / Missing Checks
    -   **Location**: `src/platform/windows/PrivilegeUtils.cpp`
    -   **Failure Scenario**: A call to a Windows API function like `OpenProcessToken` or `AdjustTokenPrivileges` fails.
    -   **Current Behavior**: The functions in `PrivilegeUtils` check for API failures but return a simple `false`. The caller, `main_win.cpp`, ignores this return value and proceeds as if privileges were successfully adjusted. This could lead to the application failing later with less obvious permission-denied errors.
    -   **Current Behavior**: The return values are not checked, so the application continues as if the call succeeded.
    -   **Expected Behavior**: The return values of all privilege adjustment functions must be checked. A failure to acquire necessary privileges should be a fatal error that terminates the application with a clear error message.
    -   **Fix**: Add the `[[nodiscard]]` attribute to the functions in `PrivilegeUtils`. In `main_win.cpp`, check the return value and exit if the call fails.
    -   **Severity**: High. Failure to handle privilege errors can lead to non-obvious runtime failures.

2.  **Error Type**: Resource Cleanup / Handle Leaks
    -   **Location**: `src/usn/UsnMonitor.cpp`
    -   **Failure Scenario**: An error occurs in the `UsnMonitor::Initialize` method after the volume handle (`volume_handle_`) has been successfully opened but before the method completes.
    -   **Current Behavior**: The method returns `false`, but it does not close the already opened `volume_handle_`. The handle is only closed in the destructor. If the `UsnMonitor` object is not destroyed immediately after a failed initialization, the handle will be leaked until the object is eventually destroyed.
    -   **Expected Behavior**: If initialization fails at any point, all resources acquired up to that point should be released.
    -   **Fix**: Use a RAII wrapper for the `HANDLE` (e.g., a `std::unique_ptr` with a custom deleter or a dedicated `SafeHandle` class). Alternatively, add `CloseHandle(volume_handle_)` to all error-path `return false;` statements within the `Initialize` method.
    -   **Severity**: High. Handle leaks in a long-running service are a serious issue.

### Medium
1.  **Error Type**: Error Logging & Diagnostics / Logging Completeness
    -   **Location**: `src/crawler/FolderCrawler.cpp`
    -   **Failure Scenario**: The `FolderCrawler` encounters a directory it cannot access due to permissions.
    -   **Current Behavior**: The error is logged, but the log message is generic ("Failed to access directory"). It does not include the path of the directory that caused the error, making it difficult to debug permission issues on a user's system.
    -   **Expected Behavior**: The log message should include the specific path that could not be accessed.
    -   **Fix**: Update the log message to include the directory path.
      ```cpp
      // Before
      LOG_WARNING("Failed to access directory");

      // After
      LOG_WARNING("Failed to access directory: " << current_path);
      ```
    -   **Severity**: Medium. The lack of context in logs significantly increases debugging time.

2.  **Error Type**: Graceful Degradation / User Communication
    -   **Location**: `src/api/GeminiApiHttp_win.cpp`
    -   **Failure Scenario**: The application fails to connect to the Gemini API due to a network error or an invalid API key.
    -   **Current Behavior**: The error is logged to the console, but there is no feedback in the UI. The user sees a perpetual loading indicator in the AI-Assisted Search panel, with no explanation of what went wrong.
    -   **Expected Behavior**: The UI should display a clear and user-friendly error message (e.g., "Failed to connect to AI service. Please check your network connection and API key.")
    -   **Fix**: The `GeminiApiUtils` and the UI state need to be updated to store and display error information. The HTTP client should return a detailed error status, which gets propagated to the UI.
    -   **Severity**: Medium. Silent failures provide a poor user experience.

### Testing Suggestions
-   **Permission Errors**: Add tests that simulate running the application with insufficient privileges to access the USN Journal.
-   **File System Errors**: Add tests that simulate errors during file/directory enumeration (e.g., by using mock file system interfaces).
-   **Network Failures**: Add tests for the Gemini API integration that simulate network timeouts, DNS failures, and invalid API key responses.
-   **Exception Injection**: Use a testing framework to inject exceptions into various parts of the code to test the robustness of the `catch` blocks and RAII cleanup.