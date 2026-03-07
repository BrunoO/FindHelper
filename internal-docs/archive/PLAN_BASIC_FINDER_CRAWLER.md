# Plan: Folder Crawler for Initial Index Population

## Reformulated Requirements

### Purpose
Create a **file system crawler** that recursively enumerates all files and directories within a given root folder. This crawler will be used as an alternative to the USN Journal-based initial population in two scenarios:

1. **Windows (Non-Admin Users)**: When users don't have administrator privileges, they cannot access the USN Journal via `FSCTL_ENUM_USN_DATA`. The crawler provides a fallback mechanism to populate the initial index.

2. **macOS**: macOS doesn't have a USN Journal equivalent. The crawler is the primary (and only) method for initial index population on macOS.

### Performance Requirements
- **Reasonably fast**: Should complete initial indexing in a reasonable time (target: similar to or better than current USN-based enumeration for typical volumes)
- **Multi-threaded**: Should leverage multiple CPU cores to parallelize directory traversal and file discovery
- **Non-blocking**: Should run in a background thread to keep the UI responsive

### Integration Point
- The crawler should populate the `FileIndex` using the same `Insert()` or `insert_path()` methods used by the existing `InitialIndexPopulator`
- Should follow the same pattern: populate index, then call `FinalizeInitialPopulation()` to ensure all paths are correctly computed
- Should provide progress updates via an atomic counter (similar to `indexed_file_count` in `InitialIndexPopulator`)

---

## Decisions Made ✅

### 1. Scope and Platform-Specific Behavior

**✅ Q1.1**: **Single cross-platform implementation** with platform-specific directory enumeration code
- One main class (`FolderCrawler`) with platform-specific helper functions
- Conditional compilation for Windows/macOS specific code

**✅ Q1.2**: **Used automatically on macOS when no index file is provided**
- If `--index-from-file` is provided, use file loading (existing behavior)
- Otherwise, show folder picker dialog and use FolderCrawler

**✅ Q1.3**: **Automatically fall back on Windows when admin rights not available**
- If admin rights available: use USN Journal (existing behavior)
- If admin rights NOT available: show folder picker dialog and use FolderCrawler

### 2. Threading Architecture

**✅ Q2.1**: **Work-stealing queue pattern**
- Multiple worker threads process directories from a shared queue
- When a thread discovers subdirectories, it adds them to the queue
- Natural load balancing as threads grab work from the queue
- Note: WinDirStat is single-threaded, so we're implementing a modern multi-threaded approach

**✅ Q2.2**: **Configurable, default to `hardware_concurrency()`**
- Default: Use all available CPU cores
- Configurable via `FolderCrawler::Config` struct
- Can be tuned based on I/O vs CPU bound workload

**✅ Q2.3**: **Coupled with batch insertion**
- Each thread directly calls `FileIndex::insert_path()` but batches calls
- Collect paths in a vector, flush periodically (every 1000 paths) to reduce lock contention
- Simpler than decoupled approach, and batching reduces lock contention significantly

### 3. File Index Integration

**✅ Q3.1**: **Use `FileIndex::insert_path(const std::string& full_path)`**
- Same as file-based loading, keeps consistency
- Simpler than tracking parent-child relationships
- `insert_path` handles ID generation internally using `next_file_id_`

**✅ Q3.2**: **Use incrementing counter (handled by `insert_path`)**
- `insert_path` uses `next_file_id_.fetch_add(1)` internally
- No need to generate IDs manually

**✅ Q3.3**: **Batch insertions**
- Collect paths in a vector (default batch size: 1000)
- Flush batch periodically to reduce lock contention on `FileIndex::insert_path()`
- Simple to implement and provides significant performance benefit

### 4. Error Handling and Edge Cases

**✅ Q4.1**: **Skip and log approach**
- **Permission denied**: Skip the directory/file, log warning, continue
- **Symbolic links**: Skip them (documented in code for future changes)
- **Junction points (Windows)**: Skip them (same as symlinks)
- **Circular structures**: Rely on OS APIs (they prevent infinite loops)

**✅ Q4.2**: **Continue on errors**
- Skip problematic files/directories and log warnings
- Track error count for diagnostics
- Continue processing even if individual items fail

### 5. Progress Reporting and UI Integration

**✅ Q5.1**: **Atomic counter (same as `InitialIndexPopulator`)**
- Simple and efficient
- Update `indexed_file_count` atomic counter
- Additional statistics available via `GetFilesProcessed()`, `GetDirectoriesProcessed()`, `GetErrorCount()`

**✅ Q5.2**: **Every 10,000 files (same as `InitialIndexPopulator`)**
- Consistent with existing progress reporting
- Configurable via `Config::progress_update_interval`

**✅ Q5.3**: **Yes, support cancellation**
- Atomic flag that threads check periodically
- Allows user to cancel long-running crawls

