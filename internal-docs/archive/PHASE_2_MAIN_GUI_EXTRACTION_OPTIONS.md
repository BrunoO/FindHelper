# Phase 2: Additional Extraction Options from main_gui.cpp

## Current State

After Step 1.6, `main_gui.cpp` is ~874 lines. Let's analyze what else could be extracted.

---

## 1. SearchResult Helper Functions (High Priority - Cross-Platform)

**Location**: Lines 185-427 (~243 lines)

**Functions**:
- `EnsureDisplayStringsPopulated()` - Used by UIRenderer
- `EnsureModTimeLoaded()` - Used by UIRenderer  
- `UpdateTimeFilterCacheIfNeeded()` - Used by UIRenderer
- `SortSearchResults()` - Used by UIRenderer
- `PrefetchAndFormatSortData()` - Used internally by SortSearchResults
- `ApplyTimeFilter()` - Used internally by UpdateTimeFilterCacheIfNeeded

**Current Issue**: These functions are declared as `extern` in `UIRenderer.cpp`, creating a dependency from `UIRenderer` to `main_gui.cpp`.

**Extraction Option**: Create `SearchResultUtils.h/cpp`

**Benefits**:
- ✅ Eliminates `extern` declarations in `UIRenderer.cpp`
- ✅ Better code organization - SearchResult utilities in one place
- ✅ Cross-platform (except dependency on `CalculateCutoffTime`)
- ✅ Easier to test independently
- ✅ Can be reused by macOS without duplication

**Dependencies**:
- Uses `g_thread_pool` global (for `PrefetchAndFormatSortData`)
- Uses `CalculateCutoffTime()` which is Windows-specific

**Solution**:
1. Create `SearchResultUtils.h/cpp`
2. Move all SearchResult helper functions there
3. Pass `ThreadPool` as parameter instead of using global
4. Make `CalculateCutoffTime()` a parameter or platform-specific function

**Estimated Effort**: 2-3 hours

---

## 2. Windows-Specific Time Filter Function (Medium Priority)

**Location**: Lines 100-160 (~60 lines)

**Function**: `CalculateCutoffTime(TimeFilter filter)`

**Current Issue**: Windows-specific (uses `SYSTEMTIME`, `FILETIME`, Windows APIs), but used by cross-platform `ApplyTimeFilter()`.

**Extraction Options**:

### Option A: Platform-Specific Implementation Files
- Create `TimeFilterUtils_win.cpp` with Windows implementation
- Create `TimeFilterUtils_mac.cpp` with macOS implementation (using `std::chrono`)
- Add platform-specific function to `TimeFilterUtils.h`

**Benefits**:
- ✅ Cross-platform time filtering
- ✅ Clean separation of platform code
- ✅ macOS can have proper time filtering

**Estimated Effort**: 2-3 hours

### Option B: Keep in main_gui.cpp (Windows-only)
- Keep `CalculateCutoffTime()` in `main_gui.cpp`
- Pass as parameter to `ApplyTimeFilter()` or make it a function pointer

**Benefits**:
- ✅ Minimal changes
- ⚠️ Still Windows-specific

**Estimated Effort**: 1 hour

---

## 3. Windows-Specific Font Functions (Low Priority - Keep in main_gui.cpp)

**Location**: Lines 745-870 (~125 lines)

**Functions**:
- `ConfigureFontsFromSettings()` - Windows-specific (Windows font paths)
- `ApplyFontSettings()` - Windows-specific (DirectX invalidation)

**Recommendation**: **Keep in `main_gui.cpp`**

**Rationale**:
- These are Windows-specific and tightly coupled to DirectX
- They're only called during initialization
- Moving them would require platform-specific abstraction that adds complexity
- The code is already well-contained

**Alternative**: If we want to support macOS fonts later, we could create:
- `FontUtils_win.cpp` / `FontUtils_mac.cpp`
- But this is not needed for Phase 2

---

## 4. Windows-Specific Helper Functions (Low Priority)

**Location**: Lines 69-78, 89-98

**Functions**:
- `GetUserProfilePath()` - Windows-specific (USERPROFILE env var)
- `SetQuickFilter()` - Cross-platform but only used in `main_gui.cpp`

**Recommendation**: **Keep in `main_gui.cpp`**

**Rationale**:
- `GetUserProfilePath()` is only used for Windows-specific paths
- `SetQuickFilter()` is a small helper only used locally
- Not worth extracting for minimal benefit

---

## 5. Obsolete Forward Declarations (Quick Fix)

**Location**: Lines 83-87

