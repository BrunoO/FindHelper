# Search & Index Performance Opportunities

**Date:** 2026-03-06
**Scope:** `src/index/`, `src/search/`, `src/path/`, `src/utils/StringSearch.h`

---

## Background

The previous analysis covered the initial indexing and crawling path
(`docs/analysis/2026-03-05_INDEXING_CRAWLING_PERFORMANCE_OPPORTUNITIES.md`).
All 11 items from that document have been implemented.

This document covers the remaining hot paths: the search engine, the in-memory
index data structures, lazy attribute loading, and the streaming results
pipeline.

---

## QUICK WINS

### 1. `InternUnderLock` signature forces a heap allocation on every insert

**File:** `src/index/FileIndexStorage.h:43`, called from
`src/index/FileIndexStorage.cpp:20`

**Problem:**

```cpp
// Current signature
const std::string* InternUnderLock(const std::string& str);

// Called from InsertLocked:
ext = extension_string_pool_.InternUnderLock(std::string(extension)); // temp alloc
```

`InsertLocked` receives `extension` as `std::string_view` but must convert it
to `std::string` just to satisfy the parameter type — before even checking
whether the extension already exists in the pool. For a 1M-file crawl where
`.txt` is seen 200k times, this is 200k `std::string` temporaries that are
immediately discarded once the pool hit is confirmed.

**Fix (two parts):**

1. Change the signature to `InternUnderLock(std::string_view str)` — eliminates
   the caller-side `std::string(extension)` conversion.

2. Inside `InternUnderLock`, avoid the `ToLower(str)` allocation on pool hits.
   File extensions are almost always already lowercase. Check first:

```cpp
const std::string* InternUnderLock(std::string_view str) {
  // Fast path: str is already lowercase and interned (most extensions)
  if (auto it = interned_strings_.find(str); it != interned_strings_.end()) {
    return &(*it);
  }
  // Slow path: normalize and insert (first occurrence or mixed-case)
  auto [it2, _] = interned_strings_.insert(ToLower(std::string(str)));
  return &(*it2);
}
```

`hash_set_stable_t<std::string>` supports heterogeneous `find(string_view)`
when the underlying container is `boost::unordered_set` (FAST_LIBS_BOOST=ON).
For the `std::unordered_set` fallback (C++17), the fast-path `find` still needs
a `std::string` key — but you can do the in-place lowercase check
(`std::all_of`) and pass `str` as `std::string` only once, still saving the
double-allocation.

**Effort:** Small. **Expected gain:** Eliminates 1 heap alloc per extension
lookup in the hot insert path (~half of all inserts hit a cached extension).

---

### 2. `StreamingResultsCollector::AddResult` acquires one mutex per match result

**File:** `src/search/SearchWorker.cpp:459`, `src/search/StreamingResultsCollector.cpp:14`

**Problem:**

```cpp
// ProcessOneReadyFutureIfReady — called when a search thread future is ready
const std::vector<SearchResultData> chunk_data = search_futures[index].get();
for (const auto& data : chunk_data) {
  collector.AddResult(data);  // one scoped_lock per result
}
```

Each `AddResult` call acquires `mutex_`, copies into `all_results_`, appends
to `current_batch_`, reads the clock, and possibly flushes. A query matching
50 000 results across 8 futures = 50 000 individual mutex acquisitions during
post-processing.

**Fix:**

Add `AddResults(const std::vector<SearchResultData>& batch)` to
`StreamingResultsCollector` that acquires the mutex once, bulk-inserts all
items, then decides whether to flush:

```cpp
void StreamingResultsCollector::AddResults(const std::vector<SearchResultData>& batch) {
  const std::scoped_lock lock(mutex_);
  all_results_.insert(all_results_.end(), batch.begin(), batch.end());
  current_batch_.insert(current_batch_.end(), batch.begin(), batch.end());
  auto now = std::chrono::steady_clock::now();
  if (current_batch_.size() >= batch_size_ || (now - last_flush_time_) >= notification_interval_) {
    FlushCurrentBatch();
  }
}
```

