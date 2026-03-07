# Performance Review - 2026-01-04

## Executive Summary
- **Performance Score**: 7/10
- **Top 3 Bottlenecks**:
    1.  String copies and allocations in search hot path.
    2.  Lack of `reserve()` calls for vectors.
    3.  Potential for lock contention in `SearchWorker`.
- **Scalability Assessment**: The application should be able to handle a 10x increase in files, but performance may degrade due to the identified bottlenecks.

## Findings

### High
1.  **String Copies in Search Hot Path**
    - **Location**: `ParallelSearchEngine.cpp`, `FileIndex.cpp`
    - **Issue Type**: Data Structure Efficiency
    - **Current Cost**: Unnecessary string copies and memory allocations in the search hot path can significantly degrade performance, especially for large result sets.
    - **Improvement**: Use `std::string_view` instead of `const std::string&` in function parameters where the function only needs to read the string. This will avoid unnecessary copies and allocations.
    - **Benchmark Suggestion**: Measure the time it takes to perform a search with a large number of results before and after the change.
    - **Effort**: M
    - **Risk**: Low. `std::string_view` is a non-owning view, so care must be taken to ensure that the underlying string is not modified or destroyed while the view is in use.

2.  **Missing `reserve()` calls for vectors**
    - **Location**: `FileIndex.cpp` and others.
    - **Issue Type**: Memory Allocation Patterns
    - **Current Cost**: Repeated reallocations of vectors can be a significant performance bottleneck, especially when adding a large number of elements.
    - **Improvement**: Call `reserve()` on vectors before adding a known number of elements to them.
    - **Benchmark Suggestion**: Measure the time it takes to index a large number of files before and after the change.
    - **Effort**: S
    - **Risk**: Low.

### Medium
1.  **Potential for Lock Contention in `SearchWorker`**
    - **Location**: `SearchWorker.cpp`
    - **Issue Type**: Lock Contention & Synchronization
    - **Current Cost**: The mutex in `SearchWorker` is held for the entire duration of the search, which can block other threads and reduce concurrency.
    - **Improvement**: Reduce the scope of the lock to only protect the shared data that is being accessed.
    - **Benchmark Suggestion**: Measure the search throughput with multiple concurrent searches before and after the change.
    - **Effort**: S
    - **Risk**: Medium. Incorrectly reducing the scope of the lock could lead to race conditions.

2.  **`std::map` where `std::unordered_map` might be better**
    - **Location**: `FileIndex.cpp`
    - **Issue Type**: Data Structure Efficiency
    - **Current Cost**: `std::map` has a logarithmic time complexity for lookups, while `std::unordered_map` has an average constant time complexity. For large maps, this can make a significant difference.
    - **Improvement**: Consider using `std::unordered_map` instead of `std::map` if the order of elements is not important.
    - **Benchmark Suggestion**: Measure the time it takes to perform lookups in the map before and after the change.
    - **Effort**: S
    - **Risk**: Low.

## Summary

-   **Performance Score**: 7/10. The application has some good performance optimizations, such as the use of AVX2 for string searching. However, there are also some significant bottlenecks that could be addressed to improve performance.
-   **Top 3 Bottlenecks**:
    1.  String copies and allocations in the search hot path.
    2.  Lack of `reserve()` calls for vectors.
    3.  Potential for lock contention in `SearchWorker`.
-   **Scalability Assessment**: The application should be able to handle a 10x increase in files, but performance may degrade due to the identified bottlenecks.
-   **Quick Wins**:
    1.  Add `reserve()` calls to vectors.
    2.  Use `std::string_view` in the search hot path.
-   **Benchmark Recommendations**:
    1.  Measure search performance with a large number of results.
    2.  Measure indexing performance with a large number of files.
    3.  Measure search throughput with multiple concurrent searches.
