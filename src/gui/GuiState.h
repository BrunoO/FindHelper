#pragma once

#include <array>  // NOLINT(clang-diagnostic-error) - False positive on macOS header analysis
#include <atomic>
#include <chrono>
#include <future>
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
#include "search/SearchWorker.h"
#include "utils/HashMapAliases.h"

// Forward declaration to avoid circular include with SearchTypes.h
struct SearchResult;

// A token to signal cancellation to running sort attribute loading tasks.
// This prevents tasks from continuing to run when a new sort is requested.
// NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - POD-like struct for cancellation token
struct SortCancellationToken {
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - POD-like struct for cancellation token
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
// at call sites readable (e.g. state.selectedRow, state.timeFilter). A few
// internal members use snake_case_ with trailing underscore per project
// convention (e.g. sort_cancellation_token_, gemini_description_input_).
// See docs/standards/CXX17_NAMING_CONVENTIONS.md; this is the documented
// exception for UI state aggregates.
//
// NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,clang-analyzer-optin.performance.Padding) - GuiState is intentionally a POD-like aggregate (62 fields) for UI state management; padding optimization not critical for UI state
class GuiState {  // NOSONAR(cpp:S1820) - GuiState aggregates all UI state (62 fields): splitting would require passing multiple objects, reducing encapsulation and increasing complexity
 public:
  // Input fields (encapsulated to address "Primitive Obsession" code smell)
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  SearchInputField extensionInput;
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  SearchInputField filenameInput;
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  SearchInputField pathInput;
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool foldersOnly = false;
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool caseSensitive = false;                // If true, case-sensitive search; if false,
                                             // case-insensitive (default)
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  TimeFilter timeFilter = TimeFilter::None;  // Time-based filter for "Last Modified"
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  SizeFilter sizeFilter = SizeFilter::None;  // Size-based filter for file size

