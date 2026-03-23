#pragma once

#include <algorithm>
#include <array>  // NOLINT(clang-diagnostic-error) - False positive on macOS header analysis
#include <atomic>
#include <cassert>
#include <chrono>
#include <future>
#include <iterator>
#include <memory>
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

// Forward declaration to avoid circular include with SearchTypes.h
struct SearchResult;

// A token to signal cancellation to running sort attribute loading tasks.
// This prevents tasks from continuing to run when a new sort is requested.

struct SortCancellationToken {

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

// GUI State class to encapsulate all UI state.
//
// Naming: Public members use camelCase (not snake_case_) by design. This matches
// common UI/widget naming (e.g. ImGui, JavaScript UI state) and keeps access
// at call sites readable (e.g. state.selected_row_, state.timeFilter). A few
// internal members use snake_case_ with trailing underscore per project
// convention (e.g. sort_cancellation_token_, gemini_description_input_).
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
  // Path pool: single buffer backing fullPath string_views in searchResults/partialResults.
  // Cleared when results are cleared or replaced; must outlive any SearchResult referencing it.
  // NOLINTNEXTLINE(readability-identifier-naming,readability-redundant-member-init) - POD-like struct for UI state, camelCase is intentional
  std::vector<char> searchResultPathPool{};
  // NOLINTNEXTLINE(readability-identifier-naming,readability-redundant-member-init) - POD-like struct for UI state, camelCase is intentional, {} makes intent explicit
  std::vector<SearchResult> searchResults{};
  // Partial results for streaming (appended incrementally)
  // NOLINTNEXTLINE(readability-identifier-naming,readability-redundant-member-init) - POD-like struct for UI state, camelCase is intentional, {} makes intent explicit
  std::vector<SearchResult> partialResults{};
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

  // Folder statistics cache for current display results (recursive file counts and total sizes).
  // This cache is rebuilt only when the underlying display results or filters change to avoid
  // extra O(N) work on every frame.
  struct FolderStats {  // NOLINT(readability-identifier-naming) - Nested struct uses PascalCase per project type-alias/struct conventions
    size_t fileCount = 0;              // NOLINT(readability-identifier-naming)
    uint64_t totalSizeBytes = 0;       // NOLINT(readability-identifier-naming)
  };
  // NOLINTNEXTLINE(readability-identifier-naming,readability-redundant-member-init) - POD-like struct for UI state, camelCase is intentional, {} makes intent explicit
  std::unordered_map<std::string, FolderStats> folderStatsByPath{};
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool folderStatsValid = false;
  // Batch and filter snapshot used to know when folderStatsByPath must be rebuilt.
  // NOLINTNEXTLINE(readability-identifier-naming)
  uint64_t folderStatsResultsBatchNumber = 0;
  // NOLINTNEXTLINE(readability-identifier-naming)
  TimeFilter folderStatsTimeFilter = TimeFilter::None;
  // NOLINTNEXTLINE(readability-identifier-naming)
  SizeFilter folderStatsSizeFilter = SizeFilter::None;
  // NOLINTNEXTLINE(readability-identifier-naming)
  bool folderStatsShowingPartialResults = false;

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

  /** Invalidates cached folder statistics so they will be recomputed on next use. */
  void InvalidateFolderStats() {
    folderStatsValid = false;
    folderStatsByPath.clear();
  }

  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool searchActive = false;
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool resultsComplete = true;
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  uint64_t resultsBatchNumber = 0;
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool showingPartialResults = false;
  // NOLINTNEXTLINE(readability-identifier-naming,readability-redundant-string-init) - POD-like struct for UI state, camelCase is intentional, = "" makes intent explicit
  std::string searchError = "";
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool instantSearch = false;     // Search as you type (with debounce) - default off
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool autoRefresh = false;      // Re-run search when index changes
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool searchWasManual = false;  // True if current search was manually triggered (button/Enter),
                                 // false for instant/auto-refresh
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  size_t lastIndexSize = 0;
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool resultsUpdated = false;
  // Set by ClearInputs; consumed by SearchController::Update to call SearchWorker::DiscardResults.
  // Ensures streaming collector is cleared so PollResults does not re-apply results on the next frame.
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
  bool loadingAttributes =
    false;  // True when file attributes are being loaded (e.g., during sorting)
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool computingFolderSizes =
    false;  // True when FolderSizeAggregator has pending background work
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool sortDataReady =
    false;  // True when all data is already loaded and sort can happen immediately
  std::shared_ptr<std::atomic<int>>
    attributeLoadingCounter;  // NOLINT(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional - Atomic countdown latch for async attribute loading tasks; null or zero when idle
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, snake_case with trailing underscore is intentional
  SortCancellationToken sort_cancellation_token_;
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, snake_case with trailing underscore is intentional
  SortGeneration sort_generation_ = 0;            // Incremented for each new sort operation
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, snake_case with trailing underscore is intentional
  SortGeneration completed_sort_generation_ = 0;  // Generation of the last completed sort

