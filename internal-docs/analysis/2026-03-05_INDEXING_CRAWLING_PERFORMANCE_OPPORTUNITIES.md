# Indexing & Crawling Performance Opportunities

**Date:** 2026-03-05
**Scope:** `src/crawler/FolderCrawler.cpp`, `src/index/InitialIndexPopulator.cpp`,
`src/index/FileIndex.cpp`, `src/index/FileIndexStorage.cpp`,
`src/index/PathRecomputer.cpp`, `src/crawler/FolderCrawlerIndexBuilder.cpp`

---

## Background

This document records performance opportunities identified during a review of the
initial indexing and crawling code. Two code paths are covered:

- **Windows MFT path** — `PopulateInitialIndex` enumerates the Master File Table
  via `FSCTL_ENUM_USN_DATA` then calls `RecomputeAllPaths`.
- **Crawler path** — `FolderCrawler` walks the directory tree recursively with a
  multi-threaded work-stealing queue and calls `FileIndex::InsertPaths` in batches.

---

## HIGH IMPACT

### 1. `entries` vector reallocated every loop iteration in `WorkerThread`

**File:** `src/crawler/FolderCrawler.cpp:386`

**Problem:**

```cpp
while (!should_exit) {
    ...
    std::vector<DirectoryEntry> entries;   // new heap allocation every directory
    if (!EnumerateDirectorySafe(this, dir_path, entries, ...))
```

`EnumerateDirectory` calls `entries.clear()` at the start of every call, which
resets the size to zero but does not release capacity. However, because `entries`
is declared inside the loop it is destroyed and reconstructed on every iteration,
discarding that capacity and causing a fresh heap allocation for every directory
processed. On a crawl of 50 000+ directories across N worker threads this is
50 000+ × N redundant `malloc`/`free` pairs in the hot path.

**Fix:** Declare `entries` once above the `while` loop. `EnumerateDirectory`
already calls `entries.clear()` so the invariant is maintained. The vector will
grow to the high-water mark of the largest directory encountered and reuse that
allocation for all subsequent directories.

---

### 2. One `queue_mutex_` acquisition per subdirectory found

**File:** `src/crawler/FolderCrawler.cpp:307–310` (inside `ProcessEntry`)

**Problem:**

```cpp
if (entry.is_directory) {
    const std::scoped_lock lock(queue_mutex_);   // once per subdirectory
    work_queue_.push(full_path);
    queue_cv_.notify_one();
```

`ProcessEntries` iterates every entry in a directory and acquires the queue lock
individually for each subdirectory found. A directory containing 40 subdirectories
causes 40 separate lock acquisitions and 40 `notify_one()` calls. With N worker
threads all doing the same thing concurrently, this is a significant contention
point.

**Fix:** Collect subdirectory paths into a local `std::vector<std::string>` while
iterating entries. After the loop, push the entire collection under a single lock
acquisition and issue a single `notify_all()`. This reduces lock traffic from
O(subdirs_per_dir) to O(1) per directory processed.

---

### 3. MFT enumeration buffer too small — excessive kernel transitions

**File:** `src/index/InitialIndexPopulator.cpp:31`

**Problem:**

```cpp
constexpr int kBufferSize = 64 * 1024; // 64KB buffer for MFT enumeration
```

Each `DeviceIoControl(FSCTL_ENUM_USN_DATA)` call is a user-to-kernel transition.
At approximately 64 bytes per USN record, a 64 KB buffer holds around 1 000
records per call. For a volume with 500 000 files this means roughly 500 kernel
transitions just to enumerate. Each transition carries fixed overhead (context
switch, parameter validation, DMA setup). MSDN uses 64 KB as a minimum example;
production tools (e.g. Everything, Listary) use 256 KB–1 MB.

**Fix:** Increase `kBufferSize` to 256 KB or 512 KB. The buffer is stack-allocated
via `std::vector<char>` so heap fragmentation is not a concern. Measure with
`ScopedTimer` already present in `PopulateInitialIndex` — the gain on volumes
with 200 000+ files is typically 4–8× fewer `DeviceIoControl` calls.

---

### 4. macOS: `opendir`/`readdir` instead of `getattrlistbulk`

**File:** `src/crawler/FolderCrawler.cpp:556–618` (Apple section)

**Problem:**

The macOS `EnumerateDirectory` uses `opendir`/`readdir`. Although APFS fills
`d_type` (avoiding per-entry `lstat`), `readdir` returns one `dirent*` at a time
through the C library abstraction. For a directory with 500 entries, this is 500
successive calls into the kernel's VFS layer.

macOS 10.10+ provides `getattrlistbulk(2)`, which returns hundreds of entries
**together with their type and attributes** in a single syscall. This is the API
used by `Finder.app` and `mdfind`. Benchmark data from Apple developer sessions
shows 3–10× reduction in syscall count for large directories, translating directly
to faster initial crawl times on macOS.

