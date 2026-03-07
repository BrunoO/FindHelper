# Error Handling Review - 2026-01-31

## Executive Summary
- **Health Score**: 9/10
- **Critical Issues**: 0
- **High Issues**: 0
- **Total Findings**: 4
- **Estimated Remediation Effort**: 4 hours

## Findings

### Medium

#### 1. Potential for Inconsistent State on Partial USN Processing
- **Location**: `src/usn/UsnMonitor.cpp` - `ProcessorThread`
- **Failure Scenario**: If an individual USN record in a buffer is malformed, the loop logs a warning and breaks, skipping the rest of the buffer.
- **Current Behavior**: The loop breaks, potentially skipping valid records following the malformed one.
- **Expected Behavior**: Attempt to skip only the malformed record by using the reported `RecordLength` if it's within sane bounds, or clear the index and re-populate if corruption is detected.
- **Fix**: Add a more sophisticated recovery logic that attempts to find the next valid record header in the buffer if one record fails validation.
- **Severity**: Medium

### Low

#### 2. Silent Failures in IsProcessElevated
- **Location**: `src/platform/windows/ShellContextUtils.cpp:138`
- **Failure Scenario**: If `OpenProcessToken` or `GetTokenInformation` fails for any reason other than normal operation.
- **Current Behavior**: Returns `false` and logs a warning.
- **Expected Behavior**: Normal for a query function, but could lead to confusing behavior if the user *is* elevated but the check fails.
- **Fix**: Improve error reporting to the user if elevation check fails unexpectedly.
- **Severity**: Low

#### 3. Redundant Catch Blocks in LazyAttributeLoader
- **Location**: `src/index/LazyAttributeLoader.cpp:380-424`
- **Failure Scenario**: Logging statistics during shutdown.
- **Current Behavior**: 6 different catch blocks doing almost the same thing (logging the error and swallowing it).
- **Expected Behavior**: This is safe, but verbose and repetitive.
- **Fix**: Consolidate into a single `catch(const std::exception&)` followed by `catch(...)` to reduce code duplication while maintaining the same safety guarantees.
- **Severity**: Low

#### 4. Missing retry for transient SHFileOperationW failures
- **Location**: `src/platform/windows/FileOperations_win.cpp:166`
- **Failure Scenario**: Deleting a file to Recycle Bin while another process (like an anti-virus or search indexer) has a temporary lock on it.
- **Current Behavior**: Logs an error and returns `false`.
- **Expected Behavior**: For a user-triggered action, a single retry or a short wait might improve the success rate for transient locks.
- **Fix**: Implement a simple retry loop (e.g., 3 attempts with 50ms delay) for common transient error codes.
- **Severity**: Low

## Summary Requirements

- **Reliability Score**: 9/10. The application demonstrates high reliability through consistent use of RAII, meticulous Windows API error checking, and defensive programming for shutdown scenarios.
- **Critical Gaps**: No critical gaps identified that would lead to data loss or unrecoverable crashes.
- **Logging Improvements**:
  1. Add more context to `ProcessorThread` warnings (e.g., which record in the buffer failed).
  2. Differentiate between "expected" errors (e.g., user cancelled elevation) and "unexpected" system errors in logs.
- **Recovery Recommendations**:
  1. Implement an "Index Rebuild" trigger if USN journal corruption is detected.
  2. Add transient error retries for shell file operations.
- **Testing Suggestions**:
  1. Add unit tests that mock `DeviceIoControl` to return malformed USN records.
  2. Add tests for `UsnMonitor` start/stop cycles during high activity.
