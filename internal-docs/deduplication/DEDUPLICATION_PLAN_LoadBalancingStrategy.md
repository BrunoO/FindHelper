# Deduplication Plan: `src/utils/LoadBalancingStrategy.cpp`

**File:** `src/utils/LoadBalancingStrategy.cpp`  
**Current Status:** 76 duplicate blocks (highest priority source file)  
**Target:** Reduce to 0 duplicate blocks

---

## Analysis Summary

This file implements 4 load balancing strategies (Static, Hybrid, Dynamic, Interleaved) with significant code duplication across:
- Lambda setup and initialization
- Thread timing recording
- Exception handling
- Thread pool validation
- Pattern matcher creation (duplicate with ParallelSearchEngine)

---

## Identified Duplication Patterns

### 1. **Lambda Setup Code** (HIGH PRIORITY)
**Location:** Repeated in all 4 strategies (Static: 246-260, Hybrid: 409-421, Dynamic: 565-577, Interleaved: 696-718)

**Duplicate Code:**
```cpp
auto start_time = std::chrono::high_resolution_clock::now();
std::vector<FileIndex::SearchResultData> local_results;
local_results.reserve(...);  // Different heuristics per strategy

// CRITICAL: Acquire shared_lock in worker thread
std::shared_lock lock(index.GetMutex());

// Get SoA view after acquiring lock
PathStorage::SoAView soaView = index.GetSearchableView();
size_t storage_size = index.GetStorageSize();

// Create pattern matchers once per thread
auto matchers = ParallelSearchEngine::CreatePatternMatchers(context);
```

**Impact:** ~15 lines × 4 strategies = ~60 lines of duplication

**Solution:** Extract to helper function:
```cpp
namespace load_balancing_detail {
struct ThreadSetup {
  std::chrono::high_resolution_clock::time_point start_time;
  std::vector<FileIndex::SearchResultData> local_results;
  std::shared_lock<std::shared_mutex> lock;
  PathStorage::SoAView soaView;
  size_t storage_size;
  ParallelSearchEngine::PatternMatchers matchers;
};

ThreadSetup SetupThreadWork(
    const ISearchableIndex& index,
    const SearchContext& context,
    size_t reserve_size);
}
```

---

### 2. **Thread Timing Recording** (HIGH PRIORITY)
**Location:** Static (284-302), Hybrid (464-482), Dynamic (613-626), Interleaved (774-801)

**Duplicate Code:**
```cpp
if (thread_timings) {
  auto end_time = std::chrono::high_resolution_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time).count();
  FileIndex::ThreadTiming timing;
  ParallelSearchEngine::RecordThreadTiming(
      timing, thread_idx, start_index, end_index,
      initial_items, dynamic_chunks_count, total_items_processed,
      total_bytes, local_results.size(), elapsed);
  if (thread_idx < static_cast<int>(thread_timings->size())) {
    (*thread_timings)[thread_idx] = timing;
  }
}
```

**Variations:**
- Static: `initial_items=0, dynamic_chunks=0, total_items=end_index-start_index`
- Hybrid: Uses `initial_items`, `dynamic_chunks_count`, `initial_items + dynamic_items_total`
- Dynamic: Uses `dynamic_chunks_count`, `total_items_processed`, `total_bytes_processed`
- Interleaved: Complex calculation for `first_chunk_start` and `last_chunk_end`

**Impact:** ~18 lines × 4 strategies = ~72 lines (with variations)

**Solution:** Extract to helper function with strategy-specific parameters:
```cpp
namespace load_balancing_detail {
void RecordThreadTimingIfRequested(
    std::vector<FileIndex::ThreadTiming>* thread_timings,
    int thread_idx,
    const std::chrono::high_resolution_clock::time_point& start_time,
    size_t start_index,
    size_t end_index,
    size_t initial_items,
    size_t dynamic_chunks_count,
    size_t total_items_processed,
    size_t total_bytes,
    size_t results_count,
    const PathStorage::SoAView& soaView,
    size_t storage_size);
}
```

