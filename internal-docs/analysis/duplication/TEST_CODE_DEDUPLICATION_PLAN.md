# Test Code Deduplication Plan

## Executive Summary

Based on duplo analysis results, there are **4,100+ duplication matches** in test code. This plan identifies the main duplication patterns and proposes specific refactoring strategies to reduce code duplication **ONLY in test code** (not production code).

## Analysis Overview

The duplo report shows significant duplication in:
1. **PathUtilsTests.cpp** - Extensive duplication in path truncation test patterns
2. **SearchContextTests.cpp** - Repeated SearchContext setup and validation patterns
3. **GeminiApiUtilsTests.cpp** - Repeated JSON test string construction patterns
4. **Other test files** - Various helper patterns and setup code

## Priority Areas

### Priority 1: High Impact, High Duplication

#### 1. PathUtilsTests.cpp - Path Truncation Test Patterns

**Problem:**
- Repeated patterns for testing `TruncatePathAtBeginning()` with similar structure:
  - Setup: Create path string, set max_width, define calculator
  - Call: `path_utils::TruncatePathAtBeginning(path, max_width, calc)`
  - Assert: Check result width, ellipsis presence, path prefix/suffix
- Windows vs non-Windows platform-specific duplication
- Similar assertions repeated across many test cases

**Duplication Examples:**
```cpp
// Pattern repeated ~100+ times:
std::string path = "...";
float max_width = ...;
std::string result = path_utils::TruncatePathAtBeginning(path, max_width, monospace_calc);
REQUIRE(result.substr(0, 3) == "...");
float result_width = monospace_calc(result);
REQUIRE(result_width <= max_width + 0.1f);
```

**Solution:**
1. Create helper functions in `TestHelpers.h`:
   - `VerifyTruncatedPath()` - Validates truncated path result
   - `CreatePathTruncationTestCase()` - Parameterized test helper
   - `GetPlatformSpecificPath()` - Returns Windows/non-Windows paths

2. Use parameterized tests (doctest SUBCASE) for similar test scenarios

3. Extract common assertion patterns:
   - `AssertPathWidthWithinLimit()`
   - `AssertEllipsisPresent()`
   - `AssertPathPrefix()`

**Expected Reduction:** ~60-70% of PathUtilsTests.cpp duplication

---

#### 2. SearchContextTests.cpp - Context Setup and Validation Patterns

**Problem:**
- Repeated patterns for:
  - Creating `SearchContext`, setting values, calling `ValidateAndClamp()`, checking results
  - Creating `SearchContext`, setting extensions, calling `PrepareExtensionSet()`, checking results

**Duplication Examples:**
```cpp
// Pattern repeated ~30+ times:
SearchContext ctx;
ctx.dynamic_chunk_size = 1000;
ctx.hybrid_initial_percent = 75;
ctx.ValidateAndClamp();
CHECK(ctx.dynamic_chunk_size == 1000);
CHECK(ctx.hybrid_initial_percent == 75);
```

**Solution:**
1. Extend `TestHelpers.h` with:
   - `CreateSearchContextWithValues()` - Factory function with parameters
   - `ValidateAndAssertSearchContext()` - Validates context after ValidateAndClamp
   - `CreateSearchContextWithExtensions()` - Factory for extension tests
   - `AssertExtensionSetContains()` - Validates extension set contents

2. Use parameterized tests for boundary value testing (min/max values)

**Expected Reduction:** ~50-60% of SearchContextTests.cpp duplication

---

#### 3. GeminiApiUtilsTests.cpp - JSON Test String Construction

**Problem:**
- Repeated JSON string construction with same structure:
  ```cpp
  std::string json = R"({
    "candidates": [{
      "content": {
        "parts": [{
          "text": "..."
        }]
      }
    }]
  })";
  ```
- Repeated pattern: Build JSON → Call `ParseSearchConfigJson()` → Check `result.success` → Check specific fields

**Duplication Examples:**
- JSON wrapper structure repeated ~50+ times
- Same assertion pattern: `CHECK(result.success == true)` followed by field checks

**Solution:**
1. Create helper functions in `TestHelpers.h`:
   - `CreateGeminiJsonResponse()` - Builds JSON wrapper with inner text
   - `CreateGeminiJsonResponseFromObject()` - Builds JSON from nlohmann::json object
   - `AssertParseSuccess()` - Validates successful parse result
   - `AssertParseFailure()` - Validates failed parse result

