# Performance Review - 2026-01-31

## Executive Summary
- **Health Score**: 8/10
- **Critical Issues**: 0
- **High Issues**: 1
- **Total Findings**: 6
- **Estimated Remediation Effort**: 10 hours

## Findings

### High

#### 1. File I/O in Search Hot Path (LoadSettings)
- **Location**: `src/search/ParallelSearchEngine.cpp` - `SearchAsyncWithData`
- **Issue Type**: I/O & System Call Overhead
- **Current Cost**: Every search operation (triggered on every keystroke during instant search) calls `LoadSettings`, which opens and parses `settings.json` from disk. This adds significant latency and disk I/O, especially on slower drives.
- **Improvement**: Cache settings in `GuiState` or `Application` and pass a const reference to the search engine.
- **Benchmark Suggestion**: Measure search latency with and without `LoadSettings` call using `search_benchmark`.
- **Effort**: Small
- **Risk**: Low

### Medium

#### 2. Potential Contention on Long-running ForEachEntry
- **Location**: `src/index/FileIndex.h` - `ForEachEntry`
- **Issue Type**: Lock Contention & Synchronization
- **Current Cost**: `ForEachEntry` holds a `shared_lock` on the entire index while iterating over millions of entries. While readers don't block each other, they *do* block writers (USN monitor updates). A full scan can take hundreds of milliseconds, delaying real-time updates.
- **Improvement**: Process in chunks, releasing and re-acquiring the lock, or use a lock-free snapshot approach.
- **Benchmark Suggestion**: Measure USN update latency while a "Show All" search is active.
- **Effort**: Medium
- **Risk**: Medium (requires careful handling of index modifications during gaps)

#### 3. Redundant Text Width Calculations for Path Truncation
- **Location**: `src/ui/ResultsTable.cpp` - `TruncatePathAtBeginning`
- **Issue Type**: Algorithm Complexity
- **Current Cost**: For every frame, for every visible row, the table recalculates path truncation by repeatedly calling `ImGui::CalcTextSize`.
- **Improvement**: Cache the truncated path string in the `SearchResult` object when results are received or sorted.
- **Benchmark Suggestion**: Measure UI frame time with 1000+ visible rows (using a large monitor/small font).
- **Effort**: Medium
- **Risk**: Low

#### 4. Default Container Choice (std::unordered_map)
- **Location**: `src/index/FileIndexStorage.h`
- **Issue Type**: Container Choice
- **Current Cost**: Standard `std::unordered_map` is generally slower than "flat" alternatives or `boost::unordered_map` due to node-based storage and pointer chasing.
- **Improvement**: Enable `FAST_LIBS_BOOST` by default or switch to a faster hash map implementation like `tsl::hopscotch_map` or `robin-hood-hashing`.
- **Benchmark Suggestion**: Compare index build time with and without `FAST_LIBS_BOOST`.
- **Effort**: Small
- **Risk**: Low (already supported via CMake flag)

### Low

#### 5. Unnecessary string conversions in FileIndexStorage::InsertLocked
- **Location**: `src/index/FileIndexStorage.cpp:49`
- **Issue Type**: Memory Allocation Patterns
- **Current Cost**: `std::string(name)` creates a temporary string even if the entry already exists in the map (though `try_emplace` is used, the argument is constructed).
- **Improvement**: Use `find` first or use a transparent lookup if the map supports it (C++20 feature, but can be emulated or use Boost).
- **Benchmark Suggestion**: Measure memory allocation count during initial crawl.
- **Effort**: Small
- **Risk**: Low

#### 6. ASCII-only AVX2 Fast Path
- **Location**: `src/utils/StringSearch.h` - `TryAVX2Path`
- **Issue Type**: Missed Vectorization
- **Current Cost**: The AVX2 optimization is skipped for any string containing non-ASCII characters. While safe, it misses performance gains for internationalized filenames.
- **Improvement**: Implement a UTF-8 aware or raw-byte AVX2 search that handles non-ASCII characters correctly.
- **Benchmark Suggestion**: Compare search speed on a dataset with many non-ASCII filenames.
- **Effort**: Large
- **Risk**: Medium

## Summary Requirements

- **Performance Score**: 8/10. The application is generally very fast due to SoA layout and SIMD optimizations.
- **Top 3 Bottlenecks**:
  1. Disk I/O in search path (`LoadSettings`).
  2. Write-blocking during full index scans.
  3. UI-thread overhead from repeated text width calculations.
- **Scalability Assessment**: The SoA layout is highly scalable. The main limit will be RAM for the `FileIndex` and lock contention between the USN monitor and UI searches as item count exceeds 10M+.
- **Quick Wins**:
  1. Cache settings to avoid disk I/O in search (High impact/Low effort).
  2. Cache truncated paths in the result view (Medium impact/Medium effort).
  3. Enable Boost containers by default for production builds (Medium impact/Low effort).
- **Benchmark Recommendations**:
  1. `search_benchmark` should be part of the CI to detect regressions in matching speed.
  2. Add a "contention benchmark" that simulates high USN activity during active searches.
