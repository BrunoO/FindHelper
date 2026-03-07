# Performance Review - 2026-03-06

## Executive Summary
- **Health Score**: 9/10
- **Top 3 Bottlenecks**:
  1. **std::string Allocation in Path Interning** (Category 1/2)
  2. **Wait Latency in Search Thread Pool** (Category 3)
  3. **Repeated std::regex Construction** (Category 6/7)
- **Total Findings**: 5
- **Estimated Remediation Effort**: 16 hours

## Findings

### Critical
None.

### High
1. **Unnecessary std::string Allocations in Interning** (Category 1)
   - **Location**: `src/index/FileIndexStorage.cpp::Intern`
   - **Issue Type**: Memory Allocation Patterns.
   - **Current Cost**: Significant allocation overhead during initial index build (millions of strings).
   - **Improvement**: Use a pool-based allocator or `std::string_view` with a custom hash map to intern strings without creating temporary `std::string` objects for lookups.
   - **Benchmark Suggestion**: Measure `FileIndex::Insert` time with 1M entries.
   - **Effort**: Medium (4 hours)
   - **Risk**: Low - interning is a pure performance optimization.

### Medium
1. **Lock Contention in StreamingResultsCollector** (Category 3)
   - **Location**: `src/search/StreamingResultsCollector.cpp`
   - **Issue Type**: Lock Contention & Synchronization.
   - **Current Cost**: Multiple threads competing for the same mutex when pushing results.
   - **Improvement**: Use per-thread local buffers and merge them periodically or at the end of the search phase to reduce contention.
   - **Benchmark Suggestion**: Measure search time with 16+ threads on a high-match-count query.
   - **Effort**: Medium (2 hours)
   - **Risk**: Medium - concurrency bugs during merge.

2. **Regex Re-compilation Overhead** (Category 7)
   - **Location**: `src/utils/StdRegexUtils.cpp`
   - **Issue Type**: Algorithm Complexity / System Call Overhead.
   - **Current Cost**: `std::regex` construction is expensive and happens for every search where regex is used.
   - **Improvement**: Cache compiled regex objects if the same pattern is used repeatedly in search workers.
   - **Benchmark Suggestion**: Measure regex search time for 100 consecutive searches with same pattern.
   - **Effort**: Small (2 hours)
   - **Risk**: Low.

### Low
1. **Pass-by-Value in SearchContext** (Category 2)
   - **Location**: `src/search/SearchContext.h`
   - **Issue Type**: Data Structure Efficiency.
   - **Current Cost**: Small copy overhead for search parameters.
   - **Improvement**: Pass `SearchContext` by `const&` or use `std::string_view` for members like `filename_query`.
   - **Benchmark Suggestion**: Micro-benchmark `ParallelSearchEngine::SearchAsync`.
   - **Effort**: Small (1 hour)
   - **Risk**: Very Low.

2. **Missing Reserve in Parallel Search Workers** (Category 1)
   - **Location**: `src/search/SearchWorker.cpp`
   - **Issue Type**: Memory Allocation Patterns.
   - **Current Cost**: Result vector re-allocations.
   - **Improvement**: Use a heuristic or average result count to `reserve()` space in the result vector before starting the search.
   - **Benchmark Suggestion**: Measure peak memory and time during search.
   - **Effort**: Small (1 hour)
   - **Risk**: Very Low.

## Performance Score: 9/10
The application is highly optimized with SIMD (AVX2) and efficient parallel search strategies. The core data structures (SoA) are cache-friendly and minimize fragmentation. Further gains are likely in the area of micro-optimizations (allocations, locking) rather than architectural changes.

## Scalability Assessment
The architecture is well-suited for scaling to 10M+ files. The use of SoA for `PathStorage` ensures memory access remains predictable even as the dataset grows beyond cache limits.

## Quick Wins
- Add `reserve()` to result vectors in `SearchWorker`.
- Convert `last_save_message` in `SettingsWindow` to `std::string_view`.
- Implement basic caching for `std::regex` objects.

## Benchmark Recommendations
- Run `search_benchmark` on various thread counts (1-32) to evaluate scalability.
- Profile `FileIndex::Insert` to confirm allocation hotspots in interning.
