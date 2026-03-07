# Full Result Materialization - Implementation Plan

## Overview

This document outlines a plan to implement virtualized result lists on top of the existing partial-results streaming pipeline. The goal is to replace the current approach of storing all `SearchResult` objects in memory for the final result set with a more memory-efficient solution that stores only file IDs and loads data on-demand, while keeping the current streaming behaviour for responsive UI.

### Current Status (2026-02-02)

- **Partial results streaming is implemented**:
  - `StreamingResultsCollector` batches `SearchResult` objects and exposes them to the UI as partial batches.
  - `GuiState` tracks `partialResults`, `resultsComplete`, `resultsBatchNumber`, and `showingPartialResults` to distinguish streaming vs final states.
  - `SearchController::PollResults()` consumes streaming batches, updates `partialResults`, and on completion moves them into `searchResults`.
  - `SearchResultsService::GetDisplayResults()` returns `partialResults` while streaming (`!resultsComplete && showingPartialResults`), and falls back to filtered/final `searchResults` afterwards.
- **Final result set is still fully materialized**:
  - `StreamingResultsCollector::all_results_` and `GuiState::searchResults` still hold a complete `std::vector<SearchResult>` for the final result set.
  - Filter caches (`filteredResults`, `sizeFilteredResults`) and sort operations still operate on fully materialized `SearchResult` vectors.
- **This plan remains focused on removing full materialization for the final result set**:
  - Streaming has solved **time-to-first-result** and perceived responsiveness.
  - The remaining problem is **memory usage and sort/filter cost** for very large result sets, which virtualized, ID-based storage is intended to address.

## Current State Analysis

### Current Architecture

**Storage:**
- All `SearchResult` objects stored in `std::vector<SearchResult>` in `GuiState`
- Each `SearchResult` contains:
  - `std::string fullPath` (~100-200 bytes average)
  - `std::string_view filename` (points into fullPath)
  - `std::string_view extension` (points into fullPath)
  - `uint64_t fileId`
  - `bool isDirectory`
  - Mutable cached fields: `fileSize`, `fileSizeDisplay`, `lastModificationTime`, `lastModificationDisplay`, `truncatedPathDisplay`
- Multiple result vectors: `searchResults`, `filteredResults`, `sizeFilteredResults`

**Memory Cost:**
- For 1 million results: ~100-200 MB (just for fullPath strings)
- Additional overhead: display strings, cached attributes
- Total: **200-400 MB for 1M results**

**Performance Issues:**
- Slow initial materialization (all paths built upfront)
- Slow sorting (must move entire SearchResult objects)
- Slow filtering (must copy entire SearchResult objects)
- High memory pressure for large result sets

### Current Data Flow

**Non-streaming (full materialization) path:**

1. **Search Phase**: `SearchWorker` collects `SearchResultData` (IDs + paths).
2. **Post-Process Phase**: Converts to `SearchResult` objects with full paths.
3. **UI Update**: All results stored in `GuiState::searchResults`.
4. **Filtering**: Creates `filteredResults` and `sizeFilteredResults` copies.
5. **Rendering**: ImGui displays all visible rows (uses `ImGuiListClipper` for virtualization).

**Streaming path (current when `streamPartialResults` is enabled):**

1. `SearchWorker` configures a `StreamingResultsCollector` and feeds `SearchResult` objects into it as search futures complete.
2. `SearchController::PollResults()` calls `collector->GetAllPendingBatches()` once per frame and appends them to `GuiState::partialResults`, marking `showingPartialResults = true` and `resultsComplete = false`.
3. `SearchResultsService::GetDisplayResults()` returns `partialResults` while streaming is in progress, so the results table renders from the incremental set.
4. When `collector->IsSearchComplete()` becomes true, `FinalizeStreamingSearchComplete()` moves `partialResults` into `searchResults` and marks `resultsComplete = true`, after which sorting and filtering run on the full `searchResults` vector as before.

