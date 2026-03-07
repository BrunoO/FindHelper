# Options to Address Post-Processing Map Lookups (GetEntry per Result)

**Date:** 2026-02-06  
**Context:** [2026-02-06_REVIEW_FINDINGS_SUMMARY_AND_PRIORITIES.md](2026-02-06_REVIEW_FINDINGS_SUMMARY_AND_PRIORITIES.md) (lines 16–17)  
**Finding:** Expensive map lookups in result post-processing: `SearchWorker::ConvertSearchResultData` calls `file_index.GetEntry(data.id)` once per result. At 500k+ results this is O(N) hash lookups with cache-miss and hash-computation cost.

**Current flow (simplified):**

- Search produces `SearchResultData` (id, fullPath, isDirectory) from SoA via `CreateResultData` in `ParallelSearchEngine.h`.
- Post-processing in `ConvertSearchResultData` (and `ConvertSearchResultDataVector`) calls `GetEntry(data.id)` to read `fileSize` and `lastModificationTime` (and `IsNotLoaded()` state) from `FileIndexStorage::index_` (hash_map id → FileEntry).
- Purpose: copy already-loaded attributes into `SearchResult` so that sorting by size/date does not trigger lazy loading; set sentinels when not loaded.

**Data layout (relevant):**

- **PathStorage (SoA):** path_storage, path_offsets, path_ids, filename_start, extension_start, is_deleted, is_directory. No size/time.
- **FileIndexStorage:** `index_` = hash_map<uint64_t, FileEntry>. FileEntry has LazyFileSize, LazyFileTime (and name, extension, isDirectory, etc.).

---

## Option 1: Do Nothing

**Description:** Keep the current design: one `GetEntry(id)` per result in `ConvertSearchResultData`.

**Benefits:**

- **Zero effort and zero risk.** No code or design changes.
- **Correct and simple.** Single source of truth for metadata (FileIndexStorage); no duplication or sync issues.
- **Acceptable when N is small.** For typical result counts (hundreds to low thousands), N hash lookups are fast; post-processing is often dominated by path string work and vector growth.
- **No regression risk.** No new code paths, no new invariants.

**Costs:**

- **Scales with result count.** At 500k results, 500k unordered_map lookups; each lookup is O(1) average but has hash cost and potential cache misses.
- **Post-processing can become a measurable bottleneck** for very large result sets (e.g. “show all” or broad filters on large indices).

**When to choose:** Result counts are usually modest, or improving this path is lower priority than other work. Prefer when avoiding risk and churn is more important than shaving post-processing time at 500k+ results.

---

## Option 2: Bulk Metadata API

**Description:** Add a batch API on `FileIndexStorage` (and expose via `FileIndex`/`ISearchableIndex` if needed), e.g. `GetMetadataBatch(ids, out_sizes, out_times)` or `GetEntriesBatch(ids)` returning a vector of (size, time, loaded flags), and use it in `ConvertSearchResultDataVector` so that one call serves the whole result set.

**Benefits:**

- **Single lock scope for the whole batch.** If callers are later required to hold a shared lock when reading the index, one batch call implies one lock acquire/release instead of N. Today `GetEntry` does not take a lock (caller’s responsibility); a batch API can document “hold shared lock for the duration” and still reduce lock traffic if multiple call sites are converted.
- **Same number of lookups, better batching.** Still N hash lookups, but in one place with a single reserve/loop; can improve instruction-cache behavior and make it easier to add metrics (e.g. post-processing time, cache hit rate).
- **Localized change.** Conversion loop becomes: “call GetMetadataBatch once; then loop over results and assign from batch arrays.” No change to SoA or to search-phase code.
- **Moderate effort, low structural risk.** New API + one call site; no change to index layout or lazy loading.

**Costs:**

- **Hash lookups remain random.** Cache locality of the hash table is unchanged; we do not reduce the number of lookups or make them sequential.
- **Extra storage for batch output.** Two (or three) vectors of length N for sizes, times, and possibly “loaded” flags; temporary and released after convert.

**When to choose:** You want a clear, low-risk improvement and are willing to add a small API and one lock scope (or documented locking contract) for the whole convert phase.

---

## Option 3: Add Size/Time to SearchResultData at Match Time

**Description:** Extend `SearchResultData` with optional size and last-modification time (and “not loaded” flags). When building results during search (e.g. in `CreateResultData` or at the point where a match is pushed), perform one `GetEntry(id)` (or a small batch) and fill size/time into `SearchResultData`. Then `ConvertSearchResultData` only copies from `SearchResultData` and no longer calls `GetEntry`.

**Benefits:**

- **Moves lookups from single-threaded post-processing to parallel search phase.** Each worker thread does lookups for its own chunk of results; total lookups still N, but spread across threads and potentially overlapping with other work.
- **Can improve effective cache behavior.** Each thread’s lookups are for ids that were just produced from the same SoA chunk; depending on index layout and access pattern, some reuse may occur.
- **Post-processing becomes a pure copy.** No index access in `ConvertSearchResultData`; simpler and easier to optimize (e.g. vectorization of copy, or merging with path parsing).
- **No change to PathStorage or FileIndexStorage layout.** Only `SearchResultData` and the match-time code path change.

**Costs:**

