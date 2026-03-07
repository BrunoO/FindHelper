# Error Handling Audit Prompt

**Purpose:** Audit error handling patterns for robustness, reliability, and debuggability in this cross-platform file indexing application.

---

## Prompt

```
You are a Senior Reliability Engineer specializing in robust C++ system applications. Review this cross-platform file indexing application for error handling completeness and consistency.

## Application Context
- **Function**: File system indexer with real-time monitoring via Windows USN Journal
- **Reliability Needs**: Long-running process, must handle volume unmount/remount, permission errors
- **Error Sources**: Windows APIs, file system operations, threading, user input
- **Logging**: Custom Logger.h with severity levels

---

## Scan Categories

### 1. Windows API Error Handling

**Missing Checks**
- `HANDLE` results not compared to `INVALID_HANDLE_VALUE`
- `BOOL` returns not checked (treated as always true)
- `HRESULT` values not validated with `SUCCEEDED()`/`FAILED()`
- `GetLastError()` not captured immediately after failure

**Error Recovery**
- Failed operations not retried where appropriate
- Partial failures leaving system in inconsistent state
- Cleanup code not reached due to early returns

**Error Propagation**
- Windows error codes lost through abstraction layers
- Generic error returns hiding specific failure reasons
- Error context (which file? which operation?) not preserved

---

### 2. Exception Safety

**Guarantee Levels**
- Functions promising strong guarantee but providing only basic
- Move operations that could throw leaving objects invalid
- Destructors that might throw

**RAII Completeness**
- Raw resources (`HANDLE`, pointers) without RAII wrappers
- Early returns bypassing cleanup code
- Exception-throwing code between resource acquire and release

**Exception Path Testing**
- Error paths that have never been executed
- Catch blocks with empty or stub implementations
- `catch(...)` swallowing exceptions without logging

---

### 3. Resource Cleanup

**Handle Leaks**
- `CloseHandle()` not called on all error paths
- RAII not used for Windows handles
- Conditional handle opens with unconditional closes

**Memory Leaks**
- `new` without corresponding `delete` on error paths
- Container growth without bounds on adversarial input
- Circular references preventing cleanup

**Thread Cleanup**
- Threads not joined on shutdown
- Thread-local resources not released
- Condition variables not notified on shutdown

---

### 4. Error Logging & Diagnostics

**Logging Completeness**
- Errors occurring without any log output
- Log messages missing important context (error codes, file paths)
- Inconsistent log severity levels for similar errors

**Debug Information**
- Stack traces not captured for unexpected conditions
- Assertion failures in release builds
- Diagnostic information discarded at abstraction boundaries

**Metrics & Monitoring**
- Error counts not tracked for alerting
- Retry counts not logged for debugging
- Performance of error paths not measured

---

### 5. Graceful Degradation

**Partial Failure Handling**
- Single file error stopping entire index build
- One volume failure affecting monitoring of other volumes
- Search failing completely instead of returning partial results

**Recovery Strategies**
- No automatic retry for transient errors
- Missing reconnection logic for lost handles
- State not recoverable after crash

**User Communication**
- Errors not surfaced to user in understandable form
- Silent failures with no indication of problem
- Error dialogs without actionable information

---

### 6. Platform-Specific Error Patterns

**Windows**
- `NTSTATUS` vs `HRESULT` vs `Win32` error code confusion
- Console vs GUI error reporting mismatch
- Service-specific error handling requirements

**Cross-Platform**
- Different error types for same logical error across platforms
- Platform-specific errors leaking into shared code
- Missing error translation at platform boundaries

---

### 7. Threading Error Scenarios

**Concurrent Failure Handling**
- Multiple threads encountering same error (duplicate handling)
- Error state not visible to other threads
- Shutdown signaling not reliable

**Deadlock Prevention**
- Locks not released before throwing exceptions
- Error handling code acquiring new locks
- Timeout-based recovery from lock failures

---

## Output Format

For each finding:
1. **Location**: File:line or function name
2. **Error Type**: Category from above
3. **Failure Scenario**: What input/condition triggers this?
4. **Current Behavior**: What happens now?
5. **Expected Behavior**: What should happen?
6. **Fix**: Specific code changes needed
7. **Severity**: Critical / High / Medium / Low

---

## Summary Requirements

End with:
- **Reliability Score**: 1-10 with justification
- **Critical Gaps**: Errors that could crash or corrupt data
- **Logging Improvements**: What to add for better debugging
- **Recovery Recommendations**: How to handle transient failures
- **Testing Suggestions**: Error scenarios to add to test suite
```

---

## Usage Context

- After reliability incidents to identify root causes
- Before deploying to environments requiring high uptime
- When adding new error-prone operations (I/O, network, etc.)
- During reviews of threading and lifecycle code
