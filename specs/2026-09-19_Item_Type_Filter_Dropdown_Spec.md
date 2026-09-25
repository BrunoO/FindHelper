# Specification: Item Type Filter Dropdown (All items / Files only / Folders only)

**Document:** `specs/2026-09-19_Item_Type_Filter_Dropdown_Spec.md`  
**Date:** 2026-09-19  
**Status:** Draft / Ready for Implementation  
**Target:** USN_WINDOWS / FindHelper (C++17, ImGui)

---

## 1. Overview & Motivation

FindHelper currently supports filtering search results to directories via a "Folders Only" checkbox. Users frequently want the inverse capability: searching strictly for files while filtering out directories (e.g. searching for a filename like `config` or `log` without seeing all directories with that name).

Rather than adding a second mutually exclusive checkbox ("Files Only") which could lead to contradictory UI states (both checked yielding 0 results), this feature upgrades the "Folders Only" option into a unified **3-way Dropdown Combo**:
- **All items** (default)
- **Files only**
- **Folders only**

---

## 2. Requirements & Constraints

### 2.1 Functional Requirements
1. **Dropdown Selector**: The UI "Search Options" toolbar shall display a dropdown combo allowing selection among `All items`, `Files only`, and `Folders only`.
2. **Immediate Search Trigger**: Selecting an option in the dropdown shall trigger search re-evaluation (`MarkInputChanged()`).
3. **Filtering Correctness**:
   - `All items`: Results include both files and directories.
   - `Files only`: Results include non-directory entries only (`is_directory == 0`).
   - `Folders only`: Results include directory entries only (`is_directory != 0`).
4. **Active Filter Badges**:
   - When `Folders only` is active, display a `[Folders Only (x)]` badge.
   - When `Files only` is active, display a `[Files Only (x)]` badge.
   - Clicking `(x)` on either badge resets the filter back to `All items`.
   - When `All items` is active, no type filter badge is displayed.
5. **Persistence & Search History**:
   - Search history entries shall record the selected item type filter.
   - Existing saved searches and search history containing `foldersOnly: true/false` shall load correctly and map to `FoldersOnly` / `All items` without data loss or crashes.
6. **AI Search (Gemini) Integration**:
   - If the Gemini API returns `"folders_only": true`, the filter is set to `FoldersOnly`.
   - If the Gemini API returns `"files_only": true`, the filter is set to `FilesOnly`.
   - Otherwise defaults to `All items`.

### 2.2 Non-Functional & Architectural Constraints
- **C++17 Compliance**: No C++20 features; explicit lambda captures in templates; `(std::min)`/`(std::max)`.
- **Zero Allocation in Hot Loop**: Directory checking must use the existing `soaView.is_directory` SoA array without string parsing, filesystem I/O, or heap allocations.
- **UI Thread Confinement**: ImGui combo runs exclusively on the main UI thread. Worker threads receive the filter as an immutable value in `SearchContext`.
- **Backward Compatibility**: Regression test hooks (`SetSearchParamsForRegressionTest`) and test cases must continue functioning.

---

## 3. Architecture & Data Flow

### 3.1 Data Model
An enum `ItemTypeFilter` defined in `src/search/SearchTypes.h`:
```cpp
enum class ItemTypeFilter : uint8_t {
  All = 0,
  FilesOnly = 1,
  FoldersOnly = 2
};
```

### 3.2 Component Flow

```
[SearchInputs::RenderSearchOptions]
        │  (User selects "Files only")
        ▼
[GuiState::searchCriteria.item_type_filter = ItemTypeFilter::FilesOnly]
        │
        ▼
[SearchCriteria::BuildParams()]
        │  (Copies itemTypeFilter into SearchParams)
        ▼
[SearchWorker::WorkerThread]
        │
        ▼
[FileIndex::SearchAsyncWithData]
        │  (SearchContextBuilder sets context.item_type_filter)
        ▼
[ParallelSearchEngine]
        │
        ▼
[Filter check in chunk loop]
if (context.item_type_filter == ItemTypeFilter::FilesOnly && soaView.is_directory[i] != 0) continue;
if (context.item_type_filter == ItemTypeFilter::FoldersOnly && soaView.is_directory[i] == 0) continue;
```

---

## 4. User Stories & Acceptance Criteria

| ID | Title | Given | When | Then |
|---|---|---|---|---|
| US-1 | Default state | Fresh application start or "Clear All" | UI renders search options | Dropdown shows "All items"; search results contain both files and folders. |
| US-2 | Filter to files only | Search query matches files and folders | User selects "Files only" in dropdown | Results table only displays files (`isDirectory == false`); badge `[Files Only (x)]` appears. |
| US-3 | Filter to folders only | Search query matches files and folders | User selects "Folders only" in dropdown | Results table only displays directories (`isDirectory == true`); badge `[Folders Only (x)]` appears. |
| US-4 | Dismiss badge | Filter is "Files only" or "Folders only" | User clicks the `(x)` on the active badge | Filter resets to "All items"; search results refresh to show both. |
| US-5 | Search history round-trip | Search performed with "Files only" | App reloads or user inspects history | History entry correctly displays "Files only" and restores "Files only" when clicked. |
| US-6 | Backward-compat load | Legacy `search_history.json` with `"foldersOnly": true` | App loads history | Entry is loaded as `ItemTypeFilter::FoldersOnly`. |

---

## 5. Verification Plan

1. **Automated Unit Tests**:
   - `gui_state_tests`: Verify `ApplySearchConfig`, `ApplyShowAllPreset`, and badge counters with `ItemTypeFilter`.
   - `search_history_tests`: Verify serialization and deserialization of `ItemTypeFilter` including legacy `foldersOnly` compatibility.
   - `file_index_search_strategy_tests`: Verify `ParallelSearchEngine` filters correctly for `FilesOnly`, `FoldersOnly`, and `All`.
2. **Full Test Suite**:
   - Run `./scripts/build_tests_macos.sh --no-asan` to verify all existing and new unit tests pass.
3. **Manual Smoke Verification**:
   - Open FindHelper UI.
   - Switch dropdown between All items, Files only, Folders only.
   - Confirm results update instantly, badges render and clear accurately, and search history reflects the state.
