# StringUtilsTests Deduplication Analysis

## Summary

Refactored `StringUtilsTests.cpp` to eliminate repetitive test patterns using template helper functions in `TestHelpers.h`.

## Duplication Pattern Identified

### Before Refactoring

Each test case followed the same repetitive pattern:

1. **Define TestCase struct** (3-4 lines):
   ```cpp
   struct TestCase {
     std::string input;
     std::string expected;  // or std::vector<std::string> for ParseExtensions
   };
   ```

2. **Create test cases vector** (variable length, but same structure)

3. **Loop with DOCTEST_SUBCASE** (5-7 lines):
   ```cpp
   for (const auto &tc : test_cases) {
     DOCTEST_SUBCASE(tc.input.c_str()) {
       CHECK(func(tc.input) == tc.expected);
       CHECK(func(std::string_view(tc.input)) == tc.expected);
     }
   }
   ```

### Duplication Statistics

- **5 test cases** (ToLower, Trim, GetFilename, GetExtension, ParseExtensions)
- **~10 lines of boilerplate per test case** (struct definition + loop pattern)
- **Total duplication: ~50 lines** of repetitive code

## Solution Implemented

### 1. Created Helper Structures

Added to `TestHelpers.h`:
- `StringUtilsTestCase` - For functions returning `std::string`
- `StringUtilsVectorTestCase` - For functions returning `std::vector<std::string>`

### 2. Created Template Helper Functions

Added to `TestHelpers.h`:
- `RunParameterizedStringTests<Func>()` - Handles the loop and dual string/string_view checking for string-returning functions
- `RunParameterizedVectorTests<Func>()` - Handles the loop and dual string/string_view checking for vector-returning functions

### 3. Refactored All Test Cases

Each test case now:
1. Defines test cases using the helper structures
2. Calls the appropriate helper function

**Example Before:**
```cpp
TEST_CASE("ToLower") {
  struct TestCase {
    std::string input;
    std::string expected;
  };

  std::vector<TestCase> test_cases = {{"HELLO", "hello"}, ...};

  for (const auto &tc : test_cases) {
    DOCTEST_SUBCASE(tc.input.c_str()) {
      CHECK(ToLower(tc.input) == tc.expected);
      CHECK(ToLower(std::string_view(tc.input)) == tc.expected);
    }
  }
}
```

**Example After:**
```cpp
TEST_CASE("ToLower") {
  std::vector<test_helpers::StringUtilsTestCase> test_cases = {
      {"HELLO", "hello"}, ...};

  test_helpers::RunParameterizedStringTests(ToLower, test_cases);
}
```

## Results

### Code Reduction

- **Before:** 166 lines
- **After:** 114 lines
- **Reduction:** 52 lines (31% reduction)
- **Eliminated:** ~50 lines of duplicated boilerplate

### Test Coverage

- **5 test cases** (unchanged)
- **146 assertions** (unchanged)
- **All tests pass:** ✅

### Benefits

1. **Eliminated repetitive struct definitions** - Now using shared helper structures
2. **Eliminated repetitive loop patterns** - Encapsulated in template functions
3. **Consistent test structure** - All tests follow the same pattern
4. **Easier to maintain** - Changes to test pattern only need to be made in one place
5. **Type-safe** - Template functions ensure correct function signatures

## Files Modified

1. **`tests/TestHelpers.h`**
   - Added `doctest/doctest.h` include
   - Added `StringUtilsTestCase` and `StringUtilsVectorTestCase` structures
   - Added `RunParameterizedStringTests()` template function
   - Added `RunParameterizedVectorTests()` template function

2. **`tests/StringUtilsTests.cpp`**
   - Added `#include "TestHelpers.h"`
   - Replaced all 5 test cases with simplified versions using helper functions
   - Removed all `struct TestCase` definitions
   - Removed all loop patterns

## Pattern Reusability

This pattern can be reused for other parameterized tests that:
- Test functions with both `std::string` and `std::string_view` overloads
- Follow the same input/expected output pattern
- Use `DOCTEST_SUBCASE` for individual test cases

## Verification

- ✅ All 5 test cases pass
- ✅ All 146 assertions pass
- ✅ No compilation errors
- ✅ No linker errors
- ✅ Code reduction verified (166 → 114 lines)
