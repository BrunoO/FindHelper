# Logging Standards and Guidelines

**Purpose**: Establish consistent error logging patterns across the codebase to improve debuggability and maintainability.

**Issue**: Inconsistent error logging makes it difficult to diagnose production issues. Some code logs detailed errors with context, while others log generic messages or nothing at all.

---

## Logging Standards

### 1. Required Information in Error Logs

Every error log must include:

1. **Function/Operation Name**: What operation was being performed
2. **Context**: Relevant identifiers (file paths, IDs, thread indices, etc.)
3. **Error Code/Message**: Specific error code (Windows: `GetLastError()`, exceptions: `e.what()`)
4. **Severity Level**: Appropriate log level (ERROR, WARNING, INFO)

### 2. Log Message Format

**Standard Format:**
```
[Operation] failed: [specific error]. Context: [relevant details]
```

**Examples:**
```cpp
// ✅ GOOD: Detailed error with context
LOG_ERROR("OpenFileDefault failed: Failed to convert path to wide string. Path: " + full_path + ", Error: " + std::to_string(GetLastError()));

// ✅ GOOD: Windows API error with context
LOG_ERROR_BUILD("DeviceIoControl failed: FSCTL_READ_USN_JOURNAL returned error " << err << ". Volume: " << config_.volume_path);

// ❌ BAD: Generic error without context
LOG_ERROR("Operation failed");

// ❌ BAD: Missing error code
LOG_ERROR("Failed to open file: " + full_path);
```

### 3. Log Level Guidelines

| Level | When to Use | Examples |
|-------|-------------|----------|
| **ERROR** | Operation failed and cannot continue | API call failed, exception caught, resource allocation failed |
| **WARNING** | Operation failed but can continue/retry | Retryable error, degraded functionality, non-critical failure |
| **INFO** | Important state change | Initialization complete, privilege dropped, successful operation |
| **DEBUG** | Detailed diagnostic information | Function entry/exit, intermediate values, detailed state |

### 4. Windows API Error Handling Pattern

**Standard Pattern:**
```cpp
if (!WindowsApiCall(...)) {
  DWORD err = GetLastError();  // Capture immediately after failure
  LOG_ERROR_BUILD("OperationName failed: " << GetWindowsErrorString(err) 
                << ". Context: " << context_value << ", Error code: " << err);
  return false;  // or handle error appropriately
}
```

**Example Implementation:**
```cpp
// In UsnMonitor.cpp
if (!DeviceIoControl(volume_handle, FSCTL_QUERY_USN_JOURNAL, nullptr, 0,
                    &usn_journal_data, sizeof(usn_journal_data),
                    &bytes_returned, nullptr)) {
  DWORD err = GetLastError();
  auto error_msg = LOG_ERROR_BUILD_AND_GET(
      "Failed to query USN Journal. Volume: " << config_.volume_path
      << ", Error: " << err);
  std::cerr << error_msg << std::endl;
  monitoring_active_.store(false, std::memory_order_release);
  init_promise_.set_value(false);
  if (queue_) {
    queue_->Stop();
  }
  return;
}
```

### 5. Exception Handling Pattern

**Standard Pattern:**
```cpp
try {
  // Operation that might throw
  PerformOperation();
} catch (const std::exception& e) {
  LOG_ERROR("OperationName failed: Exception: " + std::string(e.what()) 
            + ". Context: " + context_value);
  // Handle error appropriately
} catch (...) {
  LOG_ERROR("OperationName failed: Unknown exception. Context: " + context_value);
}
```

**Example Implementation:**
```cpp
// In FileIndex.cpp search strategies
try {
  ProcessChunkRangeForSearch(...);
} catch (const std::exception& e) {
  LOG_ERROR_BUILD("Exception in ProcessChunkRangeForSearch ("
                  << strategy_name << " strategy, thread " << thread_idx
                  << ", range [" << start << "-" << end << "]): " << e.what());
  return {};  // Return empty results
} catch (...) {
  LOG_ERROR_BUILD("Unknown exception in ProcessChunkRangeForSearch ("
                  << strategy_name << " strategy, thread " << thread_idx << ")");
  return {};  // Return empty results
}
```

