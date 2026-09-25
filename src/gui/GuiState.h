#pragma once

#include <algorithm>
#include <array>  // NOLINT(clang-diagnostic-error) - False positive on macOS header analysis
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <future>
#include <iterator>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "api/GeminiApiUtils.h"
#include "filters/SizeFilter.h"  // SizeFilter enum (separated for testability)
#include "filters/TimeFilter.h"  // TimeFilter enum (separated for testability)
#include "gui/SearchCriteria.h"
#include "imgui.h"
#include "search/SearchInputField.h"
#include "search/SearchTypes.h"
#include "utils/AsyncUtils.h"
#include "utils/HashMapAliases.h"
#include "utils/ThreadUtils.h"

// A container that asserts (in debug builds) it is only mutated on the UI thread.
// Reads (const access) are allowed from any thread — render functions that take
// const GuiState& automatically use the const overload which does not assert.
//
// In release builds the assert is compiled away: zero overhead.
//
// Usage:
//   UIThreadOwned<std::vector<Foo>> vec;
//   *vec             // mutable reference — asserts IsUIThread()
//   vec->method()    // mutable call     — asserts IsUIThread()
//   *std::as_const(vec)  // const reference — no assert
//   (const context)  // const overloads called automatically
template <typename T>
class UIThreadOwned {
 public:
  [[nodiscard]] T& operator*() {
    assert(IsUIThread());
    return value_;
  }
  [[nodiscard]] const T& operator*() const { return value_; }
  [[nodiscard]] T* operator->() {
    assert(IsUIThread());
    return &value_;
  }
  [[nodiscard]] const T* operator->() const { return &value_; }

 private:
  T value_{};  // NOLINT(readability-identifier-naming)
};

// Forward declaration to avoid circular include with SearchTypes.h
struct SearchResult;

// A token to signal cancellation to running sort attribute loading tasks.
// This prevents tasks from continuing to run when a new sort is requested.

struct SortCancellationToken {
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like token; camelCase matches surrounding UI/token style
  std::shared_ptr<std::atomic<bool>> cancelled = std::make_shared<std::atomic<bool>>(false);

  // NOLINTNEXTLINE(readability-make-member-function-const) - False positive: not libc code; Cancel() modifies state
  void Cancel() {
    if (cancelled) {
      cancelled->store(true);
    }
  }

  [[nodiscard]] bool IsCancelled() const {
    return cancelled && cancelled->load();
  }
};

// A generation counter to uniquely identify each sort operation.
// This prevents race conditions where results from an old (and superseded)
// sort operation could overwrite the results of a newer one.
using SortGeneration = uint64_t;

// Three-way state machine for an async sort operation.
// Replaces the previous dual-bool (sortDataReady + loading) that allowed
// illegal combinations. Illegal combinations are now unrepresentable.
//
//  Idle    — no sort in progress; counter null, no visible status bar message.
//  Loading — tasks are running; counter > 0; "Loading attributes..." shown.
//  Ready   — tasks complete or nothing to load; sort can proceed this frame.
enum class SortReadyState : uint8_t {
  Idle,     // No sort in progress.
  Loading,  // Tasks are running; counter > 0.
  Ready,    // Tasks complete or pre-loaded; apply + sort can proceed.
};

// All state for one async sort operation: cancellation token, countdown counter,
// ready-state machine, and staging vectors that workers write under mutex_.
//
// PrefetchAndFormatSortDataBlocking allocates a local AsyncSortState to reuse
// StartAttributeLoadingAsync without constructing a full GuiState.
//
// Lifetime rules:
//  - counter, sort_ready_state_, token — UI-thread-only; no mutex required.
//  - sizes_, times_, flags_ — written by worker threads under mutex_.
//  - generation_ — incremented only by BeginNewSort() on the UI thread; never
//    zeroed; monotonically increasing.
//  - staging_generation_ — tag written when staging buffers are (re)allocated;
//    ApplyPendingSortAttributeUpdates rejects values when this does not match the
//    sort_generation passed from the UI (superseded sort).
struct AsyncSortState {
  // Countdown latch: initialised to task_count before enqueueing; each task
  // decrements on completion (including cancellation). Null when idle.
  // NOLINTNEXTLINE(readability-identifier-naming)
  std::shared_ptr<std::atomic<int>> counter;

