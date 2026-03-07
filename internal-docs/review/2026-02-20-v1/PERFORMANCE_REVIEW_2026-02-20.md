# Performance Review - 2026-02-20

## Executive Summary
- **Performance Score**: 9/10
- **Top 3 Bottlenecks**:
  1. **Regex Performance**: Use of `std::regex` is significantly slower than literal string search or simple glob matching.
  2. **Path Storage Rebuilds**: `RebuildPathBuffer` copies the entire path buffer, which can be expensive for millions of paths.
  3. **Frequent Resize in `AppendString`**: `PathStorage::AppendString` resizes the buffer for every new string, leading to many reallocations during initial indexing.

## Findings

### 1. Memory Allocation Patterns

**Frequent Resize in `AppendString`**
- **Location**: `src/path/PathStorage.cpp:241` (`AppendString`)
- **Issue Type**: Memory Allocation Patterns
- **Current Cost**: O(N) reallocations during initial index build. Each `resize()` call may trigger a `memcpy` of the entire buffer.
- **Improvement**: Use a growth strategy (e.g., doubling capacity) or pre-allocate based on estimated total path size.
- **Benchmark Suggestion**: Measure `StartIndexBuilding` time with 1M files.
- **Effort**: Small
- **Risk**: Low
- **Note (2026-02-21)**: Partially addressed. The "O(N) reallocations" wording was overstated: the constructor already reserves 64 MB for path storage and `std::vector` grows geometrically, so reallocations are O(log N). A reserve-before-append fix was applied in **RebuildPathBuffer()** only: we now compute total path bytes and reserve `path_storage_` and the other vectors once before the append loop, eliminating reallocations during rebuild. Initial index build (InsertPath one-by-one) was left unchanged as total size is unknown up front.

### 2. Lock Contention

**Coarse Locking in `UsnMonitor`**
- **Location**: `src/usn/UsnMonitor.cpp`
- **Issue Type**: Lock Contention
- **Current Cost**: Potential UI stalls during heavy USN journal activity.
- **Improvement**: Use `std::shared_mutex` to allow multiple readers (UI thread, search threads) to access the index simultaneously while USN monitor is idle.
- **Benchmark Suggestion**: Measure UI frame time during heavy background file operations (e.g., `npm install`).
- **Effort**: Medium
- **Risk**: Medium (requires careful auditing of all lock sites)

### 3. Algorithm Complexity

**Linear Scan in `UpdatePrefix`**
- **Location**: `src/path/PathStorage.cpp:81` (`UpdatePrefix`)
- **Issue Type**: Time Complexity
- **Current Cost**: O(N) where N is the total number of paths in the index. This happens on every directory rename.
- **Improvement**: If directory renames are frequent, consider a tree-based path storage or a prefix-indexed structure. For a file indexer, O(N) is often acceptable as renames are relatively rare compared to searches.
- **Benchmark Suggestion**: Measure time to rename a top-level directory with 1M sub-items.
- **Effort**: Large
- **Risk**: High

### 4. SIMD & Vectorization

**AVX2 optimized Search**
- **Location**: `src/utils/StringSearchAVX2.cpp`
- **Status**: Excellent use of SIMD for hot path.
- **Improvement**: Consider AVX-512 for newer CPUs, though AVX2 is the current high-performance baseline.

## Summary

- **Performance Score**: 9/10. The application is highly optimized for its target use case. The use of SoA (Structure of Arrays) in `PathStorage` and AVX2-accelerated string searching are best-in-class optimizations.
- **Top 3 Bottlenecks**:
  1. `std::regex` overhead for complex searches.
  2. ~~Sequential reallocations in `PathStorage::AppendString`~~ (mitigated: reserve in `RebuildPathBuffer`; initial build still uses vector growth).
  3. Coarse-grained locking in `UsnMonitor`.
- **Scalability Assessment**: The SoA layout and parallel search engine are designed to scale to millions of files. The bottleneck for 10M+ files will likely be the initial indexing speed and memory usage of the path pool.
- **Quick Wins**:
  - ~~Implement a better growth strategy in `PathStorage::AppendString`~~ (done for `RebuildPathBuffer`: reserve once before append loop).
  - Switch `UsnMonitor` to `std::shared_mutex`.
