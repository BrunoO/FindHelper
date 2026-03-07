# Phase 2: Testing & Verification Checklist

## Overview

This document provides a comprehensive testing checklist to verify feature parity between macOS and Windows implementations of FindHelper.

**Testing Date**: [To be filled]
**Tester**: [To be filled]
**Build Version**: [To be filled]

---

## Prerequisites

### macOS Testing Setup

1. **Build the application**:
   ```bash
   cmake -S . -B build
   cmake --build build
   ```

2. **Prepare test index file**:
   - Generate an index file on Windows using the Windows version
   - Or create a sample index file for testing
   - Example: `./build/FindHelper.app/Contents/MacOS/FindHelper --index-from-file /path/to/index.json`

3. **Verify build output**:
   - Location: `build/FindHelper.app/Contents/MacOS/FindHelper`
   - Or run: `open build/FindHelper.app`

---

## Test Categories

### 1. Application Launch & Basic Functionality

#### 1.1 Application Launch
- [ ] Application launches without crashes
- [ ] Window appears with correct size (from settings or default)
- [ ] Window title shows "FindHelper"
- [ ] Application version is displayed correctly (short git hash)

#### 1.2 Index Loading
- [ ] Application starts without index (shows appropriate message)
- [ ] `--index-from-file` argument loads index successfully
- [ ] Index loading shows progress/status
- [ ] Index statistics display correctly after loading
- [ ] Error handling for invalid index file path
- [ ] Error handling for corrupted index file

**Test Command**:
```bash
./build/FindHelper.app/Contents/MacOS/FindHelper --index-from-file /path/to/index.json
```

#### 1.3 Window Management
- [ ] Window can be resized
- [ ] Window size persists across sessions (saved to settings)
- [ ] Window can be minimized/maximized
- [ ] Window position is reasonable on first launch

---

### 2. Search Functionality

#### 2.1 Path Pattern Search
- [ ] Path pattern input field is visible and functional
- [ ] Path pattern search executes correctly
- [ ] Wildcards work (`*`, `?`)
- [ ] Multiple path patterns work (comma-separated)
- [ ] Path pattern search results are accurate
- [ ] Empty path pattern returns all files

**Test Cases**:
- Pattern: `*test*` (should find paths containing "test")
- Pattern: `*/src/*` (should find files in src directories)
- Pattern: `*.cpp,*.h` (should find C++ source files)

#### 2.2 Filename Pattern Search
- [ ] Filename input field is visible and functional
- [ ] Filename pattern search executes correctly
- [ ] Wildcards work (`*`, `?`)
- [ ] Filename search results are accurate
- [ ] Empty filename pattern returns all files

**Test Cases**:
- Pattern: `test*` (should find files starting with "test")
- Pattern: `*.txt` (should find all .txt files)
- Pattern: `file?.cpp` (should find file1.cpp, file2.cpp, etc.)

#### 2.3 Extension Filter
- [ ] Extension filter input field is visible and functional
- [ ] Extension filter works correctly
- [ ] Multiple extensions work (comma-separated)
- [ ] Extension filter results are accurate
- [ ] Empty extension filter returns all files

**Test Cases**:
- Extensions: `cpp,h` (should find C++ source files)
- Extensions: `txt,md` (should find text files)
- Extensions: `jpg,png` (should find image files)

#### 2.4 Folders-Only Filter
- [ ] "Folders Only" checkbox is visible and functional
- [ ] When checked, only folders are returned
- [ ] When unchecked, files and folders are returned
- [ ] Filter works in combination with other search criteria

#### 2.5 Case Sensitivity
- [ ] "Case-Insensitive" checkbox is visible and functional
- [ ] When checked, search is case-insensitive
- [ ] When unchecked, search is case-sensitive
- [ ] Case sensitivity applies to all search fields

**Test Cases**:
- Search for "TEST" with case-insensitive: should find "test", "Test", "TEST"
- Search for "TEST" with case-sensitive: should only find "TEST"

#### 2.6 Time Filter
- [ ] Time filter dropdown is visible and functional
- [ ] All options are available: "All", "Today", "This Week", "This Month", "This Year", "Older"
- [ ] Time filter works correctly for each option
- [ ] Time filter results are accurate
- [ ] Time filter works in combination with other search criteria

**Test Cases**:
- "Today": Should only show files modified today
- "This Week": Should only show files modified this week
- "This Month": Should only show files modified this month
- "This Year": Should only show files modified this year
- "Older": Should only show files older than one year

#### 2.7 Auto-Search (Debounced)
- [ ] Auto-search triggers after typing stops (debounced)
- [ ] Debounce delay is reasonable (not too fast, not too slow)
- [ ] Auto-search works for all input fields
- [ ] Auto-search doesn't trigger while user is still typing

#### 2.8 Auto-Refresh
- [ ] "Auto-refresh" checkbox is visible and functional
- [ ] When checked, search refreshes automatically at intervals
- [ ] When unchecked, search only runs manually or on input change
- [ ] Auto-refresh interval is reasonable

#### 2.9 Manual Search Trigger
- [ ] "Search" button is visible and functional
- [ ] Clicking "Search" button triggers search
- [ ] F5 keyboard shortcut triggers search
- [ ] Manual search works regardless of auto-search settings

