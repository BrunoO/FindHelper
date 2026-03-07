# Folder Crawl Code Review and Optimization Research

**Date:** 2026-02-02  
**Scope:** `FolderCrawler`, `FolderCrawlerIndexBuilder`, `FileIndex::InsertPath`, platform-specific enumeration.

---

## 1. Current Architecture Summary

### 1.1 Flow

1. **FolderCrawlerIndexBuilder** starts a background thread that creates a `FolderCrawler` and calls `Crawl(root_path, progress_counter, cancel_flag)`.
2. **FolderCrawler::Crawl** spawns N worker threads (default: `hardware_concurrency()`), pushes the root path onto a shared queue, and waits on a completion condition variable.
3. **Worker threads** repeatedly:
   - Pop a directory path from the queue (`GetWorkFromQueue`, 100 ms timeout when idle).
   - Call **EnumerateDirectory(dir_path)** (platform-specific) to get a list of `DirectoryEntry` (name, is_directory, is_symlink).
   - For each entry: build full path, skip symlinks, push path into a **batch**; if directory, push full path onto the work queue.
   - When batch size ≥ `config_.batch_size` (default 1000), call **FlushBatch(batch)**.
4. **FlushBatch** calls **file_index_.InsertPath(path)** for each path in the batch (no batch insert API).
5. After crawl completes, **RecomputeAllPaths()** is called once to rebuild path storage.

### 1.2 Design strengths

- **Multi-threaded traversal**: Work queue spreads directory work across cores; good for deep trees and many small directories.
- **Batching**: Reduces how often we touch the index (every `batch_size` paths), but each path still acquires the FileIndex mutex once inside `InsertPath`.
- **Platform-specific enumeration**: Windows uses FindFirstFileEx; macOS uses opendir/readdir + lstat; Linux uses `std::filesystem::directory_iterator` + status.
- **Cancellation**: `cancel_flag` and `should_stop_` with 100 ms polling allow responsive stop.
- **Error isolation**: EnumerateDirectory and FlushBatch use try/catch; one failure does not abort the whole crawl.

---

## 2. Per-Platform Enumeration Review

### 2.1 Windows (`FolderCrawler.cpp`, `#ifdef _WIN32`)

**Current:**

- `FindFirstFileExW(..., FindExInfoBasic, ..., FIND_FIRST_EX_LARGE_FETCH)`.
- Single UTF-8→wide conversion for the directory path; each entry name is converted back with `WideToUtf8(find_data.cFileName)`.
- Skips `.` / `..` and uses `FILE_ATTRIBUTE_DIRECTORY` and `FILE_ATTRIBUTE_REPARSE_POINT` from the same structure (no extra syscalls per entry).

**Assessment:** Already uses the recommended high-performance options (Basic info + Large Fetch). No per-entry GetFileAttributesEx or stat-style calls.

**Research (Wholetomato, Schöner):**

- `FindFirstFileEx` with `FindExInfoBasic` + `FIND_FIRST_EX_LARGE_FETCH` is among the fastest standard APIs; benchmarks show large gains over `GetFileAttributesEx` and `std::filesystem`.
- **GetFileInformationByHandleEx** with a large buffer (e.g. 64 KB) and `FileFullDirectoryInfo` can be **~50x faster** than GetFileAttributesEx and often faster than FindFirstFileEx on large directories (e.g. 5000 files): one big read into a user buffer, then parse entries in memory. Trade-off: more code, handle management, and parsing of `FILE_FULL_DIR_INFO` (variable-length entries).

**Conclusion:** Windows enumeration is already in good shape. The next step up is **GetFileInformationByHandleEx** with a large buffer if we need maximum speed on Windows and accept extra complexity.

---

### 2.2 macOS (`#elif defined(__APPLE__)`)

**Current:**

- `opendir` + `readdir` for names.
- For **each** entry: build full path and call **lstat(full_path)** to get `is_directory` and `is_symlink`.

**Assessment:** One `lstat` per file means many syscalls on large directories. This is the main cost on macOS.

**Research (mjtsai, Apple docs, getattrlistbulk):**

- **getattrlistbulk()** (Yosemite+) can return names and attributes in bulk without creating a vnode per file; good when you need both name and type/attributes.
- **readdir** on macOS can return `d_type` in `struct dirent`; when it’s not `DT_UNKNOWN`, we can avoid `lstat` for that entry. Many local filesystems (e.g. APFS) do fill `d_type`.
- **fts_open() / fts_read()** are optimized for full tree traversal and can be faster than hand-rolled opendir/readdir + lstat for “walk entire tree” workloads.

**Conclusion:** Biggest win on macOS is to **avoid lstat when possible**: use `d_type` from `readdir` when `d_type != DT_UNKNOWN`; only call `lstat` when `d_type == DT_UNKNOWN`. Optional follow-up: consider **getattrlistbulk()** for bulk name+attributes in large directories.

---

### 2.3 Linux (`#elif defined(__linux__)`)

**Current:**

- `std::filesystem::directory_iterator` over `dir_path`.
- For each entry: `fs_entry.path().filename().string()` and **fs_entry.status(status_ec)** (effectively stat/lstat under the hood).

**Assessment:** Two operations per entry (iterator advance + status). `std::filesystem` is convenient but not the fastest; it can hide extra allocations and syscalls.

**Research (getdents64, LWN):**

- **getdents64()** returns many directory entries in one syscall; `struct dirent64` has `d_type`. When the filesystem provides `d_type` (most do), we can distinguish file/dir/symlink **without** a separate stat.
- When `d_type == DT_UNKNOWN` (some filesystems), we must fall back to stat/lstat for that entry.
- Proposals like **xgetdents** / **dirreadahead** aim to batch inode reads; they are not standard yet. The immediate win is **getdents64 + d_type** to minimize stat calls.

