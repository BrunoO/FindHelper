# Implementation Plan: Issue #8 - Empty State Enhancements

## Overview
Enhance the empty state UI to provide better user guidance by implementing:
1. **Recent searches** - Display last 3-5 searches as clickable buttons
2. **Indexed file count** - Show "📁 Indexing 1,234,567 files across your drives"
3. **Example searches** - Display clickable example search patterns

**Status**: 📋 Planning  
**Effort**: M (2-8 hours)  
**Priority**: Medium

## Existing Infrastructure (Leveraged)

This implementation **reuses existing code** to minimize duplication:

✅ **`SavedSearch` struct** (`Settings.h`) - Already has all fields needed (filename, extension, path, foldersOnly, caseSensitive, timeFilter, sizeFilter)

✅ **`ApplySavedSearchToGuiState()`** (`TimeFilterUtils.h/cpp`) - Already exists to restore search parameters to GuiState

✅ **Serialization pattern** (`Settings.cpp`) - Already implemented for `savedSearches` - can copy the same pattern for `recentSearches`

✅ **`SearchParams` to search fields** - Already handled by `GuiState::BuildCurrentSearchParams()` (reverse operation)

**What's new:**
- `recentSearches` vector in `AppSettings` (reuses `SavedSearch` struct)
- `RecordRecentSearch()` helper to convert `SearchParams` → `SavedSearch` and add to recent list
- UI rendering in empty state (new, but uses existing `ApplySavedSearchToGuiState()`)

---

## Current State

### Empty State Location
- **File**: `ui/ResultsTable.cpp`
- **Method**: `ResultsTable::RenderPlaceholder(const GuiState &state)`
- **Called from**: `Application.cpp:389` when `!state_.searchActive && state_.searchResults.empty()`
- **Current behavior**: Shows centered text "Enter a search term above to begin searching."

### Available Context
From `Application.cpp`, we have access to:
- `GuiState &state_`
- `FileIndex *file_index_` (for file count)
- `SearchController &search_controller_` (for triggering searches)
- `SearchWorker &search_worker_` (for search execution)
- `AppSettings *settings_` (for persistent storage)

---

## Implementation Details

### 1. Recent Searches (Last 3-5 searches)

#### Requirements
- Track the last 3-5 completed searches
- Display as clickable buttons in the empty state
- Clicking a button should restore that search and execute it
- Persist across application restarts (store in `AppSettings`)

#### Data Structure
**Leverage existing `SavedSearch` struct** - No new struct needed!

Add to `AppSettings` in `Settings.h`:
```cpp
struct AppSettings {
  // ... existing fields ...
  std::vector<SavedSearch> savedSearches;  // User-saved presets (with names)
  std::vector<SavedSearch> recentSearches;  // Auto-tracked recent searches (max 5, names optional)
};
```

**Key difference from `savedSearches`**:
- `savedSearches`: User manually saves with names (requires `name` field)
- `recentSearches`: Automatically tracked, names are optional (can be empty or auto-generated for display)

#### Implementation Steps

1. **Add recentSearches to AppSettings**
   - Add `std::vector<SavedSearch> recentSearches;` to `AppSettings` in `Settings.h`
   - **Reuse existing `SavedSearch` struct** - no new struct needed!

2. **Add serialization for recentSearches**
   - **Leverage existing pattern** from `savedSearches` serialization in `Settings.cpp`
   - Add `recentSearches` array handling in `LoadSettings()` (similar to lines 254-314)
   - Add `recentSearches` array handling in `SaveSettings()` (similar to lines 359-378)
   - **Modify validation**: Allow empty `name` for recent searches (unlike savedSearches which requires name)

3. **Add RecentSearch tracking**
   - Add method `void RecordRecentSearch(const SearchParams &params, AppSettings &settings)` 
     - Convert `SearchParams` to `SavedSearch` (auto-generate display name from params)
     - Add to front of `settings.recentSearches`
     - Limit to 5 most recent (remove oldest when adding 6th)
     - Call `SaveSettings(settings)` to persist
   - Call this method when a search completes successfully (in `SearchController::PollResults`)

4. **Update RenderPlaceholder signature**
   - Change from: `RenderPlaceholder(const GuiState &state)`
   - To: `RenderPlaceholder(GuiState &state, SearchController &search_controller, SearchWorker &search_worker, AppSettings &settings)`
   - Update call site in `Application.cpp`

