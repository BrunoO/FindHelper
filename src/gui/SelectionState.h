#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "search/SearchTypes.h"
#include "utils/HashMapAliases.h"

// Snapshot the display order as path views (no deep copy) for
// SelectionState::RemapSelectionAfterDisplayResultsChange after an in-place
// reorder. Views stay valid as long as the path pool is not replaced — true for
// in-place sorts and filter-cache rebuilds; commit paths remap before replacing.
[[nodiscard]] inline std::vector<std::string_view> SnapshotDisplayPaths(
    const std::vector<SearchResult>& results) {
  std::vector<std::string_view> paths;
  paths.reserve(results.size());
  for (const auto& result : results) {
    paths.push_back(result.fullPath);
  }
  return paths;
}

// All state related to item selection and deletion in the results table.
//
// Selection is primarily tracked via selected_rows_ (sorted vector of indices).
// Single selection is a special case (selected_rows_.size() == 1).
// Deletion state tracks files marked for deletion or pending background removal.
struct SelectionState {  // NOSONAR(cpp:S5414) - Intentional: public camelCase UI fields (GuiState aggregate exception) with private selection indices; see class comment below
  // --- Fields (camelCase matches GuiState UI aggregate exception; see class comment below) ---

  // Request that the results table scrolls to the primary selected row next frame.
  // NOLINTNEXTLINE(readability-identifier-naming) - GuiState UI aggregate camelCase exception
  bool scrollToSelectedRow = false;

  // List of full paths marked for deletion (populated before opening delete popup).
  // NOLINTNEXTLINE(readability-identifier-naming) - GuiState UI aggregate camelCase exception
  std::vector<std::string> filesToDelete;

  // Set of paths currently being deleted in the background.
  // ResultsTable uses this to skip rendering or show "Deleting..." status.
  // NOLINTNEXTLINE(readability-identifier-naming) - GuiState UI aggregate camelCase exception
  std::set<std::string, std::less<>> pendingDeletions;

  // Set of file IDs that are marked for bulk operations (dired-style).
  // NOLINTNEXTLINE(readability-identifier-naming) - GuiState UI aggregate camelCase exception
  hash_set_t<uint64_t> markedFileIds;

  // Popup visibility flags.
  // NOLINTNEXTLINE(readability-identifier-naming) - GuiState UI aggregate camelCase exception
  bool showDeletePopup = false;
  // NOLINTNEXTLINE(readability-identifier-naming) - GuiState UI aggregate camelCase exception
  bool showBulkDeletePopup = false;

  // --- Selection Accessors ---

  [[nodiscard]] bool HasSelection() const {
    return selected_row_ >= 0;
  }

  [[nodiscard]] int GetSelectedRow() const {
    return selected_row_;
  }

  // Read-only access to the full sorted, unique selection vector.
  [[nodiscard]] const std::vector<int>& GetSelectedRows() const {
    return selected_rows_;
  }

  // Returns true when row is present in the multi-selection.
  [[nodiscard]] bool IsRowSelected(int row) const {
    return std::binary_search(selected_rows_.begin(), selected_rows_.end(), row);  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
  }

  // Returns true when the given path is in the pending deletions set.
  [[nodiscard]] bool IsPendingDeletion(std::string_view path) const {
    return pendingDeletions.find(path) != pendingDeletions.end();
  }

  // Returns the "primary" selected row: anchor if valid and selected, else first selected, else -1.
  [[nodiscard]] int GetPrimarySelectedRow() const {
    if (selection_anchor_row_ >= 0 && IsRowSelected(selection_anchor_row_)) {
      return selection_anchor_row_;
    }
    if (!selected_rows_.empty()) {
      return selected_rows_.front();
    }
    return -1;
  }

  // Bounds-checked variant: returns -1 if the primary row is out of range.
  template <typename DisplayResults>
  [[nodiscard]] int GetPrimarySelectedRow(const DisplayResults& display_results) const {
    if (const int primary = GetPrimarySelectedRow();
        primary >= 0 && primary < static_cast<int>(std::size(display_results))) {
      return primary;
    }
    return -1;
  }

  // --- Selection Mutators ---