  // Three-way state machine replacing the previous dual-bool (sortDataReady + loading).
  // NOLINTNEXTLINE(readability-identifier-naming)
  SortReadyState sort_ready_state_ = SortReadyState::Idle;

  // Cancellation token shared with worker tasks via pointer in SortAttributeEnqueueContext.
  // Replaced (not mutated) by BeginNewSort() — always after draining old tasks.
  // NOLINTNEXTLINE(readability-identifier-naming)
  SortCancellationToken token;

  // Canonical sort-operation counter (BeginNewSort only).
  // NOLINTNEXTLINE(readability-identifier-naming)
  SortGeneration generation_ = 0;

  // Generation tag for the current staging buffer contents (set when buffers are
  // prepared; may differ from generation_ only in tests that simulate stale staging).
  // NOLINTNEXTLINE(readability-identifier-naming)
  SortGeneration staging_generation_ = 0;

  // Last user-requested sort specification (ResultsTable header click via
  // HandleTableSorting writes; SearchController sorts/drain reads). Kept here
  // so the full sort contract lives in one single-writer substate.
  int last_sort_column = -1;
  ImGuiSortDirection last_sort_direction = ImGuiSortDirection_None;

  // Staging buffers written by worker threads under mutex_.
  std::vector<uint64_t> sizes_{};   // NOLINT(readability-identifier-naming,readability-redundant-member-init)
  std::vector<FILETIME>  times_{};  // NOLINT(readability-identifier-naming,readability-redundant-member-init)
  std::vector<std::byte> flags_{};  // NOLINT(readability-identifier-naming,readability-redundant-member-init)
  std::mutex mutex_;                 // NOLINT(readability-identifier-naming)

  // True while tasks are actively running (Loading state). Used by the status bar
  // and UpdateDisplayedTotalSizeIfNeeded to defer work until loading completes.
  [[nodiscard]] bool IsLoading() const {
    return sort_ready_state_ == SortReadyState::Loading;
  }

  // True when a sort is pending — either tasks are running (Loading) or all data
  // is available and the sort can proceed this frame (Ready). False only when Idle.
  [[nodiscard]] bool HasPendingSort() const {
    return sort_ready_state_ != SortReadyState::Idle;
  }

  // Test-only: directly transition to Ready to bypass counter check.
  // Mirrors what StartSortAttributeLoading does when no tasks are enqueued.
  void ForceReadyForTest() {
    sort_ready_state_ = SortReadyState::Ready;
  }

  // Cancel current tasks, install a fresh cancellation token, and bump the
  // generation counter. Returns the new generation.
  //
  // The caller must drain in-flight tasks (spin on counter) between calling
  // token.Cancel() / BeginNewSort() and reusing any shared buffer, as usual.
  // BeginNewSort() cancels the old token as its first action so the caller can
  // also pre-cancel before the drain and rely on BeginNewSort() being idempotent.
  SortGeneration BeginNewSort() {
    token.Cancel();
    token = SortCancellationToken{};
    ++generation_;
    return generation_;
  }

  // Clear all in-flight state. Safe to call from the UI thread after counter has
  // reached zero (i.e. all tasks have drained).
  // Does NOT modify generation_ — that counter is monotonically increasing and is
  // only advanced by BeginNewSort().
  void Reset() {
    counter.reset();
    sort_ready_state_ = SortReadyState::Idle;
    const std::scoped_lock lock(mutex_);
    sizes_.clear();
    times_.clear();
    flags_.clear();
    staging_generation_ = 0;
  }
};

// Bundles the search result path pool, the result vector that depends on it,
// and the batch-change counter. The only safe way to clear both is through
// Clear(), which always clears results before pool — eliminating the
// pool-lifecycle bug where pool was cleared while results held string_views into it.
struct ResultPoolOwner {
  // Clear results_ first (safe — no dangling views), then pool_, then bump batch_number_.
  void Clear() {
    results_.clear();
    results_.shrink_to_fit();
    pool_.clear();
    pool_.shrink_to_fit();
    ++batch_number_;
  }

  // Increment batch_number_ without clearing results or pool (invalidates consumers that
  // compare batch numbers, e.g. after in-place result replacement). See SearchController.
  void BumpBatchNumber() { ++batch_number_; }