Replace the loop in `ProcessOneReadyFutureIfReady` with a single call.

**Effort:** Small. **Expected gain:** N mutex acquisitions → 1 per search
thread future (typically 8–16 futures per query).

---

### 3. `LazyAttributeLoader` acquires two consecutive shared locks for (check + path)

**File:** `src/index/LazyAttributeLoader.cpp:254–268`

**Problem:**

```cpp
// Lock 1: check whether loading is needed
auto [needs_loading, need_time, entry] = CheckFileSizeNeedsLoading(...);

// Lock 2: read the path
{
  const std::shared_lock lock(index_mutex_ref_);
  path = path_storage_.GetPath(id);   // separate lock acquisition
}
```

Both shared locks are released and re-acquired between them. Since the check
and path read are logically a single atomic observation (reading the current
state of one index entry), they should happen under the same lock. As-is, a
write could theoretically sneak in between the two acquisitions (though
currently benign due to invariants), and two futex operations are paid where
one would suffice.

**Fix:** Extend `CheckFileSizeNeedsLoading` to also return the path string
(retrieved while still under the shared lock):

```cpp
struct FileSizeCheckResult {
  bool needs_loading;
  bool need_time;
  const FileEntry* entry;
  std::string path;  // populated if needs_loading == true
};
```

Reduces two shared_lock acquire/release pairs to one.

**Effort:** Small. **Expected gain:** Halves locking overhead for every lazy
attribute load (each visible row in the results table triggers one load).

---

### 4. `GetDirectoryId` and `RemoveDirectoryFromCache` allocate `std::string` for `string_view` parameters

**File:** `src/index/FileIndexStorage.h:104`, `src/index/FileIndexStorage.cpp:184–189`

**Problem:**

```cpp
inline uint64_t GetDirectoryId(std::string_view path) const {
  const std::string path_str(path);  // heap alloc for every directory lookup
  if (auto it = directory_path_to_id_.find(path_str); ...)

void FileIndexStorage::RemoveDirectoryFromCache(std::string_view path) {
  const std::string path_str(path);  // heap alloc just to call erase()
  directory_path_to_id_.erase(path_str);
}
```

`GetDirectoryId` is called from `InsertPathUnderLock` for every directory
entry during crawl. For a 500k file crawl with 50k directories, this is 50k
redundant `std::string` constructions purely for the hash-map lookup key.

**Fix (tiered):**

When `FAST_LIBS_BOOST=ON`: change `directory_path_to_id_` to
`boost::unordered_map<std::string, uint64_t>` configured with a transparent
hasher (`boost::hash<std::string_view>` / `std::equal_to<>`). Boost 1.80+
supports heterogeneous `find`/`erase` by accepting `string_view` directly.

When `FAST_LIBS_BOOST=OFF` (C++17 `std::unordered_map`): heterogeneous lookup
is not available. The best available option is a one-off
`StringViewHashMap<std::string, uint64_t>` helper using a custom `struct`
with `using is_transparent = void` — only possible in C++20's standard
`std::unordered_map`. Until then, the allocation remains for the non-Boost
path.

**Effort:** Small–Medium. **Expected gain:** ~50k allocs eliminated per crawl
of 500k files.

---

## MEDIUM IMPACT

### 5. `ProcessStreamingSearchFutures` polls all pending futures with O(n) linear scan

**File:** `src/search/SearchWorker.cpp:522–541`

**Problem:**

```cpp
while (!pending_indices.empty()) {
  for (auto it = pending_indices.begin(); it != pending_indices.end(); ) {
    if (ProcessOneReadyFutureIfReady(..., *it, ...)) {
      // found one, break and restart scan
      break;
    }
    ++it;  // wait_for(0ms) on every pending future every iteration
  }
  if (!found_ready && !pending_indices.empty()) {
    search_futures[pending_indices[0]].wait_for(5ms);  // block on just one
  }
}
```

