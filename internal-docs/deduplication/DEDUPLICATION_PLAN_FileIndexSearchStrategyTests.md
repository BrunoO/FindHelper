# Deduplication Plan: FileIndexSearchStrategyTests.cpp

**Target File:** `tests/FileIndexSearchStrategyTests.cpp`  
**Current Duplicate Blocks:** 48  
**Priority:** High (Second highest priority test file)

---

## Analysis Summary

The test file contains extensive duplication in the following areas:

1. **Repeated Test Setup Pattern** (repeated ~57 times):
   - `test_helpers::TestSettingsFixture settings("strategy");`
   - `test_helpers::TestFileIndexFixture index_fixture(N);`
   - `auto results = CollectSearchResults(...);`
   - `REQUIRE(results.size() > 0);`
   - `ValidateResults(results, "file_", false);`

2. **Repeated "All Results Are Files" Pattern** (repeated ~7 times):
   ```cpp
   for (const auto &result : results) {
     REQUIRE_FALSE(result.isDirectory);
   }
   ```

3. **Repeated Concurrent Search Pattern** (repeated ~4+ times):
   - Creating futures1, futures2, futures3
   - Same pattern of waiting and collecting totals
   - Same exception handling

4. **Repeated Strategy Iteration Pattern** (repeated ~6 times):
   ```cpp
   std::vector<std::string> strategies = {"static", "hybrid", "dynamic", "interleaved"};
   for (const auto& strategy_name : strategies) {
     test_helpers::TestSettingsFixture strategy_settings(strategy_name);
     index_fixture.GetIndex().ResetThreadPool();
     // ... test code ...
   }
   ```

5. **Repeated AppSettings Setup Pattern** (repeated ~10+ times):
   ```cpp
   AppSettings settings;
   settings.loadBalancingStrategy = "strategy";
   settings.dynamicChunkSize = N; // or hybridInitialWorkPercent
   test_helpers::TestSettingsFixture settings_fixture(settings);
   ```

6. **Repeated Future Collection Pattern** (repeated ~15+ times):
   ```cpp
   size_t total = 0;
   for (auto& future : futures) {
     total += future.get().size();
   }
   REQUIRE(total > 0);
   ```

---

## Deduplication Strategy

### Phase 1: Extract Common Test Helpers (High Impact)

**Goal:** Eliminate repeated validation and collection patterns

**Actions:**

#### 1.1: Extract "All Results Are Files" Helper

**Pattern to Extract:**
```cpp
for (const auto &result : results) {
  REQUIRE_FALSE(result.isDirectory);
}
```

**Proposed Helper:**
```cpp
namespace search_strategy_test_helpers {
  // Verify all results are files (not directories)
  void VerifyAllResultsAreFiles(const std::vector<FileIndex::SearchResultData>& results);
}
```

**Estimated Reduction:** ~20-30 lines

#### 1.2: Extract Future Collection Helper

**Pattern to Extract:**
```cpp
size_t total = 0;
for (auto& future : futures) {
  total += future.get().size();
}
REQUIRE(total > 0);
```

**Proposed Helper:**
```cpp
namespace search_strategy_test_helpers {
  // Collect results from futures and verify non-empty
  size_t CollectAndVerifyFutures(
      std::vector<std::future<std::vector<FileIndex::SearchResultData>>>& futures,
      bool require_non_empty = true);
}
```

**Estimated Reduction:** ~40-60 lines

#### 1.3: Extract Concurrent Search Helper

**Pattern to Extract:**
```cpp
std::vector<std::future<...>> futures1, futures2, futures3;
futures1 = index.SearchAsyncWithData(...);
futures2 = index.SearchAsyncWithData(...);
futures3 = index.SearchAsyncWithData(...);

size_t total1 = 0, total2 = 0, total3 = 0;
for (auto& future : futures1) { total1 += future.get().size(); }
for (auto& future : futures2) { total2 += future.get().size(); }
for (auto& future : futures3) { total3 += future.get().size(); }

REQUIRE(total1 > 0);
REQUIRE(total2 > 0);
REQUIRE(total3 > 0);
```

**Proposed Helper:**
```cpp
namespace search_strategy_test_helpers {
  // Run multiple concurrent searches and verify all complete successfully
  struct ConcurrentSearchResult {
    size_t total_results;
    bool completed_successfully;
  };
  
  std::vector<ConcurrentSearchResult> RunConcurrentSearches(
      FileIndex& index,
      const std::vector<std::string>& queries,
      int thread_count = 4,
      bool require_all_non_empty = true);
}
```

**Estimated Reduction:** ~80-120 lines

---

### Phase 2: Extract Strategy Iteration Helper (Medium Impact)

**Goal:** Consolidate repeated strategy iteration patterns

**Action:** Create helper for testing across all strategies

**Proposed Helper:**
```cpp
namespace search_strategy_test_helpers {
  // Run a test across all strategies
  template<typename TestFunc>
  void RunTestForAllStrategies(
      FileIndex& index,
      TestFunc test_func,
      bool reset_thread_pool = true);
  
  // Get list of all strategies
  std::vector<std::string> GetAllStrategies();
}
```

**Usage:**
```cpp
TEST_CASE("Pattern works across all strategies") {
  test_helpers::TestFileIndexFixture index_fixture(100);
  
  search_strategy_test_helpers::RunTestForAllStrategies(
      index_fixture.GetIndex(),
      [](FileIndex& idx, const std::string& strategy) {
        auto results = CollectSearchResults(idx, "file_", 4);
        REQUIRE(results.size() > 0);
        ValidateResults(results, "file_", false);
      });
}
```