**Issue**: Old forward declarations for functions now in `TimeFilterUtils.h`:
```cpp
const char *TimeFilterToString(TimeFilter filter);
TimeFilter TimeFilterFromString(const std::string &value);
void ApplySavedSearchToGuiState(const SavedSearch &saved, GuiState &state);
```

**Fix**: **Remove these declarations** - they're now in `TimeFilterUtils.h`

**Estimated Effort**: 5 minutes

---

## 6. Global State (Consider for Future)

**Location**: Lines 162-178

**Globals**:
- `g_file_index` - FileIndex
- `g_thread_pool` - ThreadPool
- `g_monitor` - UsnMonitor (unique_ptr)
- `g_show_metrics`, `g_show_settings` - UI toggles
- `g_last_window_width`, `g_last_window_height` - Window size tracking

**Recommendation**: **Keep as-is for now**

**Rationale**:
- These are application-level singletons
- Extracting would require significant refactoring
- Not a priority for Phase 2
- Could be addressed in a future refactoring phase

---

## Recommended Extraction Plan

### Priority 1: SearchResult Utils (High Value, Cross-Platform)

**Create**: `SearchResultUtils.h/cpp`

**Move**:
- `EnsureDisplayStringsPopulated()`
- `EnsureModTimeLoaded()`
- `UpdateTimeFilterCacheIfNeeded()`
- `SortSearchResults()`
- `PrefetchAndFormatSortData()`
- `ApplyTimeFilter()`

**Changes**:
- Remove `extern` declarations from `UIRenderer.cpp`
- Pass `ThreadPool` as parameter instead of using global
- Make `CalculateCutoffTime()` a parameter or extract to platform-specific function

**Benefits**:
- ✅ Eliminates ~243 lines from `main_gui.cpp`
- ✅ Removes `extern` dependencies
- ✅ Better code organization
- ✅ Cross-platform reusable
- ✅ Easier to test

**Estimated Effort**: 2-3 hours

---

### Priority 2: Cross-Platform Time Filter Calculation (Medium Value)

**Create**: `TimeFilterUtils_win.cpp` and `TimeFilterUtils_mac.cpp`

**Move**: `CalculateCutoffTime()` with platform-specific implementations

**Benefits**:
- ✅ Enables proper time filtering on macOS
- ✅ Clean platform separation
- ✅ Completes cross-platform time filter support

**Estimated Effort**: 2-3 hours

---

### Priority 3: Quick Fix - Remove Obsolete Declarations

**Remove**: Lines 83-87 (obsolete forward declarations)

**Estimated Effort**: 5 minutes

---

## Summary

### What to Extract (Recommended)

1. **SearchResult Utils** → `SearchResultUtils.h/cpp` (~243 lines)
   - High value, cross-platform, eliminates extern dependencies
   - **Estimated**: 2-3 hours

2. **Time Filter Calculation** → Platform-specific files (~60 lines)
   - Medium value, enables macOS time filtering
   - **Estimated**: 2-3 hours

3. **Remove obsolete declarations** → Quick fix
   - **Estimated**: 5 minutes

### What to Keep in main_gui.cpp

1. **Windows-specific font functions** - Tightly coupled to DirectX
2. **Windows-specific helper functions** - Only used locally
3. **Global state** - Application-level singletons
4. **Main function** - Platform-specific initialization and main loop

---

## Code Reduction Estimate

### Before
- `main_gui.cpp`: ~874 lines

### After Priority 1 (SearchResult Utils)
- `main_gui.cpp`: ~631 lines (-243 lines)
- `SearchResultUtils.cpp`: +243 lines (new file)

### After Priority 2 (Time Filter)
- `main_gui.cpp`: ~571 lines (-60 lines)
- `TimeFilterUtils_win.cpp`: +60 lines (new file)
- `TimeFilterUtils_mac.cpp`: +60 lines (new file)

**Total reduction**: ~303 lines from `main_gui.cpp`

---

## Implementation Order

1. **Quick Fix**: Remove obsolete forward declarations (5 min)
2. **Priority 1**: Extract SearchResult Utils (2-3 hours)
3. **Priority 2**: Extract Time Filter Calculation (2-3 hours)

**Total Estimated Time**: 4-6 hours

---

## Conclusion

The most valuable extraction is **SearchResult Utils**, which:
- Eliminates ~243 lines
- Removes `extern` dependencies
- Is cross-platform reusable
- Improves code organization

This should be done **before Step 2** (macOS search UI) to ensure macOS can use these utilities without duplication.