**Fix:** Implement an APFS-optimised `EnumerateDirectory` using
`getattrlistbulk` inside a new `#if defined(__APPLE__)` block. Keep the current
`opendir`/`readdir` path as a fallback for non-APFS volumes (e.g. network mounts
where `getattrlistbulk` returns `ENOTSUP`).

---

## MEDIUM IMPACT

### 5. Double lock-acquire per file when MFT metadata reading is enabled

**File:** `src/index/InitialIndexPopulator.cpp:115–130`

**Problem:**

```cpp
ctx.file_index.Insert(file_ref_num, parent_ref_num, filename, is_directory, mod_time);
// ...
ctx.file_index.UpdateFileSizeById(file_ref_num, file_size);  // second exclusive lock
```

When `ENABLE_MFT_METADATA_READING` is active, every successfully read file
acquires the index exclusive lock twice: once for `Insert`, once for
`UpdateFileSizeById`. For a 500 000-file volume this is 1 000 000 lock
acquisitions instead of 500 000.

**Fix:** Add an optional `file_size` parameter to `FileIndex::Insert` (or a
dedicated `InsertWithMetadata` overload) so that a single lock acquisition covers
both the entry insertion and the size update. The `MFT_METADATA_READING` code path
in `ProcessUsnRecord` should pass the already-known size directly.

---

### 6. `NormalizePathForDedup` heap-allocates a `std::string` on every crawler insert

**File:** `src/index/FileIndex.cpp:160` (inside `InsertPathUnderLock`)

**Problem:**

```cpp
const std::string norm = NormalizePathForDedup(path_to_use);  // heap alloc per path
const size_t h = PathHash(norm);
```

`NormalizePathForDedup` constructs a new `std::string` for every path inserted.
For a 1 000 000-file crawl this is 1 000 000 heap allocations purely for the
deduplication hash key. The `norm` string is only used to compute `h` and, in the
collision chain, as the comparand in `PathViewEqualsNormalized`.

**Fix:** Compute the hash inline without materialising the string:

```cpp
// Hash normalised path without allocating:
size_t h = 0;
for (size_t i = 0, n = path_to_use.size(); i < n; ++i) {
    if (path_to_use[i] == '\\' || path_to_use[i] == '/') continue; // handle sep
    // fold into hash using same seed as std::hash<string_view>
}
```

The `norm` string is only needed in the collision path, which is rare for distinct
paths. A lazy allocation there (only when a hash collision is found) reduces the
common case to zero allocations.

---

### 7. `StringPool::Intern` acquires its own mutex while the index exclusive lock is already held

**File:** `src/index/FileIndexStorage.h:33`, called from `FileIndexStorage::InsertLocked`

**Problem:**

```cpp
// Inside StringPool::Intern:
const std::scoped_lock lock(pool_mutex_);   // inner lock
```

`InsertLocked` is always called while the caller holds the index's `unique_lock`.
No other thread can be inside `InsertLocked` concurrently, so the inner
`pool_mutex_` acquires an uncontended mutex on every file insert. For 500 000
files this is 500 000 uncontended mutex lock/unlock pairs — each costs a memory
barrier and a potential futex syscall on Linux even when uncontended.

**Fix:** Either remove `pool_mutex_` from `StringPool` and document that callers
must hold the index lock (the existing invariant), or split `StringPool` into a
lock-free variant used during bulk population and a thread-safe variant used during
incremental USN updates.

---

### 8. `RecomputeAllPaths()` is O(N) but largely redundant for the crawler path

**File:** `src/crawler/FolderCrawlerIndexBuilder.cpp:79`,
`src/index/PathRecomputer.cpp:22–47`

**Problem:**

`FolderCrawlerIndexBuilder` calls `file_index_.RecomputeAllPaths()` after the
crawl completes. `RecomputeAllPaths` clears `PathStorage` and rebuilds every path
from the parent-ID chain. However, `FolderCrawler` inserts paths via
`FileIndex::InsertPaths` → `InsertPathUnderLock`, which already stores the correct
full path string at insertion time. The paths are already correct; `RecomputeAllPaths`
is re-deriving the same result.

The only genuinely useful work `RecomputeAllPaths` does in this context is:
- Resetting OneDrive sentinel values (irrelevant on macOS/Linux crawls).
- Fixing paths for `DirectoryResolver`-synthesised virtual parent entries (these
  are created when a child is inserted before its parent, then overwritten when
  the real parent is later inserted by the crawler).

**Fix:** For the crawler path, replace the unconditional `RecomputeAllPaths()` call
with a targeted post-pass that only resets OneDrive sentinels (when on Windows) and
validates synthetic parent paths. On macOS/Linux this post-pass would be a no-op,
eliminating an O(N) full-index scan after every crawl. If correctness risk is a
concern, keep the call behind a `#ifdef _WIN32` guard so it only runs where OneDrive
files are plausible.

---

