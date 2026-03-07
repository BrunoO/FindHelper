# Error Handling Audit - 2026-03-06

## Executive Summary
- **Health Score**: 8.5/10
- **Critical Gaps**: 0
- **High Issues**: 1
- **Total Findings**: 6
- **Estimated Remediation Effort**: 12 hours

## Findings

### Critical
None.

### High
1. **Incomplete USN Journal Error Recovery** (Category 1)
   - **Location**: `src/usn/UsnMonitor.cpp::MonitorJournal`
   - **Error Type**: Missing Windows API check / Recovery failure.
   - **Failure Scenario**: USN Journal deletion or truncation during monitoring.
   - **Current Behavior**: Might stop monitoring or log an error without attempting re-initialization.
   - **Expected Behavior**: Detect `ERROR_JOURNAL_DELETE` or similar and re-initialize monitoring from the current MFT state.
   - **Fix**: Add specific error case handling for USN journal status and implement a re-initialization path.
   - **Severity**: High
   - **Effort**: Medium (4 hours)

### Medium
1. **Generic catch(...) Swallowing Errors** (Category 2)
   - **Location**: `src/search/SearchController.cpp::Update`
   - **Error Type**: Exception Safety - swallows exception.
   - **Failure Scenario**: Any unexpected exception (e.g., `std::bad_alloc`) during search update.
   - **Current Behavior**: Catch-all logs a generic message and swallows, potentially hiding critical failures.
   - **Expected Behavior**: Log more context or allow specific exceptions to propagate to a higher-level handler that can notify the user.
   - **Fix**: Replace with specific exception types where possible and only use `catch(...)` as a final safety net with adequate logging.
   - **Severity**: Medium
   - **Effort**: Small (1 hour)

2. **Missing GetLastError() Capture** (Category 1)
   - **Location**: `src/platform/windows/FileOperations_win.cpp`
   - **Error Type**: Windows API Error Handling.
   - **Failure Scenario**: `DeleteFile` or `MoveFile` failing.
   - **Current Behavior**: Logs generic "Failed to delete" without the underlying Win32 error code.
   - **Expected Behavior**: Capture and log the result of `GetLastError()` for easier diagnosis.
   - **Fix**: Wrap API calls with a helper that captures `GetLastError()` and converts it to a human-readable string.
   - **Severity**: Medium
   - **Effort**: Small (2 hours)

### Low
1. **RAII Underutilization: ScopedHandle.h** (Category 3)
   - **Location**: `src/utils/ScopedHandle.h`
   - **Error Type**: Resource Cleanup.
   - **Failure Scenario**: Exception thrown between handle creation and wrapping in `ScopedHandle`.
   - **Current Behavior**: The handle might leak if not immediately assigned to the wrapper.
   - **Expected Behavior**: Use a factory function that returns the `ScopedHandle` already wrapped.
   - **Fix**: Add a `MakeScopedHandle` factory function.
   - **Severity**: Low
   - **Effort**: Small (1 hour)

2. **Inconsistent Logging Severity: SearchThreadPool.cpp** (Category 4)
   - **Location**: `src/search/SearchThreadPool.cpp`
   - **Error Type**: Error Logging & Diagnostics.
   - **Failure Scenario**: Thread creation failure.
   - **Current Behavior**: Might use `LOG_ERROR` or `LOG_WARNING` inconsistently.
   - **Expected Behavior**: Use `LOG_CRITICAL` or `LOG_ERROR` for failures that impact search correctness/performance.
   - **Fix**: Standardize severity levels across threading code.
   - **Severity**: Low
   - **Effort**: Small (1 hour)

## Reliability Score: 8.5/10
The application has solid error handling foundations with widespread use of RAII and localized try-catch blocks. The main area for improvement is in detailed Windows API error diagnostics and robust recovery from transient system events like journal resets.

## Critical Gaps
No gaps identified that could cause data corruption, but USN journal monitoring remains a high-priority area for robust recovery.

## Logging Improvements
- Systematically capture Win32 error codes using `GetLastError()`.
- Add more context (file IDs, paths) to error logs.
- Standardize log severity across all platform-specific modules.

## Recovery Recommendations
- Implement a state-aware retry mechanism for transient I/O failures.
- Automate USN journal re-initialization on journal loss.

## Testing Suggestions
- Inject simulated Windows API failures (using mocks or hooks).
- Test volume unmount/remount scenarios while monitoring is active.
- Use `bad_alloc` injection to test memory exhaustion paths.
