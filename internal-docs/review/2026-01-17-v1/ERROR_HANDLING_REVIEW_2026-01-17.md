# Error Handling Audit - 2026-01-17

## Executive Summary
- **Reliability Score**: 5/10
- **Critical Gaps**:
    -   Inconsistent error handling for `DeviceIoControl` failures.
    -   Swallowing unknown exceptions in worker threads.
- **Logging Improvements**:
    -   Log more context information for errors.
    -   Use a structured logging format.
- **Recovery Recommendations**:
    -   Implement exponential backoff for retries.
    -   Use a more sophisticated mechanism for handling transient errors.
- **Testing Suggestions**:
    -   Add tests for error handling paths.

## Findings

### Critical
-   **Inconsistent Error Handling for `DeviceIoControl` Failures**: The `ReaderThread` in `UsnMonitor.cpp` has inconsistent error handling for `DeviceIoControl` failures. Some errors are treated as transient and retried, while others are treated as fatal. There is no clear logic for determining which errors are which, and there is no exponential backoff for retries. This could lead to a situation where the application gets stuck in a tight loop, consuming CPU and spamming the log with error messages.
    -   **Location**: `src/usn/UsnMonitor.cpp`
    -   **Error Type**: Windows API Error Handling
    -   **Failure Scenario**: A transient error occurs when calling `DeviceIoControl`.
    -   **Current Behavior**: The application retries the operation immediately, which could lead to a tight loop.
    -   **Expected Behavior**: The application should use exponential backoff to retry the operation.
    -   **Fix**: Implement exponential backoff for retries.
    -   **Severity**: Critical

### High
-   **Swallowing Unknown Exceptions in Worker Threads**: The `ReaderThread` and `ProcessorThread` in `UsnMonitor.cpp` have `catch(...)` blocks that swallow unknown exceptions. While this prevents the application from crashing, it also means that important error information could be lost.
    -   **Location**: `src/usn/UsnMonitor.cpp`
    -   **Error Type**: Exception Safety
    -   **Failure Scenario**: An unknown exception is thrown in one of the worker threads.
    -   **Current Behavior**: The exception is caught and ignored.
    -   **Expected Behavior**: The exception should be logged with as much context as possible.
    -   **Fix**: Log the exception and consider terminating the application.
    -   **Severity**: High

### Medium
-   **Missing Context in Log Messages**: Some log messages are missing important context, such as error codes and file paths. This makes it difficult to debug issues.
    -   **Location**: Throughout the codebase
    -   **Error Type**: Error Logging & Diagnostics
    -   **Failure Scenario**: An error occurs.
    -   **Current Behavior**: The error is logged with a generic message.
    -   **Expected Behavior**: The error should be logged with a detailed message that includes all relevant context.
    -   **Fix**: Add more context to log messages.
    -   **Severity**: Medium
