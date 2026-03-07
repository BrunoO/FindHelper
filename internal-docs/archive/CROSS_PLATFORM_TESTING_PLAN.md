# Cross-Platform Testing Plan

## Executive Summary

This document outlines a plan to make the test suite runnable on macOS (and potentially Linux) to speed up development iteration. The goal is to enable developers to run tests locally on macOS without requiring a Windows build environment, while maintaining the ability to run the same tests on Windows.

**Key Principle**: Abstract Windows-specific types and APIs behind minimal test-only interfaces, allowing tests to run on any platform while the production code remains Windows-only.

---

## Current State Analysis

### Test Files Overview

1. **StringSearchTests.cpp** ✅ **Already Cross-Platform**
   - Tests pure C++ string algorithms
   - No Windows dependencies
   - Can run on macOS immediately

2. **CpuFeaturesTests.cpp** ✅ **Already Cross-Platform**
   - `CpuFeatures.cpp` already has macOS/Linux support via inline assembly
   - Tests should work on macOS (x86_64)

3. **PathUtilsTests.cpp** ✅ **Already Cross-Platform**
   - Tests path truncation logic
   - No Windows dependencies
   - Can run on macOS immediately

4. **StringSearchAVX2Tests.cpp** ✅ **Already Cross-Platform**
   - Tests AVX2 string search (with fallback to scalar)
   - No Windows dependencies
   - Can run on macOS immediately

5. **FileIndexSearchStrategyTests.cpp** ❌ **Requires Windows**
   - Includes `<Windows.h>` directly (line 14)
   - Uses `FILETIME` type from Windows
   - Uses `kFileTimeNotLoaded` constant from `StringUtils.h` (which includes Windows.h)

### Windows Dependencies in Tested Code

1. **FileIndex.h/cpp**
   - Uses `FILETIME` type (Windows-specific)
   - Uses `kFileTimeNotLoaded` and `kFileTimeFailed` constants
   - No direct Windows API calls in testable code paths

2. **FileSystemUtils.h** ✅ **REFACTORED - Better Separation**
   - Defines `FILETIME` sentinel constants (`kFileTimeNotLoaded`, `kFileTimeFailed`)
   - Contains Windows file I/O functions (GetFileAttributes, GetFileSize, etc.)
   - Contains FILETIME helper functions (IsSentinelTime, FormatFileTime, etc.)
   - **Tests only use**: `kFileTimeNotLoaded` constant (no file I/O calls)
   - **Isolation**: File I/O functions are isolated and can be stubbed for tests

3. **StringUtils.h** ✅ **REFACTORED - Cleaner**
   - Contains UTF-8/UTF-16 encoding functions (uses Windows.h for WideCharToMultiByte)
   - Contains pure string manipulation (ToLower, Trim, GetExtension, etc.)
   - **Tests don't use**: Encoding functions (WideToUtf8, Utf8ToWide)
   - **Tests may use**: String manipulation functions (cross-platform)

4. **CpuFeatures.cpp**
   - ✅ Already has cross-platform CPUID support
   - Uses `#ifdef _WIN32` for Windows vs. Unix paths

---

## Solution Strategy

### Approach: Minimal Windows Type Stubs for Tests

Instead of making the entire codebase cross-platform, we'll create a **test-only abstraction layer** that provides minimal Windows type definitions when building tests on non-Windows platforms.

**Key Design Decisions:**

1. **Test-Only Abstraction**: Create a header that defines Windows types only when building tests on non-Windows platforms
2. **No Production Code Changes**: Production code remains Windows-only; abstraction is only for tests
3. **Conditional Compilation**: Use `#ifdef` to include Windows types only when needed
4. **Minimal Surface Area**: Only define types actually used by tests (FILETIME, sentinel constants)

---

## Impact of Recent Refactoring ✅

**Good News**: The refactoring that split `StringUtils.h` into `StringUtils.h` + `FileSystemUtils.h` **significantly simplifies** the cross-platform testing work:

### Benefits:

1. **Better Separation of Concerns**:
   - `FileSystemUtils.h` now contains all FILETIME-related code (constants, file I/O, formatting)
   - `StringUtils.h` contains pure string manipulation (mostly cross-platform)
   - Windows file I/O is isolated in one file

2. **Easier Stubbing**:
   - Only `FileSystemUtils.h` needs conditional compilation for FILETIME
   - File I/O functions are isolated (tests don't call them, but can be easily stubbed)
   - String manipulation functions in `StringUtils.h` are already cross-platform

3. **Reduced Surface Area**:
   - Tests only use `kFileTimeNotLoaded` from `FileSystemUtils.h`
   - Tests don't use encoding functions from `StringUtils.h` (WideToUtf8, Utf8ToWide)
   - Tests don't call file I/O functions (GetFileAttributes, etc.)

### What Changed in the Plan:

- ✅ **Phase 2 updated**: Now targets `FileSystemUtils.h` instead of `StringUtils.h`
- ✅ **Simpler implementation**: Only one file needs FILETIME stubs
- ✅ **Less conditional compilation**: String manipulation doesn't need changes

---

## Additional Refactoring Recommendations

While the current refactoring helps significantly, here are additional refactoring opportunities that would further simplify cross-platform testing:

### Recommendation 1: Extract FILETIME Constants to Separate Header ⭐ **HIGH VALUE**

**Current**: `FILETIME` constants are in `FileSystemUtils.h` alongside file I/O functions.

**Proposed**: Create `FileTimeTypes.h` with just the types and constants:

```cpp
// FileTimeTypes.h
#pragma once

#ifdef _WIN32
#include <windows.h>
#else
#ifdef USN_WINDOWS_TESTS
#include "tests/WindowsTypesStub.h"
#endif
#endif

// Sentinel values
const FILETIME kFileTimeNotLoaded = {UINT32_MAX, UINT32_MAX};
const FILETIME kFileTimeFailed = {0, 1};

// Helper functions (pure logic, no Windows APIs)
inline bool IsSentinelTime(const FILETIME &ft) { ... }
inline bool IsFailedTime(const FILETIME &ft) { ... }
inline bool IsValidTime(const FILETIME &ft) { ... }
```

**Benefits**:
- `FileIndex.h` can include `FileTimeTypes.h` without pulling in file I/O code
- Tests can include `FileTimeTypes.h` without Windows dependencies
- Clear separation: types/constants vs. file I/O operations

**Effort**: Low (extract constants and pure helper functions)

---

### Recommendation 2: Separate File I/O from File Time Formatting ⭐ **MEDIUM VALUE**

**Current**: `FormatFileTime()` is in `FileSystemUtils.h` and uses Windows APIs.

**Proposed**: Split into:
- `FileTimeTypes.h`: Types, constants, pure helper functions
- `FileSystemUtils.h`: File I/O operations (GetFileAttributes, etc.)
- `FileTimeFormatting.h`: Formatting functions (can be stubbed for tests)

**Benefits**:
- Tests that don't format times don't need formatting code
- Formatting can be stubbed independently from file I/O
- Clearer dependencies

**Effort**: Medium (requires updating includes across codebase)

---

### Recommendation 3: Create Test-Friendly StringUtils Header ⭐ **LOW VALUE** (Not Needed Yet)

**Current**: `StringUtils.h` has encoding functions (WideToUtf8, Utf8ToWide) that use Windows APIs.

**Proposed**: Split into:
- `StringUtils.h`: Pure string manipulation (ToLower, Trim, GetExtension) - already cross-platform
- `StringEncoding.h`: Encoding functions (Windows-only, not used by tests)

**Benefits**:
- Tests can use `StringUtils.h` without Windows dependencies
- Encoding functions are isolated

**Effort**: Low, but **not needed** since tests don't use encoding functions

**Recommendation**: **Skip this** unless tests start using encoding functions.

---

### Recommendation 4: Abstract File I/O Behind Interface (Future) ⭐ **FUTURE CONSIDERATION**

**Current**: `FileIndex.cpp` may call `GetFileAttributes()` from `FileSystemUtils.h` directly.

**Proposed**: Create a file I/O interface that can be mocked/stubbed:

```cpp
// FileIOInterface.h
struct FileIOInterface {
    virtual FileAttributes GetFileAttributes(std::string_view path) = 0;
    virtual uint64_t GetFileSize(std::string_view path) = 0;
    virtual FILETIME GetFileModificationTime(std::string_view path) = 0;
};

// FileSystemUtils.h provides Windows implementation
// Test code can provide stub implementation
```

**Benefits**:
- Easier to test FileIndex without real file I/O
- Can inject mock implementations for testing
- Better testability

**Effort**: High (requires refactoring FileIndex to use interface)

**Recommendation**: **Defer** - current approach (conditional compilation) is sufficient for cross-platform testing.

---

### Summary of Recommendations

| Recommendation | Value | Effort | Priority |
|---------------|-------|--------|----------|
| Extract FILETIME constants | High | Low | ⭐ Do this |
| Separate formatting from I/O | Medium | Medium | Consider |
| Split StringUtils encoding | Low | Low | Skip (not needed) |
| Abstract file I/O interface | Future | High | Defer |

**Recommended Action**: Implement Recommendation 1 (extract FILETIME constants) before starting cross-platform testing work. This will make the implementation cleaner and easier to maintain.

---

## Implementation Plan

### Phase 1: Create Windows Type Stubs (Foundation)

**Goal**: Provide minimal Windows type definitions for tests on non-Windows platforms.

**Files to Create:**

1. **`tests/WindowsTypesStub.h`** (New file)
   ```cpp
   #pragma once
   
   // Minimal Windows type definitions for cross-platform testing
   // Only included when building tests on non-Windows platforms
   
   #ifndef _WIN32
   
   // FILETIME structure (matches Windows definition)
   struct FILETIME {
       unsigned int dwLowDateTime;
       unsigned int dwHighDateTime;
   };
   
   // Sentinel values matching StringUtils.h
   const FILETIME kFileTimeNotLoaded = {UINT32_MAX, UINT32_MAX};
   const FILETIME kFileTimeFailed = {0, 1};
   
   #endif // !_WIN32
   ```

**Rationale**: 
- Provides exact same type layout as Windows
- Allows tests to compile and run on macOS
- No changes to production code required

---

### Phase 2: Update FileSystemUtils.h for Test Compatibility ✅ **UPDATED - Refactoring Helps**

**Goal**: Make `FileSystemUtils.h` testable without requiring full Windows.h on non-Windows platforms.

**Current State** (after refactoring):
- `FileSystemUtils.h` contains `FILETIME` constants (`kFileTimeNotLoaded`, `kFileTimeFailed`)
- `FileSystemUtils.h` contains Windows file I/O functions (not used by tests)
- Tests only need the `FILETIME` constants, not the file I/O functions

**Solution**: Use conditional includes for Windows types and stub file I/O functions.

**Changes to `FileSystemUtils.h`:**

```cpp
#pragma once

#include <cstdio>
#include <string>
#include <string_view>

// Conditional Windows includes
#ifdef _WIN32
#include <windows.h>
#include <propkey.h>
#include <shlwapi.h>
#include <shobjidl.h>
#else
// For tests on non-Windows: use stub definitions
#ifdef USN_WINDOWS_TESTS
#include "tests/WindowsTypesStub.h"
#endif
#endif

#include "StringUtils.h"  // For Utf8ToWide

// FILETIME sentinel constants (available on all platforms via stub on non-Windows)
const FILETIME kFileTimeNotLoaded = {UINT32_MAX, UINT32_MAX};
const FILETIME kFileTimeFailed = {0, 1};

// ... FileAttributes struct ...

// File I/O functions - conditional compilation
#ifdef _WIN32
// Real Windows implementation
inline FileAttributes GetFileAttributes(std::string_view path) {
    // ... existing Windows implementation ...
}
#else
// Test stub: return sentinel values (tests don't exercise file I/O)
inline FileAttributes GetFileAttributes(std::string_view path) {
    return {0, kFileTimeNotLoaded, false};
}
#endif

// Similar conditional compilation for GetFileSize(), GetFileModificationTime()
// FormatFileSize() is already cross-platform (pure C++)
// FormatFileTime() needs conditional compilation for Windows APIs
```

**Note**: `StringUtils.h` encoding functions (WideToUtf8, Utf8ToWide) are not used by tests, so they can remain Windows-only. If needed later, they can be stubbed similarly.

---

### Phase 3: Update FileIndexSearchStrategyTests.cpp

**Goal**: Remove direct Windows.h dependency from test file.

**Current Code** (line 14):
```cpp
#include <Windows.h>
```

**Changes**:

1. Remove `#include <Windows.h>`
2. Ensure `FILETIME` is available via `StringUtils.h` (which will use stub on non-Windows)
3. Add conditional include if needed:
   ```cpp
   #ifndef _WIN32
   #define USN_WINDOWS_TESTS  // Enable test mode
   #include "tests/WindowsTypesStub.h"
   #endif
   ```

**Note**: The test already uses `FILETIME` via `kFileTimeNotLoaded` from `StringUtils.h`, so removing `Windows.h` should work once `StringUtils.h` is updated.

---

### Phase 4: Update Build System for Cross-Platform Tests

**Goal**: Enable building and running tests on macOS.

**CMakeLists.txt Changes**:

1. **Add test build configuration**:
   ```cmake
   # Test configuration
   option(BUILD_TESTS "Build test suite" ON)
   option(CROSS_PLATFORM_TESTS "Enable cross-platform test support" ON)
   
   if(BUILD_TESTS)
       # Define test mode for non-Windows platforms
       if(NOT WIN32 AND CROSS_PLATFORM_TESTS)
           add_compile_definitions(USN_WINDOWS_TESTS)
       endif()
       
       # Add test executable
       add_executable(usn_tests
           tests/StringSearchTests.cpp
           tests/CpuFeaturesTests.cpp
           tests/PathUtilsTests.cpp
           tests/StringSearchAVX2Tests.cpp
           tests/FileIndexSearchStrategyTests.cpp
       )
       
       # Link doctest
       target_link_libraries(usn_tests PRIVATE doctest)
       
       # Include directories
       target_include_directories(usn_tests PRIVATE
           ${CMAKE_CURRENT_SOURCE_DIR}
           ${CMAKE_CURRENT_SOURCE_DIR}/external/doctest
       )
       
       # Add test sources that don't require Windows
       # Note: FileIndex.cpp may need conditional compilation
       if(WIN32)
           target_sources(usn_tests PRIVATE
               FileIndex.cpp
               # ... other Windows-specific sources
           )
       else()
           # For non-Windows: only include test-compatible sources
           # FileIndex.cpp may need stubs for Windows API calls
       endif()
   endif()
   ```

2. **Handle FileIndex.cpp Dependencies**:
   - `FileIndex.cpp` may call Windows APIs for file I/O
   - Options:
     a. **Mock Windows APIs**: Create stub implementations for test builds
     b. **Conditional Compilation**: Skip Windows-specific code paths in tests
     c. **Test Only Public Interface**: Test FileIndex through public API only (no direct I/O)

   **Recommendation**: Option (c) - Tests should only exercise public API, not internal I/O operations. If `FileIndex` has Windows-specific I/O in methods used by tests, we'll need to stub those.

---

### Phase 5: Handle FileIndex Windows Dependencies

**Goal**: Make `FileIndex` testable without Windows file I/O.

**Analysis**: 
- `FileIndex::Insert()` accepts `FILETIME` but doesn't require Windows APIs
- `FileIndex::GetFileModificationTimeById()` may call Windows file APIs
- Tests in `FileIndexSearchStrategyTests.cpp` use `CreateTestFileIndex()` which only calls `Insert()` and `RecomputeAllPaths()`

**Solution Options**:

1. **Stub File I/O Methods** (Recommended for tests):
   ```cpp
   // In FileIndex.cpp
   #ifdef USN_WINDOWS_TESTS
   // Test mode: stub file I/O
   FILETIME FileIndex::GetFileModificationTimeById(uint64_t id) const {
       // Return sentinel for tests (tests don't exercise this path)
       return kFileTimeNotLoaded;
   }
   #else
   // Production code: real Windows implementation
   FILETIME FileIndex::GetFileModificationTimeById(uint64_t id) const {
       // ... existing Windows implementation
   }
   #endif
   ```

2. **Conditional Compilation for Windows APIs**:
   ```cpp
   #ifdef _WIN32
   #include <windows.h>
   #include <fileapi.h>  // For GetFileAttributesEx, etc.
   #endif
   
   FILETIME FileIndex::GetFileModificationTimeById(uint64_t id) const {
       #ifdef _WIN32
       // Real Windows implementation
       #else
       // Test stub: return sentinel
       return kFileTimeNotLoaded;
       #endif
   }
   ```

**Recommendation**: Use conditional compilation (`#ifdef _WIN32`) rather than a separate test define, as it's clearer and doesn't require maintaining a separate test mode.

---

### Phase 6: Create Test Build Script for macOS

**Goal**: Provide easy way to build and run tests on macOS.

**Create `scripts/build_tests_macos.sh`**:

```bash
#!/bin/bash
# Build and run tests on macOS

set -e

BUILD_DIR="build_tests"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with test support
cmake .. \
    -DBUILD_TESTS=ON \
    -DCROSS_PLATFORM_TESTS=ON \
    -DCMAKE_BUILD_TYPE=Debug

# Build tests
cmake --build . --target usn_tests

# Run tests
./usn_tests
```

**Usage**:
```bash
chmod +x scripts/build_tests_macos.sh
./scripts/build_tests_macos.sh
```

---

## Detailed Implementation Steps

### Step 1: Create WindowsTypesStub.h

**File**: `tests/WindowsTypesStub.h`

```cpp
#pragma once

// Minimal Windows type definitions for cross-platform testing
// This file is only used when building tests on non-Windows platforms
// to allow test code to compile without Windows.h

#ifndef _WIN32

#include <cstdint>

// FILETIME structure (matches Windows winbase.h definition exactly)
// dwLowDateTime: Low-order 32 bits of the file time
// dwHighDateTime: High-order 32 bits of the file time
struct FILETIME {
    uint32_t dwLowDateTime;
    uint32_t dwHighDateTime;
    
    // Allow initialization: FILETIME{0, 0}
    FILETIME() : dwLowDateTime(0), dwHighDateTime(0) {}
    FILETIME(uint32_t low, uint32_t high) : dwLowDateTime(low), dwHighDateTime(high) {}
    
    // Comparison operators for test assertions
    bool operator==(const FILETIME& other) const {
        return dwLowDateTime == other.dwLowDateTime && 
               dwHighDateTime == other.dwHighDateTime;
    }
    bool operator!=(const FILETIME& other) const {
        return !(*this == other);
    }
};

#endif // !_WIN32
```

**Rationale**:
- Exact match to Windows FILETIME structure
- Includes constructors for easy initialization in tests
- Comparison operators for test assertions

---

### Step 2: Update FileSystemUtils.h ✅ **UPDATED - After Refactoring**

**Current State**: Contains `FILETIME` constants and Windows file I/O functions.

**Changes**:

```cpp
#pragma once

#include <cstdio>
#include <string>
#include <string_view>

// Conditional Windows includes
#ifdef _WIN32
#include <windows.h>
#include <propkey.h>
#include <shlwapi.h>
#include <shobjidl.h>
#else
// For tests on non-Windows: use stub definitions
#ifdef USN_WINDOWS_TESTS
#include "tests/WindowsTypesStub.h"
#endif
#endif

#include "StringUtils.h"  // For Utf8ToWide

// FILETIME sentinel constants (available via stub on non-Windows)
const FILETIME kFileTimeNotLoaded = {UINT32_MAX, UINT32_MAX};
const FILETIME kFileTimeFailed = {0, 1};

// ... FileAttributes struct ...

// File I/O functions - conditional compilation
#ifdef _WIN32
// Real Windows implementation
inline FileAttributes GetFileAttributes(std::string_view path) {
    // ... existing implementation ...
}
#else
// Test stub (tests don't call file I/O, but FileIndex.cpp might)
inline FileAttributes GetFileAttributes(std::string_view path) {
    (void)path;  // Suppress unused parameter warning
    return {0, kFileTimeNotLoaded, false};
}
#endif

// Similar for GetFileSize(), GetFileModificationTime()

// FormatFileTime() - conditional compilation for Windows APIs
#ifdef _WIN32
inline std::string FormatFileTime(const FILETIME &ft) {
    // ... existing Windows implementation ...
}
#else
// Test stub: return placeholder
inline std::string FormatFileTime(const FILETIME &ft) {
    if (IsSentinelTime(ft)) return "...";
    if (IsFailedTime(ft)) return "N/A";
    return "";  // Tests don't format real times
}
#endif
```

**Note**: `StringUtils.h` encoding functions are not used by tests, so they can remain Windows-only. No changes needed.

---

### Step 3: Update FileIndexSearchStrategyTests.cpp

**Remove**:
```cpp
#include <Windows.h>  // Line 14 - REMOVE THIS
```

**Add** (if needed):
```cpp
#ifndef _WIN32
#define USN_WINDOWS_TESTS
#endif
```

**Rationale**: `FILETIME` will come from `StringUtils.h` (via stub on non-Windows), so direct `Windows.h` include is not needed.

---

### Step 4: Update FileIndex.cpp (if needed)

**Check**: Does `FileIndex::GetFileModificationTimeById()` or other methods call Windows APIs that tests exercise?

**If Yes**: Add conditional compilation:

```cpp
FILETIME FileIndex::GetFileModificationTimeById(uint64_t id) const {
    // ... existing code to get path ...
    
    #ifdef _WIN32
    // Real Windows file I/O
    FileAttributes attrs = GetFileAttributes(path);
    if (attrs.success) {
        return attrs.lastModificationTime;
    }
    return kFileTimeFailed;
    #else
    // Test stub: return sentinel (tests don't exercise file I/O)
    return kFileTimeNotLoaded;
    #endif
}
```

**If No**: No changes needed (tests don't call Windows-specific methods).

---

### Step 5: Update CMakeLists.txt

**Add test configuration section**:

```cmake
# ============================================================================
# Test Configuration
# ============================================================================

option(BUILD_TESTS "Build test suite" ON)
option(CROSS_PLATFORM_TESTS "Enable cross-platform test support (macOS/Linux)" ON)

if(BUILD_TESTS)
    # Define test mode for non-Windows platforms
    if(NOT WIN32 AND CROSS_PLATFORM_TESTS)
        add_compile_definitions(USN_WINDOWS_TESTS)
        message(STATUS "Cross-platform test mode enabled")
    endif()
    
    # Test sources (all cross-platform compatible)
    set(TEST_SOURCES
        tests/StringSearchTests.cpp
        tests/CpuFeaturesTests.cpp
        tests/PathUtilsTests.cpp
        tests/StringSearchAVX2Tests.cpp
        tests/FileIndexSearchStrategyTests.cpp
    )
    
    # Test executable
    add_executable(usn_tests ${TEST_SOURCES})
    
    # Link doctest
    target_link_libraries(usn_tests PRIVATE doctest)
    
    # Include directories
    target_include_directories(usn_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/external/doctest
        ${CMAKE_CURRENT_SOURCE_DIR}/external/doctest/doctest
    )
    
    # Add sources needed by tests
    # Note: Only include sources that are test-compatible
    target_sources(usn_tests PRIVATE
        FileIndex.cpp
        StringUtils.cpp  # If it exists and is needed
        PathUtils.cpp
        CpuFeatures.cpp
        StringSearchAVX2.cpp  # If separate from header
        Settings.cpp  # If needed by tests
        SearchThreadPool.cpp  # If needed by tests
        Logger.cpp  # If needed by tests
    )
    
    # For non-Windows: may need to exclude Windows-specific sources
    # or provide stubs
    if(WIN32)
        # Windows: include all sources
    else()
        # Non-Windows: may need to conditionally exclude some sources
        # or provide stub implementations
        message(STATUS "Building tests on non-Windows platform - some features may be stubbed")
    endif()
    
    # Enable C++17
    target_compile_features(usn_tests PRIVATE cxx_std_17)
    
    # Add test target to build
    add_custom_target(test
        COMMAND usn_tests
        DEPENDS usn_tests
        COMMENT "Running test suite"
    )
endif()
```

---

### Step 6: Create macOS Build Script

**File**: `scripts/build_tests_macos.sh`

```bash
#!/bin/bash
# Build and run tests on macOS for USN_WINDOWS project
# This allows running tests locally on macOS without Windows build environment

set -e  # Exit on error

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_DIR="$PROJECT_ROOT/build_tests"
mkdir -p "$BUILD_DIR"

cd "$BUILD_DIR"

echo "Configuring CMake for tests..."
cmake "$PROJECT_ROOT" \
    -DBUILD_TESTS=ON \
    -DCROSS_PLATFORM_TESTS=ON \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_STANDARD=17

echo "Building tests..."
cmake --build . --target usn_tests -j$(sysctl -n hw.ncpu)

echo "Running tests..."
./usn_tests

echo "Tests completed successfully!"
```

**Make executable**:
```bash
chmod +x scripts/build_tests_macos.sh
```

---

## Testing Strategy

### Verification Steps

1. **On macOS**:
   ```bash
   ./scripts/build_tests_macos.sh
   ```
   - All tests should compile and run
   - No Windows.h dependencies should be required

2. **On Windows**:
   - Existing test build should continue to work
   - No regressions in test behavior

3. **Test Coverage**:
   - Verify all test files compile on macOS
   - Run test suite and verify all tests pass
   - Check that test behavior matches Windows (especially FILETIME handling)

---

## Potential Issues and Solutions

### Issue 1: FileIndex.cpp Windows API Dependencies

**Problem**: `FileIndex.cpp` may call Windows APIs that don't exist on macOS.

**Solution**: 
- Use conditional compilation (`#ifdef _WIN32`)
- Provide stub implementations for test builds
- Ensure tests don't exercise Windows-specific code paths

**Example**:
```cpp
#ifdef _WIN32
    // Real Windows implementation
    HANDLE hFile = CreateFileA(...);
#else
    // Test stub: return sentinel
    return kFileTimeNotLoaded;
#endif
```

---

### Issue 2: Settings.cpp Windows Dependencies

**Problem**: `Settings.cpp` may use Windows registry or file paths.

**Solution**:
- Tests use `test_settings::SetInMemorySettings()` (already implemented)
- Settings file I/O is not exercised by tests
- No changes needed if tests use in-memory settings

---

### Issue 3: Logger.cpp Windows Dependencies

**Problem**: Logger may use Windows-specific logging APIs.

**Solution**:
- Check if Logger has Windows dependencies
- If yes, provide stub implementation for tests
- Or use conditional compilation

---

### Issue 4: SearchThreadPool.cpp Dependencies

**Problem**: May have Windows-specific thread APIs.

**Solution**:
- C++ standard library threading should work cross-platform
- Check for Windows-specific thread APIs and stub if needed

---

## Success Criteria

✅ **All test files compile on macOS**
✅ **All tests run and pass on macOS**
✅ **No Windows.h dependency in test code**
✅ **Production code remains Windows-only (no changes required)**
✅ **Tests continue to work on Windows (no regressions)**
✅ **Build script works on macOS**

---

## Implementation Order

1. ✅ **Phase 1**: Create `WindowsTypesStub.h`
2. ✅ **Phase 2**: Update `FileSystemUtils.h` for conditional includes ✅ **UPDATED - After Refactoring**
3. ✅ **Phase 3**: Update `FileIndexSearchStrategyTests.cpp` (remove Windows.h)
4. ✅ **Phase 4**: Update CMakeLists.txt for test builds
5. ✅ **Phase 5**: Handle FileIndex.cpp Windows dependencies (if needed)
6. ✅ **Phase 6**: Create macOS build script
7. ✅ **Verification**: Test on macOS and Windows

**Note**: The refactoring that split `StringUtils.h` into `StringUtils.h` + `FileSystemUtils.h` simplifies this work:
- FILETIME constants are now isolated in `FileSystemUtils.h`
- File I/O functions are isolated (tests don't use them)
- String manipulation functions in `StringUtils.h` are already cross-platform

---

## Maintenance Notes

- **Windows Types Stub**: Keep in sync with actual Windows types if new types are added
- **Conditional Compilation**: Document any `#ifdef` blocks added for cross-platform support
- **Test Coverage**: Ensure new tests don't introduce Windows dependencies
- **Build Scripts**: Update if build system changes

---

## Future Enhancements

1. **CI/CD Integration**: Add macOS test runner to CI pipeline
2. **Linux Support**: Extend to Linux (should work with same approach)
3. **Mock Framework**: Consider adding a mocking framework for Windows APIs if needed
4. **Test Documentation**: Document which tests require Windows vs. are cross-platform

---

## References

- **Windows FILETIME Structure**: https://docs.microsoft.com/en-us/windows/win32/api/minwinbase/ns-minwinbase-filetime
- **CMake Cross-Platform Testing**: https://cmake.org/cmake/help/latest/manual/cmake.1.html
- **Doctest Framework**: https://github.com/doctest/doctest

---

## Appendix: File Dependencies Graph

```
FileIndexSearchStrategyTests.cpp
  ├── FileIndex.h (uses FILETIME)
  ├── Settings.h (uses test_settings - already cross-platform)
  ├── StringUtils.h (string manipulation - already cross-platform)
  ├── FileSystemUtils.h (defines kFileTimeNotLoaded - needs Windows.h or stub) ✅ UPDATED
  └── SearchThreadPool.h (should be cross-platform)

FileSystemUtils.h ✅ REFACTORED
  ├── Windows.h (for FILETIME, file I/O APIs) OR
  ├── tests/WindowsTypesStub.h (for tests on non-Windows)
  └── StringUtils.h (for Utf8ToWide - encoding, Windows-only but not used by tests)

StringUtils.h ✅ REFACTORED
  ├── Windows.h (for WideCharToMultiByte - encoding functions)
  └── Pure string functions (ToLower, Trim, GetExtension - cross-platform)

FileIndex.h
  ├── StringUtils.h (for string manipulation)
  └── FileSystemUtils.h (for FILETIME constants)

FileIndex.cpp
  └── May call FileSystemUtils file I/O (needs conditional compilation)
```

**Key Insight from Refactoring**: 
- FILETIME constants are now isolated in `FileSystemUtils.h` (easier to stub)
- File I/O functions are isolated (tests don't use them, but can be stubbed)
- String manipulation is separate from file system code (better separation of concerns)

---

**Document Version**: 1.0  
**Last Updated**: 2025-01-XX  
**Author**: AI Assistant  
**Status**: Planning Phase
