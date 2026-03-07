# GeminiApiUtilsTests Deduplication Plan

## Current State Analysis

**File:** `tests/GeminiApiUtilsTests.cpp`
- **Total lines:** 879
- **ParseSearchConfigJson calls:** 45
- **CHECK(result.success) patterns:** 40
- **CHECK(result.search_config.path) patterns:** 30
- **SonarQube duplication:** 43%

## Duplication Patterns Identified

### 1. Repeated ParseSearchConfigJson + Assertion Pattern (High Priority)
**Frequency:** ~40 occurrences

**Pattern:**
```cpp
std::string json = test_helpers::CreateGeminiResponseWithPath("pp:**/*.txt");
auto result = ParseSearchConfigJson(json);
CHECK(result.success == true);
CHECK(result.search_config.path == "pp:**/*.txt");
CHECK(result.error_message.empty());
```

**Solution:** Create parameterized test helper that combines JSON creation, parsing, and assertions.

### 2. Manual JSON Construction (Medium Priority)
**Frequency:** ~2-3 occurrences

**Pattern:**
```cpp
std::string json = R"({
  "candidates": [{
    "content": {
      "parts": [{
        "text": "{\"version\": \"1.0\", \"search_config\": {\"path\": \"pp:**/*.txt\"}}"
      }]
    }
  }]
})";
```

**Solution:** Use existing `CreateGeminiResponseWithPath` or create helper for complex JSON structures.

### 3. Error Response JSON Construction (Medium Priority)
**Frequency:** ~3-4 occurrences

**Pattern:**
```cpp
std::string json = R"({
  "error": {
    "code": 400,
    "message": "Invalid API key",
    "status": "INVALID_ARGUMENT"
  }
})";
```

**Solution:** Create helper function `CreateGeminiErrorResponse(code, message, status)`.

### 4. Repeated Test Structure (Low Priority)
**Frequency:** Multiple test cases with similar structure

**Pattern:** Many SUBCASE blocks follow the same pattern of creating JSON, parsing, and checking results.

**Solution:** Create parameterized test helper for common test scenarios.

## Implementation Plan

### Phase 1: Create Helper Functions in TestHelpers.h/cpp

#### 1.1 Create ParseSearchConfigJson Test Case Structure
```cpp
struct ParseSearchConfigJsonTestCase {
  std::string json_input;
  bool expected_success;
  std::string expected_path;
  std::string expected_error_substring;  // Empty if no error expected
  std::vector<std::string> expected_extensions;  // Empty if none expected
};
```

#### 1.2 Create Parameterized Test Helper
```cpp
void RunParameterizedParseSearchConfigJsonTests(
    const std::vector<ParseSearchConfigJsonTestCase>& test_cases);
```

This helper will:
- Loop through test cases
- Create DOCTEST_SUBCASE for each
- Call ParseSearchConfigJson
- Assert result.success
- Assert result.search_config.path (if expected)
- Assert result.error_message (if expected)
- Assert result.search_config.extensions (if expected)

#### 1.3 Create Error Response Helper
```cpp
std::string CreateGeminiErrorResponse(
    int code,
    std::string_view message,
    std::string_view status = "");
```

#### 1.4 Create Complex JSON Response Helper
```cpp
std::string CreateGeminiResponseWithMultipleCandidates(
    const std::vector<std::string>& paths);

std::string CreateGeminiResponseWithMultipleParts(
    const std::vector<std::string>& paths);
```

### Phase 2: Refactor Tests

#### 2.1 Refactor Simple Path Tests
Replace repetitive SUBCASE blocks with parameterized tests:
- Lines 33-43: "Valid response with pp: prefix"
- Lines 45-54: "Response with extra text before pp:"
- Lines 56-65: "Response with newline after pattern"
- Lines 67-77: "Response missing pp: prefix"
- Lines 121-128: "Complex pattern with anchors"
- Lines 130-137: "Pattern with character classes"

#### 2.2 Refactor Error Response Tests
Replace manual JSON construction with helper:
- Lines 95-108: "API error response"
- Lines 491-511: "Error response with detailed error info"
- Lines 513-526: "Error response without message field"

#### 2.3 Refactor Complex JSON Structure Tests
Replace manual JSON construction with helpers:
- Lines 257-281: "Multiple candidates (should use first)"
- Lines 283-299: "Multiple parts (should use first)"
- Lines 301-323: "Response with metadata fields"

#### 2.4 Refactor Edge Case Tests
Group similar edge case tests into parameterized tests:
- Lines 620-665: Multiple tests with CreateGeminiResponseWithPath + same assertions

### Phase 3: Verification

1. Run all tests to ensure they still pass
2. Check SonarQube duplication report
3. Verify code reduction (target: reduce by 100-150 lines)

## Expected Results

- **Code reduction:** ~100-150 lines (11-17% reduction)
- **Duplication reduction:** From 43% to <20%
- **Maintainability:** Easier to add new test cases
- **Readability:** Tests are more concise and focused

## Implementation Order

1. ✅ Add helper structures and functions to TestHelpers.h
2. ✅ Implement helper functions in TestHelpers.cpp
3. ✅ Refactor simple path tests (Phase 2.1)
4. ✅ Refactor error response tests (Phase 2.2)
5. ✅ Refactor complex JSON structure tests (Phase 2.3)
6. ✅ Refactor edge case tests (Phase 2.4)
7. ✅ Run tests and verify
8. ✅ Check SonarQube duplication report
