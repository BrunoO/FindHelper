# Error Handling Review - 2026-02-01

## Executive Summary
- **Health Score**: 7/10
- **Critical Gaps**: 1
- **High Issues**: 1
- **Total Findings**: 4
- **Estimated Remediation Effort**: 10 hours

## Findings

### Critical
1. **Race Condition and Potential Crash in `UsnMonitor` Initialization**
   - **Location**: `src/usn/UsnMonitor.cpp` (ReaderThread and HandleInitializationFailure)
   - **Error Type**: Threading Error Scenarios
   - **Failure Scenario**: `OpenVolumeAndQueryJournal` succeeds but `PopulateInitialIndex` fails.
   - **Current Behavior**: `ReaderThread` calls `init_promise_.set_value(true)`. Then `RunInitialPopulationAndPrivileges` calls `HandleInitializationFailure`, which attempts `init_promise_.set_value(false)`.
   - **Expected Behavior**: `set_value` should only be called once. Attempting to set it again throws `std::future_error`.
   - **Fix**: Move `init_promise_.set_value(true)` to after `RunInitialPopulationAndPrivileges` succeeds, or use a state flag to ensure only one call.
   - **Severity**: Critical

### High
2. **Synchronous File I/O in UI/Search Path**
   - **Location**: `src/index/FileIndex.cpp:158`
   - **Error Type**: Performance of error paths / Graceful Degradation
   - **Failure Scenario**: `LoadSettings` fails or hangs due to disk issues during a search.
   - **Current Behavior**: The entire search (and potentially UI) blocks while waiting for file I/O.
   - **Expected Behavior**: Settings should be pre-loaded or loaded asynchronously.
   - **Fix**: Inject cached settings into `FileIndex`.
   - **Severity**: High

### Medium
3. **Inconsistent Error Propagation for File Operations**
   - **Location**: `src/platform/linux/FileOperations_linux.cpp`
   - **Error Type**: Cross-Platform
   - **Failure Scenario**: File deletion or rename fails.
   - **Current Behavior**: Error is logged but often returns a generic `false` to the caller without preserving the specific error code (errno).
   - **Expected Behavior**: Return a structured error type containing the platform-specific error code.
   - **Fix**: Update `FileOperations` interface to return `std::error_code`.
   - **Severity**: Medium

### Low
4. **Swallowed Exceptions in `SearchController`**
   - **Location**: `src/search/SearchController.cpp: WaitAndCleanupFuture`
   - **Error Type**: Exception Path Testing
   - **Failure Scenario**: An attribute loading future throws an exception.
   - **Current Behavior**: `catch (...)` swallows the exception with only a comment.
   - **Expected Behavior**: Log the error at least at DEBUG/VERBOSE level to help diagnose why attributes failed to load.
   - **Fix**: Add logging to the catch block.
   - **Severity**: Low

## Summary Requirements

### Reliability Score: 7/10
Justification: The application has robust USN error handling (retry with backoff), but the initialization sequence of `UsnMonitor` has a critical flaw that could lead to crashes in error scenarios.

### Critical Gaps
The `std::future_error` in `UsnMonitor` initialization is a significant risk for application stability during startup.

### Logging Improvements
- Add more context to `WaitAndCleanupFuture` catch blocks.
- Log `errno` on Linux/macOS for all failed file operations.

### Recovery Recommendations
Implement a "Safe Mode" for settings loading: if `settings.json` is corrupt or slow, fall back to hardcoded defaults immediately without blocking.

### Testing Suggestions
Add unit tests that mock `PopulateInitialIndex` failure to verify the `UsnMonitor` handles it without throwing exceptions from the reader thread.
