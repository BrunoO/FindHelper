# Performance Review - 2026-02-14

## Executive Summary
- **Health Score**: 8/10
- **Critical Issues**: 0
- **High Issues**: 1 (1 remediated 2026-02-14)
- **Medium Issues**: 1 (1 remediated 2026-02-14)
- **Total Findings**: 5
- **Estimated Remediation Effort**: 10 hours
- **Remediation (2026-02-14):** (1) Option A (path pool) for "String Allocations in Search Hot Path" – UI-side result materialization uses one pool buffer and `std::string_view` per result. (2) ExtensionMatches – per-call heap allocation removed; case-insensitive path uses `std::array<char, 255>` stack buffer and `string_view`. See findings below and `2026-02-14_PATH_POOL_IMPLEMENTATION_REVIEW.md`.

## Findings


### High
1. **String Allocations in Search Hot Path**
   - **Code**: `src/search/SearchWorker.cpp:ConvertSearchResultData` (and upstream `ParallelSearchEngine.h:CreateResultData`)
   - **Debt type**: Performance Review
   - **Risk**: Excessive heap allocations during large result set materialization slows down UI responsiveness.
   - **Suggested fix**: Use a string pool or `std::string_view` where possible to minimize allocations.
   - **Severity**: High
   - **Effort**: 6 hours
   - **Relevance check (2026-02-14):** **KEEP.** `ConvertSearchResultData` already uses `std::string_view` for local offset math and moves `data.fullPath` into `result.fullPath` (no copy). The cost is still one `std::string` per result: each path is allocated in `CreateResultData` (`data.fullPath.assign(path)`) then moved into `SearchResult`. For large result sets this remains N heap allocations. A string pool or storing paths in shared storage with `string_view`/id in `SearchResult` would address it.

   **How to fix (two options):**

   **Option A – Path pool (recommended, preserves current lifecycle).**  
   - **Idea:** One contiguous (or chunked) buffer owns all path characters for the current result set; each `SearchResult` holds a `std::string_view` (or offset + length) into that buffer instead of an owned `std::string fullPath`.  
   - **Where the pool lives:** Next to the result list, e.g. in `GuiState`: add `std::vector<char> searchResultPathPool` (or a small wrapper that appends paths and returns views). When results are set, clear the pool, then for each result append its path (e.g. with a trailing `\0` if you need `.c_str()` later) and store the view (or start index + length) in `SearchResult`.  
   - **SearchResult change:** Replace `std::string fullPath` with either `std::string_view fullPath` (and guarantee the pool outlives `searchResults`/`partialResults`) or `uint32_t path_offset`, `uint32_t path_length` and a way to get the view from the pool (e.g. `GuiState` helper).  
   - **Hot path change:** In `CreateResultData` do **not** call `data.fullPath.assign(path)`; only store a view (e.g. `path_view` from SoA) or (path_ptr, path_len). In the worker, when building the batch to send to the UI, append each path into a single buffer and build `SearchResult` with views into that buffer. Alternatively, keep producing `SearchResultData` with a path (for minimal change) but in the **collector** or when pushing into `GuiState`, merge all path bytes into one pool and replace each `result.fullPath` by a view into the pool before storing in `state.searchResults`.  
   - **UI impact:** All current uses of `result.fullPath` (`.c_str()`, `.data()`, set lookup, `OpenFileDefault`, etc.) must work with a view. For `.c_str()` either ensure the pool stores null-terminated segments or use a small thread-local buffer when you need a null-terminated copy (some call sites already use a buffer for the filename column). For set lookups (`pendingDeletions.find(result.fullPath)`), use a transparent comparator for `string_view` (or compare view to view).  
   - **Lifecycle:** When clearing or replacing search results, clear the path pool at the same time so no view is left dangling.

   **Option B – Path from index (fileId-only + `GetPathView`).**  
   - **Idea:** `SearchResult` does not store the path; it stores only `fileId`. When the UI needs the path, call `file_index.GetPathAccessor().GetPathView(result.fileId)` and use that `std::string_view` (or copy to `std::string` only when calling APIs that require a null-terminated path).  
   - **Pros:** No path allocations in the result set.  
   - **Cons:** (1) The index must not be rebuilt or cleared while results are visible, or all path views become invalid. (2) Every place that today uses `result.fullPath` must get the path from the index (or a cached view), so you need to pass `FileIndex` (or `PathAccessor`) into the UI code that renders/uses results. (3) `SearchResultUtils.cpp` already uses `GetPathView(result.fileId)` in one path; the rest of the UI would need to be updated similarly.  
   - **Use this only if** you can guarantee index stability while results are shown and are willing to thread `FileIndex`/path accessor through the UI.

   **Recommended:** Option A (path pool) keeps the current “results are self-contained and stable even if the index is rebuilt” behavior while replacing N per-result path allocations with one (or few) pool buffer(s).

   **Concrete benefit (High):** For a search that returns **N** results (e.g. 100k–500k), today you do **N** heap allocations for path strings (each with its own capacity and allocator metadata). With a path pool you do **1** (or a small number of) allocation(s) for the pool buffer. That reduces allocator pressure, fragmentation, and time in the allocator during result materialization; the UI thread or worker that pushes results spends less time in malloc/new and can become responsive sooner. On large result sets this can shave hundreds of milliseconds off the time until the first results appear or the "search complete" state is applied, and reduces risk of stalls when the system allocator is under load.

   **Remediation (2026-02-14) – Option A implemented:**  
   - **GuiState:** Added `searchResultPathPool` (`std::vector<char>`); cleared when results are cleared or replaced.  
   - **SearchResult:** `fullPath` is now `std::string_view` into the pool; `GetFilename()` / `GetExtension()` use string_view API.  
   - **Worker/collector:** Produce `SearchResultData` (id, fullPath string, isDirectory); conversion to `SearchResult` happens on the main thread in `MergeAndConvertToSearchResults(state.searchResultPathPool, data, file_index)`, which appends each path to the pool (with trailing `\0`) and builds results with views. Pool is reserved before the append loop to avoid reallocations.  
   - **Platform/UI:** `OpenFileDefault`, `OpenParentFolder`, `CopyPathToClipboard`, `ShowContextMenu`, `StartFileDragDrop` accept `std::string_view`; `pendingDeletions` uses `std::less<>` so `find(string_view)` does not allocate.  
   - **Tradeoff:** Worker still allocates one `std::string` per result in `CreateResultData` (minimal-change approach); benefit is on the UI thread when applying results.  
   - **Review:** See `docs/review/2026-02-14-v1/2026-02-14_PATH_POOL_IMPLEMENTATION_REVIEW.md` for benefits vs. penalties check.

