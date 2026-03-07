# Date Range Filter Feature - Implementation Plan

**Date:** 2026-01-17  
**Feature:** User-selectable date range filter for search results  
**Status:** Planning

## Executive Summary

This document outlines the plan for adding a date range picker feature that allows users to select custom start and end dates as a new way to filter search results by modification time. This feature will complement the existing quick time filters (Today, This Week, This Month, This Year, Older) with more precise control.

## Current State Analysis

### Existing Time Filter System

The application currently uses a `TimeFilter` enum with predefined options:

```cpp
enum class TimeFilter {
  None,
  Today,     // Files modified today (since midnight)
  ThisWeek,  // Files modified this week (since Monday)
  ThisMonth, // Files modified this month (since 1st of month)
  ThisYear,  // Files modified this year (since January 1st)
  Older      // Files modified more than 1 year ago
};
```

**Key Components:**
- **State Management:** `GuiState.timeFilter` (enum) and `SavedSearch.timeFilter` (string for persistence)
- **Filter Logic:** `CalculateCutoffTime()` computes FILETIME cutoff values, `ApplyTimeFilter()` filters results
- **UI:** `FilterPanel::RenderTimeQuickFilters()` displays quick filter buttons
- **Active Indicators:** `FilterPanel::RenderActiveFilterIndicators()` shows active filter badges

**Filtering Flow:**
1. User selects a quick filter button → `state.timeFilter` is set
2. `UpdateTimeFilterCacheIfNeeded()` checks if cache needs rebuilding
3. `ApplyTimeFilter()` filters `searchResults` based on FILETIME cutoff
4. Filtered results stored in `state.filteredResults`
5. Results table displays filtered results

### Date/Time Representation

- **Internal Format:** `FILETIME` (Windows time format: 100-nanosecond intervals since 1601-01-01)
- **Platform Support:** 
  - Windows: Uses `SYSTEMTIME`/`FILETIME` APIs
  - macOS/Linux: Uses `std::chrono` with Unix-to-Windows epoch conversion
- **Utilities:** `TimeFilterUtils.cpp` provides platform-specific `CalculateCutoffTime()` implementations

## Requirements

### Functional Requirements

1. **Date Range Selection:**
   - User can select a start date (inclusive)
   - User can select an end date (inclusive)
   - Date selection should use a calendar picker (ImGui date picker widget)
   - Both dates are optional (can filter by start date only, end date only, or both)

2. **Filter Behavior:**
   - Files with modification time >= start date AND <= end date are included
   - If only start date is set: include files modified on or after start date
   - If only end date is set: include files modified on or before end date
   - If both dates are set: include files modified between start and end dates (inclusive)
   - If neither date is set: no date range filtering (equivalent to `TimeFilter::None`)

3. **Integration with Existing Filters:**
   - Date range filter should work alongside quick time filters (mutually exclusive)
   - When date range is active, quick time filter buttons should be disabled or cleared
   - When a quick time filter is selected, date range should be cleared
   - Both filters cannot be active simultaneously

4. **Persistence:**
   - Date range should be saved in `SavedSearch` structure
   - Date range should be included in recent searches
   - Date range should be restored when loading saved searches

5. **UI/UX:**
   - Date range picker should be accessible from the filter panel
   - Active date range should be displayed as a badge (similar to other filters)
   - Date range should be clearable via badge or explicit clear button
   - Date format should be locale-appropriate (e.g., "2026-01-17" or "Jan 17, 2026")

### Non-Functional Requirements

1. **Performance:**
   - Date range filtering should use the same caching mechanism as quick time filters
   - No performance degradation compared to existing time filters
   - Efficient FILETIME comparison (already optimized)

2. **Cross-Platform:**
   - Date picker must work on Windows, macOS, and Linux
   - Date/time conversion must handle timezone correctly (local time)
   - Consistent behavior across platforms

3. **Code Quality:**
   - Follow existing code patterns and conventions
   - Maintain separation of concerns (UI, state, filtering logic)
   - Add unit tests for date range filtering logic
   - Follow C++17 standards (no newer features)

## Design Decisions

### 1. Date Range Storage

**Option A: Extend `TimeFilter` enum** (❌ Rejected)
- Pros: Minimal changes to existing code
- Cons: Breaks enum semantics (not a single value), complicates serialization

**Option B: Separate date range fields in `GuiState`** (✅ Selected)
- Pros: Clear separation, easy to understand, doesn't break existing code
- Cons: Requires additional state management