  // Selection and Deletion state
  // scrollToSelectedRow, filesToDelete, pendingDeletions, markedFileIds remain public because
  // they are simple flags/containers consumed directly by rendering and keyboard code.
  // The three core selection fields are private below to enforce mutation through helpers.
  //
  // Read-only accessors (defined in the Selection helpers section):
  //   GetSelectedRow()       – primary row mirror (scalar backward-compat)
  //   GetSelectedRows()      – full sorted, unique selection vector
  // If true, the results table will scroll to the selected row in the next frame
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool scrollToSelectedRow = false;
  // NOLINTNEXTLINE(readability-identifier-naming,readability-redundant-member-init) - POD-like struct for UI state, camelCase is intentional, {} makes intent explicit
  std::vector<std::string> filesToDelete{};
  std::set<std::string, std::less<>>
    pendingDeletions;  // NOLINT(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional - Transparent comparator for string_view lookups
  // Set of file IDs that are marked for bulk operations (dired-style)
  // NOLINTNEXTLINE(readability-identifier-naming,readability-redundant-member-init) - POD-like struct for UI state, camelCase is intentional, {} makes intent explicit
  hash_set_t<uint64_t> markedFileIds{};  // hash_set_t for FAST_LIBS_BOOST
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool showDeletePopup = false;
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool showBulkDeletePopup = false;
  // When true, show "Matched Files" and "Matched Size" columns; default false (hidden). Toggle with Ctrl+Shift+F (global, when no text input).
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool showFolderStatsColumns = false;
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  int deleteSavedSearchIndex =
    -1;  // Index of saved search to delete (set when Delete button is clicked)

  // Popup state flags (deferred opening from child windows to parent window level)
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool openSaveSearchPopup = false;
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
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, snake_case is intentional
  bool index_build_in_progress = false;  // True while initial index is building
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, snake_case is intentional
  bool index_build_failed = false;       // True if initial index build failed
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, snake_case is intentional
  size_t index_entries_processed = 0;    // Total entries (files + dirs) processed
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, snake_case is intentional
  size_t index_files_processed = 0;      // Files processed during initial build
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, snake_case is intentional
  size_t index_dirs_processed = 0;       // Directories processed during initial build
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, snake_case is intentional
  size_t index_error_count = 0;          // Errors encountered during build
  // NOLINTNEXTLINE(readability-identifier-naming,readability-redundant-member-init) - POD-like struct for UI state, snake_case is intentional, {} makes intent explicit
  std::string index_build_status_text{};   // Human-readable status for UI
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, snake_case is intentional
  std::chrono::steady_clock::time_point index_build_start_time;  // Start time of current index build (for elapsed duration)
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, snake_case is intentional
  uint64_t index_build_last_duration_ms = 0;  // Duration in milliseconds of last completed index build
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, snake_case is intentional
  bool index_build_has_timing = false;        // True once timing has been initialized for the current session

  void MarkInputChanged();
  void ClearInputs();

  [[nodiscard]] bool IsPendingDeletion(std::string_view path) const {
    return pendingDeletions.find(path) != pendingDeletions.end();
  }