---

### 3. **Exception Handling** (MEDIUM PRIORITY)
**Location:** 
- Static: Inline (262-282) - NOT using helper
- Hybrid: Uses `ProcessChunkWithExceptionHandling` helper ✅
- Dynamic: Uses `ProcessChunkWithExceptionHandling` helper ✅
- Interleaved: Inline (742-762) - NOT using helper

**Duplicate Code (Static and Interleaved):**
```cpp
try {
  ParallelSearchEngine::ProcessChunkRange(
      soaView, start_index, end_index, local_results, context, storage_size,
      matchers.filename_matcher, matchers.path_matcher);
} catch (const std::bad_alloc& e) {
  (void)e;
  LOG_ERROR_BUILD("Memory allocation failure in [StrategyName] (thread "
                  << thread_idx << ", range [" << start_index << "-"
                  << end_index << "])");
} catch (const std::runtime_error& e) {
  (void)e;
  LOG_ERROR_BUILD("Runtime error in [StrategyName] (thread "
                  << thread_idx << ", range [" << start_index << "-"
                  << end_index << "]): " << e.what());
} catch (const std::exception &e) {
  (void)e;
  LOG_ERROR_BUILD("Exception in [StrategyName] (thread "
                  << thread_idx << ", range [" << start_index << "-"
                  << end_index << "]): " << e.what());
}
```

**Impact:** ~20 lines × 2 strategies = ~40 lines

**Solution:** Refactor Static and Interleaved to use existing `ProcessChunkWithExceptionHandling` helper

---

### 4. **Thread Pool Validation** (MEDIUM PRIORITY)
**Location:** All 4 strategies (Static: 229-232, Hybrid: 388-391, Dynamic: 556-559, Interleaved: 681-684)

**Duplicate Code:**
```cpp
SearchThreadPool &thread_pool = executor.GetThreadPool();
if (size_t pool_thread_count = thread_pool.GetThreadCount(); pool_thread_count == 0) {
  LOG_ERROR_BUILD("[StrategyName]: Thread pool has 0 threads - cannot execute search");
  return futures; // Return empty futures vector
}
```

**Impact:** ~5 lines × 4 strategies = ~20 lines

**Solution:** Extract to helper function:
```cpp
namespace load_balancing_detail {
bool ValidateThreadPool(const ISearchExecutor& executor, const char* strategy_name);
}
```

---

### 5. **Duplicate Pattern Matcher Creation** (HIGH PRIORITY)
**Location:** 
- `load_balancing_detail::CreatePatternMatchers` (line 132-161) - UNUSED
- `ParallelSearchEngine::CreatePatternMatchers` (used in all strategies)

**Issue:** The file defines `load_balancing_detail::CreatePatternMatchers` but all strategies use `ParallelSearchEngine::CreatePatternMatchers` instead. The duplicate function is dead code.

**Impact:** ~30 lines of dead code

**Solution:** Remove `load_balancing_detail::CreatePatternMatchers` and `load_balancing_detail::PatternMatchers` struct (lines 64-67, 132-161)

---

### 6. **Thread Pool Enqueue Pattern** (LOW PRIORITY)
**Location:** All 4 strategies use similar `thread_pool.Enqueue([...]() mutable { ... })` pattern

**Note:** This is structural similarity, not true duplication. The lambda bodies differ significantly between strategies. This is acceptable duplication as it's part of the strategy pattern implementation.

---

## Deduplication Phases

### Phase 1: Remove Dead Code
**Goal:** Remove unused `load_balancing_detail::CreatePatternMatchers` and `PatternMatchers` struct

**Changes:**
- Remove `load_balancing_detail::PatternMatchers` struct (lines 64-67)
- Remove `load_balancing_detail::CreatePatternMatchers` function (lines 132-161)
- Update comment on line 55 to reflect that pattern matchers are created via `ParallelSearchEngine::CreatePatternMatchers`