### 6. Context Information Guidelines

**Always Include:**
- **File Operations**: Full file path
- **Windows API Calls**: Error code (`GetLastError()`), relevant parameters
- **Thread Operations**: Thread index/ID, thread name
- **Search Operations**: Query string, search strategy, range/chunk info
- **Index Operations**: File ID, parent ID, path
- **Network/API Calls**: Endpoint, status code, response details

**Example:**
```cpp
// ✅ GOOD: Includes all relevant context
LOG_ERROR("Rename failed: File not found. FileID: " + std::to_string(id) 
          + ", NewName: " + new_name + ", OldPath: " + old_path);

// ❌ BAD: Missing context
LOG_ERROR("Rename failed");
```

---

## Implementation Plan

### Phase 1: Audit Current Logging (1 hour)

1. **Identify Inconsistent Patterns**:
   ```bash
   # Find all LOG_ERROR, LOG_WARNING calls
   grep -r "LOG_ERROR\|LOG_WARNING" --include="*.cpp" --include="*.h" .
   
   # Review for missing context
   # Look for patterns like:
   # - LOG_ERROR("Operation failed")  # Missing context
   # - LOG_ERROR("Failed: " + path)   # Missing error code
   # - LOG_WARNING("Warning")          # Too generic
   ```

2. **Categorize Issues**:
   - Missing error codes
   - Missing context (paths, IDs, parameters)
   - Generic messages without operation name
   - Silent failures (no logging)

### Phase 2: Create Helper Functions (1 hour)

**Add to `Logger.h` or create `LoggingUtils.h`:**

```cpp
// LoggingUtils.h
#pragma once
#include "Logger.h"
#include <string>

#ifdef _WIN32
#include <windows.h>

namespace logging_utils {
  // Get human-readable Windows error string
  std::string GetWindowsErrorString(DWORD error_code);
  
  // Log Windows API error with context
  void LogWindowsApiError(const std::string& operation,
                          const std::string& context,
                          DWORD error_code);
  
  // Log exception with context
  void LogException(const std::string& operation,
                   const std::string& context,
                   const std::exception& e);
  
  // Log unknown exception with context
  void LogUnknownException(const std::string& operation,
                          const std::string& context);
}
#endif
```

**Implementation:**
```cpp
// LoggingUtils.cpp
#ifdef _WIN32
#include "LoggingUtils.h"
#include <sstream>
#include <windows.h>
#include <winerror.h>

namespace logging_utils {
  std::string GetWindowsErrorString(DWORD error_code) {
    LPSTR message_buffer = nullptr;
    size_t size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error_code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&message_buffer, 0, nullptr);
    
    if (size == 0) {
      return "Unknown error " + std::to_string(error_code);
    }
    
    std::string message(message_buffer);
    LocalFree(message_buffer);
    
    // Remove trailing newline
    if (!message.empty() && message.back() == '\n') {
      message.pop_back();
    }
    if (!message.empty() && message.back() == '\r') {
      message.pop_back();
    }
    
    return message;
  }
  
  void LogWindowsApiError(const std::string& operation,
                          const std::string& context,
                          DWORD error_code) {
    std::string error_msg = GetWindowsErrorString(error_code);
    LOG_ERROR_BUILD(operation << " failed: " << error_msg 
                    << ". Context: " << context << ", Error code: " << error_code);
  }
  
  void LogException(const std::string& operation,
                   const std::string& context,
                   const std::exception& e) {
    LOG_ERROR_BUILD(operation << " failed: Exception: " << e.what() 
                    << ". Context: " << context);
  }
  
  void LogUnknownException(const std::string& operation,
                          const std::string& context) {
    LOG_ERROR_BUILD(operation << " failed: Unknown exception. Context: " << context);
  }
}
#endif
```

### Phase 3: Fix High-Priority Files (1-2 hours)