**Key Insight**: ImGui already uses `ImGuiListClipper` for rendering virtualization, and streaming has eliminated the long “wait-for-all-results” delay. However, we still materialize all final `SearchResult` objects upfront in memory; this plan focuses on replacing that final full materialization with ID-based, lazily loaded storage.

---

## Proposed Architecture

### Core Concept: ID-Based Storage with Lazy Loading

Instead of storing full `SearchResult` objects, store only:
- **File IDs** (`uint64_t`) - minimal memory footprint
- **Sort order** - maintain sorted indices
- **Filter state** - track which IDs pass filters

Load `SearchResult` data on-demand when:
- Row is visible in UI (within viewport)
- Sorting requires comparison
- User interacts with row (hover, click)

### New Data Structures

```cpp
// Virtualized result list - stores only IDs
class VirtualizedResults {
public:
  // Core storage: just file IDs
  std::vector<uint64_t> file_ids_;
  
  // Sort order (indices into file_ids_)
  std::vector<size_t> sorted_indices_;
  
  // Filter state (bitmap or set of indices)
  std::vector<bool> filter_mask_;  // Or use std::bitset for memory efficiency
  
  // Cache for visible rows (LRU cache)
  mutable std::unordered_map<uint64_t, SearchResult> visible_cache_;
  mutable size_t cache_size_limit_ = 1000;  // Cache ~1000 visible results
  
  // Get SearchResult for a specific file ID (loads on-demand)
  SearchResult GetResult(size_t index) const;
  
  // Get visible results for viewport (loads on-demand)
  std::vector<SearchResult> GetVisibleResults(size_t start, size_t count) const;
  
  // Sort by column (only sorts indices, not full objects)
  void Sort(FileIndex& file_index, SortColumn column, SortDirection direction);
  
  // Apply filters (updates filter_mask_)
  void ApplyFilters(FileIndex& file_index, const FilterState& filters);
};
```

### Modified GuiState

```cpp
struct GuiState {
  // Replace: std::vector<SearchResult> searchResults;
  // With:
  VirtualizedResults searchResults;
  
  // Filtered results become views into searchResults (no copying)
  // std::vector<SearchResult> filteredResults;  // REMOVED
  // std::vector<SearchResult> sizeFilteredResults;  // REMOVED
  
  // Instead: filter masks applied to searchResults
  FilterState activeFilters;
};
```

---

## Implementation Phases

### Phase 1: Core Virtualization Infrastructure (Week 1)

**Goal**: Create `VirtualizedResults` class and basic ID storage

**Tasks:**
1. Create `src/search/VirtualizedResults.h` and `.cpp`
   - Implement ID storage (`std::vector<uint64_t>`)
   - Implement basic `GetResult(uint64_t file_id)` method
   - Load full path from `FileIndex` on-demand
   - Cache loaded results (LRU cache, ~1000 entries)

2. Update `SearchWorker` to store IDs instead of full results
   - Modify `SearchWorker::ExecuteSearch()` to return `std::vector<uint64_t>`
   - Remove `SearchResult` materialization from post-process phase
   - Keep path building logic for cache population

3. Update `SearchController` to use `VirtualizedResults`
   - Replace `std::vector<SearchResult>` with `VirtualizedResults`
   - Update `UpdateSearchResults()` to accept IDs
   - Maintain backward compatibility during transition

**Deliverables:**
- `VirtualizedResults` class with basic functionality
- Search returns IDs only
- Results can be loaded on-demand by ID

**Testing:**
- Unit tests for `VirtualizedResults::GetResult()`
- Verify memory usage reduction (measure before/after)
- Verify search still works correctly

---

### Phase 2: UI Integration - Lazy Loading (Week 2)

**Goal**: Update UI to load results on-demand for visible rows

**Tasks:**
1. Update `ResultsTable::RenderResultsTable()`
   - Replace direct access to `display_results[row]`
   - Use `VirtualizedResults::GetVisibleResults(start, count)`
   - Load results for visible viewport + small buffer (e.g., 100 rows ahead/behind)