  [[nodiscard]] const std::vector<SearchResult>& Results() const { return results_; }
  [[nodiscard]] std::vector<SearchResult>& Results() { return results_; }
  [[nodiscard]] const std::vector<char>& Pool() const { return pool_; }
  // Mutable pool access for MergeAndConvertToSearchResults and path-pool growth on apply.
  [[nodiscard]] std::vector<char>& Pool() { return pool_; }
  [[nodiscard]] uint64_t BatchNumber() const { return batch_number_; }

 private:
  // NOLINTNEXTLINE(readability-identifier-naming)
  std::vector<SearchResult> results_;
  // NOLINTNEXTLINE(readability-identifier-naming)
  std::vector<char> pool_;
  // NOLINTNEXTLINE(readability-identifier-naming)
  uint64_t batch_number_ = 0;
};

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

// Export CSV workflow state: notification banner + export-result modal popup.
// Writers: export service (Application) + RenderExportResultPopup (Popups.cpp).
// GuiState::ClearInputs may only clear notification / errorMessage / notificationTime —
// clearing must not blank an open export modal (see ClearInputs).
struct ExportWorkflowState {
  std::string notification;
  std::string error_message;
  std::chrono::steady_clock::time_point notification_time = std::chrono::steady_clock::now();
  bool show_popup = false;
  bool success = false;
  std::string file_path;
  std::size_t result_count = 0;
};

// Gemini API-assisted search workflow state.
// Writers: AI-assisted search panel (SearchInputsGeminiHelpers) only.
// description_input stays a fixed 512-byte buffer: ImGui::InputTextMultiline requires
// char* (performance-critical, called every frame).
struct GeminiWorkflowState {
  std::array<char, 512> description_input = {};
  bool api_call_in_progress = false;
  std::future<gemini_api_utils::GeminiApiResult> api_future;  // default-constructed; assigned when API call starts
  std::string error_message;
  std::chrono::steady_clock::time_point error_display_time = std::chrono::steady_clock::now();
};

// Initial index build progress shared across all platforms.
// Writer: Application::UpdateIndexBuildState from the per-frame IsIndexBuilding()
// snapshot (IndexBuildState::active alone is insufficient — USN monitor / finalize
// phases are included). Keep in_progress in sync with UIActions::IsIndexBuilding()
// so status bar timing and search gating stay consistent.
struct IndexBuildProgressState {
  bool in_progress = false;     // True while initial index is building
  bool failed = false;          // True if initial index build failed
  size_t entries_processed = 0; // Total entries (files + dirs) processed
  size_t files_processed = 0;   // Files processed during initial build
  size_t dirs_processed = 0;    // Directories processed during initial build
  size_t error_count = 0;       // Errors encountered during build
  std::string status_text;      // Human-readable status for UI
  std::chrono::steady_clock::time_point start_time;  // Start time of current index build
  uint64_t last_duration_ms = 0;  // Duration in milliseconds of last completed index build
  bool has_timing = false;      // True once timing initialized for the current session
};

// Search history panel interaction state. Stable ids (not indices) so mutations
// never silently invalidate them; selected_id is validated each frame.
// Writers: SearchHistoryWindow rendering + rename/delete popups.
struct HistoryInteractionState {
  std::string selected_id;
  std::string pending_rename_id;  // Id of entry awaiting rename; triggers RenderHistoryRenamePopup
  std::string pending_delete_id;  // Id of entry awaiting deletion; triggers RenderHistoryDeletePopup
};

// Monotonic version of the search-result set (god-object Phase 3).
// Bumped whenever the result content changes (new batch applied, cloud
// attrinas complete, sort invalidation). Filter caches capture it when they
// rebuild; validity requires the version to still match.
using ResultsVersion = uint64_t;

// One filter cache slice. Writers: SearchResultUtils cache-update helpers only.
// snake_case POD public members (same rule as other GuiState substates).
struct TimeFilterCacheSlice {
  std::vector<SearchResult> results;
  size_t count = 0;
  TimeFilter cached_filter = TimeFilter::None;
  bool valid = false;
  ResultsVersion captured_version = 0;

  // Validity: stored AND the results version has not changed since.
  [[nodiscard]] bool IsValidFor(ResultsVersion version) const {
    return valid && captured_version == version;
  }
  // Record a (re)build against the current results version.
  void MarkStoredVersion(ResultsVersion version) {
    valid = true;
    captured_version = version;
  }
};

struct SizeFilterCacheSlice {
  std::vector<SearchResult> results;
  size_t count = 0;
  SizeFilter cached_filter = SizeFilter::None;
  bool valid = false;
  ResultsVersion captured_version = 0;

