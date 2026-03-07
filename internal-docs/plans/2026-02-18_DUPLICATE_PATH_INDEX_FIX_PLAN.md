# Plan: Robust fix for duplicate paths in the index

**Date:** 2026-02-18  
**Context:** Duplicate folder (and potentially file) paths can appear in the index, causing duplicate search results and unnecessary memory use. Display-side deduplication fixes the UI but not the root cause.

**Invariant we want:** One logical path ⇒ one index entry (one row in PathStorage, one FileEntry in storage).

---

## Options

### Option A: Crawler-side only – deduplicate before calling the index

**Idea:** In `FolderCrawler`, track full paths already sent this run; before `InsertPaths(batch)`, filter the batch to remove paths already in that set; after each flush, add the batch’s paths to the set.

**Pros**
- Small, localized change (FolderCrawler + maybe a small helper).
- Index stays unchanged (no path→id map, no new locking).
- Fixes the main current source of duplicates (crawl feeding the same path in multiple batches).

**Cons**
- Only affects FolderCrawler. Does not help:
  - USN or other callers of `InsertPath` / `InsertPaths`.
  - Double crawl without clear, or IndexFromFilePopulator + crawl overlap.
- “Already sent this run” is per-run state; if we ever have multiple producers (e.g. crawl + USN), each would need its own view of “what’s in the index” or we still get duplicates.
- Does not enforce the invariant at the index; duplicates from any other source remain possible.

**Verdict:** Reduces duplicates from crawl only; does not give a single source of truth. Not sufficient as the only fix.

---

### Option B: Index-side only – idempotent insert (path uniqueness in FileIndex)

**Idea:** FileIndex maintains a **path → id** map (normalized full path → existing id). In `InsertPathUnderLock(full_path, is_directory)`:
- Normalize `full_path` (e.g. canonical separator, optional case normalization).
- If normalized path is in the map → **reuse** that id: refresh path row via `InsertPathLocked(existing_id, full_path, is_directory)`; do **not** allocate a new id or insert a new storage entry.
- If not in the map → allocate new id, insert as today, and add `path_to_id_[normalized] = id`.

All mutating operations that change “which path an id has” must keep the map in sync:
- **Remove(id):** Get path (e.g. `GetPathLocked(id)`), then `operations_.Remove(id)`, then `path_to_id_.erase(normalize(path))`.
- **Rename(id, new_name):** After `operations_.Rename`, get old path (before) and new path (e.g. `GetPathLocked(id)` after); `path_to_id_.erase(normalize(old_path))`, `path_to_id_[normalize(new_path)] = id`.
- **Move(id, new_parent_id):** Same as Rename: update map for old path → remove, new path → id.
- **Clear():** `path_to_id_.clear()`.
- **RecomputeAllPaths():** After `path_recomputer_.RecomputeAllPaths()`, rebuild the map by iterating storage and `path_to_id_[normalize(GetPathLocked(id))] = id` for each id.

**Pros**
- Single source of truth: no matter who calls InsertPath (crawler, USN, IndexFromFilePopulator, future code), we never store two entries for the same path.
- Saves memory (no duplicate path strings, no duplicate SoA rows, no duplicate FileEntry).
- Enforces “one path = one entry” at the only place that owns the catalog (the index).

**Cons**
- More invasive: new structure, normalization rules, and every path-mutating path must update the map.
- Path normalization must be defined and consistent (separator, trailing slash, case on Windows vs macOS/Linux).
- Slightly more work on insert (lookup + maybe refresh) and on Remove/Rename/Move/Clear/RecomputeAllPaths.

**Verdict:** Best long-term fix. Ensures the invariant and fixes memory and display for all producers.

---

### Option C: Crawler + index (defense in depth)

**Idea:** Do both A and B: crawler deduplicates per run before calling the index; index also enforces path uniqueness.

**Pros**
- Crawler sends fewer redundant inserts (less work and fewer no-op refreshes in the index).
- Index still guarantees one path = one entry even if crawler (or anything else) misbehaves.

**Cons**
- Two places to maintain; crawler dedup is optional once the index is robust.

**Verdict:** Optional. B alone is sufficient; A can be added later if we want to reduce no-op re-inserts during crawl.

---

### Option D: Index-side “replace by path” without a path→id map

**Idea:** On insert, somehow detect “path P already exists” without a dedicated map (e.g. scan PathStorage or storage).

**Cons**
- Scan is O(n) per insert; unacceptable for large indexes. Any fast “path exists?” check effectively becomes a path→id (or path→index) structure, i.e. Option B.

**Verdict:** Not viable. Rejected.

---

## Recommendation

- **Primary:** Implement **Option B** (index-side idempotent insert with path→id map in FileIndex).
- **Optional later:** Add **Option A** (crawler-side dedup) to cut redundant work during crawl; not required for correctness once B is in place.
- **Display-side deduplication (SearchWorker.cpp, SearchController.cpp):** With Option B in place, the index never contains duplicate paths, so search results will never contain two items with the same full path. The deduplication in `MergeAndConvertToSearchResults` (SearchWorker.cpp) and in `ConsumePendingStreamingBatches` (SearchController.cpp) is therefore **no longer strictly necessary**. You may **remove it** when implementing Option B to simplify the merge path (single pass in MergeAndConvertToSearchResults, no cross-batch filter in streaming), or **keep it as a safety net** in case any code path ever inserts without going through the path_to_id_ check. If you remove it, add a brief comment that it was redundant with Option B (index guarantees one path = one entry).