**Expected Reduction:** ~30 lines, eliminates duplicate with `ParallelSearchEngine.cpp`

---

### Phase 2: Extract Thread Pool Validation
**Goal:** Extract thread pool validation to helper function

**Changes:**
- Add `load_balancing_detail::ValidateThreadPool` helper function
- Replace validation code in all 4 strategies with helper call

**Expected Reduction:** ~20 lines

---

### Phase 3: Standardize Exception Handling
**Goal:** Use `ProcessChunkWithExceptionHandling` helper in Static and Interleaved strategies

**Changes:**
- Refactor Static strategy (lines 262-282) to use `ProcessChunkWithExceptionHandling`
- Refactor Interleaved strategy (lines 742-762) to use `ProcessChunkWithExceptionHandling`

**Expected Reduction:** ~40 lines

---

### Phase 4: Extract Thread Setup
**Goal:** Extract common lambda setup code to helper

**Changes:**
- Create `load_balancing_detail::ThreadSetup` struct
- Create `load_balancing_detail::SetupThreadWork` helper function
- Refactor all 4 strategies to use helper

**Expected Reduction:** ~60 lines

**Challenges:**
- Different reserve heuristics per strategy (need parameter)
- Lock lifetime management (RAII in struct)
- SoA view lifetime (must be valid while lock is held)

---

### Phase 5: Extract Thread Timing Recording
**Goal:** Extract thread timing recording to helper function

**Changes:**
- Create `load_balancing_detail::RecordThreadTimingIfRequested` helper
- Handle strategy-specific parameter variations
- Refactor all 4 strategies to use helper

**Expected Reduction:** ~72 lines

**Challenges:**
- Interleaved strategy has complex `first_chunk_start`/`last_chunk_end` calculation
- Different strategies pass different parameters to `RecordThreadTiming`
- Need to handle optional `thread_timings` parameter

---

## Implementation Order

1. **Phase 1** (Quick win, no risk) - Remove dead code
2. **Phase 2** (Simple extraction) - Thread pool validation
3. **Phase 3** (Use existing helper) - Standardize exception handling
4. **Phase 4** (Complex, requires careful design) - Thread setup extraction
5. **Phase 5** (Complex, parameter variations) - Thread timing extraction

---

## Expected Results

**Before:** 76 duplicate blocks  
**After Phase 1:** ~70 duplicate blocks (removes duplicate with ParallelSearchEngine)  
**After Phase 2:** ~65 duplicate blocks  
**After Phase 3:** ~55 duplicate blocks  
**After Phase 4:** ~25 duplicate blocks  
**After Phase 5:** ~0 duplicate blocks (target)

**Total Lines Eliminated:** ~220 lines of duplicate code

---

## Testing Strategy

After each phase:
1. Run `DUPLO_MIN_LINES=5 ./scripts/run_duplo.sh` to verify reduction
2. Run unit tests: `scripts/build_tests_macos.sh`
3. Verify all 4 strategies still work correctly
4. Check performance hasn't regressed (timing code is critical path)

---

## Notes

- The `ProcessChunkWithExceptionHandling` helper already exists and is used by Hybrid and Dynamic strategies
- Static and Interleaved strategies should be refactored to use it (Phase 3)
- Thread setup extraction (Phase 4) is the most complex due to RAII and lifetime management
- Thread timing extraction (Phase 5) requires careful handling of strategy-specific parameters
- All changes must maintain thread safety and performance characteristics

---

## Related Files

- `src/search/ParallelSearchEngine.cpp` - Contains `CreatePatternMatchers` (duplicate with Phase 1)
- `src/utils/LoadBalancingStrategy.h` - Strategy interface
- `src/index/FileIndex.cpp` - Uses strategies
- `tests/FileIndexSearchStrategyTests.cpp` - Tests all strategies
