# Performance Review - 2026-02-09

## Executive Summary
- **Performance Score**: 8/10
- **Top 3 Bottlenecks**:
  1. `std::string` allocations in `ConvertSearchResultData`
  2. Sequential `LoadSettings` on search hot path
  3. `std::regex` backtracking for complex patterns
- **Total Findings**: 12
- **Estimated Remediation Effort**: 32 hours

## Findings

### High
- **Location**: `src/search/SearchWorker.cpp` - `ConvertSearchResultData`
- **Issue Type**: Memory Allocation Patterns
- **Current Cost**: Significant overhead during result materialization for large result sets (>10,000 items). Each result requires several `std::string` allocations (path, displays).
- **Improvement**: Use a string pool or `string_view` where possible for result display. Pre-allocate/reserve results vector.
- **Benchmark Suggestion**: Measure search time for a query returning 100k+ results.
- **Effort**: M
- **Risk**: Low

- **Location**: `src/core/Application.cpp` - `LoadSettings`
- **Issue Type**: I/O & System Call Overhead
- **Current Cost**: A previous review identified `LoadSettings` being called in the search hot path or frequently during UI updates, causing disk I/O bottlenecks.
- **Improvement**: Cache settings in memory and only reload on explicit change or periodically in the background.
- **Benchmark Suggestion**: Profile UI responsiveness during intensive searching.
- **Effort**: S
- **Risk**: Low

### Medium
- **Location**: `src/utils/StringSearchAVX2.h`
- **Issue Type**: SIMD & Vectorization
- **Current Cost**: Current implementation is optimized for AVX2, but lacks AVX-512 paths for newer CPUs.
- **Improvement**: Implement AVX-512 substring search for even higher throughput on supported hardware.
- **Benchmark Suggestion**: Run `SearchBenchmark` on an AVX-512 capable machine.
- **Effort**: L
- **Risk**: Medium

- **Location**: `src/search/ParallelSearchEngine.cpp`
- **Issue Type**: Lock Contention & Synchronization
- **Current Cost**: While `shared_mutex` is used, the granularity could be improved.
- **Improvement**: Ensure `items_checked & 127` pattern for cancellation checks is used consistently to avoid excessive atomic loads.
- **Benchmark Suggestion**: Scale test with 32+ cores.
- **Effort**: S
- **Risk**: Low

### Low
- **Location**: `src/filters/SizeFilterUtils.cpp`
- **Issue Type**: Cache Efficiency
- **Current Cost**: Minor overhead in size formatting.
- **Improvement**: Use a lookup table or faster branchless logic for size unit conversions.
- **Benchmark Suggestion**: Micro-benchmark `FormatFileSize`.
- **Effort**: S
- **Risk**: Very Low

## Summary Requirements

- **Performance Score**: 8/10. The use of SoA (Structure of Arrays) and AVX2 demonstrates a high level of performance awareness. The bottlenecks are mainly at the boundaries (materializing results for UI, loading settings).
- **Top 3 Bottlenecks**:
  1. Result set materialization (string allocations).
  2. Excessive settings I/O.
  3. Non-linear regex engine for complex queries.
- **Scalability Assessment**: The architecture will handle 10x more files (up to ~10M) well due to SoA and parallel processing, but UI result list virtualization and materialization will become the primary bottleneck.
- **Quick Wins**:
  1. Cache `AppSettings` to avoid redundant I/O.
  2. Reserve capacity in `SearchResult` vectors.
  3. Hoist `HasExtensionFilter()` checks in search loops.
- **Benchmark Recommendations**:
  1. `SearchBenchmark` with varying thread pool sizes.
  2. `PathPatternBenchmark` with complex regex vs simple patterns.
