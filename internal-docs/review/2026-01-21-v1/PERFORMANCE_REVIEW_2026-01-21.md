# Performance Review - 2026-01-21

## Executive Summary
- **Performance Score**: 7/10
- **Top 3 Bottlenecks**: Full search result materialization, redundant pattern matcher creation, and lack of `reserve()` in vector operations.
- **Scalability Assessment**: The application will likely not scale to 10x more files without significant architectural changes. The current memory allocation strategy of materializing all search results into a single vector is a major bottleneck that will lead to excessive memory consumption and slow sorting/filtering operations with very large result sets.

The application's core search functionality is well-optimized, leveraging SIMD for string matching and a cache-friendly Structure of Arrays (SoA) data layout. However, several significant performance issues exist in the surrounding framework, particularly in memory allocation and object creation patterns.

## Findings

### High
1.  **Full Materialization of Search Results**
    - **Location:** `src/search/ParallelSearchEngine.cpp`
    - **Issue Type:** Algorithm Complexity / Space Complexity
    - **Current Cost:** For a search that matches a million files, the application allocates a `std::vector` that can consume hundreds of megabytes of RAM. This vector is then sorted and filtered, which can be very slow for large result sets.
    - **Improvement:** Adopt a virtualized, on-demand approach. The search engine should return only the file IDs of the matching results. The UI should then request the full `SearchResultData` for only the visible portion of the results list. This would reduce peak memory usage by orders of magnitude and make the UI responsive even with millions of matches.
    - **Benchmark Suggestion:** Measure peak memory usage and UI responsiveness for a search that returns 1 million+ results.
    - **Effort:** L
    - **Risk:** High (Requires significant architectural changes to the UI and data retrieval logic)

2.  **Redundant Creation of Pattern Matchers in Hot Path**
    - **Location:** `src/search/ParallelSearchEngine.cpp`
    - **Issue Type:** Memory Allocation Patterns / Allocation in Hot Paths
    - **Current Cost:** The `CreatePatternMatchers` function is called within the worker lambda for each chunk of work. This means that for a single search, the same regex or path pattern objects are compiled and allocated multiple times, creating unnecessary overhead in the search hot path.
    - **Improvement:** Create the pattern matchers once before the search begins and pass them to the worker threads. The `LightweightCallable` can be used to pass these pre-compiled matchers efficiently.
    - **Benchmark Suggestion:** Profile the `ParallelSearchEngine::SearchAsync` function, measuring the time spent in `CreatePatternMatchers`.
    - **Effort:** M
    - **Risk:** Low

### Medium
1.  **Missing `reserve()` for Search Result Vectors**
    - **Location:** `src/search/ParallelSearchEngine.cpp`
    - **Issue Type:** Memory Allocation Patterns / `std::vector` growing without `reserve()`
    - **Current Cost:** The `local_results` vector in `ProcessChunkRangeIds` is grown without a call to `reserve()`. This can lead to multiple reallocations and copies, especially when a search query matches a large percentage of the items in a chunk.
    - **Improvement:** Add a heuristic to pre-allocate the `local_results` vector. For example, reserve a percentage of the chunk size.
      ```cpp
      // In ProcessChunkRangeIds
      std::vector<uint64_t> local_results;
      local_results.reserve(chunk_end - chunk_start); // Or a fraction of it
      ```
    - **Benchmark Suggestion:** Measure the number of reallocations in `ProcessChunkRangeIds` using a custom allocator or a memory profiler.
    - **Effort:** S
    - **Risk:** Low

2.  **Pass-by-Value of `std::string` in `ToLower`**
    - **Location:** `src/utils/StringUtils.h`
    - **Issue Type:** Data Structure Efficiency / `std::string` copies
    - **Current Cost:** The `ToLower(std::string s)` function takes its string parameter by value, causing an unnecessary copy for every call. This function is used in performance-sensitive areas like search query processing.
    - **Improvement:** Change the function signature to take a `std::string_view` or `const std::string&` and return a new string.
      ```cpp
      std::string ToLower(std::string_view s);
      ```
    - **Benchmark Suggestion:** Profile a search with a complex, case-insensitive query and measure the time spent in the `ToLower` function.
    - **Effort:** M
    - **Risk:** Low (Requires updating all call sites)

## Quick Wins
1.  **Add `reserve()` to `local_results`:** This is a one-line change that can provide a significant performance improvement by reducing reallocations in the search hot loop.
2.  **Change `ToLower` to take `string_view`:** This is a simple signature change that will eliminate unnecessary string copies throughout the application.
3.  **Use `std::unordered_map` where appropriate:** A quick review of `std::map` usage might reveal opportunities to switch to `std::unordered_map` for O(1) lookups where ordering is not required.
