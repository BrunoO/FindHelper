# String Search Functions Extraction and Unit Testing Plan

## Current State Analysis

### Functions to Extract

The following string search functions are currently in `StringUtils.h` and should be extracted for better testability and organization:

1. **`ContainsSubstring`** (2 overloads)
   - Case-sensitive substring search
   - Uses `string_view` and `string` overloads
   - Optimized with prefix check and reverse comparison

2. **`ContainsSubstringI`** (2 overloads)
   - Case-insensitive substring search
   - Similar optimizations to `ContainsSubstring`
   - Uses `ToLowerChar` helper

3. **`StrStrCaseInsensitive`**
   - Case-insensitive `strstr` equivalent
   - Returns pointer to first match or `nullptr`

4. **`ToLowerChar`** (helper)
   - Character-to-lowercase conversion helper
   - Used by case-insensitive functions

### Current Usage

These functions are used in:
- `FileIndex.cpp` - Main search implementation (lines 392, 396, 429, 433, 699, 703, 736, 740, 1004, 1008, 1041, 1045)
- `SearchWorker.cpp` - Search worker thread (line 169)

### Current Structure

All functions are **inline** in `StringUtils.h`, which:
- ✅ **Pros**: No function call overhead, header-only (easy to include)
- ❌ **Cons**: Harder to test in isolation, mixed with other utilities

---

## Extraction Strategy

### Option 1: Separate Header File (Recommended) ⭐

**Approach**: Extract to `StringSearch.h` while keeping functions inline

**Structure**:
```
StringSearch.h          // New file with extracted functions
StringUtils.h          // Keep other utilities (ToLower, Trim, etc.)
```

**Benefits**:
- ✅ Clear separation of concerns
- ✅ Easy to test (can include just `StringSearch.h` in tests)
- ✅ No performance penalty (still inline)
- ✅ Minimal changes to existing code (just update includes)

**Implementation**:
```cpp
// StringSearch.h
#pragma once
#include <string>
#include <string_view>
#include <cctype>

namespace string_search {

// Helper function
inline char ToLowerChar(unsigned char c) {
  return static_cast<char>(std::tolower(c));
}

// Case-sensitive substring search
inline bool ContainsSubstring(const std::string_view &text,
                              const std::string_view &pattern) {
  // ... existing implementation ...
}

inline bool ContainsSubstring(const std::string &text,
                              const std::string &pattern) {
  return ContainsSubstring(std::string_view(text), std::string_view(pattern));
}

// Case-insensitive substring search
inline bool ContainsSubstringI(const std::string_view &text,
                               const std::string_view &pattern) {
  // ... existing implementation ...
}

inline bool ContainsSubstringI(const std::string &text,
                               const std::string &pattern) {
  return ContainsSubstringI(std::string_view(text), std::string_view(pattern));
}

// Case-insensitive strstr
inline const char* StrStrCaseInsensitive(const char* haystack, const char* needle) {
  // ... existing implementation ...
}

} // namespace string_search
```

**Update existing code**:
```cpp
// StringUtils.h - Add forward declaration or include
#include "StringSearch.h"
using string_search::ContainsSubstring;
using string_search::ContainsSubstringI;
using string_search::StrStrCaseInsensitive;
```

**OR** (better approach):
```cpp
// FileIndex.cpp, SearchWorker.cpp - Update includes
#include "StringSearch.h"  // Instead of relying on StringUtils.h
```

---

### Option 2: Header + Source File (For Non-Inline)

**Approach**: Move implementations to `.cpp` file, declare in header

**Structure**:
```
StringSearch.h          // Declarations only
StringSearch.cpp        // Implementations
```

**Benefits**:
- ✅ True separation (implementation hidden)
- ✅ Faster compilation (implementation not in header)
- ✅ Easier to add AVX2 implementations later (can use function pointers)

**Drawbacks**:
- ❌ Function call overhead (unless LTO enabled)
- ❌ More complex build setup
- ❌ Template/constexpr limitations

**Verdict**: **Not recommended** for this use case - inline functions are better for performance-critical search code.

---

### Option 3: Keep in StringUtils.h but Add Tests

**Approach**: Don't extract, just add unit tests

**Benefits**:
- ✅ No code changes needed
- ✅ Can test inline functions

**Drawbacks**:
- ❌ Mixed concerns (search + other utilities)
- ❌ Harder to test in isolation
- ❌ No clear separation for future AVX2 work

**Verdict**: **Not recommended** - extraction provides better organization.

---

## Recommended Approach: Option 1 (Separate Header)

### File Structure

```
StringSearch.h          // New: Extracted search functions (inline)
StringSearch.cpp        // Future: AVX2 implementations (if needed)
StringUtils.h           // Keep: Other utilities (ToLower, Trim, etc.)
```

