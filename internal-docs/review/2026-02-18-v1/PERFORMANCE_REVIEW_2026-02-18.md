# Performance Review - 2026-02-18

## Executive Summary
- **Performance Score**: 9/10
- **Top 3 Bottlenecks**:
  1. `std::regex` execution in complex queries.
  2. Unnecessary string copies in `MergeAndConvertToSearchResults`.
  3. UI thread work in `ResultsTable` for path truncation.
- **Total Findings**: 8
- **Estimated Remediation Effort**: 12 hours

## Findings

### High
1. **`std::regex` Performance & ReDoS Risk**
   - **Location**: `src/utils/StdRegexUtils.h`
   - **Issue Type**: Algorithm Complexity / CPU Exhaustion
   - **Current Cost**: Significant CPU usage for complex regex patterns; potential for exponential time complexity on pathological input.
   - **Improvement**: Replace `std::regex` with **RE2** for guaranteed linear-time matching.
   - **Benchmark Suggestion**: Measure search time for `(a+)+$` against a string of 'a's.
   - **Effort**: Medium (4 hours)
   - **Risk**: Low (RE2 is highly compatible).

### Medium
1. **Unnecessary String Copies in Search Post-Processing**
   - **Location**: `src/search/SearchWorker.cpp:241` (`MergeAndConvertToSearchResults`)
   - **Issue Type**: Memory Allocation Patterns
   - **Current Cost**: Each search result's path is copied from the SoA buffer into a local `pool` and then used in a `std::string_view`. While this avoids individual allocations per `SearchResult`, it still involves a large `memcpy`.
   - **Improvement**: If possible, keep the results as views into the `PathStorage` SoA buffer for as long as possible, or use a more efficient multi-buffer pool.
   - **Benchmark Suggestion**: Measure time for `MergeAndConvertToSearchResults` with 100k+ results.
   - **Effort**: Medium (4 hours)
   - **Risk**: Medium (Requires careful lifetime management).

2. **UI Thread Bottleneck: Path Truncation**
   - **Location**: `src/ui/ResultsTable.cpp:1120` (`TruncatePathAtBeginning`)
   - **Issue Type**: I/O & System Call Overhead (GUI context)
   - **Current Cost**: `TruncatePathAtBeginning` is called for every visible row in every frame. It performs string manipulations and potentially font width calculations.
   - **Improvement**: Cache truncated paths or perform truncation only when the column width changes.
   - **Benchmark Suggestion**: Monitor UI frame time with a very long path list and resizing the window.
   - **Effort**: Small (2 hours)
   - **Risk**: Low.

### Low
1. **Missing `reserve()` in vector growth**
   - **Location**: Multiple (e.g., `src/search/SearchResultUtils.cpp:915`)
   - **Issue Type**: Memory Allocation Patterns
   - **Current Cost**: Minor reallocations during result gathering.
   - **Improvement**: Use `.reserve()` when the number of candidates is known.
   - **Effort**: Small (1 hour)

## Performance Score: 9/10
The application is highly optimized for its core task. The use of **Structure of Arrays (SoA)**, **AVX2 SIMD**, and **Parallel Search** shows a deep understanding of modern CPU architecture. The contiguous memory layout of `PathStorage` ensures excellent cache hit rates.

## Top 3 Bottlenecks
1. **Regex Matching**: `std::regex` is the slowest part of the search pipeline.
2. **Metadata Loading**: Lazy loading from disk is slow (inherently, but could be batched more aggressively).
3. **UI Rendering**: Recalculating layout/truncation every frame.

## Scalability Assessment
The architecture should easily handle 10x more files (e.g., 10M+) as long as RAM is sufficient. The O(N) search remains efficient due to SIMD and parallelism. Memory usage will scale linearly with the number of files (~100-150 bytes per file).

## Quick Wins
1. **Cache truncated paths** in `ResultsTable`.
2. **Add `reserve()`** to common vector growth locations.
3. **Optimize `ComputePathOffsets`** by avoiding `find_last_of` if the offset is already known from SoA.

## Benchmark Recommendations
- **Search Latency**: Baseline search vs. Regex search vs. AVX2 search.
- **Index Build Time**: Time to crawl and index 1M files.
- **Memory Footprint**: Bytes per indexed file at 1M files.
