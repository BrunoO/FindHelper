# Filtering and Sorting Flow Analysis

## Current Program Flow

### 1. Search Generation (Background Thread)
**Location:** `SearchWorker::WorkerThread()` in `src/search/SearchWorker.cpp`

- SearchWorker runs in a background thread
- Generates raw `SearchResult` objects from `FileIndex`
- Results are **unsorted** and **unfiltered**
- Returns results via `GetResults()` when ready

### 2. Result Reception (UI Thread)
**Location:** `SearchController::PollResults()` → `UpdateSearchResults()` in `src/search/SearchController.cpp`

- Polls SearchWorker for new results each frame
- Stores raw results in `state.searchResults`
- Results are still **unsorted** and **unfiltered** at this point
- Sets `state.resultsUpdated = true` when search completes

### 3. Sorting (UI Thread, During Rendering)
**Location:** `ResultsTable::Render()` → `HandleTableSorting()` in `src/ui/ResultsTable.cpp`

- Triggered by ImGui table sort specs (user clicks column header)
- Sorts `state.searchResults` **in-place**
- For Size/Last Modified columns: async attribute loading (futures)
- For other columns: immediate sort
- **Invalidates filter caches** after sorting (order changed)

### 4. Filtering (UI Thread, During Rendering)
**Location:** `ResultsTable::Render()` → `UpdateTimeFilterCacheIfNeeded()` / `UpdateSizeFilterCacheIfNeeded()` in `src/ui/ResultsTable.cpp`

- Filter chain: `searchResults` → `timeFilteredResults` → `sizeFilteredResults`
- Time filter applied first, then size filter on time-filtered results
- Uses cached filtered results (rebuilds when invalid)
- **Deferred for one frame** when results first arrive (to avoid blocking UI)

### 5. Display (UI Thread, During Rendering)
**Location:** `ResultsTable::Render()` in `src/ui/ResultsTable.cpp`

- Selects `display_results_ptr` from filtered/sorted results
- Priority: `sizeFilteredResults` > `timeFilteredResults` > `searchResults`
- Renders table using ImGui

## Current Flow Diagram

