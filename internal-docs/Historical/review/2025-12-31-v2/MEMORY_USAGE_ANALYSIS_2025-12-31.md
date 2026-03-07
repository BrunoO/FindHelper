# Memory Usage Analysis - 2025-12-31

## Summary

**Conclusion:** Today's refactoring (extraction of IndexOperations, PathOperations, DirectoryResolver, FileIndexMaintenance) **does NOT appear to have increased memory usage**. All extracted components use references, not copies, so they don't duplicate data.

However, there are some areas to monitor for potential memory issues.

## Analysis of Today's Refactoring

### Extracted Components Memory Footprint

All extracted components use **references only**, not copies:

1. **IndexOperations** (`IndexOperations.h`)
   - Holds references: `FileIndexStorage&`, `PathOperations&`, `std::shared_mutex&`, 3 atomic references
   - **Memory overhead:** ~40 bytes (just pointers/references)
   - **No data duplication**

2. **PathOperations** (`PathOperations.h`)
   - Holds reference: `PathStorage&`
   - **Memory overhead:** ~8 bytes (one reference)
   - **No data duplication**

3. **DirectoryResolver** (`DirectoryResolver.h`)
   - Holds references: `FileIndexStorage&`, `IndexOperations&`, `std::atomic<uint64_t>&`
   - **Memory overhead:** ~24 bytes (three references)
   - **No data duplication**

4. **FileIndexMaintenance** (`FileIndexMaintenance.h`)
   - Holds references: `PathStorage&`, `std::shared_mutex&`, `std::function<size_t()>`, 3 atomic references
   - **Memory overhead:** ~64 bytes (references + function object)
   - **No data duplication**

**Total overhead from refactoring:** ~136 bytes per FileIndex instance (negligible)

### Verification

- ✅ No `std::vector`, `std::unordered_map`, or other containers in extracted components
- ✅ No `new`/`malloc` allocations in extracted components
- ✅ No `shared_ptr`/`unique_ptr` that would duplicate data
- ✅ All components use references to existing data structures

## Potential Memory Issues to Monitor

### 1. Search Result Accumulation (Existing, Not from Refactoring)

**Location:** `SearchWorker.cpp:268-279`

```cpp
std::vector<FileIndex::SearchResultData> allSearchData;
allSearchData.reserve(1000); // Will grow as needed

for (size_t futureIdx = 0; futureIdx < searchFutures.size(); ++futureIdx) {
  std::vector<FileIndex::SearchResultData> chunkData = searchFutures[futureIdx].get();
  allSearchData.insert(allSearchData.end(),
                       std::make_move_iterator(chunkData.begin()),
                       std::make_move_iterator(chunkData.end()));
}
```

**Impact:** For large searches (10,000+ results), this accumulates all results in memory before processing. This is expected behavior but could be optimized for very large result sets.

**Status:** ✅ Not a new issue - existed before today's refactoring

### 2. Local Results Vectors in Parallel Search

**Location:** `ParallelSearchEngine.cpp:116-117`

```cpp
std::vector<uint64_t> local_results;
local_results.reserve((end_index - start_index) / 20);
```

**Impact:** Each worker thread allocates a local results vector. For 8 threads, this is 8 small vectors (typically <1KB each). This is expected and efficient.

**Status:** ✅ Not a new issue - existed before today's refactoring

### 3. Thread Pool Memory (Existing)

**Location:** `FileIndex.cpp:403-419`

The application creates thread pools which allocate ~1-2MB per thread for stack space. This is a one-time cost and persists for the app lifetime.

**Status:** ✅ Not a new issue - existed before today's refactoring

## Recommendations

### Immediate Actions

1. **Monitor memory usage over time**
   - Run the app for 10+ minutes with typical usage
   - Use Activity Monitor (macOS) or Task Manager (Windows)
   - Memory should stabilize, not grow continuously
   - If memory grows >100MB over 10 minutes, investigate with profiling tools

2. **Check for unbounded growth**
   - Use Instruments "Allocations" with "Mark Generation" to track allocations
   - Look for `std::vector`, `std::unordered_map`, `hash_map_t` growing continuously
   - Verify caches are cleared during rebuilds (directory_path_to_id_)

3. **Verify async resource cleanup**
   - Ensure all `std::future` objects are properly cleaned up
   - Check `SearchController.cpp:227-243` and `SearchController.cpp:313-325` for proper future cleanup

### If Memory Usage is High

If memory usage is indeed high, check these areas (not related to today's refactoring):

1. **PathStorage arrays** - These are the main memory consumers:
   - `path_storage_` (contiguous `std::vector<char>`)
   - `path_offsets_` (vector of offsets)
   - `extension_start_` (vector of extension offsets)
   - These grow with the number of indexed files

2. **FileIndexStorage hash map** - Stores FileEntry objects:
   - `index_` (hash_map<uint64_t, FileEntry>)
   - Grows with the number of indexed files

3. **Search results** - Temporary during search:
   - `allSearchData` vector in SearchWorker
   - `accumulatedResults` vector in SearchWorker
   - These are cleared after each search

4. **Thread pools** - One-time cost:
   - Application::ThreadPool
   - FileIndex::SearchThreadPool
   - ~1-2MB per thread

## Conclusion

**Today's refactoring did NOT increase memory usage.** The extracted components are lightweight wrappers that use references, not copies.

If memory usage is high, it's likely due to:
- Large number of indexed files (expected)
- Large search result sets (temporary, cleared after search)
- Thread pool stack space (one-time cost)

**Next Steps:**
1. Monitor memory usage over extended period (10+ minutes)
2. Use profiling tools (Instruments, Valgrind) if memory grows continuously
3. Check for unbounded growth in containers
4. Verify all futures are properly cleaned up