  // Validity / store contract identical to TimeFilterCacheSlice.
  [[nodiscard]] bool IsValidFor(ResultsVersion version) const {
    return valid && captured_version == version;
  }
  // Record a (re)build against the current results version.
  void MarkStoredVersion(ResultsVersion version) {
    valid = true;
    captured_version = version;
  }
};

// Progressive "total size of displayed results" accumulator + valid flag.
struct TotalSizeProgressState {
  uint64_t bytes = 0;         // Sum of file sizes of the displayed set
  bool valid = false;         // False ⇒ computation is (re)starting
  size_t computation_index = 0; // Next display-result index to scan
  uint64_t computation_bytes = 0; // Partial sum accumulated so far

  void ResetProgress() {
    computation_index = 0;
    computation_bytes = 0;
  }
  // Invalidate: mark the sum stale and clear partial progress.
  void Invalidate() {
    valid = false;
    ResetProgress();
  }
};

// Bundles time-filter / size-filter caches and the displayed-total-size
// progressive state (Phase 3: replaces four independent *_valid bools; a slice
// is stale when its captured_version no longer matches GetResultsVersion()).
struct ResultFilterCaches {
  TimeFilterCacheSlice time;
  SizeFilterCacheSlice size;
  TotalSizeProgressState total_size;
};

// Search-lifecycle state (god-object Phase 4). Sole writer: SearchController
// (plus the workflow helpers it calls); render code and status bar read-only.
// snake_case public members per the substate naming rule.
struct SearchPipelineState {
  // True while a search is running (used to gate triggers and skip re-polls).
  bool search_active = false;
  // True when the user is typing (debounce input) so SearchController discards in-flight results.
  bool results_complete = true;
  uint64_t search_session_id = 0;
  std::string search_error;

  // True if the current search was manually triggered (button/Enter); false for debounced/auto-refresh.
  // Used by Application to decide whether to record a search history entry.
  bool search_was_manual = false;
  // Set when PollResults completes a manual search. Survives auto-refresh/debounce
  // in the same frame, which clears search_was_manual before Application records history.
  // Application::UpdateSearchState consumes it (sole consumer) via ConsumeManualHistoryRecord().
  bool pending_manual_history_record = false;
  // One-shot consume (set-once/clear-once state-machine style, mirroring AsyncSortState
  // helpers): returns the previous value and clears the flag (result optional;
  // the primary use here is the clear side effect).
  bool ConsumeManualHistoryRecord() {
    return std::exchange(pending_manual_history_record, false);
  }

  // Index mutation version at the last auto-refresh trigger (baseline).
  // Version-based (not size-based) so heals and renames — which keep Size()
  // — also refresh visible results.
  uint64_t last_index_mutation = 0;
  // Timestamp of the last auto-refresh trigger; enforces a cooldown between
  // refreshes so rapid USN activity (bulk ops) does not re-trigger every frame.
  std::chrono::steady_clock::time_point last_auto_refresh_time;

  // One-shot commit flags: set by PollResults / folder-stats; consumed per frame.
  bool results_updated = false;
  // Set by PollResults after committing a pre-sorted back buffer. Consumed by
  // HandleTableSorting so sync columns skip a redundant clone+re-sort+re-remap.
  bool results_presorted_on_commit = false;
  // Set by ClearInputs; consumed by SearchController::Update to call
  // SearchWorker::DiscardResults so PollResults does not re-apply stale results.
  bool clear_results_requested = false;
  // One-frame defer when active filters need unloaded attrs
  // (see SearchResultUtils::ShouldDeferFilterCacheRebuild).
  bool defer_filter_cache_rebuild = false;

  // FolderSizeAggregator cache staleness cluster (see struct comment above).
  size_t last_folder_aggregator_index_size = 0;
  bool folder_aggregator_cache_stale = false;
  size_t pending_folder_aggregator_index_size = 0;
  bool folder_aggregator_index_size_initialized = false;
  std::chrono::steady_clock::time_point last_folder_aggregator_reset_time;

  // FlushAggregatorFolderStats idle fast-path (perf): results version at the last
  // full pending-dir scan + whether that scan found any pending directories.
  // Lets steady-state frames (same results, aggregator idle, nothing pending) skip
  // the O(N) scan. Sole writer: FlushAggregatorFolderStats itself (UI thread).
  ResultsVersion last_folder_stats_flush_version = 0;
  bool folder_stats_flush_had_pending = false;
};

