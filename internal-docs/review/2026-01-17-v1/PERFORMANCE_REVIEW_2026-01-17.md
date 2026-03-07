# Performance Profiling Review - 2026-01-17

## Executive Summary
- **Performance Score**: 5/10
- **Top 3 Bottlenecks**:
    1.  Redundant pattern compilation in search hot path.
    2.  Inefficient search result materialization.
    3.  Potential for lock contention in `FileIndex`.
- **Scalability Assessment**: The current implementation will not scale well to 10x more files due to the performance issues listed above.
- **Quick Wins**:
    -   Cache compiled search patterns.
    -   Use a more efficient mechanism for collecting search results.

## Findings

### Critical
-   **Redundant Pattern Compilation in Search Hot Path**: The `CreatePatternMatchers` function is called inside the worker lambda for each search chunk in `ParallelSearchEngine::SearchAsync`. This means that for every chunk of the index that is searched, the application is recompiling the search patterns. This is a huge waste of CPU cycles, especially for complex regex patterns.
    -   **Location**: `src/search/ParallelSearchEngine.cpp`
    -   **Issue Type**: CPU Utilization
    -   **Current Cost**: High CPU usage, especially for complex search patterns.
    -   **Improvement**: Compile the search patterns once before the search begins and pass the compiled patterns to the worker threads.
    -   **Benchmark Suggestion**: Measure the time it takes to perform a search with a complex regex pattern before and after the change.
    -   **Effort**: M
    -   **Risk**: Low

### High
-   **Inefficient Search Result Materialization**: The search results are materialized into a `std::vector<uint64_t>` for each chunk, and then these vectors are merged. This is inefficient, as it requires multiple allocations and copies.
    -   **Location**: `src/search/ParallelSearchEngine.cpp`
    -   **Issue Type**: Memory Allocation Patterns
    -   **Current Cost**: High memory usage and high CPU usage due to allocations and copies.
    -   **Improvement**: Use a single, shared results vector that is protected by a mutex, or use a lock-free queue to collect the results.
    -   **Benchmark Suggestion**: Measure the memory usage and CPU usage of a search before and after the change.
    -   **Effort**: M
    -   **Risk**: Medium

### Medium
-   **Potential for Lock Contention in `FileIndex`**: The `FileIndex` class uses a `std::shared_mutex` to protect its data structures. While this allows for concurrent reads, it can still lead to lock contention if there are many writers.
    -   **Location**: `src/index/FileIndex.h`
    -   **Issue Type**: Lock Contention & Synchronization
    -   **Current Cost**: Reduced search performance under heavy write load.
    -   **Improvement**: Consider using a more fine-grained locking strategy, or a lock-free data structure.
    -   **Benchmark Suggestion**: Measure the search performance under different write loads.
    -   **Effort**: L
    -   **Risk**: High