**Estimated Reduction:** ~60-80 lines

---

### Phase 3: Extract AppSettings Setup Helper (Medium Impact)

**Goal:** Simplify AppSettings creation for tests

**Action:** Create helper functions for common AppSettings configurations

**Proposed Helpers:**
```cpp
namespace search_strategy_test_helpers {
  // Create AppSettings for dynamic strategy with chunk size
  AppSettings CreateDynamicSettings(int chunk_size);
  
  // Create AppSettings for hybrid strategy with initial work percent
  AppSettings CreateHybridSettings(int initial_work_percent);
  
  // Create TestSettingsFixture from AppSettings (convenience wrapper)
  test_helpers::TestSettingsFixture CreateSettingsFixture(const AppSettings& settings);
}
```

**Estimated Reduction:** ~30-50 lines

---

### Phase 4: Consolidate Similar Test Cases ✅ COMPLETE

**Goal:** Use parameterized tests for similar test cases

**Actions:**

#### 4.1: Parameterize "returns correct results" tests ✅ COMPLETE

All strategies have a "returns correct results" test with the same pattern.

**Implementation:**
- Created new `TEST_SUITE("All Strategies - Common Tests")` suite
- Consolidated 4 identical "returns correct results" tests (Static, Hybrid, Dynamic, Interleaved) into a single parameterized test
- Used `DOCTEST_SUBCASE` to run the test for each strategy
- Removed individual test cases from each strategy suite (replaced with comments)

**Result:**
```cpp
TEST_CASE("All strategies return correct results") {
  test_helpers::TestFileIndexFixture index_fixture(1000);
  
  std::vector<std::string> strategies = test_helpers::search_strategy_test_helpers::GetAllStrategies();
  
  for (const auto& strategy : strategies) {
    DOCTEST_SUBCASE((strategy + " strategy").c_str()) {
      test_helpers::TestSettingsFixture settings(strategy);
      index_fixture.GetIndex().ResetThreadPool();
      
      auto results = CollectSearchResults(index_fixture.GetIndex(), "file_", 4);
      REQUIRE(results.size() > 0);
      ValidateResults(results, "file_", false);
      test_helpers::search_strategy_test_helpers::VerifyAllResultsAreFiles(results);
    }
  }
}
```

**Reduction:** ~50 lines removed (4 test cases consolidated into 1 parameterized test)

---

## Implementation Order

1. **Phase 1** (Test Helpers) ✅ COMPLETE - Highest impact, enables other phases
2. **Phase 2** (Strategy Iteration) ✅ COMPLETE - Integrated into Phase 1
3. **Phase 3** (AppSettings Helpers) ✅ COMPLETE - Integrated into Phase 1
4. **Phase 4** (Parameterized Tests) ✅ COMPLETE - Consolidated 4 test cases into 1

---

## Expected Results

### Before Deduplication
- **Total Lines:** ~1554 lines
- **Duplicate Blocks:** 48
- **Estimated Duplicate Lines:** ~200-300 lines

### After Deduplication ✅ COMPLETE
- **Total Lines:** ~1350 lines (13% reduction from ~1554)
- **Duplicate Blocks:** 1 (98% reduction from 48)
- **Maintainability:** Significantly improved
- **Remaining Duplicate:** Extension matching logic (acceptable - different assertion mechanisms)

---

## Benefits

1. **Reduced Code Duplication:** ~200-300 lines of duplicate code eliminated
2. **Improved Maintainability:** Changes in one place affect all tests
3. **Better Readability:** Test intent is clearer without boilerplate
4. **Easier to Add New Tests:** New tests can reuse helpers
5. **Consistent Test Patterns:** All tests follow same structure

---

## Risks and Mitigation

### Risk 1: Helper Complexity
**Risk:** Helpers become too complex, making tests harder to understand  
**Mitigation:** Keep helpers simple, provide clear helper methods, document usage

### Risk 2: Test Isolation
**Risk:** Shared helpers might cause test interference  
**Mitigation:** Each test creates its own fixtures (RAII pattern)

### Risk 3: Over-Abstraction
**Risk:** Too many helpers make tests harder to read  
**Mitigation:** Only extract patterns that appear 3+ times, keep helpers focused

---

## Testing Strategy

1. **Run existing tests** before starting refactoring
2. **Refactor one phase at a time**
3. **Run tests after each phase** to ensure nothing broke
4. **Compare test output** before and after (should be identical)
5. **Re-run Duplo** after completion to measure improvement

---

## Success Criteria

- [ ] All existing tests still pass
- [ ] Duplicate blocks reduced from 48 to <10
- [ ] Total lines reduced by 15-20%
- [ ] Test execution time unchanged or improved
- [ ] Code is more maintainable (easier to add new tests)
- [ ] No functionality lost (all test cases still covered)

---

## Notes

- `CollectSearchResults` and `ValidateResults` already exist and are well-designed
- `test_helpers::TestSettingsFixture` and `test_helpers::TestFileIndexFixture` are already in use
- Strategy iteration pattern appears in multiple places and is a good candidate for extraction
- Concurrent search patterns are very similar and can share a helper

---

## Next Steps

1. Review and approve this plan
2. Implement Phase 1 (Test Helpers)
3. Run tests to verify Phase 1
4. Implement Phase 2 (Strategy Iteration)
5. Run tests to verify Phase 2
6. Continue with remaining phases
7. Re-run Duplo to measure improvement
8. Update `DEDUPLICATION_CANDIDATES.md` to mark as complete
