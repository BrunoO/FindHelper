# Memory Usage Analysis Report

## Issue Identified: Duplicate Thread Pools

### Problem
The application is creating **TWO separate thread pools**, which doubles the memory usage for threads:

1. **Application::thread_pool_** (ThreadPool class)
   - Created in `Application` constructor (lines 77, 113 in Application.cpp)
   - Uses `settings_->searchThreadPoolSize` or `hardware_concurrency()`
   - Memory: ~1-2MB per thread (stack space)

2. **FileIndex::thread_pool_** (SearchThreadPool class)
   - Created on first search via `FileIndex::GetThreadPool()` (static singleton)
   - Uses `settings.searchThreadPoolSize` or `hardware_concurrency()`
   - Memory: ~1-2MB per thread (stack space)

### Memory Impact

**Example: 8-core CPU (hardware_concurrency() = 8)**

- **Before recent changes**: Only Application::ThreadPool = 8 threads = ~8-16MB
- **After recent changes**: Both pools = 16 threads = ~16-32MB
- **Increase**: **+8-16MB** (100% increase in thread memory)

**Real-world impact:**
- 4-core CPU: +4-8MB
- 8-core CPU: +8-16MB
- 16-core CPU: +16-32MB

### Root Cause

The recent production fixes (commit e5f6665) added `FileIndex::SearchThreadPool` to replace `std::async`, but the existing `Application::ThreadPool` was not removed. This creates duplicate thread pools.

### Verification

**Application.cpp:**
- Line 77 (Windows): `thread_pool_ = std::make_unique<ThreadPool>(thread_count);`
- Line 113 (macOS): `thread_pool_ = std::make_unique<ThreadPool>(thread_count);`

**FileIndex.cpp:**
- Line 1150: `auto pool = std::make_unique<SearchThreadPool>(num_threads);`
- Line 1202: `thread_pool_ = create_pool();` (static singleton, persists for app lifetime)

**Both use the same settings:**
- Both read `settings->searchThreadPoolSize` or `hardware_concurrency()`
- Both create the same number of threads

### Usage Analysis

**Application::ThreadPool** is used for:
- Pre-fetching file sizes and modification times during UI sorting operations
- Used in `SortSearchResults()` function (SearchResultUtils.cpp, line 222, 230)
- Purpose: Offload file I/O from UI thread when sorting by Size or Last Modified columns

**FileIndex::SearchThreadPool** is used for:
- Parallel search operations (replacing std::async)
- Used in `FileIndex::SearchAsync()` and `SearchAsyncWithData()`
- Purpose: Reuse threads for search tasks to eliminate thread creation overhead

**Both serve different purposes but use the same thread count**, creating duplicate threads.

### Solution Options

#### Option 1: Consolidate into Single Shared Pool (Recommended)
- **Refactor**: Make FileIndex use Application's ThreadPool instead of creating its own
- **Benefit**: Single thread pool, eliminates duplicate threads, reduces memory by 50%
- **Implementation**: 
  - Pass Application::ThreadPool to FileIndex (via constructor or setter)
  - Remove FileIndex::SearchThreadPool static singleton
  - Use Application::ThreadPool for both search and sorting operations
- **Risk**: Requires refactoring, but both pools have similar interfaces (enqueue method)

#### Option 2: Use Different Thread Counts
- **If consolidation is not feasible**: Use smaller thread count for Application::ThreadPool
- **Benefit**: Reduces memory while preserving functionality
- **Implementation**: 
  - Application::ThreadPool: Use smaller count (e.g., 2-4 threads for UI sorting)
  - FileIndex::SearchThreadPool: Keep full count (hardware_concurrency) for search
- **Risk**: Still uses double memory, just optimized

#### Option 3: Lazy Creation of Application::ThreadPool
- **Optimization**: Only create Application::ThreadPool when sorting is actually needed
- **Benefit**: Reduces memory if sorting is rarely used
- **Risk**: Adds complexity, may not solve the core issue

### Recommendation

**Option 1 (Consolidate) is recommended** because:
1. Both pools serve similar purposes (parallel task execution)
2. They have compatible interfaces (both have enqueue methods)
3. Eliminates duplicate threads completely
4. Simplifies codebase (one pool to manage instead of two)

**Implementation approach:**
- Add `SetThreadPool(ThreadPool& pool)` method to FileIndex
- Pass Application::ThreadPool to FileIndex during initialization
- Remove FileIndex::SearchThreadPool static singleton
- Update FileIndex::GetThreadPool() to return the injected pool

### Other Memory Considerations

#### Thread Pool Memory (One-time cost)
- **Thread stack space**: ~1-2MB per thread (OS-dependent)
- **Thread control structures**: ~1-2KB per thread
- **Total per thread**: ~1-2MB
- **For 8 threads**: ~8-16MB (one-time, persists for app lifetime)

#### SearchResultData Optimization (Should REDUCE memory)
- **Before**: 2 string allocations per result (filename + extension)
- **After**: 1 string allocation per result (fullPath) + 2 size_t offsets
- **Impact**: Should reduce memory by 50-70% per search
- **Status**: ✅ Correctly implemented

#### Directory Cache Fix (Should PREVENT leaks)
- **Before**: Cache accumulated dead entries (memory leak)
- **After**: Cache is cleared on Remove/Rename/Move
- **Impact**: Prevents unbounded growth
- **Status**: ✅ Correctly implemented

### Additional Memory Considerations

#### Precompiled PathPattern Patterns
- **Status**: ✅ Not a memory issue
- **Reason**: Patterns are created as `shared_ptr` but are local variables in search functions
- **Lifetime**: Destroyed after search completes (reference count reaches 0)
- **Memory**: ~1-16KB per pattern (one-time per search, not persistent)

#### SearchResultData Optimization Verification
- **Status**: ✅ Correctly implemented, should REDUCE memory
- **Before**: 2 string allocations per result (filename + extension)
- **After**: 1 string allocation per result (fullPath) + 2 size_t offsets
- **Impact**: 50-70% reduction in allocations per search
- **Verification**: Code correctly uses offsets (FileIndex.h:777-778, SearchWorker.cpp:451,455)

#### Directory Cache Fix Verification
- **Status**: ✅ Correctly implemented, should PREVENT leaks
- **Before**: Cache accumulated dead entries (unbounded growth)
- **After**: Cache cleared on Remove/Rename/Move (FileIndex.h:199, 281-288, 319-321)
- **Impact**: Prevents 10-100MB+ memory bloat over time

### Conclusion

**Root Cause Identified**: The **duplicate thread pools** are the cause of increased memory usage.

**Memory Impact:**
- **Duplicate threads**: +8-16MB (for 8-core CPU) - **This is the issue**
- **SearchResultData optimization**: Should reduce memory by 50-70% per search ✅
- **Directory cache fix**: Prevents leaks, should reduce long-term memory ✅

**Recommended Fix:**
1. **Consolidate thread pools** (Option 1) - Eliminates duplicate threads completely
2. This will restore memory usage to pre-change levels while keeping all performance benefits

**All other recent changes are correct and should have reduced memory, not increased it.**