// Cloud-file attribute loading workflow (god-object Phase 6): bundles the
// deferred cloud-file set and its background-loading futures. Cloud files are
// included optimistically in time-filter results until their attributes load
// in the background; completion invalidates the time cache (see
// SearchResultUtils::CleanUpCloudFutures).
struct CloudFileWorkflowState {
  // File IDs of cloud files being loaded asynchronously; hash_set_t for FAST_LIBS_BOOST.
  hash_set_t<uint64_t> deferred_ids;
  // Futures for background cloud file attribute loading.
  std::vector<std::future<void>> loading_futures;

  // Enqueue a file ID for deferred loading; returns true if newly enqueued.
  bool TryEnqueue(uint64_t file_id) { return deferred_ids.insert(file_id).second; }
  // Clear the deferred set only — in-flight futures keep running and are
  // self-drained by CleanUpCloudFutures on subsequent frames.
  void ClearDeferred() { deferred_ids.clear(); }
  // Teardown/reset path: wait for every in-flight future, release it, and
  // clear both the futures vector and the deferred set. Blocking wait is
  // intentional and matches the pre-refactor ClearInputs/Stop cleanup.
  void WaitAndDrain() {
    for (auto& future : loading_futures) {
      async_utils::SafeWaitFuture(future);
    }
    loading_futures.clear();
    deferred_ids.clear();
  }
};

// Input-trigger and debounce state (god-object Phase 8): the search-trigger
// toggles plus the per-frame debounce bookkeeping. Single writer: input
// widgets (SearchInputs) and preset/config appliers.
struct InputDebounceState {
  // Search-as-you-type (with debounce) - default off.
  bool instant_search = false;
  // Re-run search when the index changes.
  bool auto_refresh = false;
  // Last keystroke timestamp; SearchController uses it for the debounce window.
  std::chrono::steady_clock::time_point last_input_time = std::chrono::steady_clock::now();
  // Debounce bookkeeping: true while the user is typing (consumed per frame by SearchController).
  bool input_changed = false;
  // One-shot: set to focus the filename input next frame (consumed by the widget itself).
  bool focus_filename_input = false;
};

// UI visibility/latch state (god-object Phase 8): panel expansion
// toggles, help/popup visibility and context-menu latch. Each field's sole
// writer is the widget(s) rendered from that same domain (see ownership map
// "UI visibility" row).
struct UiVisibilityState {
  // Tracks whether the Manual Search section is expanded (stored explicitly to
  // avoid relying on ImGui internal state across layout changes).
  bool manual_search_expanded = true;
  // Tracks whether the AI-Assisted Search section is expanded (collapsed by default).
  bool ai_search_expanded = false;
  // Quick Filters / Last Modified sections visibility (hidden by default).
  bool show_quick_filters = false;
  // True when the inline "Filter in results" prompt is visible; when set,
  // ApplicationLogic skips Escape "Clear all filters" so Esc cancels the inline filter.
  bool incremental_search_active = false;
  // Regex generator popup visibility (normal + filename-prefill variant).
  bool open_regex_generator_popup = false;
  bool open_regex_generator_popup_filename = false;
  // Context-menu latch + debounce timestamp (prevents multiple opens per press).
  bool context_menu_open = false;
  std::chrono::steady_clock::time_point last_context_menu_time = std::chrono::steady_clock::now();
  // Help / search-syntax window visibility flags.
  bool show_help_window = false;
  bool show_search_help_window = false;
};

