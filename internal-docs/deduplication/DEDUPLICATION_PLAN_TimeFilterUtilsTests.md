# Deduplication Plan: TimeFilterUtilsTests.cpp

## Overview

**File:** `tests/TimeFilterUtilsTests.cpp`  
**Current Status:** 18 duplicate blocks  
**Total Lines:** 411  
**Priority:** High (4th highest priority test file)

---

## Analysis

### Duplication Patterns Identified

1. **TimeFilterToString Tests** (6 identical test cases):
   ```cpp
   TEST_CASE("None filter") {
       CHECK(std::string(TimeFilterToString(TimeFilter::None)) == "None");
   }
   TEST_CASE("Today filter") {
       CHECK(std::string(TimeFilterToString(TimeFilter::Today)) == "Today");
   }
   // ... 4 more identical patterns
   ```
   **Frequency:** 6 times  
   **Lines per occurrence:** ~3 lines  
   **Total duplicate lines:** ~18 lines

2. **TimeFilterFromString Tests** (6 identical test cases):
   ```cpp
   TEST_CASE("None string") {
       CHECK(TimeFilterFromString("None") == TimeFilter::None);
   }
   TEST_CASE("Today string") {
       CHECK(TimeFilterFromString("Today") == TimeFilter::Today);
   }
   // ... 4 more identical patterns
   ```
   **Frequency:** 6 times  
   **Lines per occurrence:** ~3 lines  
   **Total duplicate lines:** ~18 lines

3. **CalculateCutoffTime Test Patterns** (repeated in 5 tests):
   ```cpp
   // Pattern 1: Get current time and convert to local time
   auto now_time_t = std::chrono::system_clock::to_time_t(
       std::chrono::system_clock::now());
   std::tm now_tm;
   #ifdef _WIN32
       localtime_s(&now_tm, &now_time_t);
   #else
       localtime_r(&now_time_t, &now_tm);
   #endif
   
   // Pattern 2: Setup tm struct
   std::tm period_start = {};
   period_start.tm_year = now_tm.tm_year;
   period_start.tm_mon = now_tm.tm_mon;
   period_start.tm_mday = ...;
   period_start.tm_hour = 0;
   period_start.tm_min = 0;
   period_start.tm_sec = 0;
   period_start.tm_isdst = -1;
   
   // Pattern 3: Convert and verify
   int64_t expected = std::mktime(&period_start);
   constexpr int64_t kTwoHours = 2 * 60 * 60;
   CHECK(cutoff_unix >= expected - kTwoHours);
   CHECK(cutoff_unix <= expected + kTwoHours);
   CHECK(cutoff_unix <= now_unix);
   ```
   **Frequency:** 5 times (Today, ThisWeek, ThisMonth, ThisYear, Older)  
   **Lines per occurrence:** ~15-20 lines  
   **Total duplicate lines:** ~75-100 lines

---

## Deduplication Strategy

### Phase 1: Parameterize TimeFilterToString Tests (High Impact)

**Goal:** Consolidate 6 identical test cases into 1 parameterized test

**Action:** Use DOCTEST_SUBCASE to parameterize the test

**Proposed:**
```cpp
TEST_CASE("All filter types convert to string correctly") {
    const std::array<std::pair<TimeFilter, std::string>, 6> test_cases = {
        {TimeFilter::None, "None"},
        {TimeFilter::Today, "Today"},
        {TimeFilter::ThisWeek, "ThisWeek"},
        {TimeFilter::ThisMonth, "ThisMonth"},
        {TimeFilter::ThisYear, "ThisYear"},
        {TimeFilter::Older, "Older"}
    };
    
    for (const auto& [filter, expected] : test_cases) {
        DOCTEST_SUBCASE(expected.c_str()) {
            CHECK(std::string(TimeFilterToString(filter)) == expected);
        }
    }
}
```

**Estimated Reduction:** ~15-18 lines

---

### Phase 2: Parameterize TimeFilterFromString Tests (High Impact)

**Goal:** Consolidate 6 identical test cases into 1 parameterized test

**Action:** Use DOCTEST_SUBCASE to parameterize the test