  // --- Selection helpers (Phase 0 facade → Phase 1 multi-selection) ---

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
    if (const auto it = std::lower_bound(selected_rows_.begin(), selected_rows_.end(), row);  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
        it == selected_rows_.end() || *it != row) {
      selected_rows_.insert(it, row);
    }
    selection_anchor_row_ = row;
    selected_row_ = row;
    AssertSelectionInvariant();
  }


  // Returns true when row is present in the multi-selection.
  [[nodiscard]] bool IsRowSelected(int row) const {
    return std::binary_search(selected_rows_.begin(), selected_rows_.end(), row);  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
  }

  // Asserts that selected_rows_ is sorted and unique, and that selection_anchor_row_ is either
  // -1 or a member of selected_rows_. Active only in debug builds (assert is a no-op in Release).
  void AssertSelectionInvariant() const {
    for (std::size_t i = 1; i < selected_rows_.size(); ++i) {
      assert(selected_rows_[i - 1] < selected_rows_[i]);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - i and i-1 are bounded by loop condition
    }
    assert(selection_anchor_row_ == -1 || IsRowSelected(selection_anchor_row_));
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
  // Used for ImGui SetRange(Selected=true) requests that have no preceding SetAll(false):
  // - Ctrl+Click to add a row  → SetRange(row, row, true) with no prior Clear
  // - Shift+Click after Clear  → SetAll(false) empties first; AddSelectionRange then adds range
  // Plain click and Shift+Click both arrive as SetAll(false) + SetRange, so the Clear runs first
  // and AddSelectionRange fills from empty — identical to SetSelectionRange in that case.
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
    const auto first_oob = std::lower_bound(selected_rows_.begin(), selected_rows_.end(), count);  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
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
          old_results[static_cast<std::size_t>(row)].fullPath);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - guarded by range check above
      }
    }
    if (selection_anchor_row_ >= 0 && selection_anchor_row_ < static_cast<int>(old_results.size())) {
      anchor_path =
        old_results[static_cast<std::size_t>(selection_anchor_row_)].fullPath;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - guarded by range check above
    }
    // Rebuild from new results (iteration order is already sorted).
    selected_rows_.clear();
    selection_anchor_row_ = -1;
    const auto row_count = static_cast<int>(std::size(new_results));
    for (int new_row = 0; new_row < row_count; ++new_row) {
      const std::string_view path =
        new_results[static_cast<std::size_t>(new_row)].fullPath;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - bounded by row_count
      const bool was_selected =
        std::find(selected_paths.begin(), selected_paths.end(), path) !=  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
        selected_paths.end();
      if (was_selected) {
        selected_rows_.push_back(new_row);  // already sorted (new_results traversed in order)
        if (!anchor_path.empty() && path == anchor_path) {
          selection_anchor_row_ = new_row;
        }
      }
    }
    selected_row_ = GetPrimarySelectedRow();
    AssertSelectionInvariant();
  }

  // --- Focus synchronization helpers ---
  // focusRowRequest: when >= 0, the render pass will call SetKeyboardFocusHere(0) before the
  // Filename Selectable of that row, keeping ImGui's focus rectangle in sync with the
  // GuiState selection after keyboard-driven navigation. Consumed by ConsumeFocusRequest().
  // Set only from keyboard handlers (never from mouse paths). See design doc:
  // internal-docs/design/2026-03-17_RESULTS_TABLE_FOCUS_AND_SELECTION_SYNC.md

  // Request that the given row receives ImGui keyboard focus on the next rendered frame.
  void RequestFocusForRow(int row) { focus_row_request_ = row; }

  // Request focus for whichever row GetPrimarySelectedRow() currently returns.
  void RequestFocusForPrimaryRow() { focus_row_request_ = GetPrimarySelectedRow(); }

  // Returns the pending focus-row index and resets it to -1.
  // Called once per frame by the row-render path before the clipper loop.
  [[nodiscard]] int ConsumeFocusRequest() {
    const int row = focus_row_request_;
    focus_row_request_ = -1;
    return row;
  }

  // Replace current selection with a single row AND request ImGui focus for that row.
  // Use this in keyboard navigation paths instead of the bare SetSelectedRow so the
  // focus rectangle tracks the selection automatically. Do NOT use from mouse handlers
  // or data-change paths (sort/filter remap) — those must not steal ImGui focus.
  void SetSelectedRowAndFocus(int row) {
    SetSelectedRow(row);
    RequestFocusForRow(row);
  }

  // --- ImGui multi-select integration helpers ---
  // Called by ApplyMultiSelectRequests (ResultsTable.cpp) to translate ImGui
  // SetRange requests into GuiState selection mutations.

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

 private:
  // These three fields are private to enforce mutation through the selection helpers above.
  // All read access goes through GetSelectedRow(), GetSelectedRows().
  // All writes go through SetSelectedRow(), ClearSelection(), SelectRow(),
  // SetSelectionRange(), AddSelectionRange(), RemoveSelectionRange(),
  // ClampSelectionToRowCount(), RemapSelectionAfterDisplayResultsChange().
  int selected_row_ = -1;                // NOLINT(readability-identifier-naming) - snake_case_ intentional for private selection fields
  std::vector<int> selected_rows_{};     // NOLINT(readability-identifier-naming,readability-redundant-member-init) - snake_case_ intentional; {} makes intent explicit
  int selection_anchor_row_ = -1;        // NOLINT(readability-identifier-naming) - snake_case_ intentional for private selection fields
  int focus_row_request_ = -1;           // NOLINT(readability-identifier-naming) - snake_case_ intentional; -1 = no pending focus sync
};
