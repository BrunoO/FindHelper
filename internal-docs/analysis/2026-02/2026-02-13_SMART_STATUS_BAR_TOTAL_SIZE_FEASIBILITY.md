# Smart Status Bar (Total Size) – Feasibility Study

**Date:** 2026-02-13  
**Goal:** Add total size of displayed/filtered files to the status bar: `"Displayed: 50 (2.5 GB)"` instead of `"Displayed: 50"`.

---

## Executive Summary

**Feasible:** Yes, with a caching strategy.  
**Performance risk:** Low if implemented correctly; high if computed every frame.  
**Recommendation:** Implement with cached total size, invalidated when displayed results change. Omit during streaming.

---

## Current Architecture

### Status Bar Display Logic (`StatusBar.cpp`)

- **Streaming:** `"Displayed: N (no filters, no sort yet)"` – uses `state.partialResults.size()`
- **With filters:** `"Displayed: N (filtered from M)"` – uses `state.sizeFilteredCount` or `state.filteredCount`
- **No filters:** `"Displayed: N"` – uses `state.searchResults.size()`

### Data Flow

- `SearchResultsService::GetDisplayResults(state)` returns the vector to display:
  - Streaming: `&state.partialResults`
  - Size filter active: `&state.sizeFilteredResults`
  - Time filter active: `&state.filteredResults`
  - Otherwise: `&state.searchResults`

### File Size Availability

- `SearchResult` has `mutable uint64_t fileSize` (default `kFileSizeNotLoaded`).
- Sizes are lazy-loaded via `FileIndex::GetFileSizeById(id)`:
  - On Windows: MFT metadata or filesystem (can be cached).
  - On macOS/Linux: filesystem.
- Sizes are loaded when:
  - User sorts by Size column
  - User applies a size filter (Tiny, Small, etc.)
- Otherwise, most results have `fileSize == kFileSizeNotLoaded`.

---

## Performance Analysis

### What Would Be Expensive

1. **Per-frame iteration:** Summing sizes every frame over 1,000+ results would add noticeable cost.
2. **Per-frame lazy loading:** Calling `GetFileSizeById` for each unloaded result every frame would trigger many disk lookups.
3. **Streaming:** Loading sizes for all partial results during streaming would block the UI and add I/O.

### What Is Acceptable

1. **One-time computation** when the displayed set changes (new search, filter change, etc.).
2. **Cached total** in `GuiState`, invalidated when results or filters change.
3. **Reuse of existing work:** When size filter is active, `UpdateSizeFilterCacheIfNeeded` already iterates and loads sizes; total can be accumulated in the same pass.

---

## Implementation Strategy

### 1. Add Cached Total Size to GuiState

```cpp
// In GuiState.h
uint64_t displayedTotalSizeBytes = 0;   // Total size of displayed results
bool displayedTotalSizeValid = false;   // Cache validity
```

### 2. Invalidation Points

Invalidate `displayedTotalSizeValid` when:

- `UpdateSearchResults` / `ClearSearchResults` (new search, results replaced)
- `timeFilterCacheValid = false` or `sizeFilterCacheValid = false`
- `deferFilterCacheRebuild` is set
- Streaming: when `partialResults` changes (or simply never compute during streaming)

### 3. Computation Strategy

**Option A – Piggyback on filter cache rebuild (recommended)**

- In `UpdateSizeFilterCacheIfNeeded`: when iterating and calling `EnsureSizeLoaded`, accumulate `total += result.fileSize` (skip directories and failed loads).
- In `UpdateTimeFilterCacheIfNeeded`: add size loading and accumulation (or defer to Option B for no-filter case).
- For no-filter case (`searchResults`): add a dedicated `ComputeDisplayedTotalSize(state, file_index)` that iterates once, loads unloaded sizes, and sums. Call only when `!displayedTotalSizeValid` and not streaming.

**Option B – Dedicated computation function**

- `SearchResultUtils::UpdateDisplayedTotalSizeIfNeeded(state, file_index)`:
  - If streaming → skip (no size shown).
  - If `displayedTotalSizeValid` → return.
  - Get display vector via `GetDisplayResults(state)`.
  - Iterate, call `EnsureSizeLoaded` for each, sum. Set `displayedTotalSizeBytes`, `displayedTotalSizeValid = true`.