**Conclusion:** On Linux, the largest gain is to **avoid an extra status() when type is known**: use **getdents64()** (or a thin wrapper) and use `d_type` when not `DT_UNKNOWN`; call stat only when necessary. This reduces syscalls and can be significantly faster than `directory_iterator` + `status()` per entry.

---

## 3. Index Insertion Bottleneck

**Current:**

- **FlushBatch** loops over the batch and calls **file_index_.InsertPath(path)** for each path.
- **InsertPath**:
  - Takes a **unique_lock** on `FileIndex::mutex_`.
  - Parses path (directory + filename), calls **DirectoryResolver::GetOrCreateDirectoryId(directory_path)** (may recurse and insert parent directories),
  - Allocates next id, then **InsertLocked(id, parent_id, name, is_directory, ...)**.

So each path in the batch causes **one full lock acquisition** and potentially multiple internal operations (directory chain creation, storage insert, path storage update). With many workers and batch size 1000, we still have **thousands of lock acquisitions** per crawl.

**Optimization:** Introduce a **batch insert** API that holds the FileIndex lock **once** for many paths:

- e.g. `InsertPaths(const std::vector<std::string_view>& paths)` or an iterator range,
- Inside: single `unique_lock`, then for each path: parse, GetOrCreateDirectoryId, InsertLocked (reusing the same lock).
- Optionally: sort or group by directory so that GetOrCreateDirectoryId cache hits are better and directory creation is more localized.

**Impact:** Fewer lock acquisitions, less contention between crawler workers and any other code using FileIndex, and better cache behavior around DirectoryResolver and storage. This is likely one of the highest-impact changes for crawl time.

---

## 4. Other Observations

### 4.1 DirectoryResolver and is_directory

- **InsertPath** infers directory only by trailing slash. FolderCrawler does **not** pass trailing slashes for directories, so directories are currently inserted with `is_directory=false` (documented in FileIndex.cpp). GetOrCreateDirectoryId still creates parent directories with `is_directory=true`. Fixing this (e.g. by passing an explicit `is_directory` from the crawler) would improve consistency and could allow a batch insert to avoid redundant directory creation.

### 4.2 Batch size and progress

- Default `batch_size = 1000`, `progress_update_interval = 10000`. Tuning may help responsiveness vs overhead; larger batches favor fewer FlushBatch calls and would pair well with a batch InsertPaths.

### 4.3 RecomputeAllPaths

- Called once after the crawl. It walks all entries and rebuilds path storage. It’s required for correctness. If it becomes a measurable cost, it could be profiled and possibly parallelized (e.g. by range of ids), but it’s a single pass after the main crawl.

### 4.4 UTF-8 / wide conversion (Windows)

- One `Utf8ToWide(dir_path)` per directory and one `WideToUtf8(name)` per entry. Cost is proportional to path length and count; acceptable unless profiling shows it hot. Batching conversions is possible but secondary to enumeration and insert cost.

---

## 5. Recommended Optimizations (by impact and effort)

| Priority | Optimization | Platform | Effort | Impact |
|----------|--------------|----------|--------|--------|
| **1** | **Batch insert API** `InsertPaths(batch)` (single lock for whole batch) | All | Medium | High – reduces lock contention and improves scalability with many workers. |
| **2** | **Avoid lstat when d_type is known** (use readdir `d_type`, call lstat only for DT_UNKNOWN) | macOS | Low | High – removes one syscall per file on typical filesystems. |
| **3** | **Use getdents64 + d_type** (avoid extra status() when type known) | Linux | Medium | High – fewer syscalls and less overhead than directory_iterator + status per entry. |
| **4** | Pass **explicit is_directory** from crawler to InsertPath (and fix directory trailing-slash semantics) | All | Low | Medium – correct semantics and cleaner batch insert. |
| **5** | **GetFileInformationByHandleEx** with large buffer (e.g. 64 KB) for directory enumeration | Windows | High | Medium–High – can be ~50x faster than GetFileAttributesEx in benchmarks; already faster than FindFirstFileEx in some large-dir cases. |
| **6** | Consider **getattrlistbulk()** for bulk name+attributes on large directories | macOS | High | Medium – fewer syscalls when we need both names and attributes. |
| **7** | Tune batch_size and progress_update_interval; optionally increase batch size if using InsertPaths | All | Low | Low–Medium – better throughput and fewer lock round-trips. |

---

## 6. References

- **Windows:** Wholetomato blog (Nov 2024), “How to Query File Attributes 50x faster on Windows” – FindFirstFileEx vs GetFileInformationByHandleEx vs std::filesystem.  
- **Windows:** Sebastian Schöner – FIND_FIRST_EX_LARGE_FETCH, NtQueryDirectoryFileEx.  
- **Linux:** getdents(2), getdents64(2); LWN “Two paths to a better readdir()”, “xgetdents vs dirreadahead”.  
- **macOS:** “Performance Considerations When Reading Directories on macOS” (mjtsai); getattrlistbulk(2); File-System Performance Guidelines (Apple).  

---

## 7. Summary

- **Crawling** is already multi-threaded and uses solid Windows APIs (FindExInfoBasic + LARGE_FETCH). The main gains are: **(1)** a **batch insert** for the index to cut lock contention, **(2)** on macOS **using readdir’s d_type** and avoiding lstat when possible, and **(3)** on Linux **using getdents64 + d_type** instead of directory_iterator + status per entry. Optional next step on Windows is **GetFileInformationByHandleEx** with a large buffer for maximum enumeration speed at the cost of extra implementation complexity.
