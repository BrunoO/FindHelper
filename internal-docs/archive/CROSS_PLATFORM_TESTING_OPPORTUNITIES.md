# Cross-Platform Testing Opportunities

## Executive Summary

This document identifies additional areas of the codebase that could benefit from cross-platform testing (similar to `StringSearchTests.cpp`), along with the refactorings needed to enable such tests.

**Current State**: We can run tests on macOS for:
- ✅ StringSearch (string matching algorithms)
- ✅ CpuFeatures (CPU detection)
- ✅ PathUtils (path truncation)
- ✅ StringSearchAVX2 (SIMD string search)
- ✅ FileIndexSearchStrategy (with Windows type stubs)

**Goal**: Identify more pure C++ utilities that can be tested cross-platform to improve development iteration speed.

---

## High-Priority Opportunities

### 1. ⭐ **SimpleRegex.h** - Regex and Glob Matching

**Current State**:
- Pure C++ implementation (no Windows dependencies)
- Contains: `RegExMatch`, `RegExMatchI`, `GlobMatch`, `GlobMatchI`
- Already cross-platform ready

**Refactoring Needed**: **NONE** ✅
- No Windows dependencies
- All functions are inline and pure C++
- Can be tested immediately

**Test Coverage Opportunities**:
- Basic regex patterns (`.`, `*`, `^`, `$`)
- Case-sensitive vs case-insensitive matching
- Glob patterns (`*`, `?`)
- Edge cases (empty strings, special characters)
- Performance with long strings

**Estimated Effort**: **Low** (1-2 hours)
- Create `tests/SimpleRegexTests.cpp`
- Test all 4 main functions
- Test edge cases and special characters

---

### 2. ⭐ **StringUtils.h** - String Manipulation Functions

**Current State**:
- Contains both Windows-dependent (encoding) and pure C++ functions
- Pure C++ functions: `ToLower`, `Trim`, `GetFilename`, `GetExtension`, `ParseExtensions`
- Windows-dependent: `WideToUtf8`, `Utf8ToWide` (already have conditional compilation)

**Refactoring Needed**: **MINIMAL** ✅
- Pure C++ functions already work cross-platform
- Encoding functions already have `#ifdef _WIN32` stubs