2. Implement viewport-based caching
   - Cache visible rows + buffer in `VirtualizedResults`
   - Evict cache entries outside viewport
   - Prefetch rows just outside viewport for smooth scrolling

3. Update result count display
   - Show total count from `VirtualizedResults::Size()`
   - No need to materialize all results for count

**Deliverables:**
- UI loads results on-demand
- Smooth scrolling with prefetching
- Memory usage scales with viewport size, not total results

**Testing:**
- Test with 1M+ results
- Verify smooth scrolling
- Measure memory usage (should be ~10-50 MB instead of 200-400 MB)
- Verify time-to-display-first-result (should be <100ms)

---

### Phase 3: Sorting Optimization (Week 3)

**Goal**: Sort indices instead of full objects

**Tasks:**
1. Implement index-based sorting in `VirtualizedResults`
   - Create `sorted_indices_` vector
   - Sort function compares by loading only needed attributes
   - For size/date sorting: load only size/date, not full path

2. Update `SearchResultUtils::SortSearchResults()`
   - Accept `VirtualizedResults&` instead of `std::vector<SearchResult>&`
   - Sort indices, not objects
   - Load full results only for visible rows after sort

3. Optimize comparison functions
   - Load only comparison fields (not full paths)
   - Cache comparison results if needed
   - Use `FileIndex` direct lookups for attributes

**Deliverables:**
- Sorting works on indices (fast)
- Only visible rows materialized after sort
- Memory-efficient sorting for large result sets

**Testing:**
- Test sorting with 1M+ results
- Measure sort time (should be <1s for 1M results)
- Verify memory usage during sort (should stay low)

---

### Phase 4: Filtering Optimization (Week 4)

**Goal**: Replace result copying with filter masks

**Tasks:**
1. Implement filter mask in `VirtualizedResults`
   - Replace `filteredResults` and `sizeFilteredResults` vectors
   - Use `std::vector<bool>` or `std::bitset` for filter state
   - Apply filters to mask, not by copying results

2. Update filter cache logic
   - `UpdateTimeFilterCacheIfNeeded()` updates mask instead of creating vector
   - `UpdateSizeFilterCacheIfNeeded()` updates mask instead of creating vector
   - Filtered count = count of `true` bits in mask

3. Update UI to use filtered mask
   - `GetVisibleResults()` respects filter mask
   - Display count from mask, not from vector size

**Deliverables:**
- No result copying for filtering
- Filter masks instead of filtered vectors
- Memory usage independent of filter state

**Testing:**
- Test filtering with 1M+ results
- Verify filter performance (should be <100ms)
- Measure memory usage (should not increase with filters)

---

### Phase 5: Advanced Optimizations (Week 5)

**Goal**: Further optimizations and polish

**Tasks:**
1. **Prefetching Strategy**
   - Prefetch attributes for rows just outside viewport
   - Batch attribute loading for multiple rows
   - Use `LazyAttributeLoader` for efficient I/O

2. **Cache Management**
   - Implement LRU cache eviction
   - Tune cache size based on viewport
   - Monitor cache hit rates

3. **Path Building Optimization**
   - Cache built paths in `VirtualizedResults`
   - Reuse path builder for multiple results
   - Consider path string pool for common prefixes

4. **Performance Monitoring**
   - Add metrics for cache hits/misses
   - Track memory usage over time
   - Monitor load times for visible rows

**Deliverables:**
- Optimized prefetching
- Efficient cache management
- Performance metrics

**Testing:**
- Benchmark with various result set sizes
- Measure cache efficiency
- Verify smooth user experience

---

## Technical Details

### Memory Layout Comparison

**Current (1M results):**
```
std::vector<SearchResult> searchResults;  // ~200-400 MB
std::vector<SearchResult> filteredResults; // ~200-400 MB (if filtered)
std::vector<SearchResult> sizeFilteredResults; // ~200-400 MB (if size filtered)
Total: 200-1200 MB (depending on filters)
```

