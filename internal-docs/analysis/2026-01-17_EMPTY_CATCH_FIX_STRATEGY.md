# Empty Catch Statements Fix Strategy

**Date:** 2026-01-17  
**Priority:** HIGH (Safety Issue)  
**Total Warnings:** 14  
**Check:** `bugprone-empty-catch`

## Executive Summary

Empty catch statements hide errors and make debugging impossible. This document outlines the strategy for fixing all 14 empty catch statement warnings in the codebase.

## Problem

Empty catch blocks (or catch blocks with only comments) silently swallow exceptions, making it impossible to:
- Debug issues when they occur
- Understand what went wrong
- Track down bugs in production
- Handle errors appropriately

## Strategy

### Principle: Always Handle Exceptions Meaningfully

Every catch block must do one of the following:
1. **Log the error** - At minimum, log what happened
2. **Re-throw the exception** - If the error can't be handled at this level
3. **Set error state** - Update application state to reflect the error
4. **Return error code** - Return a value indicating failure
5. **Document why ignoring is acceptable** - Only for truly exceptional cases (destructors, cleanup)

### Fix Patterns

#### Pattern 1: Log and Continue (Most Common)

```cpp
// ❌ Before - Empty catch
try {
  DoSomething();
} catch (const std::exception& e) {
  // Ignore
}

// ✅ After - Log and continue
try {
  DoSomething();
} catch (const std::exception& e) {
  LOG_WARNING("Failed to do something: " << e.what());
  // Continue execution - operation is non-critical
}
```

#### Pattern 2: Log and Set Error State

```cpp
// ❌ Before - Empty catch
try {
  ProcessData();
} catch (const std::exception& e) {
  // Ignore
}

// ✅ After - Log and set error state
try {
  ProcessData();
} catch (const std::exception& e) {
  LOG_ERROR("Failed to process data: " << e.what());
  error_state_ = ErrorState::kProcessingFailed;
}
```

#### Pattern 3: Log and Return Error Code

```cpp
// ❌ Before - Empty catch
try {
  SaveFile(path);
} catch (const std::exception& e) {
  // Ignore
}

// ✅ After - Log and return error
try {
  SaveFile(path);
} catch (const std::exception& e) {
  LOG_ERROR("Failed to save file: " << e.what());
  return false;  // Or appropriate error code
}
```

#### Pattern 4: Destructor/Cleanup (Special Case)

For destructors and cleanup code where exceptions must not propagate:

```cpp
// ❌ Before - Empty catch with comment
} catch (...) {
  // NOSONAR - Destructors must not throw
}

// ✅ After - Log but don't propagate
} catch (const std::exception& e) {
  // Destructors must not throw - log error but don't propagate
  LOG_ERROR("Exception in destructor: " << e.what());
  // Explicitly ignore to prevent propagation
} catch (...) {
  // Destructors must not throw - log unknown error but don't propagate
  LOG_ERROR("Unknown exception in destructor");
  // Explicitly ignore to prevent propagation
}
```

## Identified Empty Catch Blocks

### 1. FileIndex.cpp (2 warnings)

**Location:** Lines 476-481  
**Context:** Thread pool initialization error handling

```cpp
// ❌ Current - Only comments
} catch (const std::bad_alloc&) {
  // Memory allocation failure during thread pool initialization
  return 0;
} catch (const std::runtime_error&) {
  // Thread pool initialization failed
  return 0;
} catch (const std::exception&) {
  // NOSONAR(cpp:S1181) - Catch-all safety net
  return 0;
}
```

**Fix Strategy:**
- Add logging for each exception type
- Log the error message to help diagnose initialization failures
- Keep the return 0 (fallback behavior is acceptable)

**Fixed Code:**
```cpp
} catch (const std::bad_alloc& e) {
  LOG_ERROR_BUILD("Memory allocation failure during thread pool initialization: " << e.what());
  return 0;  // Fallback: return nullptr to use default thread pool
} catch (const std::runtime_error& e) {
  LOG_ERROR_BUILD("Thread pool initialization failed: " << e.what());
  return 0;  // Fallback: return nullptr to use default thread pool
} catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - Catch-all safety net
  LOG_ERROR_BUILD("Unexpected exception during thread pool initialization: " << e.what());
  return 0;  // Fallback: return nullptr to use default thread pool
}
```

---

### 2. GeminiApiUtils.cpp (2 warnings)

**Location:** Lines 409-414  
**Context:** JSON parsing error handling

```cpp
// ❌ Current - Only comments
} catch (const json::exception&) {
  // JSON access failed (e.g., invalid index access on Windows), return empty string
  return "";
} catch (const std::out_of_range&) {
  // Array index out of range (shouldn't happen with size checks, but handle gracefully)
  return "";
}
```

**Fix Strategy:**
- Add logging to help diagnose JSON parsing issues
- Log at WARNING level (not ERROR) since this is expected in some cases
- Keep the return "" (graceful degradation)

**Fixed Code:**
```cpp
} catch (const json::exception& e) {
  // JSON access failed (e.g., invalid index access on Windows), return empty string
  LOG_WARNING_BUILD("JSON access failed in ExtractFirstTextPart: " << e.what());
  return "";
} catch (const std::out_of_range& e) {
  // Array index out of range (shouldn't happen with size checks, but handle gracefully)
  LOG_WARNING_BUILD("Array index out of range in ExtractFirstTextPart: " << e.what());
  return "";
}
```