**Proposed:**
```cpp
TEST_CASE("All filter strings convert to enum correctly") {
    const std::array<std::pair<std::string, TimeFilter>, 6> test_cases = {
        {"None", TimeFilter::None},
        {"Today", TimeFilter::Today},
        {"ThisWeek", TimeFilter::ThisWeek},
        {"ThisMonth", TimeFilter::ThisMonth},
        {"ThisYear", TimeFilter::ThisYear},
        {"Older", TimeFilter::Older}
    };
    
    for (const auto& [str, expected] : test_cases) {
        DOCTEST_SUBCASE(str.c_str()) {
            CHECK(TimeFilterFromString(str) == expected);
        }
    }
}
```

**Estimated Reduction:** ~15-18 lines

---

### Phase 3: Extract Common CalculateCutoffTime Test Patterns (Medium Impact)

**Goal:** Extract repeated time calculation and verification patterns

**Action:** Create helper functions for common patterns

**Proposed Helpers:**
```cpp
namespace test_helpers {
namespace time_filter_test_helpers {

// Get current local time as tm struct
std::tm GetCurrentLocalTime();

// Get start of period (today, week, month, year) as Unix timestamp
int64_t GetStartOfTodayUnix();
int64_t GetStartOfWeekUnix();
int64_t GetStartOfMonthUnix();
int64_t GetStartOfYearUnix();

// Verify cutoff time is within expected range
void VerifyCutoffTimeRange(int64_t cutoff_unix, int64_t expected_unix, 
                           int64_t tolerance_seconds = 2 * 60 * 60);

// Verify cutoff time is in the past and within max_age
void VerifyCutoffTimeInPast(int64_t cutoff_unix, int64_t now_unix, 
                            int64_t max_age_seconds);

} // namespace time_filter_test_helpers
} // namespace test_helpers
```

**Usage Example:**
```cpp
TEST_CASE("Today filter returns start of today (midnight)") {
    FILETIME result = CalculateCutoffTime(TimeFilter::Today);
    int64_t cutoff_unix = FileTimeToUnixSeconds(result);
    int64_t expected = test_helpers::time_filter_test_helpers::GetStartOfTodayUnix();
    
    test_helpers::time_filter_test_helpers::VerifyCutoffTimeRange(cutoff_unix, expected);
    test_helpers::time_filter_test_helpers::VerifyCutoffTimeInPast(cutoff_unix, GetCurrentUnixSeconds(), 24 * 60 * 60);
}
```

**Estimated Reduction:** ~60-80 lines

---

## Implementation Order

1. **Phase 1** (Parameterize TimeFilterToString) ✅ COMPLETE - High impact, easy win
2. **Phase 2** (Parameterize TimeFilterFromString) ✅ COMPLETE - High impact, easy win
3. **Phase 3** (Extract CalculateCutoffTime patterns) ✅ COMPLETE - Medium impact, more complex

---

## Expected Results

### Before Deduplication
- **Total Lines:** 411 lines
- **Duplicate Blocks:** 18
- **Estimated Duplicate Lines:** ~100-150 lines

### After Deduplication ✅ COMPLETE
- **Total Lines:** 279 lines (32% reduction from 411)
- **Duplicate Blocks:** 0 (100% reduction from 18)
- **Maintainability:** Significantly improved
- **Phase 1 & 2:** Consolidated 12 individual test cases into 2 parameterized tests (411 → 397 lines)
- **Phase 3:** Extracted common CalculateCutoffTime patterns into helper functions (397 → 279 lines)

---

## Implementation Notes

1. **Parameterized Tests:**
   - Use `DOCTEST_SUBCASE` for each filter type
   - Keep test names descriptive
   - Use array of pairs for test data

2. **Time Calculation Helpers:**
   - Handle platform differences (Windows vs Unix) in helpers
   - Use consistent tolerance values (2 hours for timezone/DST)
   - Document expected behavior clearly

3. **Testing:**
   - All existing tests should pass after refactoring
   - No functional changes, only code organization
   - Verify parameterized tests run for all filter types

---

## Success Criteria

- ✅ All test cases pass
- ✅ Duplicate blocks reduced from 18 to <5
- ✅ Code is more maintainable and readable
- ✅ No functional changes to test logic
