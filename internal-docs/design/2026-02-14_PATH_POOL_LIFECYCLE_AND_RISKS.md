# Path pool lifecycle and similar risks

**Date:** 2026-02-14

This document describes the lifecycle of `GuiState::searchResultPathPool` and lists risks (use-after-free, dangling `string_view`) and how they are avoided. Use it when touching search results, streaming, or the results table.

## Invariant

- `SearchResult.fullPath` is a `std::string_view` into `GuiState::searchResultPathPool`.
- **Never** clear or reallocate the pool while any `SearchResult` in `searchResults`, `partialResults`, `filteredResults`, or `sizeFilteredResults` still references it. Otherwise UI (e.g. `ResultsTable::Render`) can read freed/invalid memory → use-after-free / SIGSEGV.

## Code paths that touch the pool

| Location | Action | How we avoid dangling views |
|----------|--------|-----------------------------|
| **ConsumePendingStreamingBatches** | Append to pool for new batch | If `pool.capacity() < pool.size() + pool_bytes_needed`, migrate to a new pool and update all `partialResults[].fullPath` before appending. |
| **Non-streaming PollResults** | Clear pool, then merge new results | Clear `searchResults` and filter caches first, then `pool.clear()`, then `MergeAndConvertToSearchResults`, then assign new results. |
| **ClearSearchResults** | Clear everything | Clear `searchResults`, `partialResults`, filter caches, then `pool.clear()`. |
| **First streaming batch** | Clear pool and partialResults | We clear both together when `!state.showingPartialResults`; no existing views. |
| **UpdateSearchResults** | Replace searchResults (move) | Does not clear the pool; new results already point into the current pool. We call `WaitForAllAttributeLoadingFutures` before replacing. |
| **ApplyStreamingErrorToState** | Move partialResults → searchResults | Pool is unchanged; only ownership of the result vector changes. |

## Attribute loading (ThreadPool) and shutdown

- `StartAttributeLoadingAsync` enqueues tasks that capture references to `results`, `file_index`, `token` (into `GuiState`).
- If the app quits or state is replaced while those tasks run, those references can dangle.
- **Shutdown:** `Application::~Application()` cancels the sort token and drains `attributeLoadingFutures` and `cloudFileLoadingFutures` before any member (e.g. `state_`, `thread_pool_`) is destroyed.
- **Mid-session:** Before replacing results we call `WaitForAllAttributeLoadingFutures(state)` (e.g. in `UpdateSearchResults`, `ClearSearchResults`, non-streaming path).

## Filter caches

- `filteredResults` and `sizeFilteredResults` contain copies of `SearchResult` (same `fullPath` string_views into the pool).
- They are invalidated (cleared and cache flags set false) whenever we replace or clear `searchResults` or the pool, so they are never used with stale views.
- `GetDisplayResults` returns these only when the corresponding cache is valid; otherwise it falls back to `searchResults` or `partialResults`.

## When adding or changing code

1. **Clearing the pool:** Ensure no vector (`searchResults`, `partialResults`, `filteredResults`, `sizeFilteredResults`) still holds `SearchResult` entries that reference the pool. Clear those vectors (and invalidate caches) first, then clear the pool.
2. **Appending to the pool:** If the append might cause reallocation (e.g. `capacity() < size() + needed`), either reserve enough up front or migrate to a new pool and update all existing `SearchResult.fullPath` that pointed into the old pool.
3. **Replacing results:** Wait for attribute-loading futures (and any other async work that captures refs into state) before replacing the result vectors or the pool.
4. **New result vectors or caches:** If they hold `SearchResult` (or any type with a `string_view` into the pool), apply the same rules: don’t clear or reallocate the pool while they reference it; invalidate or rebuild them when the pool or source results change.

## References

- `src/search/SearchController.cpp` – PATH POOL LIFECYCLE comment and implementations.
- `src/search/SearchTypes.h` – `SearchResult.fullPath` as `std::string_view`.
- `src/gui/GuiState.h` – `searchResultPathPool`, `searchResults`, `partialResults`, filter cache fields.
