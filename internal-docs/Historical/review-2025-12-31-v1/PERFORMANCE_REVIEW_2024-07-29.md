# Performance Review - 2024-07-29

## Executive Summary
- **Health Score**: 6/10
- **Top Bottlenecks**: 2
- **Quick Wins**: 1
- **Overall Assessment**: The application's core data structure for searching (`PathStorage`) is exceptionally well-designed for read performance, utilizing a Structure of Arrays (SoA) layout for optimal cache efficiency. However, the performance of write and update operations has been neglected. Operations like renaming a directory or rebuilding the storage buffer are highly inefficient and will cause significant pauses in the application, degrading the user experience.

## Findings

### High
**1. Inefficient `UpdatePrefix` Implementation**
- **Location**: `PathStorage.cpp`, `UpdatePrefix`
- **Issue Type**: Memory Allocation Patterns / Algorithm Complexity
- **Current Cost**: Renaming a directory with a large number of descendants will cause a massive number of memory allocations (one `UpdateInfo` struct and one `std::string` per descendant) and will be very slow due to the linear scan of the entire index. This will likely cause the UI to freeze for several seconds.
- **Improvement**: Instead of rebuilding the strings, the `path_storage_` buffer should be modified in-place. A more advanced approach would be to implement a gap buffer or a similar data structure that allows for efficient insertions and deletions. A simpler, but still much better, approach would be to build a new `path_storage_` buffer directly, without the intermediate `UpdateInfo` vector.
- **Benchmark Suggestion**: Measure the time it takes to rename a directory containing 10,000 files before and after the optimization.
- **Effort**: Large
- **Risk**: High. Modifying the buffer in-place is complex and could lead to data corruption if not implemented carefully.

**2. High Memory Overhead in `RebuildPathBuffer`**
- **Location**: `PathStorage.cpp`, `RebuildPathBuffer`
- **Issue Type**: Memory Allocation Patterns
- **Current Cost**: The current implementation creates a temporary `std::vector<Entry>`, and each `Entry` contains a `std::string`. This means that for a brief period, the application holds two copies of the entire path store in memory. For a large index, this could double the memory usage of the application and lead to crashes on systems with less RAM.
- **Improvement**: The rebuild process should be done in-place or with a more memory-efficient approach. For example, a new set of `std::vector`s could be created, and the data from the old vectors could be moved into them one by one, after which the old vectors are cleared. This would avoid the peak memory usage of duplicating the entire `path_storage_` string buffer.
- **Benchmark Suggestion**: Measure the peak memory usage of the application during a `RebuildPathBuffer` operation before and after the optimization.
- **Effort**: Medium
- **Risk**: Medium.

### Medium
**1. Unnecessary String Allocation in `ParsePathOffsets`**
- **Location**: `PathStorage.cpp`, `ParsePathOffsets`
  ```cpp
  std::string filename = path.substr(filename_start);
  size_t last_dot = filename.find_last_of('.');
  ```
- **Issue Type**: Memory Allocation Patterns
- **Current Cost**: A small but unnecessary string allocation is performed for every file that is inserted into the index. For an index with millions of files, this adds up to millions of small allocations, which can be a significant overhead.
- **Improvement**: The `find_last_of` call can be performed on the original `path` string with an offset, avoiding the need to create a substring.
  ```cpp
  size_t last_dot = path.find_last_of('.', std::string::npos, filename_start);
  ```
- **Benchmark Suggestion**: While hard to measure in isolation, this change would reduce the overall time and memory usage of the initial indexing process.
- **Effort**: Small (Quick Win)
- **Risk**: Low.

## Summary
- **Performance Score**: 6/10. Read performance is excellent, but write/update performance is poor.
- **Top 3 Bottlenecks**:
  1. `UpdatePrefix` for directory renames.
  2. `RebuildPathBuffer` for maintenance.
  3. (Potentially) The initial indexing process due to the accumulation of small inefficiencies like the one in `ParsePathOffsets`.
- **Scalability Assessment**: The application will scale well for searching, but will become increasingly slow and unresponsive as the number of file system changes (renames, deletes) increases.
- **Quick Wins**: Fixing the unnecessary string allocation in `ParsePathOffsets` is a quick and easy win.
- **Benchmark Recommendations**:
  - Time to rename a large directory.
  - Peak memory usage during `RebuildPathBuffer`.
  - Total time for initial indexing of a large directory.