#### 2.10 Search Result Sorting
- [ ] Search results table is sortable
- [ ] All 5 columns are sortable: Filename, Extension, Size, Last Modified, Full Path
- [ ] Clicking column header sorts by that column
- [ ] Clicking again reverses sort order
- [ ] Sort indicator (arrow) is visible
- [ ] Sorting works correctly for each column type

**Test Cases**:
- Sort by Filename: alphabetical order
- Sort by Extension: alphabetical order
- Sort by Size: numerical order (largest to smallest or vice versa)
- Sort by Last Modified: chronological order (newest to oldest or vice versa)
- Sort by Full Path: alphabetical order

#### 2.11 Saved Searches
- [ ] "Save Search" button/option is available
- [ ] Saved search dialog opens correctly
- [ ] Can save current search criteria with a name
- [ ] Saved searches appear in dropdown/list
- [ ] Can load a saved search
- [ ] Can delete a saved search
- [ ] Saved searches persist across sessions

**Test Cases**:
- Save a search with name "C++ Files" (pattern: `*.cpp,*.h`)
- Load "C++ Files" and verify criteria are applied
- Delete "C++ Files" and verify it's removed

---

### 3. UI Components

#### 3.1 Search Input Fields
- [ ] Path pattern input field is visible
- [ ] Filename pattern input field is visible
- [ ] Extension filter input field is visible
- [ ] All input fields are properly sized and positioned
- [ ] Input fields accept text input
- [ ] Input fields show placeholder text or labels

#### 3.2 Search Controls
- [ ] "Search" button is visible and functional
- [ ] "Folders Only" checkbox is visible and functional
- [ ] "Case-Insensitive" checkbox is visible and functional
- [ ] "Auto-refresh" checkbox is visible and functional
- [ ] "Clear All" button is visible and functional
- [ ] "Help" button is visible and functional
- [ ] All controls are properly sized and positioned

#### 3.3 Search Results Table
- [ ] Search results table is visible
- [ ] Table displays results correctly
- [ ] All 5 columns are visible: Filename, Extension, Size, Last Modified, Full Path
- [ ] Table is scrollable when results exceed visible area
- [ ] Table rows are selectable (if applicable)
- [ ] Table shows "No results" message when search returns no results
- [ ] Table shows appropriate message when index is not loaded

#### 3.4 Status Bar
- [ ] Status bar is visible at bottom of window
- [ ] Status bar shows search progress (if applicable)
- [ ] Status bar shows result count
- [ ] Status bar shows index statistics (total files, indexed files)
- [ ] Status bar updates correctly during search
- [ ] Status bar shows appropriate message when index is not loaded

#### 3.5 Settings Window
- [ ] Settings window can be opened (menu or button)
- [ ] Settings window displays correctly
- [ ] All settings options are visible and functional:
  - Font family selection
  - Font size configuration
  - Font scale (global UI scale)
- [ ] Settings can be changed and saved
- [ ] Settings persist across sessions
- [ ] Settings window can be closed

**Test Cases**:
- Open settings window
- Change font family and verify it applies
- Change font size and verify it applies
- Change UI scale and verify it applies
- Close and reopen settings, verify changes persisted

#### 3.6 Metrics Window
- [ ] Metrics window can be opened (menu or button)
- [ ] Metrics window displays correctly
- [ ] Metrics show index statistics:
  - Total files indexed
  - Index size
  - Other relevant metrics
- [ ] Metrics window can be closed

#### 3.7 Help Popups
- [ ] Help button opens help popup
- [ ] Help popup shows search tips
- [ ] Help popup shows keyboard shortcuts
- [ ] Help popup shows regex generator (if applicable)
- [ ] Help popup can be closed

#### 3.8 Saved Search Popups
- [ ] "Save Search" popup opens correctly
- [ ] "Delete Saved Search" popup opens correctly
- [ ] Popups can be closed
- [ ] Popups show appropriate options/lists

---

### 4. Keyboard Shortcuts

#### 4.1 Focus Filename Input (Ctrl+F / Cmd+F)
- [ ] Ctrl+F (Windows) or Cmd+F (macOS) focuses filename input field
- [ ] Shortcut works when window is focused
- [ ] Shortcut doesn't interfere with browser-style find (if applicable)

#### 4.2 Refresh Search (F5)
- [ ] F5 keyboard shortcut triggers search refresh
- [ ] Shortcut works when window is focused
- [ ] Shortcut works regardless of auto-search settings

#### 4.3 Clear All Filters (Escape)
- [ ] Escape keyboard shortcut clears all search filters
- [ ] Shortcut works when window is focused
- [ ] All input fields are cleared
- [ ] All checkboxes are reset to default state
- [ ] Time filter is reset to "All"

---

### 5. File Operations

#### 5.1 Open File
- [ ] Can open file from search results (double-click or context menu)
- [ ] File opens in default application
- [ ] Error handling for invalid file paths
- [ ] Error handling for files that don't exist