// GUI State class to encapsulate all UI state.  // NOSONAR(cpp:S125) - S125 reports this comment block starting here, but it plus the ownership-map table below are authoritative docs, not commented-out code (see 5813e033)
// NOSONAR - Intentional, kept: FIELD OWNERSHIP MAP below documents each mutable
// field cluster and its single writer (markdown table rows below look like
// code to the S125 detector, but are authoritative documentation).
//
// FIELD OWNERSHIP MAP (Phase 0 of the god-object decomposition plan; see
// internal-docs/plans/2026-09-11_GUISTATE_GOD_OBJECT_DECOMPOSITION_PLAN.md).
// Before adding a write site for any field below, declare it here. Every
// future substate extraction must keep each struct single-writer.
//
// | Domain (planned substate)     | Fields                                  | Writers (sole unless noted)              |
// |-------------------------------|-----------------------------------------|------------------------------------------|
// | SearchCriteria (Phase 2)      | searchCriteria (SearchCriteria.h,       | user input events; presets via           |
// |                               | snake_case fields): extension_input,    | ApplySearchConfig()/ApplyShowAllPreset() |
// |                               | filename_input, path_input, folders_    |                                          |
// |                               | only, case_sensitive, time_filter,      |                                          |
// |                               | size_filter, instant_search, auto_      |                                          |
// |                               | refresh                                 |                                          |
// | Result caches (Phase 3)       | result_pool_, filter_caches (time/size   | SearchController (+ ResultsTable via     |
// |                               | slices + total_size progress; validity   | SearchResultUtils update helpers only)   |
// |                               | = valid && captured_version ==           | (BumpResultsVersion on results change)   |
// |                               | GetResultsVersion())                     |                                          |
// | Search pipeline (Phase 4)     | search_pipeline (SearchPipelineState     | SearchController (triggers, PollResults, |
// |                               | struct, snake_case): search_active,      | ClearInputs, check helpers in            |
// |                               | results_complete, search_session_id,     | SearchControllerDetail.h). Documented    |
// |                               | search_error, search_was_manual,         | co-writer: Application::UpdateSearchState|
// |                               | pending_manual_history_record,           | consumes pending_manual_history_record   |
// |                               | results_updated, results_presorted_      | (ConsumeManualHistoryRecord) and clears  |
// |                               | on_commit, clear_results_requested,      | search_was_manual after the history      |
// |                               | defer_filter_cache_rebuild,              | check                                    |
// |                               | last_index_mutation,                     |                                          |
// |                               | last_auto_refresh_time,                 |                                          |
// |                               | folder-aggregator staleness cluster:     |                                          |
// |                               | last_folder_aggregator_index_size,       |                                          |
// |                               | folder_aggregator_cache_stale,           |                                          |
// |                               | pending_folder_aggregator_index_size,    |                                          |
// |                               | folder_aggregator_index_size_initialized,|                                          |
// |                               | last_folder_aggregator_reset_time,      |                                          |
// |                               | flush fast-path:                        | FlushAggregatorFolderStats (UI thread,   |
// |                               | last_folder_stats_flush_version,        | via ResultsTable render)                 |
// |                               | folder_stats_flush_had_pending          |                                          |
// | Cloud-file loading (Ph. 6)    | cloud_files (CloudFileWorkflowState):    | SearchController/SearchResultUtils       |
// |                               | deferred_ids, loading_futures (enqueue   | (TryEnqueue/queue futures/CleanUpCloud-  |
// |                               | + teardown only via the struct methods)  | Futures); teardown: ClearInputs +        |
// |                               |                                          | Application shutdown (WaitAndDrain)      |
// | Async sort                    | async_sort_, lastSortColumn,            | ResultsTable (sort trigger + sort spec), |
// |                               | lastSortDirection, completed_sort_,     | SearchController (BeginNewSort/drain)    |
// |                               | computingFolderSizes                    | FolderSizeAggregator client side         |
// | Selection / deletion          | selection                               | ResultsTable (keyboard + menus)          |
// | Export workflow (Phase 1)     | export_workflow: notification,          | export service + ExportCsvPopup          |
// |                               | error_message, notification_time,       |                                          |
// |                               | show_popup, success, file_path,         |                                          |
// |                               | result_count                            |                                          |
// | Gemini workflow (Phase 1)     | gemini: description_input,              | AI-assisted search panel                 |
// |                               | api_call_in_progress, api_future,       |                                          |
// |                               | error_message, error_display_time       |                                          |
// | Index build progress (Ph. 1)  | index_build: in_progress + 9 fields     | Application::UpdateIndexBuildState       |
// | History interaction (Ph. 1)   | history: selected_id,                   | SearchHistoryWindow + popups             |
// |                               | pending_rename_id, pending_delete_id    |                                          |
// | UI visibility (Phase 8)        | ui_visibility (UiVisibilityState, snake_case):   | respective widgets (Render*)             |
// |                               | manual_search_expanded, ai_search_       |                                          |
// |                               | expanded, show_quick_filters, show_help_ |                                          |
// |                               | window, show_search_help_window,         |                                          |
// |                               | open_regex_generator_popup*,             |                                          |
// |                               | context_menu_open, last_context_menu_    |                                          |
// |                               | time, incremental_search_active          |                                          |
// | Input debounce (Ph. 8)        | input_debounce (InputDebounceState,      | input events (SearchInputs widget);      |
// |                               | snake_case): instant_search,             | presets via ApplySearchConfig etc.       |
// |                               | auto_refresh, last_input_time,           |                                          |
// |                               | input_changed, focus_filename_input      |                                          |