  // Search state
  // Path pool: single buffer backing fullPath string_views in searchResults/partialResults.
  // Cleared when results are cleared or replaced; must outlive any SearchResult referencing it.
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming,readability-redundant-member-init) - POD-like struct for UI state, camelCase is intentional
  std::vector<char> searchResultPathPool{};
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming,readability-redundant-member-init) - POD-like struct for UI state, camelCase is intentional, {} makes intent explicit
  std::vector<SearchResult> searchResults{};
  // Partial results for streaming (appended incrementally)
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming,readability-redundant-member-init) - POD-like struct for UI state, camelCase is intentional, {} makes intent explicit
  std::vector<SearchResult> partialResults{};
  // Cached results after applying time filter (to avoid re-filtering every frame)
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming,readability-redundant-member-init) - POD-like struct for UI state, camelCase is intentional, {} makes intent explicit
  std::vector<SearchResult> filteredResults{};
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  size_t filteredCount = 0;
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  TimeFilter cachedTimeFilter = TimeFilter::None;
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool timeFilterCacheValid = false;
  // Cached results after applying size filter (to avoid re-filtering every frame)
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming,readability-redundant-member-init) - POD-like struct for UI state, camelCase is intentional, {} makes intent explicit
  std::vector<SearchResult> sizeFilteredResults{};
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  size_t sizeFilteredCount = 0;
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  SizeFilter cachedSizeFilter = SizeFilter::None;
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool sizeFilterCacheValid = false;
  // Total size of displayed results (sum of file sizes); invalidated when display set changes
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  uint64_t displayedTotalSizeBytes = 0;
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool displayedTotalSizeValid = false;
  // Progressive loading for total size (avoids blocking UI on first search with many results)
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  size_t displayedTotalSizeComputationIndex = 0;
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  uint64_t displayedTotalSizeComputationBytes = 0;

  // Folder statistics cache for current display results (recursive file counts and total sizes).
  // This cache is rebuilt only when the underlying display results or filters change to avoid
  // extra O(N) work on every frame.
  struct FolderStats {  // NOLINT(readability-identifier-naming) - Nested struct uses PascalCase per project type-alias/struct conventions
    size_t fileCount = 0;              // NOLINT(misc-non-private-member-variables-in-classes,readability-identifier-naming)
    uint64_t totalSizeBytes = 0;       // NOLINT(misc-non-private-member-variables-in-classes,readability-identifier-naming)
  };
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming,readability-redundant-member-init) - POD-like struct for UI state, camelCase is intentional, {} makes intent explicit
  std::unordered_map<std::string, FolderStats> folderStatsByPath{};
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool folderStatsValid = false;
  // Batch and filter snapshot used to know when folderStatsByPath must be rebuilt.
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming)
  uint64_t folderStatsResultsBatchNumber = 0;
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming)
  TimeFilter folderStatsTimeFilter = TimeFilter::None;
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming)
  SizeFilter folderStatsSizeFilter = SizeFilter::None;
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming)
  bool folderStatsShowingPartialResults = false;

  /** Resets progressive computation state (index, accumulator). */
  void ResetDisplayedTotalSizeProgress() {
    displayedTotalSizeComputationIndex = 0;
    displayedTotalSizeComputationBytes = 0;
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

  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool searchActive = false;
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool resultsComplete = true;
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  uint64_t resultsBatchNumber = 0;
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool showingPartialResults = false;
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming,readability-redundant-string-init) - POD-like struct for UI state, camelCase is intentional, = "" makes intent explicit
  std::string searchError = "";
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool instantSearch = false;     // Search as you type (with debounce) - default off
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool autoRefresh = false;      // Re-run search when index changes
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool searchWasManual = false;  // True if current search was manually triggered (button/Enter),
                                 // false for instant/auto-refresh
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  size_t lastIndexSize = 0;
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool resultsUpdated = false;
  // Set by ClearInputs; consumed by SearchController::Update to call SearchWorker::DiscardResults.
  // Ensures streaming collector is cleared so PollResults does not re-apply results on the next frame.
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool clearResultsRequested = false;
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool deferFilterCacheRebuild =
    false;  // Defer filter cache rebuild for one frame to avoid blocking UI on large result sets
  // Track cloud files that are being loaded in the background (file IDs)
  // These files are included optimistically in filters until their attributes are loaded
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming,readability-redundant-member-init) - POD-like struct for UI state, camelCase is intentional, {} makes intent explicit
  hash_set_t<uint64_t> deferredCloudFiles{};  // File IDs of cloud files being loaded asynchronously; hash_set_t for FAST_LIBS_BOOST
  std::vector<std::future<void>>
    cloudFileLoadingFutures;  // NOLINT(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional - Futures for background cloud file attribute loading

  // UI expansion state
  // Tracks whether the Manual Search section is expanded or collapsed.
  // This is stored explicitly to avoid relying on ImGui's internal state when
  // other UI changes occur (e.g., first search, layout changes).
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool manualSearchExpanded = true;
  // Tracks whether the AI-Assisted Search section is expanded or collapsed.
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool aiSearchExpanded = false;  // Default to collapsed, Manual Search is primary
  // Tracks whether Quick Filters and Last Modified sections are visible.
  // Default to hidden since these are used less frequently.
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool showQuickFilters = false;

  // Export status notification
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming,readability-redundant-string-init) - POD-like struct for UI state, camelCase is intentional
  std::string exportNotification = "";
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming,readability-redundant-string-init) - POD-like struct for UI state, camelCase is intentional
  std::string exportErrorMessage = "";
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  std::chrono::steady_clock::time_point exportNotificationTime = std::chrono::steady_clock::now();

  // Input debouncing state
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  std::chrono::steady_clock::time_point lastInputTime = std::chrono::steady_clock::now();
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool inputChanged = false;

  // Keyboard shortcut state
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool focusFilenameInput = false;  // Set to true to focus filename input next frame
  // True when the inline "Filter in results" prompt is visible; set by ResultsTable each frame.
  // When true, ApplicationLogic skips Escape "Clear all filters" so Esc only cancels the inline filter.
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool incrementalSearchActive = false;

  // Sort state preservation
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  int lastSortColumn = -1;
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  ImGuiSortDirection lastSortDirection = ImGuiSortDirection_None;

  // Attribute loading state (for status bar display and async loading)
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool loadingAttributes =
    false;  // True when file attributes are being loaded (e.g., during sorting)
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool sortDataReady =
    false;  // True when all data is already loaded and sort can happen immediately
  std::vector<std::future<void>>
    attributeLoadingFutures;  // NOLINT(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional - Futures for async attribute loading during sorting
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, snake_case with trailing underscore is intentional
  SortCancellationToken sort_cancellation_token_;
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, snake_case with trailing underscore is intentional
  SortGeneration sort_generation_ = 0;            // Incremented for each new sort operation
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, snake_case with trailing underscore is intentional
  SortGeneration completed_sort_generation_ = 0;  // Generation of the last completed sort

  // Selection and Deletion state
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  int selectedRow = -1;
  // If true, the results table will scroll to the selected row in the next frame
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool scrollToSelectedRow = false;
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming,readability-redundant-string-init) - POD-like struct for UI state, camelCase is intentional, = "" makes intent explicit
  std::string fileToDelete = "";
  std::set<std::string, std::less<>>
    pendingDeletions;  // NOLINT(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional - Transparent comparator for string_view lookups
  // Set of file IDs that are marked for bulk operations (dired-style)
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming,readability-redundant-member-init) - POD-like struct for UI state, camelCase is intentional, {} makes intent explicit
  hash_set_t<uint64_t> markedFileIds{};  // hash_set_t for FAST_LIBS_BOOST
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool showDeletePopup = false;
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool showBulkDeletePopup = false;
  // When true, show "Matched Files" and "Matched Size" columns; default false (hidden). Toggle with Ctrl+Shift+F (global, when no text input).
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool showFolderStatsColumns = false;
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  int deleteSavedSearchIndex =
    -1;  // Index of saved search to delete (set when Delete button is clicked)

  // Popup state flags (deferred opening from child windows to parent window level)
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool openSaveSearchPopup = false;
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool openRegexGeneratorPopup = false;
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool openRegexGeneratorPopupFilename = false;
  
  // Context menu state (Windows right-click context menu)
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool contextMenuOpen = false;  // True when context menu is open (prevents multiple opens)
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  std::chrono::steady_clock::time_point lastContextMenuTime = std::chrono::steady_clock::now();  // Last time context menu was opened (for debouncing)
  
  // Window visibility flags (for floating windows like Settings, Metrics, Help, Search Help)
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool showHelpWindow = false;
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool showSearchHelpWindow = false;

  // Gemini API integration state
  // ImGui::InputTextMultiline requires char* buffer (performance-critical, called every frame)
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes, readability-identifier-naming) - POD-like struct for UI state, snake_case with trailing underscore is intentional, 512 is ImGui buffer size requirement
  std::array<char, 512> gemini_description_input_ = {};
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, snake_case with trailing underscore is intentional
  bool gemini_api_call_in_progress_ = false;  // True when API call is in progress
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming,readability-redundant-member-init) - POD-like struct for UI state, snake_case with trailing underscore is intentional, {} makes intent explicit
  std::future<gemini_api_utils::GeminiApiResult> gemini_api_future_{};  // Future for async API call (default-constructed, assigned when API call starts)
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming,readability-redundant-string-init) - POD-like struct for UI state, snake_case with trailing underscore is intentional, = "" makes intent explicit
  std::string gemini_error_message_ = "";                             // Error message to display
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, snake_case with trailing underscore is intentional
  std::chrono::steady_clock::time_point gemini_error_display_time_ =
    std::chrono::steady_clock::now();  // When to stop showing error message

  // Memory usage tracking (updated every 10 seconds to avoid costly system calls)
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, snake_case with trailing underscore is intentional
  size_t memory_bytes_ = 0;  // Current memory usage in bytes
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, snake_case with trailing underscore is intentional
  std::chrono::steady_clock::time_point last_memory_update_time_ =
    std::chrono::steady_clock::now();  // Last time memory was updated

  // Index build progress (shared across all platforms)
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, snake_case is intentional
  bool index_build_in_progress = false;  // True while initial index is building
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, snake_case is intentional
  bool index_build_failed = false;       // True if initial index build failed
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, snake_case is intentional
  size_t index_entries_processed = 0;    // Total entries (files + dirs) processed
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, snake_case is intentional
  size_t index_files_processed = 0;      // Files processed during initial build
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, snake_case is intentional
  size_t index_dirs_processed = 0;       // Directories processed during initial build
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, snake_case is intentional
  size_t index_error_count = 0;          // Errors encountered during build
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming,readability-redundant-member-init) - POD-like struct for UI state, snake_case is intentional, {} makes intent explicit
  std::string index_build_status_text{};   // Human-readable status for UI
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, snake_case is intentional
  std::chrono::steady_clock::time_point index_build_start_time;  // Start time of current index build (for elapsed duration)
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, snake_case is intentional
  uint64_t index_build_last_duration_ms = 0;  // Duration in milliseconds of last completed index build
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,readability-identifier-naming) - POD-like struct for UI state, snake_case is intentional
  bool index_build_has_timing = false;        // True once timing has been initialized for the current session

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
