# Path Pool Implementation Review – 2026-02-14

## Purpose

Verify that Option A (path pool) from the performance review delivers the expected benefits and does not introduce performance penalties.

---

## Expected Benefits (from PERFORMANCE_REVIEW_2026-02-14.md)

1. **Fewer allocations on result materialization:** One (or few) pool buffer allocation(s) instead of N heap allocations for path strings when pushing results to the UI.
2. **Less allocator pressure:** Reduces fragmentation and time in malloc during “search complete” or batch application.
3. **Faster UI responsiveness:** Less time on the UI thread (or the thread that pushes results) in allocator code when applying 100k–500k results.

---

## What the Implementation Delivers

### Benefits achieved

| Area | Before | After |
|------|--------|--------|
| **UI thread when applying results** | N `std::string` allocations (one per result) for `SearchResult.fullPath`. | One `std::vector<char>` (path pool); each `SearchResult.fullPath` is a `std::string_view` into the pool. Pool is reserved once in `MergeAndConvertToSearchResults` to avoid repeated reallocations. |
| **Result storage in GuiState** | N separate path allocations in `searchResults` / `partialResults`. | One contiguous pool; views only. |
| **Lifecycle** | Each result owned its path string. | Pool cleared with `ClearSearchResults` / when replacing results; no dangling views. |

So the implementation **does** provide the intended benefit: the thread that applies results to the UI (main thread) no longer does N path allocations; it does one pool buffer (with a single reserve) and N non-owning views.

### Worker hot path (intentional tradeoff)

- **SearchResultData** still carries `std::string fullPath`; it is filled in **ParallelSearchEngine.h** in `CreateResultData` via `data.fullPath.assign(path)`.
- So the **worker** still performs N path allocations (one per matching result). The performance review’s “Alternatively” explicitly allows this: *“keep producing SearchResultData with a path (for minimal change) but … merge all path bytes into one pool … when pushing into GuiState”*.
- Benefit is on the **consumer** side (UI / main thread), not on the producer side. Removing N allocations from the worker would require a larger change (e.g. worker appends to a shared buffer and passes views).

---

## Performance Penalties Checked

### 1. Pool reallocations in `MergeAndConvertToSearchResults`

- **Risk:** Appending to `pool` in a loop with `insert`/`push_back` can cause multiple reallocations and copy the whole buffer each time.
- **Change:** A **reserve** was added: first pass over `data` sums `datum.fullPath.size() + 1` into `pool_bytes_needed`, then `pool.reserve(pool.size() + pool_bytes_needed)` before the loop. At most one reallocation when the current capacity is exceeded.
- **Cost:** One extra O(N) pass over `data` (read path sizes only). This is negligible compared to avoiding O(log N) or more reallocations and large memcpy.

### 2. `pendingDeletions.find(result.fullPath)` with `string_view`

- **Type:** `GuiState::pendingDeletions` is `std::set<std::string, std::less<>>`. `std::less<>` is transparent, so `find(std::string_view)` does **not** construct a temporary `std::string`; no extra allocation per row.
- **Conclusion:** No performance penalty.

### 3. Platform / UI use of `result.fullPath`

- **OpenFileDefault, OpenParentFolder, CopyPathToClipboard, ShowContextMenu, StartFileDragDrop:** All take `std::string_view`; implementations copy to `std::string` only where the OS API requires it. No extra allocations in the hot path beyond what the OS call needs.
- **RenderPathTooltip / GetRowFilenameCstr:** Pool stores each path with a trailing `\0`, so `result.fullPath.data()` is valid for C APIs and tooltips. No penalty.

### 4. Streaming path

- **ConsumePendingStreamingBatches:** Clears pool only on the **first** batch (`!state.showingPartialResults`); later batches **append** to the same pool and to `partialResults`. Correct and no redundant clears.
- **StreamingResultsCollector:** Keeps `SearchResultData` (with `std::string fullPath`) in `all_results_` and in batches. This matches the “minimal change” design; the extra copies are in the worker/collector, not introduced by the path pool. Merge step still converts to one pool + views for the UI.

### 5. GetEntry per result

- **MergeAndConvertToSearchResults** calls `file_index.GetEntry(datum.id)` for each result (for size/time). Same as the previous conversion logic; no new cost.

---

## Summary

| Question | Answer |
|----------|--------|
| Does it provide the expected benefits? | **Yes.** UI-side result materialization uses one pool buffer (with one reserve) and N `string_view`s instead of N path allocations. |
| Are there performance penalties? | **No.** Reserve avoids pool reallocations; transparent comparator and `string_view` APIs avoid extra allocations; lifecycle is correct. |
| Worker still does N path allocations? | **Yes,** by design (minimal-change approach). Further reduction would require changing the worker to build a shared buffer and pass views. |

---

## Recommendation

- **Keep** the current path pool implementation; it matches the performance review’s Option A and delivers the intended UI-side benefit without adding performance penalties.
- **Optional follow-up:** If profiling later shows worker-side allocator pressure, consider a second phase where the worker (or engine) appends paths into a single buffer and produces `SearchResultData` with views or (offset, length) instead of `std::string fullPath`.