//
// Naming: Public members use camelCase (not snake_case_) by design. This matches
// common UI/widget naming (e.g. ImGui, JavaScript UI state) and keeps access
// at call sites readable (e.g. state.selected_row_, state.searchCriteria.time_filter). A few
// internal members use snake_case_ with trailing underscore per project
// convention (e.g. async_sort_, gemini.description_input).
// See docs/standards/CXX17_NAMING_CONVENTIONS.md; this is the documented
// exception for UI state aggregates.
//
class GuiState {
 public:
  // Search criteria inputs — see SearchCriteria.h (god-object Phase 2).
  // Single writer: user-input widgets and presets
  // (ApplySearchConfig / ApplyShowAllPreset); SearchController snapshots this
  // struct at search-trigger time and builds SearchParams from the snapshot.
  // NOLINTNEXTLINE(readability-identifier-naming) - GuiState UI aggregate camelCase exception
  SearchCriteria searchCriteria;

  // Search state
  // result_pool_: bundles the path-pool vector, the results vector that
  // holds string_views into it, and the batch-change counter.
  // Use result_pool_->Clear() (not direct field access) to clear both — it
  // enforces results-before-pool ordering to prevent dangling string_views.
  // UIThreadOwned enforces that mutations only occur on the UI thread (assert in
  // debug builds). Const access (render functions, read-only helpers) uses the
  // const overload and is never asserted. See UIThreadOwned<T> in GuiState.h.
  //
  // Batch-number versioning protocol (result_pool_->BatchNumber()):
  //   Incrementors: SearchController::ClearResultPool (on search start / discard) and
  //     SearchController::PollResults (once when double-buffered search completes and swaps).
  //     BumpBatchNumber() is the only mutating call.
  //   Consumers:
  //     • IncrementalSearchState::CheckBatchNumber — detects a new result set and drops
  //       its filtered_results_ cache (which holds SearchResult copies with string_views
  //       into the old pool; stale views dangle after pool reallocation).
  //     • ResultsTable folder-stats rebuild predicate — compares batch number against
  //       the value captured at last rebuild; mismatches force a cache refresh.
  //   Contract: consumers test (current != captured), not a specific absolute value, so
  //   double-increments (e.g., clear then apply in the same frame) are harmless.
  //   Never add a new consumer that relies on a specific count value — use != only.
  // NOLINTNEXTLINE(readability-identifier-naming)
  UIThreadOwned<ResultPoolOwner> result_pool_;
  // Cached results after applying time filter (to avoid re-filtering every frame)
  // Phase 3: see ResultFilterCaches above; a slice is valid iff
  // valid && captured_version == GetResultsVersion() && cached_filter == current filter.
  // NOLINTNEXTLINE(readability-identifier-naming) - GuiState UI aggregate camelCase exception
  ResultFilterCaches filter_caches;

  // Monotonic version of the current result set. Bumped by BumpResultsVersion()
  // whenever result content changes (commit, cloud attr completion...). Filter
  // cache slices capture it when (re)built; mismatch ⇒ stale.
  ResultsVersion results_version_ = 1;  // NOLINT(readability-identifier-naming) - snake_case_ private member
  /** Monotonic version for filter caches: bump whenever results change. */
  void BumpResultsVersion() { ++results_version_; }
  /** Current results version (captured by caches when rebuilt). */
  [[nodiscard]] ResultsVersion GetResultsVersion() const { return results_version_; }

  /** Resets progressive computation state (index, accumulator). */
  void ResetDisplayedTotalSizeProgress() {
    filter_caches.total_size.ResetProgress();
  }

  /** Invalidates the time/size filter cache slices and the displayed total size.
   *  Does NOT clear the cached vectors — use after an in-place sort where
   *  rebuilding reuses the vectors; stores capture GetResultsVersion() again. */
  void InvalidateFilterCacheFlags() {
    filter_caches.time.valid = false;
    filter_caches.size.valid = false;
    InvalidateDisplayedTotalSize();
  }

  /** Invalidates displayed total size cache and resets progressive computation state. */
  void InvalidateDisplayedTotalSize() {
    filter_caches.total_size.Invalidate();
  }

