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
#include <vector>

#include "api/GeminiApiUtils.h"
#include "filters/SizeFilter.h"  // SizeFilter enum (separated for testability)
#include "filters/TimeFilter.h"  // TimeFilter enum (separated for testability)
#include "imgui.h"
#include "search/SearchInputField.h"
#include "search/SearchTypes.h"
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
    pool_.clear();
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
  void AddSelectionRange(int lo, int hi, int anchor_row, int primary_row) {
    for (int i = lo; i <= hi; ++i) {
      if (const auto it = std::lower_bound(selected_rows_.begin(), selected_rows_.end(), i);  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
          it == selected_rows_.end() || *it != i) {
        selected_rows_.insert(it, i);
      }
    }
    selection_anchor_row_ = anchor_row;
    selected_row_ = primary_row;
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
  template <typename DisplayResults>
  void RemapSelectionAfterDisplayResultsChange(const DisplayResults& old_results,
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
      if (row >= 0 && row < static_cast<int>(old_results.size())) {
        selected_paths.push_back(
          old_results[static_cast<std::size_t>(row)].fullPath);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
      }
    }
    if (selection_anchor_row_ >= 0 && selection_anchor_row_ < static_cast<int>(old_results.size())) {
      anchor_path =
        old_results[static_cast<std::size_t>(selection_anchor_row_)].fullPath;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
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

// GUI State class to encapsulate all UI state.
//
// Naming: Public members use camelCase (not snake_case_) by design. This matches
// common UI/widget naming (e.g. ImGui, JavaScript UI state) and keeps access
// at call sites readable (e.g. state.selected_row_, state.timeFilter). A few
// internal members use snake_case_ with trailing underscore per project
// convention (e.g. async_sort_, gemini_description_input_).
// See docs/standards/CXX17_NAMING_CONVENTIONS.md; this is the documented
// exception for UI state aggregates.
//
// NOLINTNEXTLINE(clang-analyzer-optin.performance.Padding) - GuiState is intentionally a POD-like aggregate (62 fields) for UI state management; padding optimization not critical for UI state
class GuiState {  // NOSONAR(cpp:S1820) - GuiState aggregates all UI state (62 fields): splitting would require passing multiple objects, reducing encapsulation and increasing complexity
 public:
  // Input fields (encapsulated to address "Primitive Obsession" code smell)
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  SearchInputField extensionInput;
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  SearchInputField filenameInput;
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  SearchInputField pathInput;
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool foldersOnly = false;
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool caseSensitive = false;                // If true, case-sensitive search; if false,
                                             // case-insensitive (default)
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  TimeFilter timeFilter = TimeFilter::None;  // Time-based filter for "Last Modified"
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  SizeFilter sizeFilter = SizeFilter::None;  // Size-based filter for file size

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
  //     SearchController::ApplyNonStreamingSearchResults (once when the search completes).
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
  // NOLINTNEXTLINE(readability-identifier-naming,readability-redundant-member-init) - POD-like struct for UI state, camelCase is intentional, {} makes intent explicit
  std::vector<SearchResult> filteredResults{};
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  size_t filteredCount = 0;
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  TimeFilter cachedTimeFilter = TimeFilter::None;
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool timeFilterCacheValid = false;
  // Cached results after applying size filter (to avoid re-filtering every frame)
  // NOLINTNEXTLINE(readability-identifier-naming,readability-redundant-member-init) - POD-like struct for UI state, camelCase is intentional, {} makes intent explicit
  std::vector<SearchResult> sizeFilteredResults{};
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  size_t sizeFilteredCount = 0;
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  SizeFilter cachedSizeFilter = SizeFilter::None;
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool sizeFilterCacheValid = false;
  // Total size of displayed results (sum of file sizes); invalidated when display set changes
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  uint64_t displayedTotalSizeBytes = 0;
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool displayedTotalSizeValid = false;
  // Progressive loading for total size (avoids blocking UI on first search with many results)
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  size_t displayedTotalSizeComputationIndex = 0;
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  uint64_t displayedTotalSizeComputationBytes = 0;

  /** Resets progressive computation state (index, accumulator). */
  void ResetDisplayedTotalSizeProgress() {
    displayedTotalSizeComputationIndex = 0;
    displayedTotalSizeComputationBytes = 0;
  }

  /** Invalidates the time/size filter cache validity flags and the displayed total size.
   *  Does NOT clear the filteredResults / sizeFilteredResults vectors — use this
   *  after an in-place sort where the caches need rebuilding but the vectors can
   *  be kept until UpdateFilterCaches repopulates them. */
  void InvalidateFilterCacheFlags() {
    timeFilterCacheValid = false;
    sizeFilterCacheValid = false;
    InvalidateDisplayedTotalSize();
  }

  /** Invalidates displayed total size cache and resets progressive computation state. */
  void InvalidateDisplayedTotalSize() {
    displayedTotalSizeValid = false;
    ResetDisplayedTotalSizeProgress();
  }

  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool searchActive = false;
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool resultsComplete = true;
  // Bumped in ClearSearchResults when a new search session starts; stable for that session.
  // NOLINTNEXTLINE(readability-identifier-naming)
  uint64_t searchSessionId = 0;
  // NOLINTNEXTLINE(readability-identifier-naming,readability-redundant-string-init) - POD-like struct for UI state, camelCase is intentional, = "" makes intent explicit
  std::string searchError = "";
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool instantSearch = false;     // Search as you type (with debounce) - default off
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool autoRefresh = false;      // Re-run search when index changes
  // True if the current search was manually triggered (button/Enter); false for debounced/auto-refresh.
  // Used by Application to decide whether to record a search history entry.
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool searchWasManual = false;
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  size_t lastIndexSize = 0;
  // Timestamp of the last auto-refresh trigger; used to enforce a cooldown between refreshes so
  // rapid USN activity (bulk file operations) does not cause a continuous search-retrigger loop.
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  std::chrono::steady_clock::time_point lastAutoRefreshTime;
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool resultsUpdated = false;
  // Set by ClearInputs; consumed by SearchController::Update to call SearchWorker::DiscardResults
  // so PollResults does not re-apply stale results on the next frame.
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool clearResultsRequested = false;
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool deferFilterCacheRebuild =
    false;  // Defer filter cache rebuild for one frame to avoid blocking UI on large result sets
  // Track cloud files that are being loaded in the background (file IDs)
  // These files are included optimistically in filters until their attributes are loaded
  // NOLINTNEXTLINE(readability-identifier-naming,readability-redundant-member-init) - POD-like struct for UI state, camelCase is intentional, {} makes intent explicit
  hash_set_t<uint64_t> deferredCloudFiles{};  // File IDs of cloud files being loaded asynchronously; hash_set_t for FAST_LIBS_BOOST
  std::vector<std::future<void>>
    cloudFileLoadingFutures;  // NOLINT(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional - Futures for background cloud file attribute loading

  // UI expansion state
  // Tracks whether the Manual Search section is expanded or collapsed.
  // This is stored explicitly to avoid relying on ImGui's internal state when
  // other UI changes occur (e.g., first search, layout changes).
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool manualSearchExpanded = true;
  // Tracks whether the AI-Assisted Search section is expanded or collapsed.
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool aiSearchExpanded = false;  // Default to collapsed, Manual Search is primary
  // Tracks whether Quick Filters and Last Modified sections are visible.
  // Default to hidden since these are used less frequently.
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool showQuickFilters = false;

  // Export status notification
  // NOLINTNEXTLINE(readability-identifier-naming,readability-redundant-string-init) - POD-like struct for UI state, camelCase is intentional
  std::string exportNotification = "";
  // NOLINTNEXTLINE(readability-identifier-naming,readability-redundant-string-init) - POD-like struct for UI state, camelCase is intentional
  std::string exportErrorMessage = "";
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  std::chrono::steady_clock::time_point exportNotificationTime = std::chrono::steady_clock::now();

  // Input debouncing state
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  std::chrono::steady_clock::time_point lastInputTime = std::chrono::steady_clock::now();
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool inputChanged = false;

  // Keyboard shortcut state
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool focusFilenameInput = false;  // Set to true to focus filename input next frame
  // True when the inline "Filter in results" prompt is visible; set by ResultsTable each frame.
  // When true, ApplicationLogic skips Escape "Clear all filters" so Esc only cancels the inline filter.
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool incrementalSearchActive = false;

  // Sort state preservation
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  int lastSortColumn = -1;
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  ImGuiSortDirection lastSortDirection = ImGuiSortDirection_None;

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

  // Search History selection/action state — stable ids (not indices) so mutations never
  // silently invalidate them. selected_history_id_ is validated each frame.
  // NOLINTNEXTLINE(readability-identifier-naming,readability-redundant-string-init) - POD-like struct for UI state, snake_case with trailing underscore; = "" satisfies member-init
  std::string selected_history_id_ = "";
  // NOLINTNEXTLINE(readability-identifier-naming,readability-redundant-string-init) - POD-like struct for UI state, snake_case with trailing underscore; = "" satisfies member-init
  std::string pending_rename_id_ = "";   // Id of entry awaiting rename; triggers RenderHistoryRenamePopup
  // NOLINTNEXTLINE(readability-identifier-naming,readability-redundant-string-init) - POD-like struct for UI state, snake_case with trailing underscore; = "" satisfies member-init
  std::string pending_delete_id_ = "";   // Id of entry awaiting deletion; triggers RenderHistoryDeletePopup

  // Popup state flags (deferred opening from child windows to parent window level)
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool openRegexGeneratorPopup = false;
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool openRegexGeneratorPopupFilename = false;

  // Context menu state (Windows right-click context menu)
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool contextMenuOpen = false;  // True when context menu is open (prevents multiple opens)
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  std::chrono::steady_clock::time_point lastContextMenuTime = std::chrono::steady_clock::now();  // Last time context menu was opened (for debouncing)

  // Window visibility flags (for floating windows like Settings, Metrics, Help, Search Help)
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool showHelpWindow = false;
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool showSearchHelpWindow = false;

  // Gemini API integration state
  // ImGui::InputTextMultiline requires char* buffer (performance-critical, called every frame)
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, snake_case with trailing underscore is intentional, 512 is ImGui buffer size requirement
  std::array<char, 512> gemini_description_input_ = {};
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, snake_case with trailing underscore is intentional
  bool gemini_api_call_in_progress_ = false;  // True when API call is in progress
  // NOLINTNEXTLINE(readability-identifier-naming,readability-redundant-member-init) - POD-like struct for UI state, snake_case with trailing underscore is intentional, {} makes intent explicit
  std::future<gemini_api_utils::GeminiApiResult> gemini_api_future_{};  // Future for async API call (default-constructed, assigned when API call starts)
  // NOLINTNEXTLINE(readability-identifier-naming,readability-redundant-string-init) - POD-like struct for UI state, snake_case with trailing underscore is intentional, = "" makes intent explicit
  std::string gemini_error_message_ = "";                             // Error message to display
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, snake_case with trailing underscore is intentional
  std::chrono::steady_clock::time_point gemini_error_display_time_ =
    std::chrono::steady_clock::now();  // When to stop showing error message

  // Memory usage tracking (updated every 10 seconds to avoid costly system calls)
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, snake_case with trailing underscore is intentional
  size_t memory_bytes_ = 0;  // Current memory usage in bytes
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, snake_case with trailing underscore is intentional
  std::chrono::steady_clock::time_point last_memory_update_time_ =
    std::chrono::steady_clock::now();  // Last time memory was updated

  // Index build progress (shared across all platforms)
  // indexBuildInProgress: set only from Application::UpdateIndexBuildState from the per-frame
  // IsIndexBuilding() snapshot — not IndexBuildState::active alone (USN monitor / finalize phases
  // are included). Keeps status bar timing and search gating consistent with UIActions::IsIndexBuilding().
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool indexBuildInProgress = false;        // True while initial index is building
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool indexBuildFailed = false;            // True if initial index build failed
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  size_t indexEntriesProcessed = 0;         // Total entries (files + dirs) processed
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  size_t indexFilesProcessed = 0;           // Files processed during initial build
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  size_t indexDirsProcessed = 0;            // Directories processed during initial build
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  size_t indexErrorCount = 0;               // Errors encountered during build
  // NOLINTNEXTLINE(readability-identifier-naming,readability-redundant-member-init) - POD-like struct for UI state, camelCase is intentional; {} makes intent explicit
  std::string indexBuildStatusText{};       // Human-readable status for UI
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  std::chrono::steady_clock::time_point indexBuildStartTime;   // Start time of current index build (for elapsed duration)
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  uint64_t indexBuildLastDurationMs = 0;    // Duration in milliseconds of last completed index build
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool indexBuildHasTiming = false;         // True once timing has been initialized for the current session

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

