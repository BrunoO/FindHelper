# Conditional Compilation Refactoring Candidates

**Date:** 2025-12-28  
**Purpose:** Identify headers that could benefit from the same refactoring pattern applied to `FileOperations.h`

---

## Summary

After refactoring `FileOperations.h` to remove conditional compilation blocks by using unified APIs and `PlatformTypes.h`, several other headers could benefit from similar improvements.

---

## High Priority Candidates

### 1. `ui/ResultsTable.h` ✅ **COMPLETED**

**Previous State:**
```cpp
// Platform-specific window handle type alias
// Matches UIRenderer.h definition for consistency
#ifdef _WIN32
#include <windows.h>
using NativeWindowHandle = HWND;
#else
using NativeWindowHandle = void*;
#endif
```

**Problem:** Duplicated `NativeWindowHandle` definition that already exists in `PlatformTypes.h`

**Refactoring Applied:**
- ✅ Replaced local definition with `#include "PlatformTypes.h"`
- ✅ Removed conditional compilation block
- ✅ Single source of truth for `NativeWindowHandle`

**Status:** ✅ **COMPLETED** (2025-12-28)  
**Estimated Effort:** 5 minutes  
**Risk Level:** Very Low  
**Impact:** Eliminates code duplication, ensures consistency

---

## Medium Priority Candidates

### 2. `StringUtils.h` - Encoding Functions ✅ **COMPLETED**

**Previous State:**
```cpp
inline std::string WideToUtf8(const std::wstring &wstr) {
#ifdef _WIN32
  // Windows implementation using WideCharToMultiByte
#else
  // Stub implementation
#endif
}

inline std::wstring Utf8ToWide(std::string_view str) {
#ifdef _WIN32
  // Windows implementation using MultiByteToWideChar
#else
  // Stub implementation
#endif
}
```

**Problem:** Conditional compilation in inline function implementations

**Refactoring Applied:**
- ✅ Moved implementations to platform-specific files:
  - `StringUtils.cpp` (Windows): Uses WideCharToMultiByte/MultiByteToWideChar
  - `StringUtils_mac.cpp` (macOS): Stub implementation
  - `StringUtils_linux.cpp` (Linux): Stub implementation
- ✅ Removed all conditional compilation from header
- ✅ Clean declarations-only header
- ✅ Removed Windows-specific includes from header
- ✅ Updated CMakeLists.txt for all platforms

**Performance Consideration:**
- Functions changed from inline to out-of-line
- Impact is minimal: These functions already call Windows APIs (WideCharToMultiByte/MultiByteToWideChar) which dominate execution time
- Function call overhead (~1-5ns) is negligible compared to API call overhead (~100-1000ns)
- Used 40+ times across 21 files, but not in tight inner loops

**Status:** ✅ **COMPLETED** (2025-12-28)  
**Estimated Effort:** 1-2 hours  
**Risk Level:** Low-Medium (touches frequently-used functions)  
**Impact:** Cleaner header, minimal performance impact (API calls dominate overhead)

---

### 3. `ThreadUtils.h` - SetThreadName Function ✅ **COMPLETED**

**Previous State:**
```cpp
inline void SetThreadName(const char* threadName) {
#ifdef _WIN32
  // Windows implementation using SetThreadDescription
#elif defined(__APPLE__)
  // macOS implementation using pthread_setname_np
#else
  // Stub for other platforms
#endif
}
```

**Problem:** Large conditional compilation block in inline function

**Refactoring Applied:**
- ✅ Moved implementation to platform-specific files:
  - `ThreadUtils.cpp` (Windows)
  - `ThreadUtils_mac.cpp` (macOS)
  - `ThreadUtils_linux.cpp` (Linux)
- ✅ Removed all conditional compilation from header
- ✅ Clean declaration-only header
- ✅ Updated CMakeLists.txt for all platforms and test executables

**Status:** ✅ **COMPLETED** (2025-12-28)  
**Estimated Effort:** 30 minutes  
**Risk Level:** Low  
**Impact:** Cleaner header, minimal performance impact (function called infrequently)

---

## Low Priority Candidates

### 4. `FileSystemUtils.h` - File Attribute Functions ⚠️ **DEFERRED - Performance Critical**

**Current State:**
- Function signatures are already unified (no `#ifdef` in signatures)
- Conditional compilation only in implementations
- Functions are inline for performance

**Problem:** Large conditional compilation blocks in inline implementations

**Refactoring:**
- Move implementations to platform-specific files:
  - `FileSystemUtils.cpp` (Windows)
  - `FileSystemUtils_mac.cpp` (macOS)
  - `FileSystemUtils_linux.cpp` (Linux)