**Implementation:**
```cpp
// In GuiState.h
struct DateRange {
  bool isActive = false;
  FILETIME startDate = {0, 0};  // Sentinel {0, 0} means not set
  FILETIME endDate = {0, 0};    // Sentinel {0, 0} means not set
};

class GuiState {
  // ... existing fields ...
  TimeFilter timeFilter = TimeFilter::None;
  DateRange dateRangeFilter;  // New field for custom date range
  // ... rest of fields ...
};
```

### 2. Date Picker Widget

**Option A: Use ImGui's built-in date picker** (✅ Selected)
- ImGui provides `ImGui::DatePicker` or similar widgets (check ImGui version)
- If not available, use `ImGui::InputText` with date format validation
- Alternative: Use `ImGui::CalendarPopup` or third-party ImGui extensions

**Option B: Custom date picker widget** (❌ Rejected)
- Too much work, reinventing the wheel

**Implementation:**
- Check ImGui version and available date picker widgets
- If no built-in widget, use `ImGui::InputText` with date format (YYYY-MM-DD)
- Add date validation and conversion utilities

### 3. Filter Application Logic

**Option A: Separate filter function** (✅ Selected)
- Create `ApplyDateRangeFilter()` similar to `ApplyTimeFilter()`
- Integrate with existing filter cache system
- Clear separation of concerns

**Option B: Extend `ApplyTimeFilter()`** (❌ Rejected)
- Would complicate existing function, harder to maintain

**Implementation:**
```cpp
// In SearchResultUtils.cpp
std::vector<SearchResult> ApplyDateRangeFilter(
    const std::vector<SearchResult>& results,
    const DateRange& dateRange,
    const FileIndex& file_index,
    GuiState* state = nullptr,
    ThreadPool* thread_pool = nullptr);
```

### 4. UI Placement

**Option A: Add to `RenderTimeQuickFilters()`** (✅ Selected)
- Place date range picker below or next to quick filter buttons
- Keeps all time-related filters together
- Consistent with existing UI organization

**Option B: Separate section** (❌ Rejected)
- Would fragment the filter UI unnecessarily

**Implementation:**
- Add date range picker in `FilterPanel::RenderTimeQuickFilters()` or new method
- Show two date input fields (Start Date, End Date)
- Add "Clear" button to reset date range

### 5. SavedSearch Persistence

**Option A: Add date range fields to `SavedSearch`** (✅ Selected)
- Store start and end dates as ISO 8601 strings (YYYY-MM-DD)
- Easy to serialize/deserialize in JSON
- Human-readable in settings file

**Implementation:**
```cpp
struct SavedSearch {
  // ... existing fields ...
  std::string timeFilter;  // Keep for backward compatibility
  std::string dateRangeStart;  // ISO 8601 format: "2026-01-17" or empty
  std::string dateRangeEnd;    // ISO 8601 format: "2026-01-17" or empty
};
```

## Implementation Plan

### Phase 1: Core Data Structures and Utilities

**Files to Modify:**
- `src/gui/GuiState.h` - Add `DateRange` struct and field
- `src/core/Settings.h` - Add date range fields to `SavedSearch`
- `src/filters/TimeFilterUtils.h` - Add date range utility functions
- `src/filters/TimeFilterUtils.cpp` - Implement date range utilities

**Tasks:**
1. Define `DateRange` struct in `GuiState.h`
2. Add `dateRangeFilter` field to `GuiState` class
3. Add `dateRangeStart` and `dateRangeEnd` fields to `SavedSearch` struct
4. Create utility functions:
   - `DateRangeFromStrings(const std::string& start, const std::string& end) -> DateRange`
   - `DateRangeToStrings(const DateRange& range) -> std::pair<std::string, std::string>`
   - `IsDateRangeActive(const DateRange& range) -> bool`
   - `ClearDateRange(DateRange& range) -> void`
5. Add FILETIME conversion utilities for date strings (ISO 8601 format)

**Dependencies:**
- Existing `FILETIME` and `FileTimeTypes.h` utilities
- Platform-specific time conversion functions

### Phase 2: Filtering Logic

**Files to Modify:**
- `src/search/SearchResultUtils.h` - Add `ApplyDateRangeFilter()` declaration
- `src/search/SearchResultUtils.cpp` - Implement `ApplyDateRangeFilter()`
- `src/search/SearchResultsService.cpp` - Integrate date range filtering with cache system

**Tasks:**
1. Implement `ApplyDateRangeFilter()` function:
   - Similar structure to `ApplyTimeFilter()`
   - Handle optional start/end dates (sentinel values)
   - Support lazy loading for cloud files (same pattern as time filter)
   - Return filtered results vector