---

### 3. SearchResultUtils.cpp (2 warnings)

**Location:** Lines 349, 835  
**Context:** Future cleanup in destructor/cleanup code

```cpp
// ❌ Current - Only static_cast<void>(0) (effectively empty)
} catch (...) {
  // NOSONAR(cpp:S2738, cpp:S2486) - Ignore exceptions, just ensure cleanup happens
  // Future cleanup must not throw - prevents exceptions from propagating
  // Explicitly ignore exceptions during cleanup to prevent propagation
  static_cast<void>(0);
}
```

**Fix Strategy:**
- Add logging (destructor-safe logging)
- Use LOG_ERROR_BUILD (thread-safe, doesn't throw)
- Keep the catch-all pattern (destructors must not throw)

**Fixed Code:**
```cpp
} catch (const std::exception& e) {
  // Future cleanup must not throw - log error but don't propagate
  LOG_ERROR_BUILD("Exception during future cleanup: " << e.what());
  // Explicitly ignore to prevent propagation (destructor safety)
} catch (...) {
  // Future cleanup must not throw - log unknown error but don't propagate
  LOG_ERROR_BUILD("Unknown exception during future cleanup");
  // Explicitly ignore to prevent propagation (destructor safety)
}
```

---

### 4. WindowsIndexBuilder.cpp (2 warnings)

**Location:** Lines 95, 118  
**Context:** Exception handling in worker thread and thread join

**Current Code:**
```cpp
// Line 95 - Already has logging, but catch(...) could be improved
} catch (...) {
  state->MarkFailed();
  state->SetLastErrorMessage("Unknown error while monitoring USN");
  LOG_ERROR("WindowsIndexBuilder: unknown exception while monitoring USN");
}

// Line 118 - Has logging but could be more specific
} catch (...) {
  LOG_ERROR("WindowsIndexBuilder: exception while joining worker thread");
}
```

**Fix Strategy:**
- Split catch-all into specific exception types
- Add more detailed error messages
- Keep existing error state updates

**Fixed Code:**
```cpp
// Line 95 - Split into specific and catch-all
} catch (const std::exception& e) {
  state->MarkFailed();
  state->SetLastErrorMessage("Error while monitoring USN: " + std::string(e.what()));
  LOG_ERROR_BUILD("WindowsIndexBuilder: exception while monitoring USN: " << e.what());
} catch (...) {
  state->MarkFailed();
  state->SetLastErrorMessage("Unknown error while monitoring USN");
  LOG_ERROR("WindowsIndexBuilder: unknown exception while monitoring USN");
}

// Line 118 - Split into specific and catch-all
} catch (const std::exception& e) {
  LOG_ERROR_BUILD("WindowsIndexBuilder: exception while joining worker thread: " << e.what());
} catch (...) {
  LOG_ERROR("WindowsIndexBuilder: unknown exception while joining worker thread");
}
```

---

## Implementation Plan

### Phase 1: Critical Fixes (High Priority)
1. **FileIndex.cpp** - Thread pool initialization (2 warnings)
   - Impact: Initialization failures are silently ignored
   - Risk: HIGH - Could cause runtime issues

2. **GeminiApiUtils.cpp** - JSON parsing (2 warnings)
   - Impact: JSON parsing errors are hidden
   - Risk: MEDIUM - Could cause incorrect behavior

### Phase 2: Cleanup Code Fixes (Medium Priority)
3. **SearchResultUtils.cpp** - Future cleanup (2 warnings)
   - Impact: Cleanup errors are hidden
   - Risk: MEDIUM - Could cause resource leaks

4. **WindowsIndexBuilder.cpp** - Worker thread errors (2 warnings)
   - Impact: Thread errors are partially logged but could be improved
   - Risk: MEDIUM - Already has some logging

### Phase 3: Review Remaining (Lower Priority)
5. Review any remaining catch blocks that might be flagged
   - Some catch blocks with NOSONAR comments might still be flagged
   - Add explicit logging if needed

## Testing Strategy

After fixing each file:
1. **Build verification** - Ensure code compiles
2. **Run tests** - Verify no regressions
3. **Check logs** - Verify exceptions are now logged
4. **Manual testing** - Trigger error conditions to verify logging

## Success Criteria

- ✅ All 14 empty catch statement warnings resolved
- ✅ All exceptions are logged (at appropriate log levels)
- ✅ No regressions in functionality
- ✅ Error handling is more robust and debuggable

## Notes

- **Log Levels:**
  - `LOG_ERROR` / `LOG_ERROR_BUILD` - For critical errors
  - `LOG_WARNING` / `LOG_WARNING_BUILD` - For non-critical errors (graceful degradation)
  - Use `LOG_*_BUILD` in destructors (thread-safe, doesn't throw)

- **Exception Types:**
  - Prefer catching specific exception types first
  - Use `catch (const std::exception& e)` for standard exceptions
  - Use `catch (...)` only as last resort (with logging)

- **Destructor Safety:**
  - Destructors must not throw exceptions
  - Use `LOG_*_BUILD` (doesn't throw)
  - Explicitly ignore exceptions after logging