- **Search phase must see the index.** Strategy/lambda already receives `ISearchableIndex` (or context); we need to pass something that provides metadata (e.g. `GetEntry` or a small batch) and call it when building each `SearchResultData`. SoA view does not currently contain size/time; only the index does.
- **Same N lookups overall.** We do not reduce the total number of hash lookups; we only change when and where they happen.
- **More complex search hot path.** `CreateResultData` (or the push site) becomes stateful with respect to the index; we must be careful not to add lock contention (e.g. one shared lock per lookup could be worse than one batch lock in post-processing).

**When to choose:** You want to shorten the single-threaded convert phase and are willing to touch the parallel search path and `SearchResultData` shape. Best if you can batch lookups per thread (e.g. collect ids for the chunk, then one batch lookup per chunk) to avoid N fine-grained lock acquisitions.

---

## Option 4: Add Metadata Columns to PathStorage SoA

**Description:** Add optional `file_size` and `last_modification_time` (and possibly “loaded” flags) to the SoA in PathStorage (or a parallel SoA aligned by path index). Populate them on insert/update and when lazy loading completes. During search, `CreateResultData` reads size/time from the SoA view so that `SearchResultData` is filled without any hash lookup. `ConvertSearchResultData` then only copies from `SearchResultData`.

**Benefits:**

- **Zero lookups in both search and convert.** Metadata is in the same cache-friendly SoA as path data; one SoA index gives path, id, is_directory, size, time.
- **Scales with result count.** Convert cost is linear in N with sequential or strided access; no hash table at all in the hot path.
- **Aligns with existing SoA design.** PathStorage already uses SoA for performance; extending it with two more columns is conceptually consistent.

**Costs:**

- **Index layout and write path change.** PathStorage must store and update size/time; insert/update paths (e.g. `InsertPath`, `PathRecomputer`) must receive and write this data. FileIndexStorage remains the authority for lazy state; when LazyAttributeLoader updates an entry, it must also update PathStorage (or a shared structure). That requires a way to go from “id” to “path index” (today we have path_index → id via path_ids; the reverse may require a map or a second structure).
- **Duplication and consistency.** Size/time live in both FileEntry (hash map) and SoA; any update (insert, lazy load, rename/move) must keep both in sync. Bugs here can cause wrong sort or display.
- **Higher effort and risk.** Touches PathStorage, FileIndexStorage/IndexOperations, LazyAttributeLoader, and possibly PathRecomputer; medium–large change with non-trivial testing (rebuild index, lazy load, USN updates, etc.).

**When to choose:** You are willing to invest in an index layout change to remove metadata lookups from the hot path entirely and to maintain two copies of size/time under a clear consistency model.

---

## Option 5: Defer Metadata to Display (No Lookups in Convert)

**Description:** In `ConvertSearchResultData`, do not call `GetEntry` at all. Set `fileSize` and `lastModificationTime` on `SearchResult` to sentinel “not loaded” always (or only set from a cheap source if we add one). Rely on existing lazy loading in the UI when the user views or sorts by size/date.

**Benefits:**

- **Removes all N lookups from post-processing.** Convert is minimal: path, id, is_directory, and sentinels; no index access.
- **Very small code change.** Only `ConvertSearchResultData` (and possibly `SearchResult` defaults) change; no new APIs or storage.

**Costs:**

- **Sorting by size or date uses sentinels.** If we sort by size/date before any lazy load, “not loaded” values must be handled (e.g. sort “not loaded” last or first). That may be acceptable (e.g. “sort by size” shows loaded first, then “unknown”).
- **Possible extra lazy loads.** If the UI or sort logic triggers loading for many rows, we might do many lookups later instead of once in convert; total lookups could be similar or higher, but spread over time. If most results are never viewed or sorted by size/date, total work can be lower.
- **Product/UX impact.** Behavior change: “sort by size” immediately after search might show many “—” until attributes load; need to confirm this is acceptable.

**When to choose:** You want the smallest change and can accept that “sort by size/date” right after search uses sentinels and/or triggers lazy loading. Good if large result sets are rarely sorted by size/date or if deferring load is desirable.

---

## Summary Table

| Option | Effort | Risk | Lookups in convert | Index layout change | When to prefer |
|--------|--------|------|--------------------|----------------------|-----------------|
| 1. Do nothing | None | None | N | No | Low priority; avoid churn |
| 2. Bulk metadata API | M (small) | Low | 0 (one batch call) | No | Reduce lock scope, keep design simple |
| 3. Metadata at match time | M | Medium | 0 | No | Shorten single-thread convert; move work to parallel phase |
| 4. SoA metadata columns | L | Medium–High | 0 | Yes | Maximum hot-path performance; willing to refactor index |
| 5. Defer to display | S | Low | 0 | No | Minimal change; accept sort/display semantics |

---

## Recommendation (Neutral)

- **If the goal is to avoid risk and cost:** Option 1 (do nothing) is justified while result counts are typically under ~50k and post-processing time is not a reported bottleneck.
- **If the goal is a low-risk, localized improvement:** Option 2 (bulk API) gives a single lock scope and a clear batch contract without touching SoA or search phase.
- **If the goal is to remove convert-phase lookups without changing index layout:** Option 3 (metadata at match time) or Option 5 (defer to display) are viable; Option 3 keeps “sort by size/date” accurate using already-loaded data; Option 5 minimizes code change but changes sort/display behavior.
- **If the goal is to eliminate metadata lookups from the hot path entirely and invest in layout:** Option 4 (SoA metadata columns) is the only option that removes both lookups and hash table access from search and convert, at the cost of duplication and consistency work.

Benchmarking post-processing time at 100k and 500k results (with and without the bulk API or match-time fill) will quantify the benefit of Options 2 and 3 before committing to Option 4.
