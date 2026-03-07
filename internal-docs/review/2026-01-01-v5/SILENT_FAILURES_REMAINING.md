# Silent Failures Remaining - Error Logging Audit

**Date**: 2026-01-01  
**Last Updated**: 2026-01-01  
**Purpose**: Identify Windows API calls and error conditions that lack proper error logging

## Summary

After implementing logging standards and fixing high-priority files, the following silent failures or incomplete error logging remain:

**Status**: ✅ **HIGH PRIORITY FIXES COMPLETED** (2026-01-01)

---

## High Priority (Should Fix Soon) ✅ COMPLETED

### 1. **ShellContextUtils.cpp** - Missing Error Codes in HRESULT Logging ✅ FIXED

**Location**: `ShellContextUtils.cpp`  
**Status**: ✅ **COMPLETED** (2026-01-01)

**Fixed:**
- ✅ `SHGetDesktopFolder()` - Now uses `LogHResultError()`
- ✅ `ParseDisplayName()` - Now uses `LogHResultError()`
- ✅ `SHBindToParent()` - Now uses `LogHResultError()`
- ✅ `GetUIObjectOf()` - Now uses `LogHResultError()`
- ✅ `QueryContextMenu()` - Now uses `LogHResultError()`
- ✅ `InvokeCommand()` - Now uses `LogHResultError()`
- ✅ `CreatePopupMenu()` - Now uses `LogWindowsApiError()`
- ✅ `GetModuleFileNameW()` in `RestartAsAdmin()` - Now uses `LogWindowsApiError()`
- ✅ `ShellExecuteExW()` in `RestartAsAdmin()` - Now uses `LogWindowsApiError()` (except ERROR_CANCELLED)
- ✅ `IsProcessElevated()` - Added warning logging for failures

**Commit**: Fixed in "Fix high-priority silent failures" commits

---

### 2. **FileOperations_win.cpp** - Unchecked Return Value ✅ FIXED

**Location**: `FileOperations_win.cpp` line 118  
**Status**: ✅ **COMPLETED** (2026-01-01)

**Fixed:**
- ✅ `SHOpenFolderAndSelectItems()` - Now checks return value and logs errors using `LogHResultError()`

**Commit**: Fixed in "Fix high-priority silent failures" commit

---

### 3. **FileOperations_win.cpp** - HRESULT Logging Could Use Helpers ✅ FIXED

**Location**: `FileOperations_win.cpp` lines 185-273  
**Status**: ✅ **COMPLETED** (2026-01-01)

**Fixed:**
- ✅ `CoInitializeEx()` - Now uses `LogHResultError()`
- ✅ `CoCreateInstance()` - Now uses `LogHResultError()`
- ✅ `GetOptions()` - Now uses `LogHResultError()`
- ✅ `SetOptions()` - Now uses `LogHResultError()`
- ✅ `Show()` - Now uses `LogHResultError()` (handles ERROR_CANCELLED correctly)
- ✅ `GetResult()` - Now uses `LogHResultError()`
- ✅ `GetDisplayName()` - Now uses `LogHResultError()`

**Commit**: Fixed in "Fix high-priority silent failures" commit

---

## Medium Priority (Can Be Fixed Incrementally) ✅ COMPLETED

### 4. **PrivilegeUtils.cpp** - Silent Return on Error ✅ FIXED

**Location**: `PrivilegeUtils.cpp` lines 108, 117  
**Status**: ✅ **COMPLETED** (2026-01-01)

**Fixed:**
- ✅ First `GetTokenInformation()` call now checks return value
- ✅ Logs errors with context (except expected ERROR_INSUFFICIENT_BUFFER)
- ✅ Second `GetTokenInformation()` call now logs errors before returning

**Commit**: Fixed in "Fix medium-priority silent failures" commit

---

### 5. **UsnMonitor.cpp** - Could Use LoggingUtils for Consistency ✅ FIXED

**Location**: `UsnMonitor.cpp` lines 337-383  
**Status**: ✅ **COMPLETED** (2026-01-01)

**Fixed:**
- ✅ `DeviceIoControl(FSCTL_READ_USN_JOURNAL)` errors now use `LogWindowsApiError()`
- ✅ All error messages include volume path context
- ✅ Consistent error logging format across all DeviceIoControl calls
- ✅ Consecutive errors message includes volume path

**Commit**: Fixed in "Fix medium-priority silent failures" commit

---

## Low Priority (Nice to Have)

### 6. **DirectXManager.cpp** - Missing Error Codes

**Location**: `DirectXManager.cpp`  
**Issue**: DirectX initialization errors are logged but don't include HRESULT details

**Calls:**
- `CreateDeviceD3D()` failures - Logs "Failed to initialize Direct3D" but no HRESULT
- DirectX API calls inside `CreateDeviceD3D()` - May not have error logging

**Note**: DirectX uses HRESULT, not DWORD error codes. Would need HRESULT logging helper.

---

## Recommended Fix Order

### ✅ Phase 1: High Priority (1-2 hours) - COMPLETED
1. ✅ Fix `SHOpenFolderAndSelectItems()` unchecked return (FileOperations_win.cpp)
2. ✅ Add HRESULT logging helper to LoggingUtils
3. ✅ Fix ShellContextUtils.cpp COM API error logging

### ✅ Phase 2: Medium Priority (1 hour) - COMPLETED
4. ✅ Fix PrivilegeUtils.cpp silent returns
5. ✅ Standardize UsnMonitor.cpp error logging to use LoggingUtils

### Phase 3: Low Priority (Future) - PENDING
6. Add DirectX error logging (if needed)

---

## Helper Function Needed

**Add to LoggingUtils.h/cpp:**

```cpp
// Log HRESULT error (COM/ActiveX errors)
void LogHResultError(const std::string& operation,
                    const std::string& context,
                    HRESULT hr);
```

**Implementation:**
```cpp
void LogHResultError(const std::string& operation,
                    const std::string& context,
                    HRESULT hr) {
  // Convert HRESULT to DWORD error code
  DWORD error_code = HRESULT_CODE(hr);
  std::string error_msg = GetWindowsErrorString(error_code);
  
  LOG_ERROR_BUILD(operation << " failed: " << error_msg 
                  << " (HRESULT: 0x" << std::hex << hr << std::dec << ")"
                  << ". Context: " << context);
}
```

---

## Summary

**Total Silent Failures**: 6 categories
- **High Priority**: 3 ✅ **COMPLETED** (ShellContextUtils, SHOpenFolderAndSelectItems, HRESULT logging)
- **Medium Priority**: 2 ✅ **COMPLETED** (PrivilegeUtils, UsnMonitor consistency)
- **Low Priority**: 1 ⏸️ **PENDING** (DirectXManager)

**Completed Effort**: ~3 hours (high + medium priority fixes)
**Remaining Effort**: ~30 minutes (low priority - optional)

**Impact**: Improved debuggability for COM/Shell API failures, privilege operations, and USN Journal operations

**Status**: High and medium-priority silent failures have been fixed. All Windows API, COM/Shell API, and privilege operation errors now use standardized logging with full context and readable error messages.