---

## Implementation outline (Option B)

1. **Path normalization**
   - Define a single helper (e.g. `NormalizePathForDedup(std::string_view path) → std::string`):
     - Normalize separators (e.g. to `kPathSeparator`).
     - Optional: remove trailing separator for directories (or keep consistent rule).
     - Optional: on Windows, lowercase for case-insensitive uniqueness; on macOS/Linux, define whether to treat as case-sensitive (likely yes).
   - Document the rule in the plan or next to the map.

2. **path_to_id_ in FileIndex**
   - Type: e.g. `std::unordered_map<std::string, uint64_t>` or a custom key (e.g. path hash + string view) to avoid storing huge strings twice if needed; for simplicity, `unordered_map<string, uint64_t>` keyed by normalized path is fine.
   - Lifetime: same as FileIndex; cleared on `Clear()`, rebuilt after `RecomputeAllPaths()`.

3. **InsertPathUnderLock**
   - `norm = NormalizePathForDedup(full_path)`.
   - If `auto it = path_to_id_.find(norm); it != path_to_id_.end()`:
     - `existing_id = it->second`. Call `InsertPathLocked(existing_id, full_path, is_directory)` to refresh the path row (PathStorage); do **not** call `InsertLocked` again (storage entry already exists).
     - Return (or continue to next in batch).
   - Else:
     - Allocate `file_id` as today; `InsertLocked(...)`; `InsertPathLocked(file_id, full_path, is_directory)`; `path_to_id_[norm] = file_id`.

4. **Remove(id)**
   - Before calling `operations_.Remove(id)`: `path = GetPathLocked(id)`; then after remove, `path_to_id_.erase(NormalizePathForDedup(path))`. (If GetPath returns empty for already-removed id, we may need to get path inside the lock before remove.)

5. **Rename / Move**
   - FileIndex currently delegates to `operations_.Rename(id, new_name)` and `operations_.Move(id, new_parent_id)`. Those call `path_operations_.InsertPath(id, new_full_path, ...)`, so the path row is updated.
   - FileIndex must update the map: e.g. in a wrapper or by having the operations return old/new path, or by reading path before and after. Concretely: get `old_path = GetPathLocked(id)` before calling Rename/Move; call operations_; get `new_path = GetPathLocked(id)`; `path_to_id_.erase(NormalizePathForDedup(old_path))`; `path_to_id_[NormalizePathForDedup(new_path)] = id`.

6. **Clear()**
   - In `FileIndex::Clear()` (or equivalent), add `path_to_id_.clear()` (and ensure PathStorage/storage clear order is unchanged).

7. **RecomputeAllPaths()**
   - After `path_recomputer_.RecomputeAllPaths()` returns, rebuild the map: for each id in storage (or for each id that has a path), `path_to_id_[NormalizePathForDedup(GetPathLocked(id))] = id`. This keeps the map in sync after a full path rebuild.

8. **InsertPathLocked(id, path, is_directory)** (existing API)
   - No change to semantics. Used for “set/refresh path for this id” (PathRecomputer, Rename, Move). Do **not** insert into path_to_id_ here; path_to_id_ is only updated in InsertPathUnderLock (new path) and in Rename/Move/RecomputeAllPaths.

9. **Tests**
   - InsertPath twice with same path → second is no-op (same id, one row in PathStorage).
   - InsertPaths batch with duplicate path → only one entry.
   - Remove then re-insert same path → new id, one entry.
   - Rename then insert old path again → one entry (new id or reused id depending on desired semantics; typically “reuse” only when path already in index at insert time).

10. **Platform behavior**
   - Windows: case-insensitive path comparison is standard; normalize to lowercase for map key.
   - macOS: default FS is case-insensitive (APFS); document whether we normalize case.
   - Linux: usually case-sensitive; no case normalization.

11. **Optional cleanup: remove display-side deduplication**
   - Once Option B is implemented and tested, the path deduplication in `MergeAndConvertToSearchResults` (SearchWorker.cpp) and in `ConsumePendingStreamingBatches` (SearchController.cpp) is redundant. Removing it simplifies the code (no `seen_paths` set or second pass in MergeAndConvertToSearchResults; no cross-batch filter when appending to partialResults). If removed, add a one-line comment referencing this plan and Option B so future readers know why there is no dedup there.

---

## Summary table

| Option | Where | Pros | Cons |
|--------|--------|------|------|
| A – Crawler only | FolderCrawler | Localized, no index change | Only fixes crawl; other producers can still duplicate |
| B – Index only | FileIndex | One path = one entry for all producers; saves memory | More invasive; normalization and map maintenance |
| C – A + B | Both | Defense in depth; fewer no-op inserts | Extra code; A optional once B exists |
| D – Scan at insert | Index | — | O(n) per insert; rejected |

**Recommended:** B (index-side idempotent insert). Once B is in place, display deduplication (SearchWorker.cpp, SearchController.cpp) is redundant and may be removed to simplify; optionally keep as safety net. Optionally add A later.
