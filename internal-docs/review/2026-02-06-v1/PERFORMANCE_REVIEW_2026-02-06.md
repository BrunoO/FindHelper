# Performance Review - 2026-02-06

## Executive Summary
- **Health Score**: 8/10
- **Critical Issues**: 0
- **High Issues**: 2
- **Total Findings**: 8
- **Estimated Remediation Effort**: 10 hours

## Findings

### High
1. **Excessive String Allocations in Search Results**
   - **Location**: `src/search/SearchWorker.cpp` - `ConvertSearchResultData`
   - **Issue Type**: Memory Allocation Patterns
   - **Current Cost**: Millions of `std::string` allocations for large result sets (e.g., "Show All").
   - **Improvement**: Use a string pool or return `std::string_view` where possible, or only materialize the full path string when it's actually visible in the UI (lazy materialization).
   - **Benchmark Suggestion**: Measure "Show All" search time on an index of 1M files.
   - **Effort**: Medium
   - **Risk**: Moderate (Ownership management becomes more complex)

2. **Sequential Post-Processing of Search Results**
   - **Location**: `src/search/SearchWorker.cpp` - `ProcessSearchFutures`
   - **Issue Type**: Scalability Limits
   - **Current Cost**: After parallel search finishes, results are collected and converted sequentially.
   - **Improvement**: Move result conversion (offsets calculation, etc.) into the parallel worker threads so it's completed before the futures return.
   - **Benchmark Suggestion**: Compare "Search with 10k results" vs "Search with 500k results".
   - **Effort**: Medium
   - **Risk**: Low

### Medium
1. **Extension Filter Hoisting in SIMD Search**
   - **Location**: `src/search/ParallelSearchEngine.cpp`
   - **Issue Type**: SIMD & Vectorization
   - **Current Cost**: Extension checks are performed for every matching filename, even if the extension filter is the same for the whole chunk.
   - **Improvement**: Hoist extension filter checks or use a more efficient bitmask-based approach if multiple extensions are selected.
   - **Benchmark Suggestion**: Search with a common filename (e.g., "test") and an extension filter.
   - **Effort**: Small
   - **Risk**: Low

2. **Shared Mutex Contention during High-Rate USN Updates**
   - **Location**: `src/index/FileIndex.h`
   - **Issue Type**: Lock Contention & Synchronization
   - **Current Cost**: Search threads (shared lock) may be blocked by a burst of USN updates (unique lock).
   - **Improvement**: Use a double-buffered index or a more granular locking strategy (e.g., per-directory locks or lock-free data structures for certain operations).
   - **Benchmark Suggestion**: Measure search latency while a large background file operation (e.g., `git clone` or `npm install`) is running.
   - **Effort**: Large
   - **Risk**: High (Concurrency bugs)

### Low
1. **Repeated Time Queries in Logger**
   - **Location**: `src/utils/Logger.cpp`
   - **Issue Type**: System Call Overhead
   - **Current Cost**: Every log call queries the system clock.
   - **Improvement**: Cache the timestamp for messages occurring in the same millisecond.
   - **Benchmark Suggestion**: Microbenchmark of `Logger::Log` in a tight loop.
   - **Effort**: Small
   - **Risk**: Zero

## Quick Wins
1. **Move path conversion to parallel threads**: Significant reduction in post-processing latency for large result sets.
2. **Hoist extension checks in `ParallelSearchEngine`**: Easy optimization for filtered searches.
3. **Reserve vector capacity in `ProcessSearchFutures`**: Prevent multiple reallocations when merging results from threads.

## Recommended Actions
1. **Implement Lazy Path Materialization**: This is the single biggest opportunity to reduce memory pressure and allocation overhead.
2. **Optimize Sort Comparator**: Ensure the comparator used for results is as lean as possible (already using `inline` as per memory, but continue to audit).
3. **Profile with 10M+ Files**: Identify the next set of bottlenecks as the index grows beyond typical workstation sizes.

## Scalability Assessment
The application is well-positioned for 1M-5M files thanks to its SoA (Structure of Arrays) design and AVX2 search. To reach 10M-50M files, the primary bottlenecks will be memory usage of the path storage and the overhead of `std::string` allocations for results. A more memory-efficient path representation (e.g., prefix compression) may be needed.