  // Phase 4: search-pipeline state (see SearchPipelineState struct above).
  // Sole writer: SearchController (+ workflow helpers it calls); render code
  // and status bar read-only.
  // NOLINTNEXTLINE(readability-identifier-naming) - GuiState UI aggregate camelCase exception
  SearchPipelineState search_pipeline;
  // Deferred-cloud-file loading workflow (Phase 6): see CloudFileWorkflowState.
  // Single writer: SearchController/SearchResultUtils (enqueue + reset paths);
  // futures are drained by CleanUpCloudFutures and teardown (WaitAndDrain).
  // NOLINTNEXTLINE(readability-identifier-naming) - GuiState UI aggregate camelCase exception
  CloudFileWorkflowState cloud_files;
  // UI visibility/latch flags and input-debounce state (Phase 8) —
  // see UiVisibilityState / InputDebounceState above; per-field writers stay with
  // the corresponding widgets (see FIELD OWNERSHIP MAP rows below).
  // NOLINTNEXTLINE(readability-identifier-naming) - GuiState UI aggregate camelCase exception
  UiVisibilityState ui_visibility;
  InputDebounceState input_debounce;


  // Export workflow state (export notification banner + export CSV modal popup).
  // See ExportWorkflowState above; clear must not blank an open export modal.
  // NOLINTNEXTLINE(readability-identifier-naming) - GuiState UI aggregate camelCase exception
  ExportWorkflowState export_workflow;
  // Gemini API integration state (see GeminiWorkflowState for invariants).
  // NOLINTNEXTLINE(readability-identifier-naming) - GuiState UI aggregate camelCase exception
  GeminiWorkflowState gemini;

  // Memory usage tracking (updated every 10 seconds to avoid costly system calls)
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, snake_case with trailing underscore is intentional
  size_t memory_bytes_ = 0;  // Current memory usage in bytes
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, snake_case with trailing underscore is intentional
  std::chrono::steady_clock::time_point last_memory_update_time_ =
    std::chrono::steady_clock::now();  // Last time memory was updated

  // Index build progress (shared across all platforms) — single writer:
  // Application::UpdateIndexBuildState. See IndexBuildProgressState above.
  // NOLINTNEXTLINE(readability-identifier-naming) - GuiState UI aggregate camelCase exception
  IndexBuildProgressState index_build;


  // Attribute loading state (for status bar display and async loading)
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool computingFolderSizes =
    false;  // True when FolderSizeAggregator has pending background work
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, snake_case with trailing underscore is intentional
  SortGeneration completed_sort_generation_ = 0;  // Generation of the last completed sort
  // All async-sort in-flight state: state machine, counter, token, generation, staging buffers.
  // See AsyncSortState / SortReadyState for invariants and thread-ownership rules.
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, snake_case with trailing underscore is intentional
  AsyncSortState async_sort_;

  // Selection and Deletion state
  // Encapsulated in SelectionState to address large-aggregate complexity and improve cohesion.
  // NOLINTNEXTLINE(readability-identifier-naming)
  SelectionState selection;

  // Search History panel interaction state (see HistoryInteractionState above).
  // NOLINTNEXTLINE(readability-identifier-naming) - GuiState UI aggregate camelCase exception
  HistoryInteractionState history;



  void MarkInputChanged();
  void ClearInputs();

  /**
   * Build SearchParams from current GuiState values.
   *
   * Converts GuiState input fields into SearchParams for the SearchWorker
   * (filename, path, extensions, foldersOnly, caseSensitive, etc.).
   *
   * @return SearchParams struct ready for SearchWorker::StartSearch()
   */
  [[nodiscard]] SearchParams BuildCurrentSearchParams() const;

  /**
   * Apply a search configuration from JSON (typically from Gemini API).
   *
   * This method applies a SearchConfig to the GuiState, updating all relevant
   * fields. Missing fields in the config use defaults (empty strings, false, None).
   *
   * @param config Search configuration to apply
   */
  void ApplySearchConfig(const gemini_api_utils::SearchConfig& config);

  /**
   * Apply a built-in "Show all indexed files" preset.
   *
   * Clears filename, extension, time, and size filters, then sets the path
   * input to a catch-all path pattern (`pp:**`) and marks input as changed
   * so the next manual search will return all indexed entries.
   */
  void ApplyShowAllPreset();
};