### Migration Steps

1. **Create `StringSearch.h`**
   - Copy search functions from `StringUtils.h`
   - Wrap in `string_search` namespace
   - Keep all functions inline

2. **Update `StringUtils.h`**
   - Remove search functions
   - Add `#include "StringSearch.h"` if backward compatibility needed
   - Or remove and update all includes

3. **Update consumers**
   - `FileIndex.cpp`: Change `#include "StringUtils.h"` to `#include "StringSearch.h"`
   - `SearchWorker.cpp`: Same change
   - Or use `using` declarations in `StringUtils.h` for backward compatibility

4. **Verify compilation**
   - Ensure all files compile
   - No performance regression (functions still inline)

---

## Unit Testing Framework Recommendations

### Comparison of Popular C++ Testing Frameworks

| Framework | Type | Pros | Cons | Recommendation |
|-----------|------|------|------|----------------|
| **doctest** | Header-only | ✅ Fast compilation<br>✅ Simple API<br>✅ No dependencies<br>✅ Used by unordered_dense | ⚠️ Less popular than Catch2 | ⭐ **Recommended** |
| **Catch2** | Header-only | ✅ Very popular<br>✅ Excellent docs<br>✅ Rich features | ⚠️ Slower compilation<br>⚠️ Larger binary | ✅ **Good alternative** |
| **Google Test** | Library | ✅ Most popular<br>✅ Mature<br>✅ Rich features | ❌ Requires building<br>❌ External dependency<br>❌ More setup | ⚠️ **Overkill for this project** |
| **Boost.Test** | Library | ✅ If already using Boost | ❌ Requires Boost<br>❌ Heavyweight | ❌ **Not recommended** |
| **CppUnitTestFramework** | MSVC | ✅ Native Windows<br>✅ VS integration | ❌ Windows-only<br>❌ Less portable | ⚠️ **Windows-only option** |

---

### Recommended: doctest ⭐

**Why doctest?**
1. **Header-only**: Single header file, no build step
2. **Fast**: Minimal compilation overhead
3. **Simple API**: Easy to learn and use
4. **Already in ecosystem**: Your `unordered_dense` dependency uses it
5. **CMake-friendly**: Easy to integrate
6. **C++17 compatible**: Works with your standard

**Installation**:
```bash
# Option 1: Git submodule (recommended - already done ✅)
git submodule add https://github.com/doctest/doctest.git external/doctest
git submodule update --init --recursive

# Option 2: Download single header
# Download doctest.h from https://github.com/doctest/doctest/releases
# Place in external/doctest/doctest/doctest.h
```

**Note**: When using the git submodule, the header file is located at:
`external/doctest/doctest/doctest.h` (not `external/doctest/doctest.h`)

**CMake Integration**:
```cmake
# Add to CMakeLists.txt
option(BUILD_TESTS "Build unit tests" ON)

if(BUILD_TESTS)
    # doctest location (header is at external/doctest/doctest/doctest.h)
    set(DOCTEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/doctest")
    
    # Check if doctest exists
    if(EXISTS "${DOCTEST_DIR}/doctest/doctest.h")
        message(STATUS "Building unit tests with doctest")
        
        # Create test executable
        add_executable(string_search_tests
            tests/StringSearchTests.cpp
        )
        
        target_include_directories(string_search_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${DOCTEST_DIR}
        )
    
    target_compile_features(string_search_tests PRIVATE cxx_std_17)
    
    # Link with StringSearch if using .cpp file
    # target_link_libraries(string_search_tests PRIVATE string_search)
    
    # Enable testing
    enable_testing()
    add_test(NAME StringSearchTests COMMAND string_search_tests)
endif()
```

---

### Alternative: Catch2

**Why Catch2?**
- More popular (larger community)
- Excellent documentation
- More features (BDD-style, generators, etc.)

**Installation**:
```bash
# Download single header from https://github.com/catchorg/Catch2/releases
# Place in external/catch2/catch.hpp
```

**CMake Integration** (similar to doctest)

---

## Unit Test Implementation Plan

### Test Structure

```
tests/
  StringSearchTests.cpp    # Unit tests for string search functions
  CMakeLists.txt           # Test build configuration (optional)
```

### Test Cases to Implement

#### 1. `ContainsSubstring` Tests

