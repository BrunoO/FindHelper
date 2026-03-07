# Crash Fix Commit (cd50e32) – Keep or Drop?

**Date:** 2026-01-31  
**Commit:** `cd50e32` – "fix(streaming): prevent crash on second streaming toggle and Metal shutdown"  
**Context:** Commit `d667555` (FEAT2: implement streaming search results and fix identified crashes) **reverted** the logic of cd50e32. Current HEAD (d667555) does not contain the crash-fix behaviour.

---

## What cd50e32 Changed

1. **GetDisplayResults (SearchResultsService):** Only return filter caches when `sizeFilterCacheValid` / `timeFilterCacheValid`. Otherwise return `&state.searchResults` to avoid exposing stale or dangling data.
2. **Invalidate paths:** When replacing or clearing `searchResults`, clear `filteredResults` and `sizeFilteredResults` and set valid flags false (UpdateSearchResults, ClearSearchResults, PollResults streaming paths, ClearInputs).
3. **ResultsTable:** Take `display_results` **after** HandleTableSorting; clamp `selectedRow` to display size; pass `display_results` to HandleDeleteKeyAndPopup so delete targets the visible row.
4. **MetalManager (macOS):** In ShutdownImGui(), call `endEncoding` (and popDebugGroup) on the current render encoder if active before clearing it, to avoid MTLCommandEncoder dealloc without endEncoding (SIGABRT).
5. **SearchController streaming:** Error path: show partial results (move partialResults → searchResults) and invalidate/clear filter caches. Complete path: only call UpdateSearchResults when `!partialResults.empty()`; when complete with 0 results, clear searchResults once and invalidate caches.
6. **SearchWorker:** Drain loop – replace empty catch with logging and SetError (AGENTS document). **SearchWorker.h:** Include order (system before project).

---

## What d667555 Did

- **Reverted** the above behaviour in GuiState, SearchController, SearchResultsService, ResultsTable, MetalManager.
- **Kept** SearchResult refactor (e.g. `fullPath` as `std::string`, GetFilename()/GetExtension() via offsets). So filter caches now hold SearchResults with **owned** strings, not string_views into a shared buffer.
- **Removed** the two docs (STREAMING_IMPLEMENTATION_REVIEW.md, PARTIAL_RESULTS_VISUAL_INDICATION_OPTIONS.md) from the tree.

---

## Assessment

| Fix from cd50e32 | Still needed after d667555? | Reason |
|------------------|-----------------------------|--------|
| GetDisplayResults check valid flags | **Yes** | After replace/clear we invalidate but don’t clear vectors. Without the check, we still return stale filter caches and show wrong (old) results until rebuild. No use-after-free with owned strings, but **correctness** (right data) requires falling back to searchResults when invalid. |
| Clear filter vectors on invalidate | **Yes** | Avoids holding stale entries; aligns with “invalid = don’t use”. |
| ResultsTable: display_results after HandleTableSorting | **Helpful** | With owned strings, pointer-to-vector is not dangling when sort rebuilds; order is still clearer and safer. |
| ResultsTable: clamp selectedRow, delete from display_results | **Yes** | Otherwise delete uses `state.searchResults[selectedRow]` while the table shows filtered/partial results → wrong file can be deleted. |
| MetalManager: endEncoding before nil | **Yes** | d667555 removed it. Without it, SIGABRT on quick filter / shutdown can still happen (encoder dealloc without endEncoding). |
| Streaming: error path show partials + invalidate | **Yes** | Plan 2.1.4: on error, show partial results and error. d667555 reverted that. |
| Streaming: complete with 0 results (no UpdateSearchResults(empty) every frame) | **Yes** | Without the “only update when !partialResults.empty(); else clear once when !resultsComplete” logic, we call UpdateSearchResults(empty) on every frame after completion with 0 results, repeatedly clearing the table. |
| SearchWorker drain catch + include order | **Yes** | AGENTS document and correctness; independent of crash fix. |

**Conclusion:** The **crash-fix behaviour from cd50e32 is still required**. d667555’s SearchResult refactor (owned `fullPath`) removes the original use-after-free from **dangling string_views** in filter caches, but:

- **Correctness:** We must still invalidate and not return stale filter caches (valid flags + fallback to searchResults; clear vectors when invalidating).
- **Streaming:** Error path and “complete with 0 results” logic must be restored.
- **UI:** Delete must use the same source as the table (display_results); selectedRow must be clamped.
- **macOS:** Metal encoder must be ended in ShutdownImGui() or SIGABRT risk remains.

---

## Recommendation

**Keep the fix** – do **not** drop cd50e32’s behaviour.

**Action:** Re-apply the crash-fix logic on top of current HEAD (d667555). That means:

1. **SearchResultsService.cpp:** In GetDisplayResults, only return filter caches when `sizeFilterCacheValid` / `sizeFilterCacheValid`; otherwise return `&state.searchResults`.
2. **SearchController.cpp:** In UpdateSearchResults and ClearSearchResults, invalidate and clear filter caches. In PollResults streaming: error path – show partial results and invalidate/clear caches; complete path – only call UpdateSearchResults when `!state.partialResults.empty()`, else when `!state.resultsComplete` clear searchResults once and invalidate/clear caches.
3. **GuiState.cpp:** In ClearInputs(), invalidate and clear filter caches.
4. **ResultsTable.cpp:** Get display_results **after** HandleTableSorting; clamp selectedRow to display size; pass display_results to HandleDeleteKeyAndPopup and use it for the delete target.
5. **MetalManager.mm:** In ShutdownImGui(), if `current_render_encoder_ != nil`, call popDebugGroup, endEncoding, then set to nil.
6. Optionally re-apply SearchWorker drain catch and include order if not already present.

Do **not** rewrite history to “drop” cd50e32; it is already in the history and the later commit (d667555) reverted its logic. Re-applying the same behaviour on top of d667555 restores the fixes without reverting d667555’s SearchResult or other changes.