- Call from `StatusBar::RenderCenterGroup` (or from `SearchResultsService::UpdateFilterCaches`).

### 4. Streaming Behavior

**Recommendation:** Do not show total size during streaming.

- Rationale: Partial results typically have unloaded sizes; loading them would block or add I/O.
- Keep: `"Displayed: N (no filters, no sort yet)"` without size.
- Add size only when `state.resultsComplete` and display set is stable.

### 5. Formatting

- Reuse `FormatMemory()` from `StringUtils.h` (already used for memory display).
- Display: `"Displayed: 50 (2.5 GB)"` or `"Displayed: 50 (2.5 GB) (filtered from 200)"`.

---

## Edge Cases

| Case | Behavior |
|------|----------|
| Streaming | No size shown |
| All directories | Show `"0 B"` or `"Displayed: N"` without size |
| Size load failures | Sum only successfully loaded sizes; treat `kFileSizeFailed` as 0 |
| Empty results | `"Displayed: 0"` (no size) |
| Very large result set (10k+) | One-time O(N) pass; consider background computation later if needed |

---

## Cost Estimate

| Scenario | Cost |
|----------|------|
| Size filter active | **Minimal** – sizes already loaded during filter pass; add ~1 line to accumulate sum |
| Time filter only | **Low** – one pass over `filteredResults`, load sizes, sum |
| No filter | **Low–Medium** – one pass over `searchResults`, load sizes, sum. First-time load may hit disk for many files |
| Subsequent frames | **Zero** – use cached `displayedTotalSizeBytes` |

---

## Recommendation

### Preferred Approach: **Option B (Dedicated Function) + Piggyback for Size Filter**

| Aspect | Choice | Rationale |
|--------|--------|-----------|
| **Computation** | Dedicated `UpdateDisplayedTotalSizeIfNeeded()` | Single responsibility; easier to test and reason about. Keeps filter logic separate from aggregation. |
| **Size filter path** | Piggyback in `UpdateSizeFilterCacheIfNeeded` | Sizes are already loaded there; add one accumulator. Avoids a second full pass. |
| **Other paths** | Dedicated function | Time filter and no-filter cases need size loading anyway; one function handles both cleanly. |
| **Streaming** | Omit size | Avoids I/O and complexity; size appears when search completes. |
| **Invocation** | From `SearchResultsService::UpdateFilterCaches` | Central place where filter caches are rebuilt; total size is conceptually part of "display metadata." |

### Implementation Order

1. Add `displayedTotalSizeBytes` and `displayedTotalSizeValid` to `GuiState`.
2. Add invalidation at all cache-invalidation points.
3. Implement `UpdateDisplayedTotalSizeIfNeeded` in `SearchResultUtils`.
4. In `UpdateSizeFilterCacheIfNeeded`, accumulate total during the existing loop (optimization).
5. Call the new function from `UpdateFilterCaches` (after filter caches, so `GetDisplayResults` returns the right vector).
6. Update `StatusBar::RenderCenterGroup` to show size when valid.

### What to Avoid

- **Per-frame computation** – Always use the cache; never sum in the render path.
- **Streaming size** – Do not load sizes for partial results.
- **Duplicate logic** – Reuse `EnsureSizeLoaded` and `FormatMemory`; do not reimplement.

---

## Conclusion

- **Feasible:** Yes.
- **Performance:** Safe if total is cached and recomputed only when the displayed set changes.
- **Streaming:** Omit size during streaming to avoid extra I/O and complexity.
- **Effort:** Small–medium (GuiState fields, invalidation, computation, StatusBar formatting).

---

## References

- `src/ui/StatusBar.cpp` – `RenderCenterGroup`, Displayed count logic
- `src/search/SearchResultsService.cpp` – `GetDisplayResults`
- `src/search/SearchResultUtils.cpp` – `EnsureSizeLoaded`, `UpdateSizeFilterCacheIfNeeded`
- `src/search/SearchTypes.h` – `SearchResult.fileSize`
- `src/utils/StringUtils.h` – `FormatMemory()`