```cpp
// Basic functionality
TEST_CASE("ContainsSubstring - empty pattern returns true") {
    REQUIRE(string_search::ContainsSubstring("hello", ""));
}

TEST_CASE("ContainsSubstring - pattern found at start") {
    REQUIRE(string_search::ContainsSubstring("hello world", "hello"));
}

TEST_CASE("ContainsSubstring - pattern found in middle") {
    REQUIRE(string_search::ContainsSubstring("hello world", "lo wo"));
}

TEST_CASE("ContainsSubstring - pattern found at end") {
    REQUIRE(string_search::ContainsSubstring("hello world", "world"));
}

TEST_CASE("ContainsSubstring - pattern not found") {
    REQUIRE_FALSE(string_search::ContainsSubstring("hello world", "xyz"));
}

// Edge cases
TEST_CASE("ContainsSubstring - empty text") {
    REQUIRE_FALSE(string_search::ContainsSubstring("", "pattern"));
}

TEST_CASE("ContainsSubstring - pattern longer than text") {
    REQUIRE_FALSE(string_search::ContainsSubstring("short", "very long pattern"));
}

TEST_CASE("ContainsSubstring - exact match") {
    REQUIRE(string_search::ContainsSubstring("exact", "exact"));
}

// String vs string_view overloads
TEST_CASE("ContainsSubstring - string overload") {
    std::string text = "hello world";
    std::string pattern = "world";
    REQUIRE(string_search::ContainsSubstring(text, pattern));
}

// Performance-critical cases
TEST_CASE("ContainsSubstring - long pattern optimization") {
    std::string text = "abcdefghijklmnopqrstuvwxyz";
    std::string pattern = "mnopqr";  // >5 chars, triggers reverse comparison
    REQUIRE(string_search::ContainsSubstring(text, pattern));
}
```

#### 2. `ContainsSubstringI` Tests

```cpp
// Case-insensitive matching
TEST_CASE("ContainsSubstringI - case insensitive match") {
    REQUIRE(string_search::ContainsSubstringI("Hello World", "hello"));
    REQUIRE(string_search::ContainsSubstringI("Hello World", "WORLD"));
    REQUIRE(string_search::ContainsSubstringI("Hello World", "Lo Wo"));
}

TEST_CASE("ContainsSubstringI - mixed case pattern") {
    REQUIRE(string_search::ContainsSubstringI("hello world", "HeLLo"));
}

// Edge cases (similar to ContainsSubstring)
TEST_CASE("ContainsSubstringI - empty pattern") {
    REQUIRE(string_search::ContainsSubstringI("hello", ""));
}

TEST_CASE("ContainsSubstringI - pattern not found") {
    REQUIRE_FALSE(string_search::ContainsSubstringI("hello world", "xyz"));
}
```

#### 3. `StrStrCaseInsensitive` Tests

```cpp
TEST_CASE("StrStrCaseInsensitive - found at start") {
    const char* result = string_search::StrStrCaseInsensitive("Hello World", "hello");
    REQUIRE(result != nullptr);
    REQUIRE(result == std::string("Hello World").c_str());
}

TEST_CASE("StrStrCaseInsensitive - found in middle") {
    const char* haystack = "Hello World";
    const char* result = string_search::StrStrCaseInsensitive(haystack, "lo wo");
    REQUIRE(result != nullptr);
    REQUIRE(result == haystack + 3);  // Points to "lo World"
}

TEST_CASE("StrStrCaseInsensitive - not found") {
    REQUIRE(string_search::StrStrCaseInsensitive("Hello World", "xyz") == nullptr);
}

TEST_CASE("StrStrCaseInsensitive - empty needle") {
    const char* haystack = "Hello World";
    REQUIRE(string_search::StrStrCaseInsensitive(haystack, "") == haystack);
}

TEST_CASE("StrStrCaseInsensitive - null pointer handling") {
    REQUIRE(string_search::StrStrCaseInsensitive(nullptr, "pattern") == nullptr);
    REQUIRE(string_search::StrStrCaseInsensitive("haystack", nullptr) == nullptr);
}
```

#### 4. `ToLowerChar` Tests

```cpp
TEST_CASE("ToLowerChar - uppercase letters") {
    REQUIRE(string_search::ToLowerChar('A') == 'a');
    REQUIRE(string_search::ToLowerChar('Z') == 'z');
}

TEST_CASE("ToLowerChar - lowercase letters unchanged") {
    REQUIRE(string_search::ToLowerChar('a') == 'a');
    REQUIRE(string_search::ToLowerChar('z') == 'z');
}

TEST_CASE("ToLowerChar - non-letters unchanged") {
    REQUIRE(string_search::ToLowerChar('0') == '0');
    REQUIRE(string_search::ToLowerChar(' ') == ' ');
    REQUIRE(string_search::ToLowerChar('!') == '!');
}
```

### Test File Template

