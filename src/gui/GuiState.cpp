/**
 * @file GuiState.cpp
 * @brief Implementation of GUI state management utilities
 *
 * This file implements utility methods for the GuiState struct, which holds
 * all UI-related state including search inputs, results, settings, and flags.
 *
 * RESPONSIBILITIES:
 * - Input change tracking: Marks when user input changes for debounced search
 * - State cleanup: Clears all inputs and results, properly cleaning up futures
 * - Resource management: Ensures futures are properly cleaned up to prevent leaks
 *
 * FUTURE CLEANUP:
 * - async_sort_.counter: Wait() on the latch until drained, then reset
 * - gemini.api_future: Must wait and get result before resetting
 * - Both are cleaned up in ClearInputs() to prevent accessing invalid memory
 *
 * THREAD SAFETY:
 * - All methods are called from the UI thread only
 * - No synchronization needed (single-threaded access)
 *
 * @see GuiState.h for struct definition and all state members
 * @see SearchController.cpp for usage of MarkInputChanged()
 * @see ApplicationLogic.cpp for usage of ClearInputs()
 */

#include "gui/GuiState.h"

#include <chrono>

#include "api/GeminiApiUtils.h"
#include "filters/SizeFilter.h"
#include "filters/SizeFilterUtils.h"
#include "filters/TimeFilter.h"
#include "filters/TimeFilterUtils.h"
#include "search/SearchResultUtils.h"
#include "utils/Logger.h"

void GuiState::MarkInputChanged() {
  input_debounce.input_changed = true;
  input_debounce.last_input_time = std::chrono::steady_clock::now();
}

void GuiState::ClearInputs() {
  searchCriteria.filename_input.Clear();
  searchCriteria.path_input.Clear();
  searchCriteria.extension_input.Clear();
  searchCriteria.SetItemTypeFilter(ItemTypeFilter::All);
  searchCriteria.case_sensitive = false; // Default to case-insensitive

  // Cancel any pending sort attribute loading tasks, then drain through the
  // single shared barrier (tasks hold a raw token pointer, so the full drain
  // must complete before Reset touches staging or the token is reinstalled).
  async_sort_.token.Cancel();
  WaitForCancelledTasksFully(async_sort_.counter);
  async_sort_.Reset();
  async_sort_.ReinstallFreshTokenAfterDrain();

  // Hand off cloud file loading futures to a background drain task.
  // Must not block: teardown runs on the UI thread per clear. Shutdown joins
  // via Application's blocking WaitAndDrain.
  cloud_files.DrainInBackground();

  // Clean up Gemini API future if it exists
  // Wait for it to complete if still running, then reset it
  if (gemini.api_future.valid()) {
    if (const auto status = gemini.api_future.wait_for(std::chrono::milliseconds(0)); status != std::future_status::ready) {
      // Future is still running - wait for it to complete
      gemini.api_future.wait();
    }
    // Get the result to properly clean up the future
    (void)gemini.api_future.get();
    // Reset to invalid state
    gemini.api_future = std::future<gemini_api_utils::GeminiApiResult>();
  }
  gemini.api_call_in_progress = false;
  gemini.error_message = "";
  gemini.description_input[0] = '\0';  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - fixed-size array, index 0 always valid

  export_workflow.notification = "";
  // Keep export popup fields (export_workflow.success / export_workflow.file_path / export_workflow.result_count /
  // export_workflow.error_message / export_workflow.show_popup): Clear must not blank an open export modal.

  result_pool_->Clear();
  // Invalidate filter caches and clear vectors so we do not hold stale data.
  filter_caches.time.valid = false;
  filter_caches.size.valid = false;
  InvalidateDisplayedTotalSize();
  filter_caches.time.results.clear();
  filter_caches.size.results.clear();
  search_pipeline.search_active = false;
  search_pipeline.results_complete = true;
  search_pipeline.search_session_id = 0;
  search_pipeline.search_error = "";
  input_debounce.input_changed = false;
  search_pipeline.clear_results_requested = true;
}

