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
 * - async_sort_.counter: Spin-wait until counter reaches zero, then reset
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
#include <cstring>
#include <thread>

#include "api/GeminiApiUtils.h"
#include "filters/SizeFilter.h"
#include "filters/TimeFilter.h"
#include "utils/AsyncUtils.h"

void GuiState::MarkInputChanged() {
  input_debounce.input_changed = true;
  input_debounce.last_input_time = std::chrono::steady_clock::now();
}

void GuiState::ClearInputs() {
  searchCriteria.filename_input.Clear();
  searchCriteria.path_input.Clear();
  searchCriteria.extension_input.Clear();
  searchCriteria.folders_only = false;
  searchCriteria.case_sensitive = false; // Default to case-insensitive

  // Cancel any pending sort attribute loading tasks, then drain.
  async_sort_.token.Cancel();
  while (async_sort_.counter && async_sort_.counter->load(std::memory_order_acquire) > 0) {
    // Tasks check the cancellation token and exit promptly.
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  async_sort_.Reset();

  // Clean up cloud file loading futures
  // CRITICAL: Must call .get() on each future to properly clean up resources and prevent memory leaks
  cloud_files.WaitAndDrain();

  // Clean up Gemini API future if it exists
  // Wait for it to complete if still running, then reset it
  if (gemini.api_future.valid()) {
    if (const auto status = gemini.api_future.wait_for(std::chrono::milliseconds(0)); status != std::future_status::ready) {
      // Future is still running - wait for it to complete
      gemini.api_future.wait();
    }
    // Get the result to properly clean up the future
    gemini.api_future.get();
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
  searchCriteria.folders_only = config.folders_only;
  searchCriteria.case_sensitive = config.case_sensitive;

  // Apply time filter (convert string to enum)
  if (!config.time_filter.empty()) {
    if (config.time_filter == "Today") {
      searchCriteria.time_filter = TimeFilter::Today;
    } else if (config.time_filter == "ThisWeek") {
      searchCriteria.time_filter = TimeFilter::ThisWeek;
    } else if (config.time_filter == "ThisMonth") {
      searchCriteria.time_filter = TimeFilter::ThisMonth;
    } else if (config.time_filter == "ThisYear") {
      searchCriteria.time_filter = TimeFilter::ThisYear;
    } else if (config.time_filter == "Older") {
      searchCriteria.time_filter = TimeFilter::Older;
    } else {
      // "None" or unknown value - default to None
      searchCriteria.time_filter = TimeFilter::None;
    }
  } else {
    searchCriteria.time_filter = TimeFilter::None;
  }

  // Apply size filter (convert string to enum)
  if (!config.size_filter.empty()) {
    if (config.size_filter == "Empty") {
      searchCriteria.size_filter = SizeFilter::Empty;
    } else if (config.size_filter == "Tiny") {
      searchCriteria.size_filter = SizeFilter::Tiny;
    } else if (config.size_filter == "Small") {
      searchCriteria.size_filter = SizeFilter::Small;
    } else if (config.size_filter == "Medium") {
      searchCriteria.size_filter = SizeFilter::Medium;
    } else if (config.size_filter == "Large") {
      searchCriteria.size_filter = SizeFilter::Large;
    } else if (config.size_filter == "Huge") {
      searchCriteria.size_filter = SizeFilter::Huge;
    } else if (config.size_filter == "Massive") {
      searchCriteria.size_filter = SizeFilter::Massive;
    } else {
      // "None" or unknown value - default to None
      searchCriteria.size_filter = SizeFilter::None;
    }
  } else {
    searchCriteria.size_filter = SizeFilter::None;
  }

  // Mark input as changed to trigger search
  MarkInputChanged();
}

void GuiState::ApplyShowAllPreset() {
  searchCriteria.extension_input.Clear();
  searchCriteria.filename_input.Clear();
  searchCriteria.path_input.SetValue("pp:**");
  searchCriteria.folders_only = false;
  searchCriteria.case_sensitive = false;
  searchCriteria.time_filter = TimeFilter::None;
  searchCriteria.size_filter = SizeFilter::None;
  MarkInputChanged();
}
