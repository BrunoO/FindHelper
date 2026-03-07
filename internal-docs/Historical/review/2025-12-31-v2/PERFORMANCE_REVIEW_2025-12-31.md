# Performance Review - 2025-12-31

## Executive Summary
- **Performance Score**: 8/10
- **Top 3 Bottlenecks**:
    1.  Memory allocations in the search worker lambdas (`std::vector<uint64_t>`).
    2.  Collection of search results from futures in the main thread.
    3.  Potential for lock contention on the single `shared_mutex` in `FileIndex`.
- **Scalability Assessment**: The application should scale well to 10x more files, thanks to the efficient data storage and parallel search. However, memory usage will increase linearly with the number of files, and lock contention could become an issue at very large scales.

## Findings

### 1. Memory Allocation Patterns
-   **Location**: `ParallelSearchEngine.cpp`
-   **Issue Type**: Allocation in Hot Paths
-   **Current Cost**: In the `SearchAsync` method, each worker lambda allocates a `std::vector<uint64_t>` to store its local results. For a large number of threads and chunks, this can lead to a high number of small allocations, which can cause heap fragmentation and contention.
-   **Improvement**: Use a memory pool to allocate the local result vectors. This would reduce the number of heap allocations and improve performance.
-   **Benchmark Suggestion**: Measure the time taken for a search with a large number of threads and a large number of files, both with and without the memory pool.
-   **Effort**: M
-   **Risk**: Low, as long as the memory pool is implemented correctly.

### 2. Algorithm Complexity
-   **Location**: `FileIndex.cpp`
-   **Issue Type**: Unnecessary copies of large data structures
-   **Current Cost**: The `SearchAsync` method returns a `std::vector<std::future<std::vector<uint64_t>>>`. The calling code then has to iterate over the futures, get the results, and merge them into a single vector. This can be inefficient, as it involves multiple copies and allocations.
-   **Improvement**: Instead of returning a vector of futures, the `SearchAsync` method could take a callback function that is invoked with the results from each thread. This would allow the calling code to process the results as they become available, without having to store them all in memory at once.
-   **Benchmark Suggestion**: Measure the time and memory usage for a large search, both with the current approach and with the callback-based approach.
-   **Effort**: M
-   **Risk**: Low.

### 3. Lock Contention & Synchronization
-   **Location**: `FileIndex.h`
-   **Issue Type**: Granularity Issues
-   **Current Cost**: The `FileIndex` class uses a single `shared_mutex` to protect all of its data. This can lead to contention when different parts of the data are accessed concurrently.
-   **Improvement**: Use more granular locking to reduce contention. For example, one mutex for the main index, another for the path storage, and another for the directory cache.
-   **Benchmark Suggestion**: Use a profiler to measure the amount of time spent waiting for locks during a high-concurrency workload.
-   **Effort**: L
-   **Risk**: High, as incorrect lock management can lead to deadlocks or data corruption.

## Quick Wins
-   **Reserve vector capacity**: In the `SearchAsync` lambda in `ParallelSearchEngine.cpp`, the `local_results` vector is reserved with a heuristic (`(end_index - start_index) / 20`). This could be improved by using a more accurate estimate, or by using a small, fixed-size buffer on the stack and only allocating on the heap if more space is needed.
-   **Pre-compile regexes**: The `CreatePatternMatchers` function in `ParallelSearchEngine.cpp` compiles regexes on the fly if they are not pre-compiled. While the `SearchContext` does pre-compile them, it would be beneficial to ensure that all regexes are pre-compiled to avoid this overhead in the hot path.

## Benchmark Recommendations
-   **Search latency**: Measure the time from when a search is initiated to when the first results are displayed.
-   **Index build time**: Measure the time it takes to build the index from scratch for a large number of files.
-   **Memory usage**: Measure the peak memory usage of the application during indexing and searching.