SearchParams GuiState::BuildCurrentSearchParams() const {
    // Phase 2: pure conversion from the immutable-criteria snapshot value type.
  return searchCriteria.BuildParams();
}

/**
 * @brief Build semicolon-separated string from extension vector
 *
 * Helper function to convert a vector of extension strings into a
 * semicolon-separated string for display in the extension input field.
 * Uses semicolons to match ParseExtensions() and quick filter presets.
 *
 * @param extensions Vector of extension strings (e.g., {"pdf", "doc", "docx"})
 * @return Semicolon-separated string (e.g., "pdf;doc;docx")
 */
static std::string BuildExtensionString(const std::vector<std::string>& extensions) {
  std::string result;
  // Pre-compute total length to avoid reallocations: sum of all extension lengths
  // plus one separator per gap between extensions.
  size_t total = extensions.empty() ? 0 : extensions.size() - 1;  // separators
  for (const auto& ext : extensions) {
    total += ext.size();
  }
  result.reserve(total);
  for (size_t i = 0; i < extensions.size(); ++i) {
    if (i > 0) {
      result += ';';
    }
    result += extensions[i];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - bounds checked by loop condition i < extensions.size()
  }
  return result;
}

static void ApplyItemTypeFilter(SearchCriteria& criteria, const gemini_api_utils::SearchConfig& config) {
  if (config.folders_only) {
    criteria.SetItemTypeFilter(ItemTypeFilter::FoldersOnly);
  } else if (config.files_only) {
    criteria.SetItemTypeFilter(ItemTypeFilter::FilesOnly);
  } else {
    criteria.SetItemTypeFilter(ItemTypeFilter::All);
  }
}

void GuiState::ApplySearchConfig(const gemini_api_utils::SearchConfig &config) {
  // Apply filename input (if provided)
  if (!config.filename.empty()) {
    searchCriteria.filename_input.SetValue(config.filename);
  }

  // Apply extensions (if provided) - convert array to semicolon-separated string
  if (!config.extensions.empty()) {
    searchCriteria.extension_input.SetValue(BuildExtensionString(config.extensions));
  }

  // Apply path input (if provided)
  if (!config.path.empty()) {
    searchCriteria.path_input.SetValue(config.path);
  }

  // Apply boolean options (always set, defaults are false)
  ApplyItemTypeFilter(searchCriteria, config);
  searchCriteria.case_sensitive = config.case_sensitive;

  // Apply time filter (string -> enum via the filters module parser: the single
  // source of truth for filter-name spellings; unknown or empty maps to None).
  searchCriteria.time_filter = TimeFilterFromString(config.time_filter);
  if (searchCriteria.time_filter == TimeFilter::None && !config.time_filter.empty() &&
      config.time_filter != "None") {
    // AI-contract drift (see SearchConfig field docs): keep the None fallback,
    // but make the dropped value visible in production logs.
    LOG_WARNING("ApplySearchConfig: unrecognized time_filter '" + config.time_filter +
                "' - defaulting to None");
  }

  // Apply size filter (same shared parser; unknown or empty maps to None).
  searchCriteria.size_filter = SizeFilterFromString(config.size_filter);
  if (searchCriteria.size_filter == SizeFilter::None && !config.size_filter.empty() &&
      config.size_filter != "None") {
    LOG_WARNING("ApplySearchConfig: unrecognized size_filter '" + config.size_filter +
                "' - defaulting to None");
  }

  // Mark input as changed to trigger search
  MarkInputChanged();
}

void GuiState::ApplyShowAllPreset() {
  searchCriteria.extension_input.Clear();
  searchCriteria.filename_input.Clear();
  searchCriteria.path_input.SetValue("pp:**");
  searchCriteria.SetItemTypeFilter(ItemTypeFilter::All);
  searchCriteria.case_sensitive = false;
  searchCriteria.time_filter = TimeFilter::None;
  searchCriteria.size_filter = SizeFilter::None;
  MarkInputChanged();
}