  // Replace current selection with a single row. Clears multi-selection state.
  void SetSelectedRow(int row) {
    selected_row_ = row;
    selected_rows_.clear();
    selection_anchor_row_ = -1;
    if (row >= 0) {
      selected_rows_.push_back(row);
      selection_anchor_row_ = row;
    }
    AssertSelectionInvariant();
  }

  // Clear all selection state (single-row mirror, multi-selection vector, and anchor).
  void ClearSelection() {
    selected_row_ = -1;
    selected_rows_.clear();
    selection_anchor_row_ = -1;
    AssertSelectionInvariant();
  }

  // Add a row to the multi-selection (sorted, unique). Updates anchor to row.
  void SelectRow(int row) {
    if (const auto it = std::find_if(selected_rows_.begin(), selected_rows_.end(), [row](int r) { return r >= row; });  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
        it == selected_rows_.end() || *it != row) {
      selected_rows_.insert(it, row);
    }
    selection_anchor_row_ = row;
    selected_row_ = row;
    AssertSelectionInvariant();
  }

  // Replace selection with the contiguous range [lo, hi]. anchor_row and primary_row
  // must both lie within [lo, hi]. Triggers AssertSelectionInvariant on exit.
  void SetSelectionRange(int lo, int hi, int anchor_row, int primary_row) {
    assert(lo <= hi);
    assert(anchor_row >= lo && anchor_row <= hi);
    assert(primary_row >= lo && primary_row <= hi);
    selected_rows_.clear();
    selected_rows_.reserve(static_cast<std::size_t>(hi - lo) + 1U);  // hi >= lo asserted above
    for (int i = lo; i <= hi; ++i) {
      selected_rows_.push_back(i);
    }
    selection_anchor_row_ = anchor_row;
    selected_row_ = primary_row;
    AssertSelectionInvariant();
  }

  // Merge the contiguous range [lo, hi] into the existing selection without clearing it.
  // anchor_row comes from ImGui's RangeSrcItem and is not trustworthy: it can be
  // stale (results replaced between frames) or lie outside [lo, hi] (a plain
  // click/double-click carries the previous range anchor). The invariant requires
  // the anchor to be a selected row, so fall back instead of asserting on external
  // input (assert abort = silent close in Debug, observed on double-click open).
  void AddSelectionRange(int lo, int hi, int anchor_row, int primary_row) {
    for (int i = lo; i <= hi; ++i) {
      if (const auto it = std::lower_bound(selected_rows_.begin(), selected_rows_.end(), i);  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
          it == selected_rows_.end() || *it != i) {
        selected_rows_.insert(it, i);
      }
    }
    selected_row_ = primary_row;
    selection_anchor_row_ = anchor_row;
    if (selection_anchor_row_ != -1 && !IsRowSelected(selection_anchor_row_)) {
      selection_anchor_row_ =
        IsRowSelected(primary_row) ? primary_row : GetPrimarySelectedRow();
    }
    AssertSelectionInvariant();
  }

  // Remove all rows in [lo, hi] from the multi-selection. Updates selected_row_
  // and selection_anchor_row_ if they fall inside the removed range.
  void RemoveSelectionRange(int lo, int hi) {
    selected_rows_.erase(
        std::remove_if(selected_rows_.begin(), selected_rows_.end(),  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
                       [lo, hi](int row) { return row >= lo && row <= hi; }),
        selected_rows_.end());
    if (selected_row_ >= lo && selected_row_ <= hi) {
      selected_row_ = selected_rows_.empty() ? -1 : selected_rows_.front();
    }
    if (selection_anchor_row_ >= lo && selection_anchor_row_ <= hi) {
      selection_anchor_row_ = -1;
    }
    AssertSelectionInvariant();
  }

  // Ensure at least one row is selected when results are non-empty.
  template <typename DisplayResults>
  void EnsureSomeSelection(const DisplayResults& display_results) {
    if (!HasSelection() && !std::empty(display_results)) {
      SelectRow(0);
    }
  }

