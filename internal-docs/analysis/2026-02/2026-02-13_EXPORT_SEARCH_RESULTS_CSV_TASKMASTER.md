# Export Search Results (CSV) – Taskmaster Instructions

**Date:** 2026-02-13  
**Goal:** Allow users to export search data externally (reports, Excel, scripts).

---

## 1. Goal Statement

Add an "Export to CSV" feature so users can save the currently displayed search results to a CSV file for use in external tools (Excel, reports, scripts).

---

## 2. Current State Analysis

### 2.1 File Dialog Availability

- **`ShowFolderPickerDialog`** exists in `FileOperations` (Windows, macOS, Linux).
- **No `ShowSaveFileDialog`** exists in the codebase.
- **Action:** Check `src/platform/FileOperations.h` and platform implementations (`FileOperations_win.cpp`, `FileOperations_mac.mm`, `FileOperations_linux.cpp`) for any save-file dialog. If none exists, use fallback: save to `Desktop/search_results.csv` and notify the user (e.g., toast or status bar message).

### 2.2 Button Placement Options

| Location | Pros | Cons |
|----------|------|------|
| **FilterPanel** (near Saved Searches) | Always visible when not minimalistic; logical grouping with other result-related actions | Slightly removed from the results table |
| **ResultsTable** (above table when results shown) | Contextually close to the data being exported | Only visible when results are displayed; requires adding a header row |

**Recommendation:** Prefer **FilterPanel** (same row as Saved Searches, after Delete button) for consistency and visibility. Alternatively, add a small toolbar row above the table when `display_results` is non-empty.

### 2.3 Data Source

- **`SearchResultsService::GetDisplayResults(state)`** returns the same vector shown in the table:
  - Streaming: `state.partialResults`
  - Size filter active: `state.sizeFilteredResults`
  - Time filter active: `state.filteredResults`
  - Otherwise: `state.searchResults`
- **`SearchResult`** fields: `fullPath`, `GetFilename()`, `GetExtension()`, `fileSize`, `fileSizeDisplay`, `lastModificationTime`, `lastModificationDisplay`, `isDirectory`.
- Size and last-modified are **lazy-loaded** via `FileIndex::GetFileSizeById()` and `FileIndex::GetFileModificationTimeById()`. For export, either:
  - Load on demand during export (iterate and call `FileIndex` for unloaded results), or
  - Export what is already loaded (empty or "..." for unloaded fields). Simpler; acceptable for MVP.

### 2.4 Existing Utilities

- **`path_utils::GetDesktopPath()`** – use for fallback save location.
- **`SearchResultUtils::FormatFileSize()`**, **`FormatFileTime()`** – for consistent display in CSV.
- **`SearchResultUtils::EnsureSizeAndTimeLoaded()`** (or equivalent) – if you want to load attributes during export.

---

## 3. Prerequisites / Verification

Before implementing:

1. **Confirm no save-file dialog exists:**
   - `grep -r "SaveFile\|Save.*Dialog\|save.*file" src/platform/`
2. **Confirm `GetDesktopPath()` is available:**
   - `path_utils::GetDesktopPath()` in `src/path/PathUtils.h`
3. **Confirm `GetDisplayResults` usage:**
   - `SearchResultsService::GetDisplayResults(state)` in `src/search/SearchResultsService.h`

---

## 4. Implementation Steps

### Step 4.1: Create CSV Export Logic

1. Create `src/search/ExportSearchResultsService.h` and `.cpp` (or add to an existing search utility).
2. Implement:
   - `ExportToCsv(const std::vector<SearchResult>& results, FileIndex& file_index, const std::string& output_path) -> bool`
   - CSV header: `Filename,Size,Last Modified,Full Path,Extension`
   - For each result:
     - Filename: `result.GetFilename()`
     - Size: Use `result.fileSizeDisplay` if non-empty; else load via `file_index.GetFileSizeById(result.fileId)` and format.
     - Last Modified: Use `result.lastModificationDisplay` if non-empty; else load via `file_index.GetFileModificationTimeById(result.fileId)` and format.
     - Full Path: `result.fullPath`
     - Extension: `result.GetExtension()`
   - Escape CSV cells: wrap in double quotes if cell contains comma, newline, or double quote; double any internal quotes.
3. Use `std::ofstream` or equivalent for writing. Ensure UTF-8 encoding if needed for cross-platform compatibility.

### Step 4.2: Determine Output Path

1. **If a save-file dialog helper exists:** Use it to get `output_path`; if user cancels, return without exporting.
2. **If no save-file dialog exists (current state):**
   - Default path: `path_utils::JoinPath(path_utils::GetDesktopPath(), "search_results.csv")`.
   - Optionally add timestamp to avoid overwriting: `search_results_2026-02-13_143022.csv`.
   - Write to that path directly.

### Step 4.3: Add Export Button – FilterPanel

1. In `FilterPanel::RenderSavedSearches()`, after the Delete button (`ImGui::SameLine()`), add:
   - `ImGui::SameLine();`
   - `if (ImGui::SmallButton(ICON_FA_FILE_EXPORT " Export CSV")) { ... }` (or `ICON_FA_SAVE` / `ICON_FA_ARROW_DOWN` if icon not added)
