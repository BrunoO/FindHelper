# Performance Review - 2026-01-12

## Executive Summary
- **Performance Score**: 7/10
- **Top 3 Bottlenecks**:
    1.  **Redundant Matcher Creation**: Expensive pattern matchers are created inside the search loop, causing significant redundant computation.
    2.  **Full Result Materialization**: All search results are collected into a single `std::vector` before being displayed, leading to high memory usage and slow sorting for large result sets.
    3.  **Unnecessary String Allocations**: Widespread use of `const std::string&` instead of `std::string_view` causes frequent, unnecessary heap allocations in performance-sensitive code.
- **Scalability Assessment**: The current architecture will struggle to scale to 10x more files primarily due to the memory pressure from materializing the entire result set. The core search scales well due to the parallel design, but the result handling is a major bottleneck.

## Findings

### High Impact
1.  **Issue Type**: Algorithm Complexity / Redundant Computation
    -   **Location**: `src/search/ParallelSearchEngine.cpp` (within the search worker lambda)
    -   **Current Cost**: High. For every file in the index, the search loop creates new `PathPatternMatcher`, `SimpleRegex`, or `std::regex` objects. This is extremely wasteful, as the patterns do not change during a search.
    -   **Improvement**: Create the matcher objects once, outside the parallel search loop. Pass a `std::function` or a pre-built matcher object to the worker threads. This avoids millions of redundant, expensive object constructions.
    -   **Benchmark Suggestion**: Measure the time taken for a complex regex search on a large index before and after the change.
    -   **Effort**: M
    -   **Risk**: Low. This is a straightforward refactoring to hoist invariant computations out of a loop.

2.  **Issue Type**: Memory Allocation Patterns / Data Structure Efficiency
    -   **Location**: `src/search/SearchWorker.cpp`
    -   **Current Cost**: High. All `SearchResult` objects are stored in a single `std::vector<SearchResult>`. For a search with 1 million results, this can consume hundreds of megabytes of RAM and make sorting/filtering very slow.
    -   **Improvement**: Implement a virtualized results list. Instead of storing `SearchResult` objects, store only the IDs or indices of the matching files. The full `SearchResult` data would then be retrieved on-demand as the user scrolls through the results in the UI. This dramatically reduces the memory footprint and initial processing time.
    -   **Benchmark Suggestion**: Measure peak memory usage and time-to-display-first-result for a search that yields >1 million results.
    -   **Effort**: L
    -   **Risk**: Medium. This requires significant changes to the UI and data handling logic.

3.  **Issue Type**: Memory Allocation Patterns / String Storage
    -   **Location**: Codebase-wide, especially in search and path manipulation functions.
    -   **Current Cost**: Medium. Functions frequently take `const std::string&` as parameters, forcing heap allocations for string literals or `char*` buffers. In hot paths like path matching, this adds up to significant overhead.
    -   **Improvement**: Systematically replace `const std::string&` with `std::string_view` for read-only parameters. This eliminates unnecessary memory allocations and copies.
    -   **Benchmark Suggestion**: Profile a search operation with many path components, measuring time spent in `std::string` constructors.
    -   **Effort**: M
    -   **Risk**: Low. This is a safe, modern C++ practice.

### Medium Impact
1.  **Issue Type**: Data Structure Efficiency / Container Choice
    -   **Location**: `src/index/FileIndex.h`
    -   **Current Cost**: Medium. The `FileIndex` uses `std::unordered_map` for its various lookup tables. While good, `boost::unordered_map` (available via `FAST_LIBS_BOOST=ON`) or a sorted `std::vector` with binary search could be faster for the specific access patterns (high-rate lookups, infrequent insertions).
    -   **Improvement**: Benchmark replacing `std::unordered_map` with `boost::unordered_map` or a sorted vector. The existing `FAST_LIBS_BOOST` option makes this easy to test.
    -   **Benchmark Suggestion**: Measure the performance of `FileIndex::Search` with different underlying hash map implementations.
    -   **Effort**: S (since the abstraction already exists)
    -   **Risk**: Low.

2.  **Issue Type**: I/O & System Call Overhead
    -   **Location**: `src/index/LazyAttributeLoader.cpp`
    -   **Current Cost**: Medium. File attributes (size, timestamp) are loaded on-demand, which is good. However, they are loaded one file at a time, potentially leading to inefficient I/O patterns if many attributes are requested at once (e.g., when sorting).
    -   **Improvement**: Implement batching. When attributes for one file are requested, prefetch the attributes for other nearby files in the result list that are also missing them. This would consolidate multiple system calls into fewer, potentially more efficient, I/O operations.
    -   **Benchmark Suggestion**: Measure the time it takes to sort a large result list by "Date Modified" for the first time.
    -   **Effort**: M
    -   **Risk**: Low.

### Quick Wins
1.  **Issue Type**: Memory Allocation Patterns
    -   **Location**: `src/path/PathBuilder.cpp`
    -   **Current Cost**: Low but unnecessary. The `BuildFullPath` function concatenates strings using `+`, which can lead to multiple small allocations.
    -   **Improvement**: Pre-calculate the required string length, `reserve()` the memory in the output string, and then `append()` the components. This guarantees a single allocation.
    -   **Benchmark Suggestion**: Microbenchmark the `BuildFullPath` function.
    -   **Effort**: S
    -   **Risk**: Low.

2.  **Issue Type**: Cache Efficiency
    -   **Location**: `src/search/SearchResult.h`
    -   **Current Cost**: Low. The `SearchResult` struct mixes frequently accessed data (like file ID) with infrequently accessed data (like the full path string), potentially wasting cache space when only the IDs are needed.
    -   **Improvement**: This is related to the "Full Result Materialization" issue. By storing only file IDs and loading other data on demand, the cache efficiency of processing search results would be significantly improved.
    -   **Benchmark Suggestion**: N/A (covered by the virtualization benchmark).
    -   **Effort**: S (as part of the larger virtualization effort)
    -   **Risk**: Low.