**Proposed (1M results):**
```
std::vector<uint64_t> file_ids_;           // 8 MB (1M * 8 bytes)
std::vector<size_t> sorted_indices_;      // 8 MB (1M * 8 bytes)
std::vector<bool> filter_mask_;           // ~125 KB (1M bits / 8)
std::unordered_map<uint64_t, SearchResult> visible_cache_; // ~10-50 MB (1000 visible rows)
Total: ~26-66 MB (85-95% reduction)
```

### API Changes

**Before:**
```cpp
void UpdateSearchResults(GuiState& state, std::vector<SearchResult>&& results);
const std::vector<SearchResult>& display_results = state.searchResults;
SearchResult result = display_results[row];
```

**After:**
```cpp
void UpdateSearchResults(GuiState& state, std::vector<uint64_t>&& file_ids);
auto visible_results = state.searchResults.GetVisibleResults(start, count);
SearchResult result = state.searchResults.GetResult(file_id);
```

### Backward Compatibility

**Strategy**: Implement both APIs during transition
- Keep old `std::vector<SearchResult>` API temporarily
- Add new `VirtualizedResults` API
- Migrate callers gradually
- Remove old API once migration complete

---

## Risk Mitigation

### Risk 1: UI Responsiveness
**Concern**: Loading results on-demand might cause UI lag  
**Mitigation**:
- Prefetch rows ahead of viewport
- Use background threads for attribute loading
- Show loading indicators for rows being loaded
- Cache aggressively for smooth scrolling

### Risk 2: Sorting Performance
**Concern**: Loading attributes for comparison might be slow  
**Mitigation**:
- Load only comparison fields (not full paths)
- Cache comparison results
- Use `FileIndex` direct lookups (fast)
- Consider pre-sorting during search phase

### Risk 3: Filter Complexity
**Concern**: Filter masks might be complex to maintain  
**Mitigation**:
- Start with simple boolean mask
- Use `std::bitset` for memory efficiency
- Test thoroughly with various filter combinations
- Keep filter logic isolated in `VirtualizedResults`

### Risk 4: Cache Invalidation
**Concern**: Cache might become stale if data changes  
**Mitigation**:
- Invalidate cache on search update
- Use file IDs as cache keys (stable)
- Clear cache when filters change
- Monitor cache hit rates

---

## Testing Strategy

### Unit Tests
- `VirtualizedResults::GetResult()` - loads correct data
- `VirtualizedResults::Sort()` - sorts indices correctly
- `VirtualizedResults::ApplyFilters()` - filter mask correct
- Cache eviction - LRU works correctly

### Integration Tests
- Search with 1M+ results - memory usage <100 MB
- Sorting 1M results - completes in <1s
- Filtering 1M results - completes in <100ms
- Scrolling through 1M results - smooth, no lag

### Performance Benchmarks
- **Memory**: Peak memory usage for 1M results
- **Time to first result**: Should be <100ms
- **Sort time**: Should be <1s for 1M results
- **Filter time**: Should be <100ms for 1M results
- **Scroll performance**: 60 FPS while scrolling

### Regression Tests
- All existing search functionality works
- All existing filter functionality works
- All existing sort functionality works
- UI behavior unchanged (from user perspective)

---

## Success Criteria

### Performance Targets
- **Memory**: <100 MB for 1M results (vs 200-400 MB currently)
- **Time to first result**: <100ms (vs 500ms-2s currently)
- **Sort time**: <1s for 1M results (vs 5-10s currently)
- **Filter time**: <100ms for 1M results (vs 1-2s currently)
- **Scroll FPS**: 60 FPS (smooth scrolling)

### Functional Requirements
- All existing features work
- No user-visible behavior changes
- Backward compatible during transition
- Graceful degradation if cache exhausted

---

## Migration Path

### Step 1: Parallel Implementation
- Implement `VirtualizedResults` alongside existing code
- Add feature flag to switch between implementations
- Test new implementation thoroughly

