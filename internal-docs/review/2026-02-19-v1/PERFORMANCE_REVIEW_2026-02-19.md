# Performance Review - 2026-02-19

## Executive Summary
- **Health Score**: 9/10
- **Critical Issues**: 0
- **High Issues**: 0
- **Total Findings**: 8
- **Estimated Remediation Effort**: 24 hours

## Findings

### Critical
*None identified.*

### High
*None identified. The core search engine is highly optimized.*

### Medium
1. **Unnecessary String Allocations in Folder Stats**
   - **Location**: `src/ui/ResultsTable.cpp:BuildFolderStatsIfNeeded`
   - **Issue Type**: Memory Allocation Patterns: Allocation in Hot Paths
   - **Current Cost**: O(N) string allocations (where N is the number of results) every time filters or search results change.
   - **Improvement**: Use `std::string_view` as a key in a specialized hash map if possible, or use a more efficient interning strategy for folder paths.
   - **Benchmark Suggestion**: Measure UI frame time when toggling "Show Folder Stats" on a 100k result set.
   - **Effort**: Medium
   - **Risk**: Low

2. **Lock Contention on FileIndex Mutex**
   - **Location**: `src/index/FileIndex.h`
   - **Issue Type**: Lock Contention & Synchronization: Granularity Issues
   - **Current Cost**: Write operations (USN updates) block all search readers.
   - **Improvement**: Use a finer-grained locking scheme or a read-copy-update (RCU) like approach for the index.
   - **Benchmark Suggestion**: Run search benchmark while simulating heavy USN Journal activity.
   - **Effort**: Large
   - **Risk**: High (Complex synchronization)

### Low
1. **Potential False Sharing in SoA**
   - **Location**: `src/path/PathStorage.h`
   - **Issue Type**: Data Structure Efficiency: Layout Optimization
   - **Current Cost**: Possible cache line bouncing if adjacent entries in `is_deleted` or `is_directory` are modified frequently.
   - **Improvement**: Pad or align arrays if write frequency becomes high.
   - **Benchmark Suggestion**: Cache miss profiling during heavy USN monitoring.
   - **Effort**: Small
   - **Risk**: Low

2. **Regex Performance (std::regex)**
   - **Location**: `src/utils/StdRegexUtils.h`
   - **Issue Type**: Algorithm Complexity: Time Complexity
   - **Current Cost**: `std::regex` is significantly slower than alternatives like `boost::regex` or `RE2`.
   - **Improvement**: Transition to `boost::regex` (already supported via flag) or a faster regex library by default.
   - **Benchmark Suggestion**: Compare search times for complex regex patterns.
   - **Effort**: Medium
   - **Risk**: Low

## Summary Requirements

- **Performance Score**: 9/10. The application is exceptionally well-optimized for its primary task (searching millions of files).
- **Top 3 Bottlenecks**:
  1. `std::regex` execution for complex patterns.
  2. Synchronous file metadata loading for non-cloud files (though partially mitigated by async sort).
  3. String allocations during folder statistics aggregation.
- **Scalability Assessment**: The SoA design is ready for 10x more files (e.g., 10M-20M), but lock contention on the central index will become a more significant bottleneck.
- **Quick Wins**:
  1. Standardize on a faster regex engine.
  2. Optimize folder statistics aggregation to reduce string allocations.
- **Benchmark Recommendations**:
  1. 10M item search benchmark.
  2. Search latency during 1000 USN records/second burst.