2. Add date range cache management:
   - Add `cachedDateRange` and `dateRangeCacheValid` to `GuiState`
   - Implement `UpdateDateRangeFilterCacheIfNeeded()` in `SearchResultsService`
   - Integrate with existing `UpdateFilterCaches()` method
3. Update `GetDisplayResults()` to prioritize date range filter:
   - Priority: sizeFiltered > dateRangeFiltered > timeFiltered > raw searchResults
4. Ensure mutual exclusivity:
   - When date range is set, clear `timeFilter`
   - When `timeFilter` is set, clear date range

**Dependencies:**
- Phase 1 (data structures)
- Existing `ApplyTimeFilter()` implementation as reference

### Phase 3: UI Components

**Files to Modify:**
- `src/ui/FilterPanel.h` - Add date range rendering method
- `src/ui/FilterPanel.cpp` - Implement date range UI
- `src/ui/UIRenderer.cpp` - Integrate date range UI into main render flow

**Tasks:**
1. Create `FilterPanel::RenderDateRangeFilter()` method:
   - Display "Custom Date Range:" label
   - Add two date input fields (Start Date, End Date)
   - Use ImGui date picker widget or `ImGui::InputText` with date format
   - Add "Clear" button to reset date range
   - Handle date validation and conversion
2. Update `FilterPanel::RenderTimeQuickFilters()`:
   - Disable quick filter buttons when date range is active
   - Clear date range when quick filter is selected
3. Update `FilterPanel::RenderActiveFilterIndicators()`:
   - Add date range badge when active
   - Display formatted date range (e.g., "Jan 17, 2026 - Jan 31, 2026")
   - Add clear button for date range badge
4. Integrate into main UI:
   - Call `RenderDateRangeFilter()` in `UIRenderer::RenderManualSearchContent()`
   - Place below or next to time quick filters

**Dependencies:**
- Phase 1 (data structures)
- Phase 2 (filtering logic) - for testing
- ImGui date picker widget availability

### Phase 4: Persistence and Integration

**Files to Modify:**
- `src/filters/TimeFilterUtils.cpp` - Update `ApplySavedSearchToGuiState()` and `RecordRecentSearch()`
- `src/core/Settings.cpp` - Update JSON serialization/deserialization
- `src/gui/GuiState.cpp` - Update `BuildCurrentSearchParams()` if needed

**Tasks:**
1. Update `ApplySavedSearchToGuiState()`:
   - Load date range from `SavedSearch.dateRangeStart` and `dateRangeEnd`
   - Convert ISO 8601 strings to `DateRange` struct
   - Apply to `GuiState.dateRangeFilter`
2. Update `RecordRecentSearch()`:
   - Convert `GuiState.dateRangeFilter` to ISO 8601 strings
   - Store in `SavedSearch.dateRangeStart` and `dateRangeEnd`
3. Update JSON serialization:
   - Add `dateRangeStart` and `dateRangeEnd` fields to JSON
   - Handle empty strings (no date range set)
4. Update `BuildCurrentSearchParams()`:
   - Ensure date range is considered when building search params (if needed)
   - Currently, time filters are applied post-search, so this may not be needed

**Dependencies:**
- Phase 1 (data structures)
- Phase 2 (filtering logic)
- Phase 3 (UI components)

### Phase 5: Testing and Validation

**Files to Create/Modify:**
- `tests/TimeFilterUtilsTests.cpp` - Add date range utility tests
- `tests/SearchResultUtilsTests.cpp` - Add date range filtering tests
- Manual testing on Windows, macOS, and Linux

**Tasks:**
1. Unit tests for date range utilities:
   - Test `DateRangeFromStrings()` with valid/invalid dates
   - Test `DateRangeToStrings()` conversion
   - Test `IsDateRangeActive()` logic
   - Test FILETIME conversion (Windows and Unix)
2. Unit tests for filtering logic:
   - Test `ApplyDateRangeFilter()` with various date ranges
   - Test optional start/end dates
   - Test edge cases (same start/end date, future dates, etc.)
   - Test with cloud files (lazy loading)
3. Integration tests:
   - Test date range with saved searches
   - Test mutual exclusivity with quick time filters
   - Test cache invalidation
4. Manual testing:
   - Test date picker UI on all platforms
   - Test date range filtering with real file system
   - Test persistence (save/load saved searches)
   - Test timezone handling

**Dependencies:**
- All previous phases

## Technical Details

### Date Format

**Storage Format:** ISO 8601 (YYYY-MM-DD)
- Example: "2026-01-17"
- Easy to parse and validate
- Human-readable in JSON settings file
- Cross-platform compatible

**Display Format:** Locale-appropriate
- Default: "Jan 17, 2026" or "2026-01-17"
- Can be customized based on user preferences (future enhancement)