### Step 2: Gradual Migration
- Migrate one component at a time
- Start with search results storage
- Then migrate filtering
- Finally migrate sorting

### Step 3: Validation
- Run both implementations in parallel
- Compare results (should be identical)
- Monitor performance metrics
- Fix any discrepancies

### Step 4: Cutover
- Enable new implementation by default
- Keep old code for rollback
- Monitor production metrics
- Remove old code after validation period

---

## Estimated Effort

- **Phase 1**: 1 week (core infrastructure)
- **Phase 2**: 1 week (UI integration)
- **Phase 3**: 1 week (sorting optimization)
- **Phase 4**: 1 week (filtering optimization)
- **Phase 5**: 1 week (advanced optimizations)
- **Total**: 5 weeks

**Risk Buffer**: +2 weeks for unexpected issues  
**Total Estimate**: 7 weeks

---

## Dependencies

### External Dependencies
- None (uses existing `FileIndex` and `LazyAttributeLoader`)

### Internal Dependencies
- `FileIndex` - for loading file data by ID
- `LazyAttributeLoader` - for efficient attribute loading
- `PathBuilder` - for building paths on-demand
- `ImGui` - already supports virtualization via `ImGuiListClipper`

### Blocking Issues
- None identified

---

## Future Enhancements

### Potential Optimizations
1. **Path String Pool**: Share common path prefixes
2. **Attribute Prefetching**: Batch load attributes for visible rows
3. **Incremental Sorting**: Sort as results arrive (not all at once)
4. **Multi-level Caching**: Cache at search, filter, and viewport levels
5. **Compression**: Compress stored IDs if needed (unlikely)

### Scalability
- Current design supports 10M+ results
- Memory scales with viewport, not total results
- Can handle even larger result sets with minimal changes

---

## References

- Performance Review: `docs/review/2026-01-12-v1/PERFORMANCE_REVIEW_2026-01-12.md`
- Remaining Work: `docs/plans/optimization/REMAINING_WORK.md`
- Current Implementation: `src/search/SearchWorker.cpp`, `src/ui/ResultsTable.cpp`
- ImGui Virtualization: `ImGuiListClipper` documentation

---

## Appendix: Code Examples

### Example: Loading Result On-Demand

```cpp
SearchResult VirtualizedResults::GetResult(size_t index) const {
  uint64_t file_id = file_ids_[index];
  
  // Check cache first
  if (auto it = visible_cache_.find(file_id); it != visible_cache_.end()) {
    return it->second;
  }
  
  // Load from FileIndex
  SearchResult result;
  result.fileId = file_id;
  
  // Build path on-demand
  result.fullPath = PathBuilder::BuildFullPath(file_id, file_index_);
  
  // Extract filename/extension
  // ... (existing logic)
  
  // Load attributes lazily (set sentinels)
  result.fileSize = kFileSizeNotLoaded;
  result.lastModificationTime = kFileTimeNotLoaded;
  
  // Cache result (with LRU eviction)
  CacheResult(file_id, result);
  
  return result;
}
```

### Example: Sorting Indices

```cpp
void VirtualizedResults::Sort(FileIndex& file_index, SortColumn column, SortDirection dir) {
  // Sort indices, not objects
  std::sort(sorted_indices_.begin(), sorted_indices_.end(),
    [&](size_t a, size_t b) {
      uint64_t id_a = file_ids_[a];
      uint64_t id_b = file_ids_[b];
      
      // Load only comparison fields
      if (column == SortColumn::Size) {
        uint64_t size_a = file_index.GetFileSize(id_a);
        uint64_t size_b = file_index.GetFileSize(id_b);
        return dir == SortDirection::Ascending ? size_a < size_b : size_a > size_b;
      }
      // ... other columns
    });
}
```

---

**Document Status**: Draft for future reference  
**Last Updated**: 2026-01-12  
**Author**: Performance Review Analysis