  // Remove selection indices >= row_count and clamp the anchor.
  void ClampSelectionToRowCount(std::size_t row_count) {
    const auto count = static_cast<int>(row_count);
    const auto first_oob = std::find_if(selected_rows_.begin(), selected_rows_.end(), [count](int r) { return r >= count; });  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
    selected_rows_.erase(first_oob, selected_rows_.end());
    if (selection_anchor_row_ >= count) {
      selection_anchor_row_ = -1;
    }
    selected_row_ = GetPrimarySelectedRow();
    AssertSelectionInvariant();
  }

  // Rebuild selected_rows_ so the same logical items remain selected after display_results
  // has been re-sorted or regenerated (entity-based, matched by fullPath).
  // Takes the pre-reorder path order as views (see SnapshotDisplayPaths) instead of
  // a deep-copied result vector — cloning O(N) SearchResults (with owned display
  // strings) on every sort completion showed up as a ~400ms spike in profiling.
  template <typename DisplayResults>
  void RemapSelectionAfterDisplayResultsChange(const std::vector<std::string_view>& old_paths,
                                               const DisplayResults& new_results) {
    if (selected_rows_.empty()) {
      selection_anchor_row_ = -1;
      selected_row_ = -1;
      return;
    }
    // Snapshot paths of currently selected items and the anchor.
    std::vector<std::string_view> selected_paths;
    selected_paths.reserve(selected_rows_.size());
    std::string_view anchor_path;
    for (const int row : selected_rows_) {
      if (row >= 0 && row < static_cast<int>(old_paths.size())) {
        selected_paths.push_back(
          old_paths[static_cast<std::size_t>(row)]);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
      }
    }
    if (selection_anchor_row_ >= 0 && selection_anchor_row_ < static_cast<int>(old_paths.size())) {
      anchor_path =
        old_paths[static_cast<std::size_t>(selection_anchor_row_)];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    }
    // Rebuild from new results (iteration order is already sorted).
    selected_rows_.clear();
    selection_anchor_row_ = -1;
    const auto row_count = static_cast<int>(std::size(new_results));
    for (int new_row = 0; new_row < row_count; ++new_row) {
      const std::string_view path =
        new_results[static_cast<std::size_t>(new_row)].fullPath;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
      const bool was_selected =
        std::find(selected_paths.begin(), selected_paths.end(), path) !=  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
        selected_paths.end();
      if (was_selected) {
        selected_rows_.push_back(new_row);  // already sorted
        if (!anchor_path.empty() && path == anchor_path) {
          selection_anchor_row_ = new_row;
        }
      }
    }
    selected_row_ = GetPrimarySelectedRow();
    AssertSelectionInvariant();
  }

  // --- Focus Synchronization Mutators ---

  // Request that the given row receives ImGui keyboard focus on the next rendered frame.
  void RequestFocusForRow(int row) { focus_row_request_ = row; }

  // Request focus for whichever row GetPrimarySelectedRow() currently returns.
  void RequestFocusForPrimaryRow() { focus_row_request_ = GetPrimarySelectedRow(); }

  // Returns the pending focus-row index and resets it to -1.
  [[nodiscard]] int ConsumeFocusRequest() {
    const int row = focus_row_request_;
    focus_row_request_ = -1;
    return row;
  }

  // Replace current selection with a single row AND request ImGui focus for that row.
  void SetSelectedRowAndFocus(int row) {
    SetSelectedRow(row);
    RequestFocusForRow(row);
  }

  // --- Internal Invariants ---

  // Asserts that selected_rows_ is sorted and unique, and that selection_anchor_row_ is either
  // -1 or a member of selected_rows_. Active only in debug builds.
  void AssertSelectionInvariant() const {
    for (std::size_t i = 1; i < selected_rows_.size(); ++i) {
      assert(selected_rows_[i - 1] < selected_rows_[i]);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    }
    assert(selection_anchor_row_ == -1 || IsRowSelected(selection_anchor_row_));
  }

 private:
  int selected_row_ = -1;                // Primary/last selected row
  std::vector<int> selected_rows_;     // All selected indices (sorted, unique)
  int selection_anchor_row_ = -1;        // Anchor for Shift+click range selection
  int focus_row_request_ = -1;           // Pending focus request for ImGui sync
};
