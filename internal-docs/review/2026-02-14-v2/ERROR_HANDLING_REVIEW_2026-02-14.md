# Error Handling Review - 2026-02-14

## Executive Summary
- **Health Score**: 5/10
- **Critical Issues**: 1
- **High Issues**: 2
- **Total Findings**: 7
- **Estimated Remediation Effort**: 24 hours

## Findings

### Critical
- **Swallowed Exceptions in Critical Paths**:
  - **Quote**: `} catch (...) { // NOSONAR(cpp:S2738) - Catch-all needed for initialization`
  - **Location**: `src/platform/windows/AppBootstrap_win.cpp`
  - **Risk explanation**: Catching and swallowing all exceptions during bootstrap without sufficient logging or graceful failure can lead to "silent" crashes where the app window never appears and the user has no idea why.
  - **Suggested fix**: Use specific exception types and ensure at least a message box or console output is generated before exiting.
  - **Severity**: Critical

### High
- **Liberal use of `catch(...)` with `NOSONAR`**:
  - **Location**: Throughout the codebase (approx. 40+ occurrences).
  - **Risk explanation**: Using `catch(...)` to suppress errors instead of handling them correctly. `NOSONAR` masks the technical debt from automated tools.
  - **Suggested fix**: Audit all `catch(...)` blocks. Replace with specific exceptions where possible. Ensure all `catch(...)` blocks at least log the occurrence to the `Logger`.
  - **Severity**: High

- **Inconsistent Error Propagation in Search Pipeline**:
  - **Location**: `src/search/SearchWorker.cpp`
  - **Risk explanation**: Errors in worker threads (like `bad_alloc` or filesystem errors) are caught but sometimes only partially reported back to the UI.
  - **Suggested fix**: Implement a standardized `ErrorState` in `SearchMetrics` or `GuiState` that worker threads can set.
  - **Severity**: High

### Medium
- **Missing Resource Cleanup on Exception in WinAPI calls**:
  - **Location**: `src/platform/windows/ShellContextUtils.cpp`
  - **Risk explanation**: Some WinAPI calls are not wrapped in RAII. If an exception occurs between acquisition and `Release()`, resources are leaked.
  - **Suggested fix**: Use `ScopedHandle` or `ComPtr` for all Windows resources.
  - **Severity**: Medium

## Quick Wins
- Ensure all `catch(...)` blocks call `Logger::Instance().Log(...)` even if they swallow the error.
- Add `[[nodiscard]]` to all functions returning `bool` success codes.

## Recommended Actions
- **Establish a standard exception hierarchy** for the project (e.g., `FindHelperException`).
- **Refactor the bootstrap sequence** to provide better diagnostics on failure.

---
**Health Score**: 5/10 - The project relies too heavily on "catch-all" blocks and suppression comments (`NOSONAR`) which hides the true state of error handling.