- Keep declarations in header

**Estimated Effort:** 2-3 hours  
**Risk Level:** Medium (touches file I/O functions used in hot paths)  
**Impact:** Cleaner header, but may impact performance (inline → out-of-line)

**Decision:** ⚠️ **DEFERRED - Keep functions inline for performance**

**Rationale:**
- These functions (`GetFileAttributes`, `GetFileSize`, `GetFileModificationTime`, etc.) are called frequently during lazy loading operations (hot paths)
- They are used in performance-critical code paths where function call overhead matters
- Moving from inline to out-of-line would add function call overhead (~1-5ns per call) multiplied by thousands of calls during file attribute loading
- The conditional compilation blocks, while present, are contained within the implementations and don't affect the public API
- **Performance takes priority over header cleanliness for these functions**

**Status:** ⚠️ **DEFERRED** - Will remain inline to preserve performance

---

### 5. `Logger.h` - Platform-Specific Code

**Current State:**
- Header-only singleton class
- Many conditional compilation blocks throughout
- Platform-specific includes, implementations, and helper functions

**Problem:** Extensive conditional compilation throughout the class

**Refactoring:**
- Create `Logger.cpp` and move platform-specific implementations
- Convert inline methods to out-of-line
- More complex because it's a singleton with inline methods

**Estimated Effort:** 4-6 hours  
**Risk Level:** Medium-High (touches core logging infrastructure)  
**Impact:** Much cleaner header, but significant refactoring required

**Note:** Already identified in architectural review as complex. Deferred to future phase.

---

## Recommendations

### Immediate Action (Quick Win)
1. ✅ **`ui/ResultsTable.h`** - Replace local `NativeWindowHandle` definition with `PlatformTypes.h`
   - **Effort:** 5 minutes
   - **Risk:** Very Low
   - **Impact:** Eliminates duplication, ensures consistency

### Short Term
2. **`ThreadUtils.h`** - Move `SetThreadName` implementation to platform files
   - **Effort:** 30 minutes
   - **Risk:** Low
   - **Impact:** Cleaner header, minimal performance impact

### Medium Term (Performance Consideration)
3. ✅ **`StringUtils.h`** - Moved encoding functions to `.cpp` files
   - **Effort:** 1-2 hours
   - **Risk:** Low-Medium
   - **Impact:** Cleaner header, minimal performance impact (API calls dominate overhead)
   - **Status:** ✅ **COMPLETED** - Performance impact acceptable (API calls dominate)

4. ⚠️ **`FileSystemUtils.h`** - Keep file I/O functions inline
   - **Effort:** 2-3 hours (if refactored)
   - **Risk:** Medium
   - **Impact:** Cleaner header, but significant performance impact (hot paths)
   - **Decision:** ⚠️ **DEFERRED** - Keep inline for performance (functions called thousands of times during lazy loading)

### Long Term
5. **`Logger.h`** - Major refactoring to extract platform-specific code
   - **Effort:** 4-6 hours
   - **Risk:** Medium-High
   - **Impact:** Much cleaner header, but significant refactoring
   - **Decision:** Defer to future phase (already noted in architectural review)

---

## Pattern to Follow

For all refactorings, follow the pattern established with `FileOperations.h`:

1. **Create unified API in header** (no `#ifdef` in function signatures)
2. **Use type aliases from `PlatformTypes.h`** when applicable
3. **Move platform-specific implementations to `.cpp`/`.mm` files**
4. **Keep conditional compilation only in implementation files**
5. **Update call sites to use unified API**

---

## Testing Strategy

For each refactoring:
1. Build on all platforms (Windows, macOS, Linux)
2. Run existing tests to ensure no regressions
3. Verify performance impact (if moving inline → out-of-line)
4. Check that all call sites are updated

---

## Conclusion

**Completed Refactorings:**
- ✅ `ui/ResultsTable.h` - Replaced local `NativeWindowHandle` with `PlatformTypes.h`
- ✅ `StringUtils.h` - Moved encoding functions to platform-specific `.cpp` files
- ✅ `ThreadUtils.h` - Moved `SetThreadName` to platform-specific `.cpp` files

**Deferred Refactorings:**
- ⚠️ `FileSystemUtils.h` - **DEFERRED** - Keep functions inline for performance (hot paths)
- ⚠️ `Logger.h` - **DEFERRED** - Complex refactoring, defer to future phase

**Remaining Work:**
All high and medium priority candidates have been completed. The remaining candidates (`FileSystemUtils.h` and `Logger.h`) are deferred due to performance considerations or complexity.

