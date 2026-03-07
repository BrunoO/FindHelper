# Performance Review - 2026-01-11

## Executive Summary
- **Health Score**: 5/10
- **Critical Issues**: 1
- **High Issues**: 1
- **Total Findings**: 2
- **Estimated Remediation Effort**: 4 hours

## Findings

### Critical
- **Redundant Pattern Matcher Creation in Search Loop**
  - **Location**: `src/search/ParallelSearchEngine.cpp`, `SearchAsync` method's worker lambda
  - **Issue Type**: Memory Allocation Patterns
  - **Current Cost**: Inside the `SearchAsync` worker lambda, `CreatePatternMatchers` is called for every chunk of the index. This means that for a search across 1 million files with 8 threads, the pattern matchers (which can be expensive to create, especially for regex) are created 8 times. This adds unnecessary overhead to the search hot path.
  - **Improvement**: Create the pattern matchers once before the search begins and pass them to the worker threads. This can be done by creating the matchers in `SearchAsync` and capturing them by value in the lambda.
  - **Benchmark Suggestion**: Measure the time taken by `SearchAsync` for a complex regex search before and after the change. The difference will be most noticeable on searches with many threads.
  - **Effort**: Small
  - **Risk**: Low

### High
- **Inefficient `ExtractFilenameAndExtension` in Search Loop**
  - **Location**: `src/search/ParallelSearchEngine.cpp`, `parallel_search_detail::ExtractFilenameAndExtension`
  - **Issue Type**: Data Structure Efficiency
  - **Current Cost**: The `ExtractFilenameAndExtension` function is called inside the `ProcessChunkRange` loop. This function performs string operations (`assign`) that can lead to memory allocations. In a tight loop that runs millions of times, these allocations can significantly degrade performance.
  - **Improvement**: Instead of extracting the filename and extension into new strings, the matching functions should be modified to work with `std-string_view` or raw `char*` pointers into the existing path buffer. This will avoid memory allocations in the search hot path.
  - **Benchmark Suggestion**: Profile the `ProcessChunkRange` function with a tool like Valgrind's Massif or Heaptrack to measure the number of allocations before and after the change.
  - **Effort**: Medium
  - **Risk**: Medium (requires careful handling of string views to avoid lifetime issues)

## Performance Score: 5/10
The application has a solid foundation for performance with its Structure of Arrays (SoA) data layout and parallel search architecture. However, there are critical performance issues in the search hot path that are likely causing significant slowdowns. The redundant creation of pattern matchers and inefficient string handling in the search loop are major bottlenecks that need to be addressed.

## Top 3 Bottlenecks
1.  **Pattern Matcher Creation**: The repeated creation of pattern matchers in the search loop is the most critical performance issue.
2.  **String Allocations in Search Loop**: The use of `std::string` for filename and extension extraction in the search loop is a major source of overhead.
3.  **Potential for Lock Contention**: While the use of `shared_lock` is good, a high volume of small updates to the index could still lead to contention.

## Scalability Assessment
The current architecture should scale reasonably well to 10x more files, thanks to the SoA design. However, the performance issues in the search loop will become more pronounced with a larger index. Addressing these bottlenecks is crucial for maintaining acceptable search performance at scale.

## Quick Wins
- **Move `CreatePatternMatchers` out of the search loop**: This is a low-effort, high-impact change that will provide an immediate performance boost.