**Test Coverage Opportunities**:
- `ToLower`: Case conversion, special characters, empty strings
- `Trim`: Leading/trailing whitespace, empty strings, all-whitespace strings
- `GetFilename`: Path separators (`\`, `/`), edge cases
- `GetExtension`: Multiple dots, no extension, hidden files (`.gitignore`)
- `ParseExtensions`: Semicolon-separated lists, whitespace handling, leading dots

**Estimated Effort**: **Low** (2-3 hours)
- Create `tests/StringUtilsTests.cpp`
- Test all pure C++ functions
- Focus on edge cases

---

### 3. ⭐ **FileTimeTypes.h** - FILETIME Helper Functions

**Current State**:
- Contains helper functions: `IsSentinelTime`, `IsFailedTime`, `IsValidTime`, `IsEpochTime`
- Uses `FILETIME` type (available via `WindowsTypesStub.h` for tests)
- Pure logic functions (no Windows API calls)

**Refactoring Needed**: **NONE** ✅
- Already uses conditional compilation for `FILETIME` type
- Functions are pure logic (no Windows API dependencies)

**Test Coverage Opportunities**:
- `IsSentinelTime`: Test sentinel value detection
- `IsFailedTime`: Test failed value detection
- `IsValidTime`: Test valid vs invalid times
- `IsEpochTime`: Test epoch detection, timezone edge cases
- Edge cases: All-zero, all-ones, boundary values

**Estimated Effort**: **Low** (1-2 hours)
- Create `tests/FileTimeTypesTests.cpp`
- Test all helper functions
- Test boundary conditions

---

### 4. ⭐ **FileSystemUtils.h** - FormatFileSize Function

**Current State**:
- `FormatFileSize`: Pure C++ function (no Windows dependencies)
- Formats bytes as human-readable strings (B, KB, MB, GB)
- Already cross-platform

**Refactoring Needed**: **NONE** ✅
- Pure C++ implementation
- No Windows dependencies

**Test Coverage Opportunities**:
- Zero bytes
- Small values (< 1024 bytes)
- KB range (1024 - 1MB)
- MB range (1MB - 1GB)
- GB range (>= 1GB)
- Boundary values (1023, 1024, 1025, etc.)
- Precision of decimal formatting

**Estimated Effort**: **Low** (1 hour)
- Add tests to existing test file or create new one
- Test all size ranges and boundaries

---

## Medium-Priority Opportunities

### 5. **UsnRecordUtils.h** - Type Conversion Utilities

**Current State**:
- Contains type-safe conversion functions: `SizeOfUsnRecordV2`, `SizeOfUsn`, `ToDword`, `AddWords`
- Uses Windows types: `DWORD`, `WORD`, `USN_RECORD_V2`
- Pure logic functions (no Windows API calls)

**Refactoring Needed**: **MODERATE**
- Need to stub Windows types (`DWORD`, `WORD`, `USN_RECORD_V2`) in `WindowsTypesStub.h`
- Functions are pure logic, so easy to test once types are stubbed

**Test Coverage Opportunities**:
- Type conversions (WORD to DWORD)
- Safe arithmetic (AddWords)
- Size calculations
- Boundary values (max WORD, max DWORD)

**Estimated Effort**: **Medium** (2-3 hours)
- Extend `WindowsTypesStub.h` with `DWORD`, `WORD`, `USN_RECORD_V2` definitions
- Create `tests/UsnRecordUtilsTests.cpp`
- Test all conversion functions

**Note**: This is lower priority because these are simple type conversion utilities with limited logic to test.

---

### 6. **FileSystemUtils.h** - FormatFileTime Function (Partial)

**Current State**:
- `FormatFileTime`: Uses Windows APIs (`FileTimeToLocalFileTime`, `FileTimeToSystemTime`)
- Has conditional compilation for non-Windows (returns empty string)

**Refactoring Needed**: **MODERATE**
- Need to stub Windows time conversion APIs for tests
- Or: Test only the sentinel/failed value handling (which is pure C++)

**Test Coverage Opportunities**:
- Sentinel value handling (returns `"..."`)
- Failed value handling (returns `"N/A"`)
- Format validation (if we stub Windows APIs)

**Estimated Effort**: **Medium** (2-3 hours)
- Option 1: Stub Windows time APIs (complex)
- Option 2: Test only sentinel/failed handling (simple, but limited coverage)

**Recommendation**: **Option 2** - Test only the pure C++ parts (sentinel/failed handling). The Windows API parts are well-tested by Windows itself.

---

## Low-Priority Opportunities

### 7. **HashMapAliases.h** - Type Aliases

**Current State**:
- Contains type aliases for hash maps/sets
- No logic to test (just type definitions)

**Refactoring Needed**: **NONE**
**Test Coverage**: **NONE** (no logic to test)

**Verdict**: **Skip** - No testable logic.

---

### 8. **ThreadUtils.h** - Thread Name Setting

**Current State**:
- `SetThreadName`: Platform-specific implementation
- Uses Windows API (`SetThreadDescription`) or pthread

**Refactoring Needed**: **HIGH**
- Would need to mock thread APIs
- Limited value (mostly tests platform API, not our logic)

**Verdict**: **Skip** - Low value, high effort.

---

## Refactoring Summary

### No Refactoring Needed ✅
1. **SimpleRegex.h** - Ready to test
2. **StringUtils.h** (pure C++ functions) - Ready to test
3. **FileTimeTypes.h** - Ready to test
4. **FileSystemUtils.h::FormatFileSize** - Ready to test

### Minimal Refactoring Needed
- None identified (all high-priority items are ready)

### Moderate Refactoring Needed
1. **UsnRecordUtils.h** - Need to stub `DWORD`, `WORD`, `USN_RECORD_V2` types
2. **FileSystemUtils.h::FormatFileTime** - Partial testing (sentinel/failed only)

---

## Recommended Implementation Order

### Phase 1: Quick Wins (High Value, Low Effort)
1. **SimpleRegexTests.cpp** (1-2 hours)
2. **StringUtilsTests.cpp** (2-3 hours)
3. **FileTimeTypesTests.cpp** (1-2 hours)
4. **FormatFileSize tests** (1 hour)

**Total Effort**: ~5-8 hours
**Value**: High - Tests core utility functions used throughout the codebase

### Phase 2: Medium Priority (If Needed)
1. **UsnRecordUtilsTests.cpp** (2-3 hours, requires type stubs)
2. **FormatFileTime partial tests** (1-2 hours, sentinel/failed only)

**Total Effort**: ~3-5 hours
**Value**: Medium - Tests type safety utilities

---

## Benefits of Cross-Platform Testing

### Development Speed
- ✅ Run tests locally on macOS without Windows VM
- ✅ Faster iteration cycle (no need to compile on Windows)
- ✅ Catch bugs earlier in development

### Code Quality
- ✅ Better test coverage for pure C++ utilities
- ✅ Tests edge cases that might be missed
- ✅ Documents expected behavior

### Maintainability
- ✅ Tests serve as documentation
- ✅ Prevents regressions when refactoring
- ✅ Makes code more robust

---

## Implementation Notes

### Test File Structure
Follow the existing pattern:
```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "SimpleRegex.h"  // or other header

TEST_SUITE("SimpleRegex") {
    TEST_SUITE("RegExMatch") {
        TEST_CASE("basic patterns") {
            // Test cases
        }
    }
}
```

### Windows Type Stubs
For any new Windows types needed, extend `tests/WindowsTypesStub.h`:
```cpp
#ifndef _WIN32
// Add new type definitions here
typedef uint32_t DWORD;
typedef uint16_t WORD;
// etc.
#endif
```

### Conditional Compilation
Ensure tested headers use conditional compilation:
```cpp
#ifdef _WIN32
#include <windows.h>
#else
#ifdef USN_WINDOWS_TESTS
#include "tests/WindowsTypesStub.h"
#endif
#endif
```

---

## Conclusion

**High-Priority Opportunities** (Ready to implement):
1. ✅ SimpleRegex.h - **No refactoring needed**
2. ✅ StringUtils.h (pure C++ functions) - **No refactoring needed**
3. ✅ FileTimeTypes.h - **No refactoring needed**
4. ✅ FormatFileSize - **No refactoring needed**

**Total Estimated Effort**: 5-8 hours for comprehensive test coverage

**Recommendation**: Start with Phase 1 (SimpleRegex, StringUtils, FileTimeTypes, FormatFileSize) as these provide high value with minimal effort and no refactoring required.

