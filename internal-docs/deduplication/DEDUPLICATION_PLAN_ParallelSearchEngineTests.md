# Deduplication Plan: ParallelSearchEngineTests.cpp

## Overview

**File:** `tests/ParallelSearchEngineTests.cpp`  
**Current Status:** 8 duplicate blocks  
**Total Lines:** 311  
**Priority:** Medium (5th highest priority test file)

---

## Analysis

### Duplication Patterns Identified

1. **Repeated Engine Setup** (appears in 13 tests):
   ```cpp
   auto thread_pool = std::make_shared<SearchThreadPool>(4);
   ParallelSearchEngine engine(thread_pool);
   ```
   **Frequency:** 13 times  
   **Lines per occurrence:** 2 lines  
   **Total duplicate lines:** ~26 lines

2. **Index Setup Patterns**:
   ```cpp
   MockSearchableIndex index;
   PopulateSimpleIndex(index);
   // or
   PopulateLargeIndex(index, 100);
   ```
   **Frequency:** 11 times  
   **Pattern:** Creating index and populating it

3. **Search Execution Pattern** (appears in 8+ tests):
   ```cpp
   SearchContext ctx = CreateSimpleSearchContext("test");
   auto futures = engine.SearchAsync(index, "test", -1, ctx);
   auto results = CollectResults(futures);
   ```
   **Frequency:** 8+ times  
   **Lines per occurrence:** ~3 lines  
   **Total duplicate lines:** ~24 lines

---

## Deduplication Strategy

### Phase 1: Create RAII Fixture (Highest Impact)

**Goal:** Eliminate repeated engine setup code by creating a fixture class

**Action:** Create `TestParallelSearchEngineFixture` in `TestHelpers.h/cpp`

**Proposed Fixture:**
```cpp
namespace test_helpers {

/**
 * RAII fixture for managing ParallelSearchEngine test setup.
 * 
 * Automatically creates and configures all dependencies needed for ParallelSearchEngine tests:
 * - SearchThreadPool (default: 4 threads)
 * - ParallelSearchEngine instance
 * 
 * This eliminates the repetitive 2-line setup pattern in ParallelSearchEngine tests.
 */
class TestParallelSearchEngineFixture {
public:
  /**
   * Create fixture with default thread pool size (4 threads).
   */
  explicit TestParallelSearchEngineFixture(int thread_count = 4);
  
  ~TestParallelSearchEngineFixture() = default;
  
  /**
   * Get reference to the ParallelSearchEngine.
   * 
   * @return Reference to the configured ParallelSearchEngine
   */
  ParallelSearchEngine& GetEngine() { return engine_; }
  const ParallelSearchEngine& GetEngine() const { return engine_; }
  
  /**
   * Get reference to the SearchThreadPool.
   * 
   * @return Shared pointer to the thread pool
   */
  std::shared_ptr<SearchThreadPool> GetThreadPool() { return thread_pool_; }
  
  // Non-copyable, non-movable (RAII fixture should be created in place)
  TestParallelSearchEngineFixture(const TestParallelSearchEngineFixture&) = delete;
  TestParallelSearchEngineFixture& operator=(const TestParallelSearchEngineFixture&) = delete;
  TestParallelSearchEngineFixture(TestParallelSearchEngineFixture&&) = delete;
  TestParallelSearchEngineFixture& operator=(TestParallelSearchEngineFixture&&) = delete;

private:
  std::shared_ptr<SearchThreadPool> thread_pool_;
  ParallelSearchEngine engine_;
};

} // namespace test_helpers
```

**Usage Example:**
```cpp
// Before (2 lines):
auto thread_pool = std::make_shared<SearchThreadPool>(4);
ParallelSearchEngine engine(thread_pool);

// After (1 line):
test_helpers::TestParallelSearchEngineFixture fixture;
```

**Estimated Reduction:** ~20-25 lines (13 tests × ~2 lines setup)

---

### Phase 2: Extract Common Search Execution Helpers (Medium Impact)

**Goal:** Simplify search execution patterns

**Action:** Create helper functions for common search patterns

**Proposed Helpers:**
```cpp
namespace test_helpers {
namespace parallel_search_test_helpers {

// Execute search and collect results
std::vector<uint64_t> ExecuteSearchAndCollect(
    ParallelSearchEngine& engine,
    MockSearchableIndex& index,
    const std::string& query,
    int thread_count,
    const SearchContext& ctx);

// Execute search with data and collect results
std::vector<FileIndex::SearchResultData> ExecuteSearchWithDataAndCollect(
    ParallelSearchEngine& engine,
    MockSearchableIndex& index,
    const std::string& query,
    int thread_count,
    const SearchContext& ctx);

} // namespace parallel_search_test_helpers
} // namespace test_helpers
```

**Usage Example:**
```cpp
// Before:
SearchContext ctx = CreateSimpleSearchContext("test");
auto futures = engine.SearchAsync(index, "test", -1, ctx);
auto results = CollectResults(futures);

// After:
SearchContext ctx = CreateSimpleSearchContext("test");
auto results = test_helpers::parallel_search_test_helpers::ExecuteSearchAndCollect(
    fixture.GetEngine(), index, "test", -1, ctx);
```

**Estimated Reduction:** ~15-20 lines

---

## Implementation Order

1. **Phase 1** (RAII Fixture) ✅ COMPLETE - Highest impact, eliminates ~20-25 lines
2. **Phase 2** (Search Execution Helpers) ✅ COMPLETE - Medium impact, simplifies search patterns

---

## Expected Results

### Before Deduplication
- **Total Lines:** 311 lines
- **Duplicate Blocks:** 8
- **Estimated Duplicate Lines:** ~50-70 lines

### After Deduplication ✅ COMPLETE
- **Total Lines:** 293 lines (6% reduction from 311)
- **Duplicate Blocks:** 0 (100% reduction from 8)
- **Maintainability:** Significantly improved
- **Phase 1:** Created TestParallelSearchEngineFixture, refactored 13 tests to use fixture (311 → 298 lines)
- **Phase 2:** Extracted common search execution patterns into helper functions (298 → 293 lines)

---

## Implementation Notes

1. **Fixture Design:**
   - Support configurable thread pool size
   - Provide accessors for engine and thread pool
   - Keep it simple - just encapsulate the setup

2. **Helper Functions:**
   - Keep helpers focused on common patterns
   - Don't over-abstract - some tests need direct access to futures

3. **Testing:**
   - All existing tests should pass after refactoring
   - No functional changes, only code organization
   - Verify fixture properly initializes all components

---

## Success Criteria

- ✅ All test cases pass
- ✅ Duplicate blocks reduced from 8 to <3
- ✅ Code is more maintainable and readable
- ✅ No functional changes to test logic
