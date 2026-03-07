# Performance Review - 2026-01-02

## Executive Summary
- **Performance Score**: 8/10
- **Top 3 Bottlenecks**: Lock contention in `FileIndex`, string allocations in search, and potential for inefficient regex patterns.
- **Scalability Assessment**: The application should scale well to 10x more files, but lock contention in `FileIndex` may become a bottleneck.
- **Quick Wins**: Use `std::string_view` in more places to avoid string allocations.

## Findings

### High

**1. Lock Contention in `FileIndex`**
- **Location**: `FileIndex.h`
- **Issue Type**: Lock Contention & Synchronization
- **Current Cost**: The single `std::shared_mutex` in `FileIndex` can become a bottleneck when there are many concurrent readers and writers.
- **Improvement**: Use a more granular locking strategy. For example, use a separate mutex for each of the main data structures in `FileIndex`.
- **Benchmark Suggestion**: Measure the time spent waiting for the `FileIndex` mutex under a high load of concurrent searches and index updates.
- **Effort**: Medium
- **Risk**: High - incorrect use of multiple mutexes can lead to deadlocks.

### Medium

**2. Unnecessary String Allocations in Search**
- **Location**: `FileIndex.cpp`
- **Issue Type**: Memory Allocation Patterns
- **Current Cost**: The `SearchAsyncWithData` function allocates a new `std::string` for the full path of each search result. This can be expensive when there are many search results.
- **Improvement**: Modify the `SearchResultData` struct to store a `std::string_view` instead of a `std::string`. This will avoid the need to allocate a new string for each search result.
- **Benchmark Suggestion**: Measure the memory usage of the application during a search that returns a large number of results.
- **Effort**: Small
- **Risk**: Low

**3. Potential for Inefficient Regex Patterns**
- **Location**: `SearchPatternUtils.h`
- **Issue Type**: Algorithm Complexity
- **Current Cost**: The application does not have any protection against inefficient regex patterns. A malicious user could craft a search query with a computationally expensive regular expression that could cause the application to hang or crash.
- **Improvement**: Use a regex engine that is not vulnerable to ReDoS, or implement a timeout for regex matching operations.
- **Benchmark Suggestion**: Measure the time it takes to perform a search with a known inefficient regex pattern.
- **Effort**: Medium
- **Risk**: Low

### Low

**4. `std::vector` growth without `reserve()`**
- **Location**: `FileIndex.cpp`
- **Issue Type**: Memory Allocation Patterns
- **Current Cost**: The `GetPathsView` function does not call `reserve()` on the output vector, which can lead to multiple reallocations if the number of paths is large.
- **Improvement**: Call `reserve()` on the output vector before filling it.
- **Benchmark Suggestion**: Measure the time it takes to call `GetPathsView` with a large number of IDs.
- **Effort**: Small
- **Risk**: Low

## Top 3 Bottlenecks
1.  **Lock contention in `FileIndex`**: This is the most significant bottleneck, as it can limit the scalability of the application.
2.  **String allocations in search**: Unnecessary string allocations in the search path can have a significant impact on performance.
3.  **Inefficient regex patterns**: The lack of protection against inefficient regex patterns is a potential denial of service vector.

## Scalability Assessment
The application should scale well to 10x more files, but lock contention in `FileIndex` may become a bottleneck. The use of a Structure of Arrays (SoA) layout for path storage is a key optimization that will help the application to scale.

## Quick Wins
- **Use `std::string_view` in more places to avoid string allocations**: This is a low-risk change that can have a significant impact on performance.
- **Call `reserve()` on vectors before filling them**: This is a simple change that can improve performance by avoiding unnecessary reallocations.
