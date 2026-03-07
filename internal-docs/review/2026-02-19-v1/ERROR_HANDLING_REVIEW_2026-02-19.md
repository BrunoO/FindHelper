# Error Handling Review - 2026-02-19

## Executive Summary
- **Health Score**: 9/10
- **Critical Issues**: 0
- **High Issues**: 0
- **Total Findings**: 6
- **Estimated Remediation Effort**: 12 hours

## Findings

### Critical
*None identified.*

### High
*None identified.*

### Medium
1. **Implicit Exception Swallowing in Destructors**
   - **Location**: `src/usn/UsnMonitor.h` and others
   - **Error Type**: Exception Safety: Destructors that might throw
   - **Failure Scenario**: If `Stop()` throws an exception during stack unwinding.
   - **Current Behavior**: Exceptions are caught and logged, which is correct for destructors, but the reliance on `catch (...)` without specialized handling in some places might hide the original cause if logging fails.
   - **Expected Behavior**: Specialized handling for known exception types before `catch (...)`.
   - **Fix**: Use `HandleExceptions` utility more consistently.
   - **Severity**: Medium

2. **Transient Error Recovery in USN Journal**
   - **Location**: `src/usn/UsnMonitor.cpp:HandleReadJournalError`
   - **Error Type**: Windows API Error Handling: Error Recovery
   - **Failure Scenario**: Intermittent network or volume access issues.
   - **Current Behavior**: Retries with a fixed delay.
   - **Expected Behavior**: Exponential backoff for all transient errors (some already have it, but inconsistent).
   - **Fix**: Standardize backoff logic.
   - **Severity**: Medium

### Low
1. **Missing [[nodiscard]] on internal success flags**
   - **Location**: Multiple internal methods in `FileIndex`
   - **Error Type**: Exception Safety: RAII Completeness
   - **Failure Scenario**: Programmer error by ignoring a return value indicating partial success.
   - **Current Behavior**: Silent ignore.
   - **Expected Behavior**: Compiler warning.
   - **Fix**: Add `[[nodiscard]]`.
   - **Severity**: Low

2. **Manual GetLastError() capture vs Utility**
   - **Location**: `src/usn/UsnMonitor.cpp`
   - **Error Type**: Windows API Error Handling: Missing Checks
   - **Failure Scenario**: `GetLastError()` being overridden by a successful intervening call (unlikely but possible).
   - **Current Behavior**: Captured manually.
   - **Expected Behavior**: Captured immediately using a RAII guard or specialized utility.
   - **Fix**: Use a small utility to capture and store error state immediately.
   - **Severity**: Low

## Summary Requirements

- **Reliability Score**: 9/10. The application demonstrates a high awareness of error handling, especially regarding Windows API calls and multi-threaded edge cases.
- **Critical Gaps**: None. The use of `std::promise`/`std::future` for initialization synchronization is a strong pattern.
- **Logging Improvements**: Add more context to "Unknown Exception" logs by identifying the thread/component more precisely.
- **Recovery Recommendations**: Implement a more robust "Re-arm" logic for USN monitoring if the journal is completely lost (e.g., volume reformat).
- **Testing Suggestions**: Add unit tests that mock `DeviceIoControl` to return various error codes (`ERROR_JOURNAL_ENTRY_DELETED`, `ERROR_INVALID_PARAMETER`) to verify recovery paths.
