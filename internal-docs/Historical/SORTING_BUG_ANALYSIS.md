# Sorting Bug Analysis: When and How It Was Introduced

## Summary

The sorting bug was introduced in **commit 533d328** (Dec 27, 2025) when async attribute loading was added. The bug occurred because `display_results` was set **before** sorting completed, causing the UI to display unsorted data even though `state.searchResults` was correctly sorted.

## Timeline

### 1. Initial Implementation: Time Filtering (Dec 16, 2025)
**Commit**: `28be3c8` - "feat: Add caching mechanism for time-filtered search results"

- Introduced `display_results` reference that points to either `state.searchResults` or `state.filteredResults`
- `display_results` was set **before** sorting logic
- At this time, sorting was **synchronous** and happened **before** `display_results` was used
- **Status**: ✅ **Working correctly** - sorting happened before display

**Code pattern at this time**:
```cpp
// Sort happens synchronously
SortSearchResults(state.searchResults, ...);
UpdateTimeFilterCacheIfNeeded(state, file_index);
const std::vector<SearchResult> &display_results = ...; // Points to sorted data
```

### 2. Bug Introduction: Async Attribute Loading (Dec 27, 2025)
**Commit**: `533d328` - "Improve status bar granularity and fix async attribute loading memory leak"

- Introduced async attribute loading for Size/Last Modified columns
- Added `StartSortAttributeLoading()` and `CheckSortAttributeLoadingAndSort()`
- **Problem**: `display_results` was still set **before** async sorting completed
- Sorting now happened **asynchronously** and completed **after** `display_results` was already set
- **Status**: ❌ **Bug introduced** - sorting happens after display_results is set

**Code pattern that introduced the bug**:
```cpp
// display_results set BEFORE sorting
UpdateTimeFilterCacheIfNeeded(state, file_index);
const std::vector<SearchResult> &display_results = ...; // Points to UNSORTED data

// Then async sorting happens
if (!state.attributeLoadingFutures.empty()) {
  if (CheckSortAttributeLoadingAndSort(...)) { // Sorts state.searchResults
    UpdateTimeFilterCacheIfNeeded(state, file_index); // Rebuilds filteredResults
  }
}
// But display_results still points to OLD unsorted data!
```

### 3. Bug Fix (Current)
**Commit**: `b1e32f4` - "Fix sorting by moving display_results assignment after sorting"

- Moved `display_results` assignment to **after** all sorting logic completes
- Ensures `display_results` always points to sorted data
- **Status**: ✅ **Fixed**

## Root Cause Analysis

### Why the Bug Occurred

1. **Original Design**: When time filtering was added, sorting was synchronous
   - Sorting happened → then `display_results` was set → UI displayed sorted data
   - This worked because sorting completed before `display_results` was used

2. **Async Introduction**: When async loading was added, the flow changed
   - `display_results` was set → then async sorting started → sorting completed later
   - But `display_results` was already pointing to unsorted data
   - Even after sorting completed, `display_results` still referenced the old unsorted vector

3. **Why It Wasn't Caught**: 
   - The bug only affected Size/Last Modified columns (columns 2 and 3)
   - Other columns sorted synchronously and worked correctly
   - The async sorting was completing, but the UI wasn't reflecting it

### Technical Details

**The Problem**:
- `display_results` is a `const std::vector<SearchResult> &` (reference)
- References are set once and don't change
- When `display_results` is set to point to `state.searchResults`, it captures that reference
- Even if `state.searchResults` is sorted later, `display_results` still points to the same vector
- However, if `state.filteredResults` is rebuilt, `display_results` would need to be re-evaluated

**The Fix**:
- Move `display_results` assignment to **after** all sorting completes
- This ensures `display_results` points to the sorted data
- `UpdateTimeFilterCacheIfNeeded()` is called after sorting to rebuild `filteredResults`
- `display_results` then correctly references either sorted `searchResults` or rebuilt `filteredResults`

## Impact

- **Affected Features**: Sorting by Size (column 2) and Last Modified (column 3)
- **User Experience**: Sorting appeared to not work - clicking column headers didn't change the order
- **Severity**: Medium - functionality broken but no crashes or data loss
- **Duration**: Bug existed from Dec 27, 2025 until fixed (approximately 1-2 days)

## Lessons Learned

1. **Reference Lifetime**: When using references, ensure they're set **after** all mutations complete
2. **Async Operations**: When introducing async operations, verify that dependent code (like display references) is updated **after** async completion
3. **Testing**: The bug only affected specific columns, highlighting the need for comprehensive testing of all sorting scenarios

## Related Commits

- `28be3c8` (Dec 16, 2025): Introduced `display_results` - ✅ Working
- `533d328` (Dec 27, 2025): Introduced async loading - ❌ Bug introduced
- `8fb86dc` (Recent): Fixed memory leak in async loading - Still had sorting bug
- `b1e32f4` (Current): Fixed sorting by moving `display_results` assignment - ✅ Fixed

