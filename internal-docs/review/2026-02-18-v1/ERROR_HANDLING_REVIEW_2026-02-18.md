# Error Handling Audit - 2026-02-18

## Executive Summary
- **Reliability Score**: 8/10
- **Critical Gaps**: 0
- **High Issues**: 0 (1 was fixed: find_handle RAII)
- **Total Findings**: 10
- **Estimated Remediation Effort**: 20 hours

## Findings

### High
1. **Handle Leak on Exception in `FolderCrawler::EnumerateDirectory`** — **FIXED**
   - **Location**: `src/crawler/FolderCrawler.cpp` (Windows `EnumerateDirectory` path).
   - **Error Type**: Resource Cleanup / RAII Completeness
   - **Failure Scenario**: An exception thrown during directory enumeration (e.g., `std::bad_alloc` during `entries.push_back` or `WideToUtf8`) would have bypassed `FindClose`.
   - **Fix applied**: File-local RAII: `FindHandleDeleter` and `UniqueFindHandle` (`std::unique_ptr<void, FindHandleDeleter>`). The find handle is held by `UniqueFindHandle`, so `FindClose` runs on scope exit or on exception. Explicit `FindClose` call removed. See also TECH_DEBT_REVIEW_2026-02-18 (same item, marked FIXED).
   - **Severity**: High (was); now addressed.

### Medium
1. **Swallowing Unknown Exceptions in `Start()`**
   - **Location**: `src/usn/UsnMonitor.cpp` destructor and `Start()`
   - **Error Type**: Exception Path Testing / Catch Blocks
   - **Failure Scenario**: An unknown exception occurs during `Stop()` in the destructor.
   - **Current Behavior**: The exception is caught and logged, but the error might be critical for application shutdown.
   - **Expected Behavior**: While logging is good, the application should ensure it doesn't leave the system in an inconsistent state. (Current behavior is acceptable for a destructor but worth noting).
   - **Severity**: Medium

2. **Missing `GetLastError()` Context in Some Logs**
   - **Location**: `src/crawler/FolderCrawler.cpp:513`
   - **Error Type**: Error Logging & Diagnostics
   - **Failure Scenario**: `FindNextFileW` fails with an error other than `ERROR_NO_MORE_FILES`.
   - **Current Behavior**: The error is logged with `GetLastError()`, but the context (the specific file that failed) might be useful.
   - **Expected Behavior**: Log more context about which specific entry failed to process.
   - **Severity**: Medium

### Low
1. **Inconsistent Log Severity for Access Denied** — **Already consistent**
   - **Location**: `src/crawler/FolderCrawler.cpp` (Windows, macOS, Linux `EnumerateDirectory` paths).
   - **Error Type**: Logging Completeness
   - **Study**: All "Access denied to directory" cases use `LOG_WARNING_BUILD` (Windows `ERROR_ACCESS_DENIED` ~line 493, macOS `EACCES` ~562, Linux `EACCES` ~625). No other code in the repo logs "Access denied" with a different severity. No change needed.
   - **Severity**: Low (was); addressed by existing consistency.

## Reliability Score: 8/10
The application has a robust error handling framework (`ExceptionHandling.h`) and uses it consistently in many places. The `UsnMonitor` includes advanced retry and backoff logic for transient Windows API errors. The previous use of a raw find handle in `FolderCrawler::EnumerateDirectory` has been addressed with RAII (`UniqueFindHandle`).

## Critical Gaps
None identified that would lead to immediate crashes, the handle leak in `FolderCrawler` has been fixed (RAII for find handle).

## Logging Improvements
- Add more context (file IDs, thread IDs) to search-related error logs.
- Ensure all Windows API failures include the `dwError` code and the human-readable error message.

## Recovery Recommendations
- Implement a "Self-Healing" mechanism for the `UsnMonitor` that can re-initialize the journal if it becomes corrupted or "wrapped" too many times.
- Add a retry mechanism for failed file metadata loads in the UI.

## Testing Suggestions
- **Fault Injection**: Mock the Windows API to return `ERROR_ACCESS_DENIED` or `ERROR_DISK_FULL` during crawling.
- **Stress Test**: Run the crawler on a directory with deeply nested paths and simulate memory pressure to trigger `std::bad_alloc`.
