# Performance Review - 2026-02-14

## Executive Summary
- **Performance Score**: 8/10
- **Top Bottlenecks**: 2
- **Quick Wins**: 3
- **Effort**: Medium

## Findings

### High
- **String Allocation Hotspot in `MergeAndConvertToSearchResults`**:
  - **Location**: `src/search/SearchWorker.cpp`
  - **Issue Type**: Memory Allocation Patterns
  - **Current Cost**: Significant CPU time spent copying `std::string` from `SearchResultData` into a `vector<char>` pool and then into `SearchResult`.
  - **Improvement**: If `SearchResultData` could use a shared string pool or `string_view` earlier in the pipeline, these copies could be minimized.
  - **Effort**: L

- **Lock Contention on `mutex_` in `SearchWorker`**:
  - **Location**: `src/search/SearchWorker.cpp`
  - **Issue Type**: Lock Contention
  - **Current Cost**: Multiple UI-triggered calls (`IsSearching`, `IsBusy`, `HasNewResults`) compete for a single mutex with the background worker thread.
  - **Improvement**: Use `std::atomic<bool>` for state flags where possible to avoid locking for simple status checks.
  - **Effort**: S

### Medium
- **Unnecessary `std::string` formatting in `FormatBytes`**:
  - **Location**: `src/search/SearchWorker.cpp`
  - **Issue Type**: Allocation in Hot Paths
  - **Current Cost**: `std::to_string` and string concatenation create temporary strings every time a result is logged or formatted for display.
  - **Improvement**: Use a pre-allocated buffer or `fmt` library (if available) or `std::to_chars` for faster conversion.
  - **Effort**: S

### Low
- **Pass-by-value of `std::string` in `SetLastErrorMessage`**:
  - **Location**: `src/core/IndexBuildState.h`
  - **Issue Type**: Memory & Performance Debt
  - **Improvement**: Use `std::string_view` or move semantics.
  - **Effort**: S

## Summary Requirements
- **Performance Score**: 8/10 - The application is highly optimized for search (AVX2, parallel chunking), but the "materialization" phase (converting raw hits to UI results) has some allocation overhead.
- **Top 3 Bottlenecks**:
  1. String pool copies in `MergeAndConvertToSearchResults`.
  2. Mutex contention in `SearchWorker`.
  3. `std::regex` overhead in `rs:` searches (already noted as a security risk).
- **Scalability Assessment**: Excellent. The hybrid load-balancing strategy handles millions of files well. Memory usage is the primary limit.
- **Quick Wins**:
  - Convert `SearchWorker` status flags to atomics.
  - Use `reserve()` more aggressively in `MergeAndConvertToSearchResults`.