5. **Render recent search buttons**
   - Display up to 5 recent searches as buttons
   - Format: Show search pattern (e.g., "*.cpp" or "main.cpp" or "*.cpp in C:\\")
   - **Leverage existing `ApplySavedSearchToGuiState()`** from `TimeFilterUtils.h` to restore search
   - On click: Call `ApplySavedSearchToGuiState(recent, state)` then `SearchController::TriggerManualSearch()`

#### UI Layout
```
Recent Searches:
[*.cpp] [main.cpp] [*.h in C:\] [re:^test] [folders only]
```

---

### 2. Indexed File Count Display

#### Requirements
- Display total indexed file count in the empty state
- Format: "📁 Indexing 1,234,567 files across your drives"
- Use `FileIndex::Size()` or `UsnMonitor::GetIndexedFileCount()` to get count
- Format number with thousand separators (e.g., 1,234,567)

#### Implementation Steps

1. **Update RenderPlaceholder signature**
   - Add parameter: `const FileIndex &file_index` or `size_t indexed_file_count`
   - Get count from `file_index_->Size()` or `monitor_->GetIndexedFileCount()`

2. **Format number with separators**
   - Create helper function `FormatNumberWithSeparators(size_t count)` in `ResultsTable.cpp`
   - Use locale-aware formatting or manual formatting (e.g., "1,234,567")

3. **Render file count**
   - Display below the main placeholder text
   - Use slightly dimmed text color (similar to placeholder)
   - Icon: 📁 (emoji or text)

#### UI Layout
```
Enter a search term above to begin searching.

📁 Indexing 1,234,567 files across your drives
```

---

### 3. Example Searches

#### Requirements
- Display 3-5 example search patterns as clickable buttons
- Examples should demonstrate different search capabilities:
  - Wildcard: `*.cpp`
  - Regex: `re:^main\.(cpp|h)$`
  - Extension filter: `*.h`
  - Path search: `*.cpp in C:\Projects`
- Clicking an example should populate the search fields and trigger a search

#### Implementation Steps

1. **Define example searches**
   - Create static array of example search configurations
   - Each example contains: `filename`, `extension`, `path`, `foldersOnly`, `caseSensitive`

2. **Render example buttons**
   - Display as clickable buttons below file count
   - Format: "Try: *.cpp" or "Try: re:^main\\.(cpp|h)$"
   - Use button style with subtle background

3. **Handle example clicks**
   - On click: Populate `GuiState` fields with example values
   - Trigger search via `SearchController::TriggerManualSearch`

#### Example Searches
```cpp
struct ExampleSearch {
  const char* displayText;  // "Try: *.cpp"
  const char* filename;      // "*.cpp" or "re:^main\\.(cpp|h)$"
  const char* extension;     // "" or "cpp"
  const char* path;          // "" or "C:\\Projects"
  bool foldersOnly = false;
  bool caseSensitive = false;
};

static const ExampleSearch kExampleSearches[] = {
  {"Try: *.cpp", "*.cpp", "", "", false, false},
  {"Try: re:^main\\.(cpp|h)$", "re:^main\\.(cpp|h)$", "", "", false, false},
  {"Try: *.h", "", "h", "", false, false},
  {"Try: folders only", "", "", "", true, false},
};
```

#### UI Layout
```
Example Searches:
[Try: *.cpp] [Try: re:^main\\.(cpp|h)$] [Try: *.h] [Try: folders only]
```

---

## Complete UI Layout

```
                    [Vertical spacing]
        Enter a search term above to begin searching.
                    [Vertical spacing]
        📁 Indexing 1,234,567 files across your drives
                    [Vertical spacing]
                    Recent Searches:
        [*.cpp] [main.cpp] [*.h in C:\] [re:^test]
                    [Vertical spacing]
                    Example Searches:
        [Try: *.cpp] [Try: re:^main\\.(cpp|h)$] [Try: *.h]
                    [Vertical spacing]
```

---

## File Changes Summary

### New Files
- None (all changes in existing files)

### Modified Files

1. **Settings.h**
   - Add `recentSearches` vector to `AppSettings` (reuse existing `SavedSearch` struct)

2. **Settings.cpp**
   - **Leverage existing serialization pattern** from `savedSearches`
   - Add `recentSearches` array handling in `LoadSettings()` (copy pattern from lines 254-314)
   - Add `recentSearches` array handling in `SaveSettings()` (copy pattern from lines 359-378)
   - **Modify validation**: Allow empty `name` for recent searches (unlike savedSearches)

