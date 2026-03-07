# Performance Review - 2026-01-05

## Executive Summary
- **Performance Score**: 7/10
- **Top 3 Bottlenecks**:
    1.  String operations (copies and allocations) in the search hot path.
    2.  Inefficient use of `std::vector` without `reserve()`.
    3.  Potential for lock contention in the `SearchWorker` and `FileIndex` classes.
- **Scalability Assessment**: The application should be able to handle a 10x increase in files, but search latency will likely increase due to the string operations and potential lock contention.
- **Quick Wins**:
    -   Replace `std::string` with `std::string_view` in many function parameters.
    -   Add `reserve()` to `std::vector`s where the size is known.

## Findings

### 1. Memory Allocation Patterns

-   **Location:** `src/index/FileIndex.cpp` and other files
-   **Issue Type:** `std::vector` growing without `reserve()`
-   **Current Cost:** In loops where the number of elements to be added to a `std::vector` is known, failing to call `reserve()` can lead to multiple reallocations, which is inefficient. This can cause a significant performance hit, especially when dealing with millions of files.
-   **Improvement:** Call `reserve()` on the vector before entering the loop to pre-allocate the required memory. This will avoid reallocations and improve performance.
-   **Benchmark Suggestion:** Measure the time it takes to build a large index (e.g., 1 million files) with and without `reserve()`.
-   **Effort:** S (< 1hr)
-   **Risk:** Low.

### 2. Data Structure Efficiency

-   **Location:** Across the codebase
-   **Issue Type:** `std::string` copies where `std::string_view` works
-   **Current Cost:** Many functions take `const std::string&` as a parameter, even when they only need to read the string. This can lead to unnecessary string copies, especially in the search hot path.
-   **Improvement:** Replace `const std::string&` with `std::string_view` in function parameters where the function does not need to own the string. This will avoid allocations and improve performance.
-   **Benchmark Suggestion:** Profile the search function with a profiler like Perf or Instruments to measure the time spent in string operations before and after the change.
-   **Effort:** M (1-4hrs)
-   **Risk:** Medium. Requires careful analysis to ensure that the `string_view` does not outlive the underlying string.

-   **Location:** `src/index/FileIndex.cpp`
-   **Issue Type:** Duplicate strings that could use interning
-   **Current Cost:** The `FileIndex` stores the full path for every file. In a large file system, there will be many duplicate directory names, leading to wasted memory.
-   **Improvement:** Implement a string interning system (string pool) for directory paths. This would store each unique directory path only once and use a pointer or an ID to refer to it.
-   **Benchmark Suggestion:** Measure the memory usage of the application with a large index before and after implementing string interning.
-   **Effort:** L (> 4hrs)
-   **Risk:** Medium. Requires careful implementation to ensure thread safety.

### 3. Lock Contention & Synchronization

-   **Location:** `src/search/SearchWorker.cpp`
-   **Issue Type:** Lock held during expensive computation
-   **Current Cost:** The main search loop in `SearchWorker` holds a lock for the entire duration of the search, including post-processing. This can block other threads and reduce parallelism.
-   **Improvement:** Reduce the lock scope to only the critical sections where data is being accessed. Release the lock before performing expensive computations that don't require it.
-   **Benchmark Suggestion:** Use a profiler with thread visualization to identify lock contention under heavy search load.
-   **Effort:** M (1-4hrs)
-   **Risk:** Medium. Requires careful analysis to avoid data races.

### 4. Algorithm Complexity

-   **Location:** `src/ui/ResultsTable.cpp`
-   **Issue Type:** Repeated sorting of mostly-sorted data
-   **Current Cost:** When the user sorts the results table, the entire result set is re-sorted. If the user clicks the same column header again, the data is re-sorted even though it is already in the correct order.
-   **Improvement:** Before sorting, check if the data is already sorted by the selected column. If it is, simply reverse the order of the data instead of re-sorting it.
-   **Benchmark Suggestion:** Measure the time it takes to sort a large result set multiple times on the same column.
-   **Effort:** S (< 1hr)
-   **Risk:** Low.