```cpp
// tests/StringSearchTests.cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "StringSearch.h"

TEST_SUITE("StringSearch") {
    
    TEST_SUITE("ContainsSubstring") {
        // ... test cases ...
    }
    
    TEST_SUITE("ContainsSubstringI") {
        // ... test cases ...
    }
    
    TEST_SUITE("StrStrCaseInsensitive") {
        // ... test cases ...
    }
    
    TEST_SUITE("ToLowerChar") {
        // ... test cases ...
    }
}
```

---

## Implementation Steps

### Phase 1: Extract Functions (No Tests Yet)

1. ✅ Create `StringSearch.h` with extracted functions
2. ✅ Update `StringUtils.h` (remove functions, add include or using declarations)
3. ✅ Update `FileIndex.cpp` and `SearchWorker.cpp` includes
4. ✅ Verify compilation and runtime behavior (no regressions)

### Phase 2: Add Unit Testing Infrastructure

1. ✅ Download doctest header to `external/doctest/doctest.h`
2. ✅ Update `CMakeLists.txt` with test configuration
3. ✅ Create `tests/` directory
4. ✅ Create `tests/StringSearchTests.cpp` with initial test cases

### Phase 3: Implement Comprehensive Tests

1. ✅ Add all test cases listed above
2. ✅ Add edge case tests (boundary conditions)
3. ✅ Add performance regression tests (optional)
4. ✅ Run tests and verify all pass

### Phase 4: Integration

1. ✅ Add test target to build system
2. ✅ Document how to run tests
3. ✅ Consider adding to CI/CD (if applicable)

---

## CMakeLists.txt Updates

### Minimal Addition

```cmake
# Add after main executable definition

# --- Unit Tests ---
option(BUILD_TESTS "Build unit tests" ON)

if(BUILD_TESTS)
    # doctest location (header is at external/doctest/doctest/doctest.h)
    set(DOCTEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/doctest")
    
    # Check if doctest exists
    if(EXISTS "${DOCTEST_DIR}/doctest/doctest.h")
        message(STATUS "Building unit tests with doctest")
        
        # String search tests
        add_executable(string_search_tests
            tests/StringSearchTests.cpp
        )
        
        target_include_directories(string_search_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${DOCTEST_DIR}
        )
        
        target_compile_features(string_search_tests PRIVATE cxx_std_17)
        
        # Match main executable compiler options
        if(MSVC)
            target_compile_options(string_search_tests PRIVATE 
                $<$<CONFIG:Debug>:/Zi /Od>
            )
        endif()
        
        # Enable testing
        enable_testing()
        add_test(NAME StringSearchTests COMMAND string_search_tests)
        
        message(STATUS "  Test executable: string_search_tests")
    else()
        message(WARNING "doctest.h not found at ${DOCTEST_DIR}/doctest/doctest.h")
        message(WARNING "  Run: git submodule update --init --recursive")
        message(WARNING "  Or download from: https://github.com/doctest/doctest/releases")
        message(WARNING "  Unit tests will not be built")
    endif()
else()
    message(STATUS "Unit tests disabled (use -DBUILD_TESTS=ON to enable)")
endif()
```

### Build and Run Tests

```bash
# Configure with tests
cmake -DBUILD_TESTS=ON ..

# Build tests
cmake --build . --target string_search_tests

# Run tests
ctest --output-on-failure

# Or run directly
./string_search_tests  # Linux/Mac
string_search_tests.exe  # Windows
```

---

## Summary

### Recommended Approach

1. **Extract to `StringSearch.h`** (Option 1)
   - Keep functions inline for performance
   - Wrap in `string_search` namespace
   - Minimal code changes

2. **Use doctest for testing**
   - Header-only, fast, simple
   - Already used by your dependencies
   - Easy CMake integration

3. **Implementation order**
   - Phase 1: Extract functions (verify no regressions)
   - Phase 2: Add testing infrastructure
   - Phase 3: Write comprehensive tests
   - Phase 4: Integrate into build system

### Benefits

- ✅ **Testability**: Functions can be tested in isolation
- ✅ **Organization**: Clear separation of search utilities
- ✅ **Maintainability**: Easier to add AVX2 implementations later
- ✅ **Quality**: Unit tests catch regressions early
- ✅ **Documentation**: Tests serve as usage examples

### Next Steps

1. Review and approve this plan
2. Create `StringSearch.h` with extracted functions
3. Update existing code to use new header
4. Set up doctest infrastructure
5. Write and run unit tests

---

## References

- [doctest Documentation](https://github.com/doctest/doctest)
- [Catch2 Documentation](https://github.com/catchorg/Catch2)
- [Google Test Documentation](https://google.github.io/googletest/)
- [CMake Testing Guide](https://cmake.org/cmake/help/latest/manual/ctest.1.html)
