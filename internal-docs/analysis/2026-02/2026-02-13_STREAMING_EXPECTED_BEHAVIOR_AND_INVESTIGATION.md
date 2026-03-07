# Streaming Search Results – Expected Behavior and Investigation

**Date:** 2026-02-13  
**Purpose:** Document expected streaming behavior for large result sets and investigate causes of non-deterministic results  
**References:**  
- `docs/design/STREAMING_SEARCH_RESULTS_DESIGN.md`  
- Streaming implementation plan and research (maintainer-only; see project maintainer for details)

---

## Expected Behavior When There Is a Large Number of Files

### Design Intent (from Implementation Plan and Design Docs)

1. **Process all results** – No limit during search. Every matching file from the index is collected.
2. **Stream incrementally** – Results are batched (default 500 items, 50 ms interval) and delivered to the UI as they arrive.
3. **Same result set as non-streaming** – Streaming and non-streaming should produce the **same set of results** (same count, same files). Only the **timing of display** differs.
4. **Deterministic order** – After the index-order fix, futures are processed in fixed order (0, 1, 2, …), so result order should match non-streaming.

### Data Flow for Large Result Sets

```
ParallelSearchEngine (launches N futures, one per thread/chunk)
        ↓
SearchWorker::ProcessStreamingSearchFutures (consumes futures in index order 0,1,2,...)
        ↓
StreamingResultsCollector::AddResult (batches into current_batch_, flushes to pending_batches_)
        ↓
PollResults (once per frame) → GetAllPendingBatches() → MergeAndConvertToSearchResults
        ↓
state.partialResults (accumulated; on complete → UpdateSearchResults → searchResults)
```

### Key Invariants (from Design Doc)

- **Producer:** Only SearchWorker calls `AddResult`, `MarkSearchComplete`, `SetError`.
- **Consumer:** Only UI thread calls `GetAllPendingBatches`, `IsSearchComplete`, etc.
- **Batching:** Flush when `current_batch_.size() >= batch_size` (500) or `notification_interval_ms` (50 ms) elapsed.
- **Final handoff:** When `IsSearchComplete()`, `FinalizeStreamingSearchComplete` uses `state.partialResults` (accumulated from all `ConsumePendingStreamingBatches` calls). The last batch is flushed in `MarkSearchComplete` before `search_complete_` is set, so it is included in the next `GetAllPendingBatches()`.

### What Should Be Identical Between Runs

For the **same search** (same query, same index snapshot):

- **Result count** – Same number of files.
- **Result set** – Same file IDs and paths.
- **Result order** – Same order (after index-order fix).

---

## Potential Causes of Different Results (Investigation)

Since completion order was ruled out, the following areas may explain non-deterministic behavior:

### 1. Lazy Attribute Loading (Size, Mod Time)

**Blind Spot §14 (Index consistency):**  
"Partial results hold references (e.g. IDs) into the FileIndex. During a long search, the index might be updated."

- `MergeAndConvertToSearchResults` creates `SearchResult` with `fileId`, `fullPath`, etc.
- Size and mod time are loaded lazily via `LazyAttributeLoader` when rows enter the visible window.
- **If** the index changes between runs (re-crawl, file deletion), or `GetFileSizeById` / similar fails for some entries (cloud, deleted, etc.), the **displayed** size/mod time can differ even when the underlying result set is the same.
- **Symptom:** Same count and paths, but different displayed sizes or mod times; total size in status bar varies (e.g. 54 GB vs 8 GB vs 63 GB).

### 2. Index Mutation During or Between Searches

- If the index is updated (re-crawl, USN events) **between** two "Search now" clicks, the result set can legitimately differ.
- If the index is mutated **during** a search, the design assumes a shared lock prevents that; if that guarantee is violated, results could be inconsistent.

### 3. Path Pool Migration (ConsumePendingStreamingBatches)

When appending would reallocate `searchResultPathPool`, the code migrates existing `partialResults[].fullPath` (string_views) to a new pool. A bug in this migration could corrupt paths and make results appear different.

### 4. Frame Timing and Batch Consumption

- `PollResults` runs once per frame (~60 FPS).
- If `HasNewBatch` or `IsSearchComplete` is true, we call `ConsumePendingStreamingBatches` and get **all** pending batches in one call.
- `MarkSearchComplete` flushes the last batch **before** setting `search_complete_`, so the last batch should always be in `pending_batches_` when the UI sees completion.
- **Theoretical race:** If the UI never runs `PollResults` after the worker sets `search_complete_` (e.g. window minimized, frame skip), we could miss the last batch. This would be a rare edge case.

### 5. Non-Streaming vs Streaming Conversion Path

- **Non-streaming:** `GetResultsData()` returns all `SearchResultData` at once; `MergeAndConvertToSearchResults` runs once on the full set.
- **Streaming:** `MergeAndConvertToSearchResults` runs **per batch** in `ConsumePendingStreamingBatches`; each batch is converted and appended to `partialResults`.
- Both use the same `MergeAndConvertToSearchResults` and `file_index`. The conversion logic is shared, but the **index state** can differ between batches (e.g. if a background process modifies the index). This could affect lazy-loaded attributes.

### 6. Load Balancing and Chunk Assignment

- **Static:** Fixed chunks per thread → deterministic.
- **Hybrid/Dynamic:** Threads compete for dynamic chunks via atomic `fetch_add`. Chunk assignment varies by run, but the **union** of all chunks should cover the full index with no overlap.
- If there is a bug (overlap, gap, or double-count), result count or set could differ. This would require a code review of the load-balancing logic.

---

## Recommended Next Steps

1. **Clarify "different results":**
   - Different **count** (e.g. 1000 vs 1005)?
   - Different **paths** (different files)?
   - Different **order** (same files, different order)?
   - Different **display** (e.g. size, mod time) with same paths?

2. **Add diagnostic logging (temporary):**
   - Log total result count at streaming completion.
   - Log first/last few file IDs or paths.
   - Compare streaming vs non-streaming for the same search.

3. **Reproduce with Static strategy:**
   - Set `load_balancing_strategy = "static"` and rerun. If results become deterministic, the issue may be in Hybrid/Dynamic chunk assignment.

4. **Verify path pool migration:**
   - Review `ConsumePendingStreamingBatches` path pool logic for correctness.
   - Add assertions or checks for path validity after migration.

5. **Check index stability:**
   - Ensure no index mutation during search (shared lock).
   - Check if re-crawl or USN processing runs concurrently with search.

---

## Summary

**Expected behavior:** Streaming should produce the same result set and count as non-streaming for a given search and index state. Results appear incrementally (batches of ~500 every ~50 ms) instead of all at once.

**Most likely causes of "different results" if completion order is ruled out:**

1. **Lazy size/mod time loading** – Same result set, different displayed attributes (e.g. total size).
2. **Index changes between runs** – Legitimate differences if the index is updated.
3. **Path pool migration bug** – Could corrupt paths during batch append.
4. **Load balancing bug** – Overlap or gap in chunk coverage (less likely if non-streaming is stable).

Narrowing down whether the difference is in count, set, order, or display will guide the next fix.