2. Button should be disabled when there are no results to export:
   - Use `SearchResultsService::GetDisplayResults(state)`; if empty, wrap in `ImGui::BeginDisabled()` / `ImGui::EndDisabled()`.
3. On click:
   - Get `display_results` via `GetDisplayResults(state)`.
   - Determine `output_path` (dialog or default).
   - Call export logic.
   - On success: notify user (e.g., set `state.exportSuccessMessage` or use a toast; show path in status bar briefly).
   - On failure: set `state.exportErrorMessage` or log and show error.

### Step 4.4: Wire Dependencies

- `FilterPanel::RenderSavedSearches` receives `GuiState`, `AppSettings`, `UIActions`, `is_index_building`.
- Export needs `FileIndex` for lazy-loading. Options:
  - Pass `FileIndex*` (or `FileIndex&`) to `RenderSavedSearches` (requires signature change and call-site updates in `UIRenderer`).
  - Add `UIActions::ExportToCsv(state, file_index)` and call from `FilterPanel` via `actions->ExportToCsv(...)` (keeps UI layer thin).

**Recommendation:** Add `ExportToCsv(GuiState& state, FileIndex& file_index)` to `UIActions`; `FilterPanel` calls it when the button is clicked. This keeps export logic in the action layer and avoids passing `FileIndex` through multiple UI layers.

### Step 4.5: User Notification (Fallback Path)

When using the default path (no dialog):

- Show a brief message: e.g., "Exported N results to Desktop/search_results.csv" in the status bar or a non-modal notification.
- Consider `state.exportNotification` (string + timestamp) that `StatusBar` or a small toast renders for a few seconds.

---

## 5. Acceptance Criteria

- [ ] "Export to CSV" button appears in FilterPanel (near Saved Searches) or above ResultsTable when results exist.
- [ ] Button is disabled when there are no results to export.
- [ ] Clicking exports the **currently displayed** results (respecting streaming, time filter, size filter).
- [ ] CSV has header: `Filename,Size,Last Modified,Full Path,Extension`.
- [ ] CSV cells are properly escaped (commas, quotes, newlines).
- [ ] If a save-file dialog exists: user can choose path; cancel does nothing.
- [ ] If no save-file dialog: save to `Desktop/search_results.csv` (or timestamped variant) and notify user of the path.
- [ ] Export works on Windows, macOS, and Linux (cross-platform).
- [ ] No crashes or hangs; large result sets (e.g., 10k rows) complete within a few seconds.

---

## 6. Technical Notes

### CSV Escaping (RFC 4180–style)

- If a cell contains `,`, `"`, or newline: wrap entire cell in `"` and escape internal `"` as `""`.
- Example: `C:\Users\file "test".txt` → `"C:\Users\file ""test"".txt"`

### Lazy Loading During Export

- For large exports, loading size/time for every result may be slow. Options:
  - **MVP:** Export only what is already loaded; use empty string or "-" for unloaded.
  - **Enhanced:** Use `FileIndex::GetFileSizeById` / `GetFileModificationTimeById` in a loop; consider batching or progress indication for very large sets.

### File Naming (Fallback)

- `search_results.csv` – simple, may overwrite.
- `search_results_YYYY-MM-DD_HHMMSS.csv` – unique per export.

### Icon

- `IconsFontAwesome.h` does not currently define `ICON_FA_FILE_EXPORT` or `ICON_FA_FILE_CSV`.
- **Option A:** Add `#define ICON_FA_FILE_EXPORT "\xef\x95\x6e"` (U+F56E, FontAwesome 6 file-export) to `IconsFontAwesome.h`.
- **Option B:** Use existing `ICON_FA_SAVE` or `ICON_FA_ARROW_DOWN` as fallback (Save is used for Saved Search; Arrow Down suggests download).

---

## 7. Files to Create or Modify

| File | Action |
|------|--------|
| `src/search/ExportSearchResultsService.h` | Create – export API |
| `src/search/ExportSearchResultsService.cpp` | Create – CSV generation and file write |
| `src/ui/FilterPanel.cpp` | Modify – add Export button, wire to export |
| `src/ui/FilterPanel.h` | Modify – if new parameters needed |
| `src/gui/UIActions.h` | Modify – add `ExportToCsv(GuiState&, FileIndex&)` to interface; implement in `Application.cpp` |
| `src/ui/UIRenderer.cpp` | Modify – pass `FileIndex` to `RenderSavedSearches` if needed |
| `CMakeLists.txt` | Modify – add new source files to build |
| `src/gui/GuiState.h` | Optional – add `exportNotification` / `exportErrorMessage` for user feedback |

---

## 8. Order of Operations

1. Verify no save-file dialog exists; document fallback behavior.
2. Implement `ExportSearchResultsService` (CSV generation + write).
3. Add `ExportToCsv` to `UIActions` (or equivalent) and wire `FileIndex`.
4. Add Export button to `FilterPanel::RenderSavedSearches`.
5. Implement user notification for fallback path.
6. Run `scripts/build_tests_macos.sh` to validate build and tests.