### FILETIME Conversion

**Start Date:** Convert to FILETIME at 00:00:00 (midnight) local time
**End Date:** Convert to FILETIME at 23:59:59.9999999 (end of day) local time

**Implementation:**
- Use existing `CalculateCutoffTime()` pattern for date-to-FILETIME conversion
- Platform-specific: Windows uses `SYSTEMTIME`/`FILETIME`, macOS/Linux uses `std::chrono`

### Filter Comparison Logic

```cpp
bool MatchesDateRange(const FILETIME& fileTime, const DateRange& range) {
  if (!range.isActive) return true;
  
  bool afterStart = (range.startDate.dwHighDateTime == 0 && range.startDate.dwLowDateTime == 0) ||
                    CompareFileTime(&fileTime, &range.startDate) >= 0;
  
  bool beforeEnd = (range.endDate.dwHighDateTime == 0 && range.endDate.dwLowDateTime == 0) ||
                   CompareFileTime(&fileTime, &range.endDate) <= 0;
  
  return afterStart && beforeEnd;
}
```

### Cache Management

Follow existing pattern from time filter cache:
- `GuiState.cachedDateRange` - Last applied date range
- `GuiState.dateRangeCacheValid` - Cache validity flag
- `UpdateDateRangeFilterCacheIfNeeded()` - Rebuild cache when needed

## Risk Assessment

### High Risk

1. **ImGui Date Picker Availability**
   - **Risk:** ImGui may not have a built-in date picker widget
   - **Mitigation:** Use `ImGui::InputText` with date format validation, or integrate third-party ImGui date picker extension
   - **Fallback:** Simple text input with date format (YYYY-MM-DD) and validation

2. **Timezone Handling**
   - **Risk:** Date range filtering may behave differently across timezones
   - **Mitigation:** Use local time consistently (same as existing time filters)
   - **Testing:** Test on different timezones and DST transitions

### Medium Risk

1. **Performance with Large Result Sets**
   - **Risk:** Date range filtering may be slower than quick filters
   - **Mitigation:** Use same caching mechanism, lazy loading for cloud files
   - **Testing:** Profile with large result sets (10K+ files)

2. **UI Complexity**
   - **Risk:** Date range picker may clutter the filter panel
   - **Mitigation:** Keep UI simple, use collapsible section if needed
   - **Testing:** User testing for usability

### Low Risk

1. **Backward Compatibility**
   - **Risk:** Existing saved searches may not have date range fields
   - **Mitigation:** Handle missing fields gracefully (default to empty/inactive)
   - **Testing:** Load old saved searches, verify they work

2. **Date Validation**
   - **Risk:** Invalid dates may cause crashes or incorrect filtering
   - **Mitigation:** Validate dates on input, use sentinel values for invalid dates
   - **Testing:** Test with invalid dates, edge cases (leap years, etc.)

## Success Criteria

1. ✅ User can select start and end dates via UI
2. ✅ Date range filter correctly filters search results
3. ✅ Date range filter works mutually exclusively with quick time filters
4. ✅ Date range is persisted in saved searches
5. ✅ Date range filter performs similarly to quick time filters
6. ✅ Date range filter works on Windows, macOS, and Linux
7. ✅ Unit tests cover date range utilities and filtering logic
8. ✅ Code follows existing patterns and conventions

## Future Enhancements

1. **Time Selection:** Allow users to select specific times (not just dates)
2. **Relative Date Ranges:** "Last 7 days", "Last 30 days" quick options
3. **Date Range Presets:** Save common date ranges as presets
4. **Multiple Date Ranges:** Support OR logic (files matching any of multiple ranges)
5. **Calendar View:** Visual calendar for date selection
6. **Date Range Suggestions:** Suggest date ranges based on file modification times

## References

- Existing time filter implementation: `src/filters/TimeFilterUtils.cpp`
- Filter application logic: `src/search/SearchResultUtils.cpp`
- UI rendering: `src/ui/FilterPanel.cpp`
- State management: `src/gui/GuiState.h`
- Persistence: `src/core/Settings.h`

## Timeline Estimate

- **Phase 1:** 2-3 hours (data structures and utilities)
- **Phase 2:** 4-5 hours (filtering logic and cache integration)
- **Phase 3:** 4-6 hours (UI components and integration)
- **Phase 4:** 2-3 hours (persistence and integration)
- **Phase 5:** 3-4 hours (testing and validation)

**Total Estimate:** 15-21 hours

**Note:** Timeline assumes ImGui has a date picker widget or simple text input is sufficient. If a custom date picker widget is needed, add 4-6 hours.