## LOWER IMPACT

### 9. Linux: `readdir` wrapper vs direct `getdents64` with large buffer

**File:** `src/crawler/FolderCrawler.cpp:623–680`

The Linux `EnumerateDirectory` uses `opendir`/`readdir`. The C library internally
calls `getdents64` with a small (typically 32 KB) buffer and returns one entry at a
time. Calling `getdents64` directly with a 64 KB buffer would make the same number
of entries available in fewer syscalls, reducing kernel transitions for large
directories (e.g. `node_modules`, log directories).

This is a modest improvement but measurable on Linux servers with directories
containing thousands of entries.

---

### 10. All worker threads share one `queue_mutex_` — bottleneck on fast NVMe

**File:** `src/crawler/FolderCrawler.h:118`

```cpp
std::queue<std::string> work_queue_;
std::mutex queue_mutex_;
```

With `hardware_concurrency()` threads (e.g. 16 on a modern machine), all threads
contend on a single `queue_mutex_` when pushing new subdirectories or pulling the
next directory to process. At `batch_size = 2500` the per-flush contention is
already amortised, but the subdirectory-push contention (item 2 above) and the
pull contention both converge on this single lock.

The `FAST_LIBS_BOOST` option already enables `boost::lockfree` for other
containers. A `boost::lockfree::queue<std::string*>` (pointer-based, since
`lockfree::queue` requires trivially copyable elements) or a per-thread deque with
work stealing would eliminate this bottleneck on NVMe storage where enumeration
itself is fast and lock contention becomes the limiting factor.

---

### 11. 100ms poll timeout extends crawl completion latency

**File:** `src/crawler/FolderCrawler.cpp:38, 142`

```cpp
constexpr std::chrono::milliseconds::rep kWaitPollIntervalMs = 100;
```

The main `Crawl()` wait loop uses a 100ms `wait_for` timeout as a cancellation
poll fallback. The `completion_cv_` is notified when the last worker finishes, so
in the common case the thread wakes immediately. However, if the notify races with
the `wait_for` entering its timed-wait phase, up to 100ms of unnecessary latency
is added to the reported crawl duration.

**Fix:** Reduce `kWaitPollIntervalMs` to 10 ms, or restructure the final
`active_workers_` decrement to guarantee notification under the lock:

```cpp
{
    const std::scoped_lock lock(queue_mutex_);
    active_workers_.fetch_sub(1, std::memory_order_release);
    if (work_queue_.empty() && active_workers_.load() == 0) {
        completion_cv_.notify_all();
    }
}
```

This matches the existing completion check but moves the notification to the same
critical section that decrements the counter, closing the race window.

---

## Priority Summary

| # | File(s) | Issue | Effort | Expected gain |
|---|---------|-------|--------|---------------|
| 1 | `FolderCrawler.cpp:386` | `entries` vector re-allocated per directory | Trivial | Eliminate N × 50k+ allocs per crawl |
| 2 | `FolderCrawler.cpp:307` | One lock acquire per subdirectory found | Small | 10–40× less lock traffic per directory |
| 3 | `InitialIndexPopulator.cpp:31` | 64 KB MFT buffer — too many kernel transitions | Trivial | 4–16× fewer `DeviceIoControl` calls |
| 4 | `FolderCrawler.cpp:556` | macOS `readdir` vs `getattrlistbulk` | Medium | 3–10× fewer syscalls on macOS |
| 5 | `InitialIndexPopulator.cpp:115` | Double lock-acquire per file with MFT enabled | Small | Halve lock traffic per MFT file |
| 6 | `FileIndex.cpp:160` | `NormalizePathForDedup` allocates per insert | Medium | Eliminate 1M allocs per 1M-file crawl |
| 7 | `FileIndexStorage.h:33` | `StringPool` inner mutex under index exclusive lock | Small | Remove 500k+ redundant mutex ops |
| 8 | `FolderCrawlerIndexBuilder.cpp:79` | `RecomputeAllPaths()` redundant on crawler path | Small | Skip full O(N) pass after crawl |
| 9 | `FolderCrawler.cpp:623` | `readdir` wrapper vs `getdents64` direct (Linux) | Medium | Modest gain on large directories |
| 10 | `FolderCrawler.h:118` | Single shared `queue_mutex_` for N threads | Large | Reduce contention on fast NVMe |
| 11 | `FolderCrawler.cpp:38,142` | 100ms poll timeout adds tail latency | Trivial | Cut crawl-finish latency by ~100ms |

**Recommended order of attack:** Items 1, 3, and 11 are trivial line-count changes
with guaranteed wins and zero risk of regression. Item 2 (batch subdirectory pushes)
and item 8 (skip `RecomputeAllPaths` on crawler path) are small refactors with high
ROI. Items 4 (`getattrlistbulk`) and 6 (`NormalizePathForDedup` allocation) are the
most impactful medium-effort changes for macOS and the crawler path respectively.