**Priority Order:**
1. **UsnMonitor.cpp**: Critical for production debugging
2. **FileOperations_win.cpp**: User-facing operations
3. **FileIndex.cpp**: Core functionality
4. **SearchWorker.cpp**: Search operations
5. **Other files**: As time permits

**Fix Pattern:**
```cpp
// BEFORE:
if (!SomeApiCall()) {
  LOG_ERROR("Operation failed");
  return false;
}

// AFTER:
if (!SomeApiCall()) {
  DWORD err = GetLastError();
  logging_utils::LogWindowsApiError("SomeApiCall", 
                                    "Context: " + context_value, 
                                    err);
  return false;
}
```

### Phase 4: Add Logging to Silent Failures (1 hour)

**Find Silent Failures:**
```bash
# Find Windows API calls without error logging
grep -r "DeviceIoControl\|CreateFile\|OpenFile" --include="*.cpp" . | \
  grep -v "LOG_ERROR\|LOG_WARNING"
```

**Add Logging:**
```cpp
// BEFORE:
if (!SomeApiCall()) {
  return false;  // Silent failure
}

// AFTER:
if (!SomeApiCall()) {
  DWORD err = GetLastError();
  logging_utils::LogWindowsApiError("SomeApiCall", 
                                    "Context: " + context_value, 
                                    err);
  return false;
}
```

---

## Code Review Checklist

When reviewing code, ensure:

- [ ] All Windows API failures log error code (`GetLastError()`)
- [ ] All exceptions are caught and logged with context
- [ ] All error logs include operation name
- [ ] All error logs include relevant context (paths, IDs, parameters)
- [ ] No silent failures (all failures are logged)
- [ ] Appropriate log level is used (ERROR vs WARNING)
- [ ] Error messages are specific, not generic

---

## Examples

### ✅ Good Examples

```cpp
// Windows API with full context
if (!DeviceIoControl(handle, FSCTL_READ_USN_JOURNAL, ...)) {
  DWORD err = GetLastError();
  LOG_ERROR_BUILD("DeviceIoControl failed: FSCTL_READ_USN_JOURNAL returned error " 
                  << err << ". Volume: " << config_.volume_path 
                  << ", Bytes read: " << bytes_read);
  return false;
}

// Exception with context
try {
  file_index.Insert(id, parent_id, name);
} catch (const std::exception& e) {
  LOG_ERROR("Insert failed: Exception: " + std::string(e.what()) 
            + ". FileID: " + std::to_string(id) 
            + ", ParentID: " + std::to_string(parent_id) 
            + ", Name: " + name);
  return false;
}

// File operation with path
if (!OpenFileDefault(full_path)) {
  LOG_ERROR("OpenFileDefault failed: Path: " + full_path 
            + ", Error: " + std::to_string(GetLastError()));
  return false;
}
```

### ❌ Bad Examples

```cpp
// Missing error code
if (!DeviceIoControl(...)) {
  LOG_ERROR("DeviceIoControl failed");
  return false;
}

// Missing context
try {
  file_index.Insert(id, parent_id, name);
} catch (...) {
  LOG_ERROR("Insert failed");
  return false;
}

// Silent failure
if (!SomeApiCall()) {
  return false;  // No logging!
}

// Generic message
LOG_ERROR("Error occurred");
```

---

## Testing

### Manual Testing
1. Trigger error conditions (invalid paths, permission errors, etc.)
2. Verify error logs contain all required information
3. Check log files for consistent format

### Automated Testing
1. Create unit tests that verify error logging
2. Use mocking to simulate API failures
3. Verify log messages contain expected context

---

## Maintenance

- **Code Reviews**: Enforce logging standards in PR reviews
- **Linting**: Consider adding clang-tidy rules for logging patterns
- **Documentation**: Keep this document updated as patterns evolve
- **Training**: Share standards with team members

---

## References

- **Logger Implementation**: `Logger.h`
- **Current Logging Examples**: `UsnMonitor.cpp`, `FileOperations_win.cpp`