2. Use helper to reduce JSON boilerplate:
   ```cpp
   auto result = ParseSearchConfigJson(
     test_helpers::CreateGeminiJsonResponse(R"({"version": "1.0", "search_config": {...}})")
   );
   test_helpers::AssertParseSuccess(result);
   ```

**Expected Reduction:** ~70-80% of GeminiApiUtilsTests.cpp duplication

---

### Priority 2: Medium Impact, Medium Duplication

#### 4. Test Setup and Teardown Patterns

**Problem:**
- Repeated setup code across multiple test files
- Similar fixture patterns

**Solution:**
1. Expand `TestHelpers.h` with more fixture classes:
   - `TestFileIndexFixture` - RAII for test FileIndex
   - `TestSearchContextFixture` - RAII for SearchContext with common setup

2. Create test data generators:
   - `GenerateTestPaths()` - Creates test path vectors
   - `GenerateTestExtensions()` - Creates extension vectors

**Expected Reduction:** ~30-40% of setup code duplication

---

#### 5. Assertion Helper Patterns

**Problem:**
- Repeated assertion patterns across test files
- Similar validation logic

**Solution:**
1. Create assertion macros/helpers in `TestHelpers.h`:
   - `ASSERT_SEARCH_RESULT()` - Validates SearchResult structure
   - `ASSERT_FILE_INDEX_ENTRY()` - Validates FileIndex entry
   - `ASSERT_PATH_MATCHES()` - Validates path patterns

**Expected Reduction:** ~20-30% of assertion duplication

---

## Implementation Plan

### Phase 1: High-Impact Refactoring (Weeks 1-2)

1. **PathUtilsTests.cpp Refactoring**
   - [ ] Add path truncation helpers to `TestHelpers.h`
   - [ ] Refactor `PathUtilsTests.cpp` to use helpers
   - [ ] Verify tests still pass
   - [ ] Run duplo to measure reduction

2. **SearchContextTests.cpp Refactoring**
   - [ ] Add SearchContext factory helpers to `TestHelpers.h`
   - [ ] Refactor `SearchContextTests.cpp` to use helpers
   - [ ] Verify tests still pass
   - [ ] Run duplo to measure reduction

3. **GeminiApiUtilsTests.cpp Refactoring**
   - [ ] Add JSON response helpers to `TestHelpers.h`
   - [ ] Refactor `GeminiApiUtilsTests.cpp` to use helpers
   - [ ] Verify tests still pass
   - [ ] Run duplo to measure reduction

### Phase 2: Medium-Impact Refactoring (Weeks 3-4)

4. **Test Setup Patterns**
   - [ ] Add fixture classes to `TestHelpers.h`
   - [ ] Refactor test files to use fixtures
   - [ ] Verify tests still pass

5. **Assertion Helpers**
   - [ ] Add assertion helpers to `TestHelpers.h`
   - [ ] Refactor test files to use assertion helpers
   - [ ] Verify tests still pass

### Phase 3: Verification and Documentation (Week 5)

6. **Final Verification**
   - [ ] Run full test suite
   - [ ] Run duplo analysis to measure total reduction
   - [ ] Update `TestHelpers.h` documentation
   - [ ] Create examples of using new helpers

---

## Detailed Refactoring Specifications

### TestHelpers.h Additions

#### Path Truncation Helpers

```cpp
namespace test_helpers {

/**
 * Verify a truncated path result meets common requirements.
 * 
 * @param result The truncated path result
 * @param max_width Maximum allowed width
 * @param calc Text width calculator
 * @param expected_prefix Expected prefix (e.g., "C:\\" or "...")
 * @param should_contain_ellipsis Whether result should contain "..."
 * @param expected_suffix Expected suffix (optional, empty to skip check)
 */
void VerifyTruncatedPath(
    const std::string& result,
    float max_width,
    path_utils::TextWidthCalculator calc,
    std::string_view expected_prefix,
    bool should_contain_ellipsis,
    std::string_view expected_suffix = "");

/**
 * Get platform-specific test path.
 * 
 * @param windows_path Path to use on Windows
 * @param unix_path Path to use on Unix-like systems
 * @return Platform-appropriate path
 */
std::string GetPlatformSpecificPath(
    std::string_view windows_path,
    std::string_view unix_path);

} // namespace test_helpers
```

#### SearchContext Helpers

