# Error Handling Review - 2026-01-11

## Executive Summary
- **Health Score**: 2/10
- **Critical Issues**: 1
- **High Issues**: 2
- **Total Findings**: 3
- **Estimated Remediation Effort**: 4 hours

## Findings

### Critical
- **Swallowing Unknown Exceptions in Threads**
  - **Location**: `src/usn/UsnMonitor.cpp`, `ReaderThread` and `ProcessorThread` methods
  - **Error Type**: Exception Safety
  - **Failure Scenario**: An unexpected, non-standard exception (e.g., a structured exception from a driver or a hardware fault) is thrown in one of the monitoring threads.
  - **Current Behavior**: The `catch(...)` block catches the exception and logs a generic "Unknown exception" message. The thread then terminates, but the `UsnMonitor` and the rest of the application are not notified of the failure. The application continues to run in a degraded state where real-time file system updates are no longer being received.
  - **Expected Behavior**: The application should have a global unhandled exception handler that logs the error and terminates the application. A silent failure of a critical background thread is a serious reliability issue.
  - **Fix**: Implement a top-level exception handler in `main_common.h` that ensures any unhandled exception is logged and the application exits. Remove the `catch(...)` blocks from the `ReaderThread` and `ProcessorThread` to allow exceptions to propagate to the global handler.
  - **Severity**: Critical

### High
- **No Recovery from Fatal Monitoring Errors**
  - **Location**: `src/usn/UsnMonitor.cpp`, `ReaderThread` method
  - **Error Type**: Graceful Degradation
  - **Failure Scenario**: The `ReaderThread` encounters a fatal error, such as `ERROR_JOURNAL_DELETE_IN_PROGRESS`, or exceeds the maximum number of consecutive errors.
  - **Current Behavior**: The `ReaderThread` logs the error and exits, setting `monitoring_active_` to `false`. The application continues to run, but the UI does not inform the user that real-time monitoring has stopped.
  - **Expected Behavior**: The `UsnMonitor` should expose a "health" status that the UI can check. If the monitor fails, the UI should display a prominent, non-dismissible warning to the user indicating that real-time updates are off and a restart is required.
  - **Fix**:
    1.  Add an enum `MonitoringStatus { OK, DEGRADED, FAILED }` to `UsnMonitor`.
    2.  Update the status to `FAILED` when the `ReaderThread` exits due to an unrecoverable error.
    3.  In the main UI loop, check this status and render a warning banner if it's not `OK`.
  - **Severity**: High

- **Potential `VolumeHandle` Leak in Destructor**
  - **Location**: `src/usn/UsnMonitor.h`, `VolumeHandle::~VolumeHandle()`
  - **Error Type**: Resource Cleanup
  - **Failure Scenario**: The `CloseHandle` function in the `VolumeHandle` destructor throws an exception.
  - **Current Behavior**: The `catch(...)` block in the destructor swallows the exception to prevent the application from terminating. However, it does not log the error, so the failure to close the handle is silent.
  - **Expected Behavior**: While destructors should not throw, the failure to close a handle is a serious issue that should be logged.
  - **Fix**: Add logging to the `catch(...)` block in the `VolumeHandle` destructor to record the failure.
    ```cpp
    ~VolumeHandle() {
        if (handle_ != INVALID_HANDLE_VALUE) {
            try {
                if (!CloseHandle(handle_)) {
                    // Log the error
                }
            } catch (...) {
                // Log the error
            }
        }
    }
    ```
  - **Severity**: High

## Reliability Score: 2/10
The current error handling strategy in the `UsnMonitor` has critical gaps that can lead to silent failures and leave the application in a degraded, non-functional state without informing the user. The swallowing of unknown exceptions in critical background threads is a particularly severe issue.

## Critical Gaps
- The lack of a global unhandled exception handler is a major reliability risk.
- The failure to notify the user of a permanent loss of real-time monitoring functionality is a critical flaw in the user experience and reliability of the application.

## Logging Improvements
- All `catch(...)` blocks should be replaced with specific exception handlers that log detailed context about the error.
- Failures to close handles or release other resources should always be logged.

## Recovery Recommendations
- For unrecoverable errors in the `UsnMonitor`, the application should clearly communicate the failure to the user and suggest a restart.
- For transient errors, the retry logic in the `ReaderThread` is a good start, but it should be augmented with exponential backoff to avoid busy-waiting.