### Medium
1. **Hotspot in `ExtensionMatches` and `GetExtensionView`**
   - **Code**: `src/search/ParallelSearchEngine.h` (GetExtensionView, GetPathLength, ProcessChunkRange), `src/search/SearchPatternUtils.h` (ExtensionMatches)
   - **Debt type**: Performance Review
   - **Risk**: Redundant allocations and path length calculations in inner loops.
   - **Suggested fix**: Hoist `path_len` calculations and use transparent lookups.
   - **Severity**: Medium
   - **Effort**: 3 hours
   - **Relevance check (2026-02-14):** **KEEP (with corrections).** The review location was wrong: the hotspot is in **ParallelSearchEngine.h** and **SearchPatternUtils.h**, not PathStorage.h (PathStorage.h only defines the SoA layout). **path_len:** Already computed once per iteration via `GetPathLength(soaView, i, storage_size)` (offset subtraction, no strlen); it cannot be hoisted out of the loop because it is per-item. **Transparent lookups:** `ExtensionSet` is already `std::set<std::string, std::less<void>>`, so `find(string_view)` is already transparent and does not allocate. **Remaining issue (now fixed):** In `ExtensionMatches` (SearchPatternUtils.h), when `case_sensitive` is false the code previously allocated a `std::string lower_buf` on every call—one heap allocation per path in the hot loop when extension filter is used with case-insensitive matching. See Remediation below.
   - **How to fix (applied):** In `ExtensionMatches`, replace the per-call `std::string lower_buf` with a stack buffer (e.g. `std::array<char, 256>`) for the lowercased extension. Copy and lowercase into that buffer, then `std::string_view lower_view(buf.data(), ext_view.size())` and `extension_set.find(lower_view)`. This removes heap allocation from the hot path; extensions longer than 255 chars are already rejected above. (Implemented with `std::array<char, 255>` in SearchPatternUtils.h.)

   **Concrete benefit:** Only applies when the user has an **extension filter** and **case-insensitive** search. In that case, for every path scanned (e.g. 1M paths) the current code does one heap allocation in `ExtensionMatches` for the lowercased extension. Replacing that with a stack buffer removes up to **1M allocations per search** in the worst case (e.g. "*.txt" filter, case-insensitive, 1M paths). That reduces allocator pressure and can noticeably shorten search time on large indexes when extension filter is used; no change when no extension filter or when case-sensitive.

   **Remediation (2026-02-14):** In `SearchPatternUtils.h::ExtensionMatches`, the per-call `std::string lower_buf` was replaced with a stack buffer: `std::array<char, k_max_ext_len> lower_buf` (k_max_ext_len = 255). Extensions longer than 255 chars return false; otherwise the extension is lowercased into the buffer and `std::string_view lower_view(lower_buf.data(), ext_view.size())` is used with `extension_set.find(lower_view)`. No heap allocation in the case-insensitive hot path.

## Quick Wins
- ~~Optimize `ConvertSearchResultData` allocations (path pool).~~ **Done (2026-02-14):** Option A path pool implemented; UI-side materialization uses one pool + views.
- ~~Remove per-call heap allocation in `ExtensionMatches` (use stack buffer for lowercased extension).~~ **Done:** ExtensionMatches uses `std::array<char, 255>` stack buffer and `string_view` for case-insensitive lookup.

## Recommended Actions
1. Profile search hot path with large datasets.
2. Optimize SoA layout access patterns.