```cpp
namespace test_helpers {

/**
 * Create SearchContext with specific values and validate/clamp.
 * 
 * @param chunk_size Dynamic chunk size value
 * @param hybrid_percent Hybrid initial percent value
 * @return Configured and validated SearchContext
 */
SearchContext CreateValidatedSearchContext(
    int chunk_size,
    int hybrid_percent);

/**
 * Assert SearchContext values after ValidateAndClamp.
 * 
 * @param ctx SearchContext to check
 * @param expected_chunk_size Expected chunk size
 * @param expected_hybrid_percent Expected hybrid percent
 */
void AssertSearchContextValues(
    const SearchContext& ctx,
    int expected_chunk_size,
    int expected_hybrid_percent);

/**
 * Create SearchContext with extensions and prepare extension set.
 * 
 * @param extensions Vector of extensions (with or without leading dot)
 * @param case_sensitive Whether search should be case-sensitive
 * @return Configured SearchContext with prepared extension set
 */
SearchContext CreateSearchContextWithExtensions(
    const std::vector<std::string>& extensions,
    bool case_sensitive = false);

/**
 * Assert extension set contains expected extensions.
 * 
 * @param ctx SearchContext to check
 * @param expected_extensions Vector of expected extensions (without leading dot)
 */
void AssertExtensionSetContains(
    const SearchContext& ctx,
    const std::vector<std::string>& expected_extensions);

} // namespace test_helpers
```

#### Gemini API JSON Helpers

```cpp
namespace test_helpers {

/**
 * Create Gemini API JSON response wrapper.
 * 
 * @param inner_text Inner JSON text to wrap
 * @return Complete Gemini API JSON response string
 */
std::string CreateGeminiJsonResponse(std::string_view inner_text);

/**
 * Create Gemini API JSON response from nlohmann::json object.
 * 
 * @param inner_json Inner JSON object to wrap
 * @return Complete Gemini API JSON response string
 */
std::string CreateGeminiJsonResponseFromObject(const nlohmann::json& inner_json);

/**
 * Assert ParseSearchConfigJson result indicates success.
 * 
 * @param result Parse result to check
 */
void AssertParseSuccess(const ParseSearchConfigResult& result);

/**
 * Assert ParseSearchConfigJson result indicates failure.
 * 
 * @param result Parse result to check
 * @param expected_error_substring Expected substring in error message (optional)
 */
void AssertParseFailure(
    const ParseSearchConfigResult& result,
    std::string_view expected_error_substring = "");

} // namespace test_helpers
```

---

## Success Metrics

### Quantitative Goals

1. **Overall Duplication Reduction:**
   - Target: Reduce test code duplication by 50-60%
   - Measure: Run duplo before/after and compare match counts

2. **File-Specific Goals:**
   - `PathUtilsTests.cpp`: Reduce duplication by 60-70%
   - `SearchContextTests.cpp`: Reduce duplication by 50-60%
   - `GeminiApiUtilsTests.cpp`: Reduce duplication by 70-80%

3. **Code Metrics:**
   - Lines of code reduction in test files
   - Number of helper functions added to `TestHelpers.h`
   - Test execution time (should remain same or improve)

### Qualitative Goals

1. **Maintainability:**
   - Easier to add new tests (use helpers instead of copying)
   - Clearer test intent (helpers have descriptive names)
   - Less code to review in test files

2. **Consistency:**
   - Consistent test patterns across test files
   - Standardized assertion messages
   - Uniform error handling

---

## Risks and Mitigations

### Risk 1: Breaking Existing Tests
**Mitigation:**
- Refactor incrementally (one file at a time)
- Run tests after each refactoring step
- Keep original test logic, just extract to helpers

### Risk 2: Over-Abstraction
**Mitigation:**
- Keep helpers simple and focused
- Don't abstract away test-specific logic
- Maintain test readability

### Risk 3: Test Performance Impact
**Mitigation:**
- Keep helpers inline or header-only where possible
- Profile test execution time before/after
- Optimize helpers if needed

---

## Notes

- **Scope:** This plan focuses ONLY on test code duplication. Production code duplication is out of scope.
- **Test Helpers:** The existing `TestHelpers.h` already has some helpers. This plan extends it.
- **Backward Compatibility:** All refactoring should maintain existing test behavior and pass all tests.
- **Documentation:** Update `TestHelpers.h` documentation as helpers are added.

---

## References

- Duplo Report: `duplo_report.txt`
- Existing Test Helpers: `tests/TestHelpers.h`
- Test Files: `tests/*.cpp`