### 6. Performance Optimizations

**✅ Q6.1**: **Use fast OS-specific APIs, but benchmark both**
- **Windows**: `FindFirstFileEx` with `FIND_FIRST_EX_LARGE_FETCH` flag
- **macOS**: `readdir` / `opendir` (standard POSIX, fast on macOS)
- **Assessment**: Benchmark fast API vs standard C++ filesystem for ~1M files
- **Documentation**: Document findings and rationale

**✅ Q6.2**: **Lazy-load metadata (consistent with current approach)**
- Don't collect file size/modification time during crawl
- Consistent with USN Journal approach (lazy loading)
- **Documentation**: Document how to add metadata collection if needed in future

**✅ Q6.3**: **No filtering during enumeration**
- Index everything, let search filters handle it
- Simpler implementation, more flexible

### 7. Integration with Existing Code

**✅ Q7.1**: **Call from `AppBootstrap` based on platform/privileges**
- **Windows**: In `AppBootstrap.cpp`, if not elevated, show folder picker and use FolderCrawler
- **macOS**: In `AppBootstrap_mac.mm`, if no index file provided, show folder picker and use FolderCrawler
- Keep USN monitoring separate (only used when admin rights available on Windows)

**✅ Q7.2**: **Similar interface but with config struct**
- `FolderCrawler::Crawl(root_path, indexed_file_count, cancel_flag)`
- Configurable via `FolderCrawler::Config` struct (thread count, batch size, etc.)
- More flexible than `PopulateInitialIndex()` signature

---

## Proposed Architecture

### High-Level Design

```
FolderCrawler
├── Platform-specific directory enumeration
│   ├── Windows: FindFirstFileEx / FindNextFile
│   └── macOS: readdir / opendir
├── Multi-threaded work queue
│   ├── Work-stealing queue for directories
│   └── Thread pool (configurable size)
├── FileIndex integration
│   ├── Batch insertion (reduce lock contention)
│   └── Progress reporting via atomic counter
└── Error handling
    ├── Skip problematic files/directories
    └── Log warnings for debugging
```

### Key Components

1. **DirectoryEnumerator**: Platform-specific wrapper for directory traversal
   - Windows: Uses `FindFirstFileEx` with `FIND_FIRST_EX_LARGE_FETCH` for performance
   - macOS: Uses `readdir` with `opendir`

2. **WorkQueue**: Thread-safe queue for directory paths
   - Multiple producer threads (discover subdirectories)
   - Multiple consumer threads (process directories)

3. **CrawlerThreadPool**: Manages worker threads
   - Configurable thread count
   - Work-stealing for load balancing

4. **FileIndexBatcher**: Collects file entries and inserts in batches
   - Reduces lock contention on `FileIndex::Insert()`
   - Flushes periodically or when batch reaches size threshold

### Performance Considerations

- **I/O Bound**: Directory enumeration is I/O bound, so multiple threads help by keeping multiple I/O operations in flight
- **Lock Contention**: Batch insertion reduces contention on `FileIndex` mutex
- **Memory**: Work queue should have a reasonable size limit to prevent unbounded growth

---

## Implementation Phases

### Phase 1: Single-threaded prototype
- Basic directory traversal (one platform first)
- Simple file discovery and insertion
- Verify correctness and basic performance

### Phase 2: Multi-threaded implementation
- Add thread pool and work queue
- Implement work-stealing or depth-based partitioning
- Measure performance improvements

### Phase 3: Cross-platform support
- Implement platform-specific directory enumeration
- Handle platform-specific edge cases (symlinks, permissions, etc.)

### Phase 4: Integration
- Integrate with existing `UsnMonitor` or `AppBootstrap`
- Add progress reporting and cancellation support
- Add configuration options

### Phase 5: Optimization
- Profile and optimize hot paths
- Tune thread count and batch sizes
- Add platform-specific optimizations

---

## Summary of Decisions ✅

All clarification questions have been answered. Key decisions:

1. **Architecture**: Single cross-platform implementation with platform-specific directory enumeration
2. **Threading**: Work-stealing queue pattern with configurable thread count (default: `hardware_concurrency()`)
3. **Integration**: Called from `AppBootstrap` when USN Journal unavailable (Windows non-admin) or no index file (macOS)
4. **FileIndex**: Use `insert_path()` with batch insertion (1000 paths per batch)
5. **Error Handling**: Skip and log (continue on errors)
6. **Symlinks**: Skip symlinks/junctions (documented for future changes)
7. **Metadata**: Don't collect (consistent with USN approach, documented for future)
8. **Performance**: Use fast OS APIs, benchmark vs standard C++ filesystem

## Next Steps

See `IMPLEMENTATION_PLAN_BASIC_FINDER.md` for detailed implementation plan, API design, code structure, and integration points.