#### 5.2 Reveal in Finder
- [ ] Can reveal file in Finder from search results
- [ ] File is selected/highlighted in Finder
- [ ] Error handling for invalid file paths

#### 5.3 Copy Path to Clipboard
- [ ] Can copy file path to clipboard
- [ ] Path is copied correctly (full path)
- [ ] Path can be pasted into other applications
- [ ] Keyboard shortcut works (Ctrl+C / Cmd+C on selected row)

---

### 6. Settings & Persistence

#### 6.1 Settings Window
- [ ] Settings window opens correctly
- [ ] All settings options are available
- [ ] Settings can be changed
- [ ] Settings are saved to JSON file
- [ ] Settings persist across sessions

#### 6.2 Window Size Persistence
- [ ] Window size is saved when window is resized
- [ ] Window size is restored on next launch
- [ ] Window size is within reasonable bounds (min/max)

#### 6.3 Settings JSON File
- [ ] Settings are saved to JSON file
- [ ] JSON file location is correct (user config directory)
- [ ] JSON file format is valid
- [ ] Settings can be manually edited in JSON file (if needed)

#### 6.4 Saved Searches Persistence
- [ ] Saved searches are saved to settings JSON
- [ ] Saved searches persist across sessions
- [ ] Saved searches can be loaded after restart

---

### 7. Error Handling & Edge Cases

#### 7.1 Index Loading Errors
- [ ] Error message for missing index file
- [ ] Error message for corrupted index file
- [ ] Error message for invalid index file format
- [ ] Application doesn't crash on index loading errors

#### 7.2 Search Errors
- [ ] Error handling for invalid search patterns
- [ ] Error handling for empty search results
- [ ] Application doesn't crash on search errors

#### 7.3 File Operation Errors
- [ ] Error handling for files that don't exist
- [ ] Error handling for files without permissions
- [ ] Error handling for invalid file paths
- [ ] Application doesn't crash on file operation errors

#### 7.4 Edge Cases
- [ ] Empty index (no files indexed)
- [ ] Very large index (performance test)
- [ ] Very large search results (performance test)
- [ ] Special characters in file paths
- [ ] Unicode characters in file paths
- [ ] Very long file paths

---

### 8. Performance

#### 8.1 Index Loading Performance
- [ ] Index loads in reasonable time (< 10 seconds for typical index)
- [ ] Progress indicator shows during loading
- [ ] Application remains responsive during loading

#### 8.2 Search Performance
- [ ] Search executes in reasonable time (< 1 second for typical search)
- [ ] Search doesn't block UI (if async)
- [ ] Large result sets don't cause UI lag
- [ ] Sorting is fast even with many results

#### 8.3 UI Responsiveness
- [ ] UI remains responsive during search
- [ ] UI remains responsive during index loading
- [ ] Scrolling is smooth
- [ ] Window resizing is smooth

---

### 9. Platform-Specific Features

#### 9.1 macOS-Specific
- [ ] Application runs as .app bundle (if configured)
- [ ] Application icon is correct (if configured)
- [ ] File operations use macOS APIs (NSWorkspace, NSPasteboard)
- [ ] Keyboard shortcuts use Cmd instead of Ctrl where appropriate

#### 9.2 Windows-Specific (for comparison)
- [ ] Application runs as .exe
- [ ] File operations use Windows APIs
- [ ] Keyboard shortcuts use Ctrl

---

## Test Results Summary

### Overall Status
- [ ] ✅ All tests passed
- [ ] ⚠️ Some tests failed (see details below)
- [ ] ❌ Critical tests failed

### Test Statistics
- **Total Tests**: [To be filled]
- **Passed**: [To be filled]
- **Failed**: [To be filled]
- **Skipped**: [To be filled]

### Failed Tests
[List any failed tests with details]

### Known Issues
[List any known issues or limitations]

### Notes
[Any additional notes or observations]

---

## Next Steps

After completing this checklist:

1. **Fix any failed tests**
2. **Document any platform-specific differences**
3. **Update feature parity documentation**
4. **Create bug reports for critical issues**
5. **Plan Phase 3 enhancements (if applicable)**

---

## Testing Tools & Commands

### macOS Testing Commands

```bash
# Build the application
cmake -S . -B build
cmake --build build

# Run with index file
./build/FindHelper.app/Contents/MacOS/FindHelper --index-from-file /path/to/index.json

# Run without index (to test error handling)
./build/FindHelper.app/Contents/MacOS/FindHelper

# Check version
./build/FindHelper.app/Contents/MacOS/FindHelper --version

# Check help
./build/FindHelper.app/Contents/MacOS/FindHelper --help
```

### Windows Testing Commands (for comparison)

```bash
# Build the application
cmake -S . -B build
cmake --build build --config Release

# Run (with USN monitoring)
.\build\Release\FindHelper.exe

# Run with index file (for comparison)
.\build\Release\FindHelper.exe --index-from-file C:\path\to\index.json
```

---

## References

- [Phase 2 Feature Parity Goal](./PHASE_2_FEATURE_PARITY_GOAL.md)
- [Phase 2 Implementation Order](./PHASE_2_IMPLEMENTATION_ORDER.md)
- [Next Steps](./NEXT_STEPS.md)