3. **GuiState.h** (or create helper in SearchController/ApplicationLogic)
   - Add helper function: `void RecordRecentSearch(const SearchParams &params, AppSettings &settings)`
   - Convert `SearchParams` to `SavedSearch` and add to `settings.recentSearches`

4. **SearchController.cpp** (or ApplicationLogic.cpp)
   - Call `RecordRecentSearch()` when search completes successfully
   - Only record if search returned results (optional: or if user explicitly triggered)
   - Need access to `AppSettings` - may need to pass as parameter or store reference

6. **ui/ResultsTable.h**
   - Update `RenderPlaceholder` signature:
     ```cpp
     static void RenderPlaceholder(
         GuiState &state,
         SearchController &search_controller,
         SearchWorker &search_worker,
         const FileIndex &file_index,
         const AppSettings &settings);
     ```

7. **ui/ResultsTable.cpp**
   - Update `RenderPlaceholder` implementation:
     - Add file count display
     - Add recent searches section
     - Add example searches section
     - Add helper: `FormatNumberWithSeparators(size_t count)`
     - Handle button clicks: **Use existing `ApplySavedSearchToGuiState()`** from `TimeFilterUtils.h`
     - Then call `SearchController::TriggerManualSearch()` to execute

8. **Application.cpp**
   - Update call to `RenderPlaceholder` with new parameters
   - Pass `file_index_`, `search_controller_`, `search_worker_`, `*settings_`

---

## Implementation Order

1. **Phase 1: File Count Display** (Simplest, no state management)
   - Update `RenderPlaceholder` signature
   - Add file count display
   - Test with different file counts

2. **Phase 2: Example Searches** (Static, no persistence)
   - Define example searches
   - Add example buttons to UI
   - Implement click handlers
   - Test search triggering

3. **Phase 3: Recent Searches** (Simplified - leverages existing infrastructure)
   - Add `recentSearches` vector to `AppSettings` (reuse `SavedSearch` struct)
   - Add serialization to `Settings.cpp` (copy existing `savedSearches` pattern)
   - Add `RecordRecentSearch()` helper function
   - Call from `SearchController` when search completes
   - Add recent search buttons to UI
   - **Use existing `ApplySavedSearchToGuiState()`** to restore searches
   - Test persistence across restarts

---

## Testing Checklist

- [ ] File count displays correctly with formatted numbers (1,234,567)
- [ ] File count updates when index changes
- [ ] Example searches populate fields correctly
- [ ] Example searches trigger searches when clicked
- [ ] Recent searches are recorded after successful searches
- [ ] Recent searches are limited to 5 most recent
- [ ] Recent searches persist across application restarts
- [ ] Recent search buttons restore search parameters correctly
- [ ] Recent search buttons trigger searches when clicked
- [ ] Empty state layout is visually appealing and centered
- [ ] All buttons are properly disabled during index building
- [ ] UI works correctly on Windows, macOS, and Linux

---

## Edge Cases

1. **No recent searches**: Hide "Recent Searches" section if empty
2. **Index building**: Disable all buttons during index building
3. **Zero files indexed**: Show "Indexing 0 files" (shouldn't happen, but handle gracefully)
4. **Very large numbers**: Format correctly (e.g., 1,234,567,890)
5. **Invalid recent search data**: Handle gracefully on load (skip invalid entries)
6. **Search fails**: Don't record recent search if search fails (optional)

---

## Future Enhancements (Out of Scope)

- Search history with timestamps
- Search favorites/bookmarks
- Search templates
- Recent searches with result counts
- Clear recent searches button
- Export/import search history

---

## Notes

- **Leverages existing infrastructure**:
  - Reuses `SavedSearch` struct (no new struct needed)
  - Reuses `ApplySavedSearchToGuiState()` function from `TimeFilterUtils.h`
  - Reuses existing serialization pattern from `savedSearches`
- All UI changes follow ImGui immediate mode paradigm
- All state changes must be thread-safe (GuiState is accessed from UI thread)
- Recent searches are stored in `settings.json` (same file as other settings)
- File count uses existing `FileIndex::Size()` or `UsnMonitor::GetIndexedFileCount()`
- Example searches are static (not user-configurable in this phase)
- **Key difference**: `recentSearches` allows empty `name` field (unlike `savedSearches` which requires name)

