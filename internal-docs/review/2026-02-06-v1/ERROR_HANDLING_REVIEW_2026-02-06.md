# Error Handling Review - 2026-02-06

## Executive Summary
- **Health Score**: 7/10
- **Critical Issues**: 0
- **High Issues**: 2
- **Total Findings**: 7
- **Estimated Remediation Effort**: 6 hours

## Findings

### High
1. **Silent Failures in USN Initialization**
   - **Location**: `src/usn/UsnMonitor.cpp` - `Start()`
   - **Error Type**: Windows API Error Handling
   - **Failure Scenario**: `OpenVolumeAndQueryJournal` fails due to insufficient permissions or unsupported filesystem.
   - **Current Behavior**: Returns `false`, but doesn't always log the specific `GetLastError()` reason or communicate the specific failure to the UI (user just sees "Index ready - 0 entries").
   - **Expected Behavior**: Log specific Win32 error code and description. Surface the failure reason (e.g., "Access Denied", "Not an NTFS volume") to the UI status bar.
   - **Fix**: Capture `GetLastError()` immediately and use a helper to log and store the error string.
   - **Severity**: High

2. **Uncaught Exceptions in Search Futures**
   - **Location**: `src/search/SearchWorker.cpp` - `ProcessSearchFutures`
   - **Error Type**: Exception Safety
   - **Failure Scenario**: `search_futures[future_idx].get()` throws an exception (e.g., `std::bad_alloc` or custom search exception).
   - **Current Behavior**: The current `ProcessSearchFutures` (non-streaming) doesn't have a try-catch block inside the loop. An exception from one future will propagate out and potentially terminate the worker thread.
   - **Expected Behavior**: Catch exceptions per-future, log them, and allow other futures to complete if possible, or mark the entire search as failed gracefully.
   - **Fix**: Wrap `.get()` call in a try-catch block similar to `ProcessOneReadyFutureIfReady`.
   - **Severity**: High

### Medium
1. **Inconsistent Error Translation**
   - **Location**: Platform boundaries (e.g., `FileOperations_linux.cpp` vs `FileOperations_win.cpp`)
   - **Error Type**: Cross-Platform Error Patterns
   - **Failure Scenario**: A file operation fails on Linux vs Windows.
   - **Current Behavior**: Windows code uses `GetLastError()` and logs Win32 codes. Linux code uses `errno`. The UI receives different error formats.
   - **Expected Behavior**: A unified error structure that abstracts platform-specific codes while preserving them for logs.
   - **Fix**: Create an `ErrorContext` struct that holds a platform-agnostic category and a platform-specific code/message.
   - **Severity**: Medium

2. **Resource Leaks on Thread Spawn Failure**
   - **Location**: `SearchThreadPool.cpp`
   - **Error Type**: Resource Cleanup
   - **Failure Scenario**: `std::thread` constructor throws `std::system_error` (e.g., resource limit reached).
   - **Current Behavior**: Some already-allocated resources might not be cleaned up if the constructor is interrupted.
   - **Expected Behavior**: All allocated thread objects or synchronization primitives should be cleaned up.
   - **Fix**: Use `vector::emplace_back` and ensure destructor handles partial initialization.
   - **Severity**: Medium

### Low
1. **Missing [[nodiscard]] on UpdateSize**
   - **Location**: `src/index/FileIndex.h:139`
   - **Error Type**: Exception Path Testing
   - **Failure Scenario**: Calling code ignores the return value of `UpdateSize`.
   - **Current Behavior**: If `UpdateSize` fails (e.g., file deleted), the caller doesn't know and might assume the size was updated.
   - **Expected Behavior**: Return value should be checked to handle "File not found" or "Access denied" scenarios.
   - **Fix**: Add `[[nodiscard]]`.
   - **Severity**: Low

## Quick Wins
1. **Wrap search future `.get()` in try-catch**: Prevent worker thread termination on search failures.
2. **Log `GetLastError()` on USN failures**: Immediate improvement to troubleshooting index issues.
3. **Add `[[nodiscard]]` to core index methods**: Enforce error checking at compile time.

## Recommended Actions
1. **Standardize error logging**: Create a helper like `LOG_WIN32_ERROR(context)` that automatically formats `GetLastError()`.
2. **Improve UI error reporting**: Pass specific failure reasons from the indexer to `GuiState::index_build_status_text`.
3. **Audit all `.get()` calls on futures**: Ensure every background task completion is wrapped in proper error handling.

## Reliability Score: 7/10
The application handles normal operations well, but its error-recovery paths are less mature. The "God Object" nature of `Application` and `FileIndex` makes it hard to implement localized recovery without affecting the entire system state. Switching to a more decentralized, component-based error handling model would improve reliability.