On each poll cycle: calls `wait_for(std::chrono::milliseconds(0))` on every
pending future to test readiness, then blocks waiting on just the first one.
With 16 worker threads, this is 16 × `wait_for(0ms)` system calls per poll
cycle. The 5ms block on `pending_indices[0]` means futures that complete out
of order can also stall subsequent ones.

**Fix:** Replace the linear scan with a completion-order queue. Options:

- **Simple:** Use `wait_for(5ms)` on ALL pending futures in a round-robin
  pattern (ensures we find the earliest completer).
- **Better:** Add a lightweight `std::atomic<size_t> completed_futures_count`
  to the search context; each worker increments it on completion and notifies
  a `condition_variable` to wake the collector thread.
- **Best (C++20):** Use `std::experimental::when_any` or an equivalent.

**Effort:** Medium. **Expected gain:** Eliminates O(N) polling overhead; faster
time-to-first-result for queries producing large result sets across many threads.

---

### 6. `FileEntry::name` is a `std::string` per entry — 1M heap allocations for a 1M-file index

**File:** `src/index/FileIndexStorage.h:56`, `src/index/FileIndexStorage.cpp:53`

**Problem:**

```cpp
struct FileEntry {
  uint64_t parentID = 0;
  std::string name;       // separate heap alloc per entry
  ...
};

// InsertLocked:
const FileEntry new_entry{parent_id, std::string(name), ext, ...};
```

On a 1M-file index, `FileEntry::name` produces 1M `std::string` heap
allocations (beyond SSO for names longer than ~15 chars). The `name` field
exists primarily for `PathBuilder::BuildFullPath` used during
`RecomputeAllPaths` (Windows only) and during USN maintenance events
(rename/move).

On macOS/Linux, paths are already stored in `PathStorage`'s contiguous buffer.
The `name` in `FileEntry` is entirely redundant on those platforms after the
crawl completes.

**Fix (large refactor):** Replace `FileEntry::name` with an offset/view into
a separate flat name-storage buffer — similar to how `PathStorage` stores
paths. On Windows, where `PathBuilder` traverses the parent chain via
`entry->name`, the name buffer is still needed. On macOS/Linux, `name` could
be a `string_view` into `PathStorage`, eliminating all per-entry name
allocations.

**Effort:** Large. **Expected gain:** Eliminates 1M allocs for a 1M-file
index; improves cache locality for `GetEntry`-heavy paths (lazy loading,
maintenance).

---

### 7. ARM / Apple Silicon: no SIMD path for case-insensitive search

**File:** `src/utils/StringSearch.h`, `src/utils/StringSearchAVX2.cpp`

**Problem:**

The search hot path has AVX2-accelerated `ContainsSubstringI` for x86/x64
(`STRING_SEARCH_AVX2_AVAILABLE`). On Apple Silicon (arm64) — the primary
development platform — the scalar fallback is used for every search. A 1M-file
index means the scalar loop runs on up to 1M paths per query.

ARM NEON provides 128-bit SIMD intrinsics equivalent to SSE4.2, with 16-byte
character-comparison vectors. Apple Silicon additionally supports
`NEON + AES` which enables fast bulk case-folding.

**Fix:** Add `#elif defined(__ARM_NEON)` block in `StringSearchAVX2.cpp`
(rename to `StringSearchSIMD.cpp`) with NEON `vceqq_u8`/`vorrq_u8`-based
`ContainsSubstringI` and `ContainsSubstringIAVX2` implementations that work
on arm64. Guard behind `#ifdef __ARM_NEON` and a runtime feature check.

**Effort:** Medium. **Expected gain:** 2–4× speedup for case-insensitive
queries on macOS (Apple Silicon), which is the primary development platform.

---

## LARGER REFACTORING