```
┌─────────────────────────────────────────────────────────────┐
│ Background Thread (SearchWorker)                           │
│ ┌─────────────────────────────────────────────────────────┐ │
│ │ 1. Generate raw SearchResult vector                      │ │
│ │    - Unsorted                                            │ │
│ │    - Unfiltered                                          │ │
│ └─────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│ UI Thread (SearchController)                                │
│ ┌─────────────────────────────────────────────────────────┐ │
│ │ 2. PollResults() → UpdateSearchResults()                 │ │
│ │    - Store raw results in state.searchResults            │ │
│ │    - Still unsorted, unfiltered                          │ │
│ └─────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│ UI Thread (ResultsTable::Render - Every Frame)              │
│ ┌─────────────────────────────────────────────────────────┐ │
│ │ 3. HandleTableSorting()                                  │ │
│ │    - Sort state.searchResults (in-place)                │ │
│ │    - May be async (for Size/Last Modified)              │ │
│ │    - Invalidates filter caches                          │ │
│ └─────────────────────────────────────────────────────────┘ │
│ ┌─────────────────────────────────────────────────────────┐ │
│ │ 4. UpdateTimeFilterCacheIfNeeded()                       │ │
│ │    - Creates filteredResults from searchResults          │ │
│ └─────────────────────────────────────────────────────────┘ │
│ ┌─────────────────────────────────────────────────────────┐ │
│ │ 5. UpdateSizeFilterCacheIfNeeded()                       │ │
│ │    - Creates sizeFilteredResults from filteredResults    │ │
│ └─────────────────────────────────────────────────────────┘ │
│ ┌─────────────────────────────────────────────────────────┐ │
│ │ 6. Select display_results_ptr                            │ │
│ │    - Choose filtered/sorted results to display          │ │
│ └─────────────────────────────────────────────────────────┘ │
│ ┌─────────────────────────────────────────────────────────┐ │
│ │ 7. Render table                                          │ │
│ └─────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

## Analysis: Where Filtering and Sorting Could Be Moved

### Option A: Move Filtering to SearchWorker (Background Thread)

**Implementation:**
- Apply time and size filters in `SearchWorker::WorkerThread()` before returning results
- Filter results as they're generated (streaming filter)
- Return pre-filtered results to UI

**Benefits:**
1. **Reduced Memory Usage**: Only filtered results stored in `state.searchResults`
2. **Faster UI Rendering**: No filter cache rebuild needed on UI thread
3. **Better Separation of Concerns**: Filtering logic separated from UI rendering
4. **Parallel Processing**: Filtering happens in background thread (doesn't block UI)
5. **Simpler UI Code**: No filter cache management in ResultsTable

**Drawbacks:**
1. **Filter Changes Require Re-search**: Changing filter requires new search (can't filter existing results)
2. **No Incremental Filtering**: Can't show partial results while filtering (must wait for complete)
3. **Tight Coupling**: SearchWorker needs to know about filter types (TimeFilter, SizeFilter)
4. **Filter State Synchronization**: Need to pass filter state to SearchWorker
5. **Less Flexible**: Can't easily change filters without re-searching

**Code Changes Required:**
- Add `TimeFilter` and `SizeFilter` to `SearchParams`
- Implement filtering in `SearchWorker::WorkerThread()`
- Remove filter cache logic from `ResultsTable`
- Update `SearchController` to pass filter state

**Verdict:** ❌ **Not Recommended** - Too inflexible, requires re-search for filter changes

---

### Option B: Move Sorting to SearchWorker (Background Thread)

**Implementation:**
- Sort results in `SearchWorker::WorkerThread()` before returning
- Use default sort (e.g., by filename) or pass sort preferences in `SearchParams`
- Return pre-sorted results to UI

**Benefits:**
1. **Faster UI Rendering**: No sorting needed on UI thread
2. **Parallel Processing**: Sorting happens in background thread
3. **Simpler UI Code**: No sorting logic in ResultsTable
4. **Consistent Initial Sort**: Results always arrive sorted (no unsorted display)

**Drawbacks:**
1. **User Sort Changes Require Re-search**: Changing sort column requires new search
2. **No Dynamic Sorting**: Can't change sort without re-searching
3. **Attribute Loading Complexity**: Size/Last Modified sorting still needs attribute loading (could be done in background)
4. **Less Responsive**: User must wait for complete search to see sorted results
5. **Tight Coupling**: SearchWorker needs to know about sort preferences

**Code Changes Required:**
- Add sort preferences to `SearchParams`
- Implement sorting in `SearchWorker::WorkerThread()`
- Remove sorting logic from `ResultsTable`
- Handle attribute loading in SearchWorker (for Size/Last Modified)

**Verdict:** ❌ **Not Recommended** - Too inflexible, user expects immediate sort changes

---

### Option C: Move Both to SearchController (UI Thread, Before Rendering)

**Implementation:**
- After `UpdateSearchResults()`, apply sorting and filtering in `SearchController::PollResults()`
- Store sorted/filtered results in separate state variables
- ResultsTable just displays pre-processed results

**Benefits:**
1. **Separation of Concerns**: Processing logic separated from rendering
2. **Single Processing Point**: All result processing in one place
3. **Easier Testing**: Can test filtering/sorting independently of UI
4. **Clearer Data Flow**: Results → Process → Display (linear flow)
5. **No Filter Cache Rebuilds**: Process once, display many times

**Drawbacks:**
1. **Still on UI Thread**: Doesn't move work off UI thread (no performance gain)
2. **Frame-Based Processing**: Still happens every frame (if results changed)
3. **Complex State Management**: Need to track when to reprocess
4. **Sorting Still Needs Async**: Size/Last Modified sorting still needs async attribute loading

**Code Changes Required:**
- Move sorting/filtering logic from `ResultsTable` to `SearchController`
- Add processing state tracking
- Update `ResultsTable` to use pre-processed results

**Verdict:** ⚠️ **Partially Recommended** - Better organization, but no performance gain

---

### Option D: Keep Current (In ResultsTable During Rendering)

**Current Implementation:**
- Sorting and filtering happen during `ResultsTable::Render()` (every frame)
- Filter caches are rebuilt when invalid
- Sorting is async for Size/Last Modified columns

**Benefits:**
1. **Maximum Flexibility**: User can change filters/sort without re-searching
2. **Incremental Display**: Can show results as they arrive (unsorted, then sorted)
3. **Lazy Processing**: Only process what's needed for display
4. **Responsive UI**: Filter/sort changes are immediate
5. **No Re-search Required**: Filter/sort existing results without new search

**Drawbacks:**
1. **UI Thread Work**: Filtering/sorting happens on UI thread (can cause frame drops)
2. **Complex Cache Management**: Filter caches need invalidation logic
3. **Tight Coupling**: Processing logic mixed with rendering code
4. **Frame-Based Processing**: Happens every frame (even when not needed)

**Verdict:** ✅ **Recommended** - Best user experience, maximum flexibility

---

## Recommended Approach: Hybrid Solution

### Keep Current Architecture, But Optimize

**Current flow is correct for user experience**, but we can optimize:

1. **Keep Filtering in UI Thread** (current)
   - Allows filter changes without re-search
   - Filter caches are efficient

2. **Keep Sorting in UI Thread** (current)
   - Allows immediate sort changes
   - Async attribute loading already optimizes Size/Last Modified

3. **Optimizations to Consider:**
   - **Lazy Filter Cache Rebuild**: Only rebuild when filter actually changes (not every frame)
   - **Incremental Filtering**: For very large result sets, filter incrementally
   - **Background Filter Processing**: Move filter cache rebuild to background thread (keep UI responsive)
   - **Virtual Scrolling**: Only process visible rows (already done via ImGuiListClipper)

### Specific Optimization: Background Filter Cache Rebuild

**Implementation:**
- Keep filter cache rebuild logic in `ResultsTable`
- Move actual filtering work to background thread (ThreadPool)
- UI shows unfiltered results while filtering happens
- Update display when filtering completes

**Benefits:**
- UI stays responsive during filter operations
- No re-search required (keeps flexibility)
- Better performance for large result sets

**Code Changes:**
- Use ThreadPool for filter cache rebuilds
- Add async filter futures (similar to attribute loading)
- Update display when filtering completes

**Verdict:** ✅ **Recommended Optimization** - Best of both worlds

---

## Summary

| Option | Location | Flexibility | Performance | Complexity | Recommendation |
|--------|----------|-------------|-------------|------------|----------------|
| **A: Filter in SearchWorker** | Background | ❌ Low | ✅ High | ⚠️ Medium | ❌ Not recommended |
| **B: Sort in SearchWorker** | Background | ❌ Low | ✅ High | ⚠️ Medium | ❌ Not recommended |
| **C: Both in SearchController** | UI Thread | ✅ High | ⚠️ Same | ⚠️ Medium | ⚠️ Partial |
| **D: Current (ResultsTable)** | UI Thread | ✅✅ High | ⚠️ Medium | ⚠️ High | ✅ Recommended |
| **E: Hybrid (Optimized)** | UI + Background | ✅✅ High | ✅ High | ⚠️ High | ✅✅ **Best** |

## Conclusion

**Current architecture is correct** - filtering and sorting should remain in the UI layer for maximum flexibility. However, we can optimize by:

1. **Moving filter cache rebuilds to background thread** (async, similar to attribute loading)
2. **Improving cache invalidation logic** (only rebuild when actually needed)
3. **Adding incremental filtering** for very large result sets

This maintains the current user experience (immediate filter/sort changes) while improving performance for large result sets.