### 8. `FileIndexStorage::id_to_entry_` is a heap-fragmented hash map of `FileEntry` nodes

**File:** `src/index/FileIndexStorage.h:139`

**Problem:**

```cpp
hash_map_t<uint64_t, FileEntry> id_to_entry_;
```

`std::unordered_map` (or `boost::unordered_map`) uses node-based storage:
each `FileEntry` lives at an independent heap allocation. For 1M entries this
means 1M separate allocations with no spatial locality between adjacent
file IDs. During `RecomputeAllPaths` (Windows), `GetEntry` is called once per
entry in iteration order, cache-missing for most. During lazy attribute loading
(which scans the hot table per visible row), random access into the node-based
map is cache-hostile.

**Fix:** Replace with a `flat_hash_map` or open-addressing hash map. Boost
1.81+ provides `boost::unordered_flat_map<K, V>` which uses open-addressing
with value-type storage (POD-friendly). For `FileEntry` with `std::string name`,
a custom slab allocator or pool allocator for `FileEntry` objects would also
help. The full win comes from eliminating per-node allocation and improving
iteration locality.

**Effort:** Large (requires storage format change, allocation strategy, and
careful migration of pointer-stability assumptions). **Expected gain:** Significant
reduction in memory fragmentation; faster `GetEntry` under load.

---

### 9. `PathStorage::InsertPath` marks old entry as deleted and appends a new one — no compaction during crawl

**File:** `src/path/PathStorage.cpp:17–46`

**Problem:**

When the same path ID is updated (e.g., a directory visited twice due to
`DirectoryResolver` synthetic entries later overwritten by real entries),
`InsertPath` marks the old slot as `is_deleted[idx] = 1` and appends a new
entry. This grows the SoA arrays indefinitely during the crawl and increases
the `deleted_count_` that triggers periodic `RebuildPathBuffer` compaction.

For a crawl with many `DirectoryResolver` pre-inserts (deep hierarchies), the
deleted fraction can be significant: deleted entries are iterated by all search
threads even though they contribute nothing (the `is_deleted` guard adds a
branch to the inner search loop).

**Fix:** During bulk insertion (under the index `unique_lock`), detect and
in-place update the existing slot when the ID already exists rather than
marking deleted + appending. This keeps the SoA arrays compact and avoids the
compaction trigger entirely during normal crawl patterns.

**Effort:** Medium. **Expected gain:** Fewer deleted entries in SoA = smaller
arrays = better cache utilization in the search loop; fewer `RebuildPathBuffer`
calls.

---

## Priority Summary

| # | Area | Issue | Effort | Status |
|---|------|-------|--------|--------|
| 1 | `FileIndexStorage.h:43` | `InternUnderLock` forces alloc on every insert | Small | ✅ Done |
| 2 | `StreamingResultsCollector` | N mutex acquires for N results from one future | Small | ⬜ Remaining |
| 3 | `LazyAttributeLoader` | Two consecutive shared locks (check + path) | Small | ✅ Done |
| 4 | `GetDirectoryId`/`RemoveDirectoryFromCache` | `string_view→string` alloc for map lookup | Small–Med | ✅ Done |
| 5 | `ProcessStreamingSearchFutures` | O(n) poll over all pending futures | Medium | ⬜ Remaining |
| 6 | `FileEntry::name` | `std::string` per entry, 1M allocs for 1M files | Large | ✅ Done |
| 7 | ARM NEON search | No SIMD path on Apple Silicon | Medium | ✅ Done |
| 8 | `id_to_entry_` storage | Node-based map, 1M fragmented allocs | Large | ⬜ Remaining |
| 9 | `PathStorage::InsertPath` | In-place update vs. mark-deleted + append | Medium | ⬜ Remaining |

**Remaining:** #2 (small, isolated), #5 (medium), #9 (medium), #8 (large).
Next recommended: #2 (`StreamingResultsCollector` bulk insert) — single-file change, guaranteed win.
