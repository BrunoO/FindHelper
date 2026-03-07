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
 * - attributeLoadingFutures: Must call .get() before clearing to prevent leaks
 * - gemini_api_future_: Must wait and get result before resetting
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

#include <algorithm>
#include <cstring>
#include <sstream>

#include "api/GeminiApiUtils.h"
#include "filters/SizeFilter.h"
#include "filters/TimeFilter.h"

void GuiState::MarkInputChanged() {
  inputChanged = true;
  lastInputTime = std::chrono::steady_clock::now();
}

void GuiState::ClearInputs() {
  filenameInput.Clear();
  pathInput.Clear();
  extensionInput.Clear();
  foldersOnly = false;
  caseSensitive = false; // Default to case-insensitive
  
  // Cancel any pending sort attribute loading operations.
  // This signals to any running futures that they should stop early.
  sort_cancellation_token_.Cancel();

  // Clear any pending attribute loading futures before clearing results
  // (futures reference SearchResult objects which will be destroyed)
  // CRITICAL: Must call .get() on each future to properly clean up resources and prevent memory leaks
  for (auto& f : attributeLoadingFutures) {
      if (f.valid()) {
          try {
              if (f.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
                  f.wait();
              }
              f.get();
          } catch (...) {  // NOLINT(bugprone-empty-catch) NOSONAR(cpp:S2738, cpp:S2486) - Ignore exceptions, just ensure cleanup happens. Future cleanup must not throw - prevents exceptions from propagating
          }
      }
  }
  attributeLoadingFutures.clear();
  loadingAttributes = false;
  
  // Clean up cloud file loading futures
  // CRITICAL: Must call .get() on each future to properly clean up resources and prevent memory leaks
  for (auto& f : cloudFileLoadingFutures) {
    if (f.valid()) {
      try {
        if (f.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
          f.wait();
        }
        f.get();
      } catch (...) {  // NOLINT(bugprone-empty-catch) NOSONAR(cpp:S2738, cpp:S2486) - Ignore exceptions, just ensure cleanup happens. Future cleanup must not throw - prevents exceptions from propagating
      }
    }
  }
  cloudFileLoadingFutures.clear();
  deferredCloudFiles.clear();
  
  // Clean up Gemini API future if it exists
  // Wait for it to complete if still running, then reset it
  if (gemini_api_future_.valid()) {
    if (auto status = gemini_api_future_.wait_for(std::chrono::milliseconds(0)); status != std::future_status::ready) {
      // Future is still running - wait for it to complete
      gemini_api_future_.wait();
    }
    // Get the result to properly clean up the future
    gemini_api_future_.get();
    // Reset to invalid state
    gemini_api_future_ = std::future<gemini_api_utils::GeminiApiResult>();
  }
  gemini_api_call_in_progress_ = false;
  gemini_error_message_ = "";
  gemini_description_input_[0] = '\0';

  exportNotification = "";
  exportErrorMessage = "";
  
  searchResultPathPool.clear();
  searchResults.clear();
  partialResults.clear();
  // Invalidate filter caches and clear vectors so we do not hold stale data.
  timeFilterCacheValid = false;
  sizeFilterCacheValid = false;
  InvalidateDisplayedTotalSize();
  InvalidateFolderStats();
  filteredResults.clear();
  sizeFilteredResults.clear();
  searchActive = false;
  resultsComplete = true;
  resultsBatchNumber = 0;
  showingPartialResults = false;
  searchError = "";
  inputChanged = false;
  clearResultsRequested = true;
}

SearchParams GuiState::BuildCurrentSearchParams() const {
  SearchParams params;
  params.filenameInput = filenameInput;
  params.extensionInput = extensionInput;
  params.pathInput = pathInput;
  params.foldersOnly = foldersOnly;
  params.caseSensitive = caseSensitive;

  return params;
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
  std::ostringstream ext_stream;
  for (size_t i = 0; i < extensions.size(); ++i) {
    if (i > 0) {
      ext_stream << ";";
    }
    ext_stream << extensions[i];
  }
  return ext_stream.str();
}

void GuiState::ApplySearchConfig(const gemini_api_utils::SearchConfig &config) {
  // Apply filename input (if provided)
  if (!config.filename.empty()) {
    filenameInput.SetValue(config.filename);
  }
  
  // Apply extensions (if provided) - convert array to semicolon-separated string
  if (!config.extensions.empty()) {
    extensionInput.SetValue(BuildExtensionString(config.extensions));
  }
  
  // Apply path input (if provided)
  if (!config.path.empty()) {
    pathInput.SetValue(config.path);
  }
  
  // Apply boolean options (always set, defaults are false)
  foldersOnly = config.folders_only;
  caseSensitive = config.case_sensitive;
  
  // Apply time filter (convert string to enum)
  if (!config.time_filter.empty()) {
    if (config.time_filter == "Today") {
      timeFilter = TimeFilter::Today;
    } else if (config.time_filter == "ThisWeek") {
      timeFilter = TimeFilter::ThisWeek;
    } else if (config.time_filter == "ThisMonth") {
      timeFilter = TimeFilter::ThisMonth;
    } else if (config.time_filter == "ThisYear") {
      timeFilter = TimeFilter::ThisYear;
    } else if (config.time_filter == "Older") {
      timeFilter = TimeFilter::Older;
    } else {
      // "None" or unknown value - default to None
      timeFilter = TimeFilter::None;
    }
  } else {
    timeFilter = TimeFilter::None;
  }
  
  // Apply size filter (convert string to enum)
  if (!config.size_filter.empty()) {
    if (config.size_filter == "Empty") {
      sizeFilter = SizeFilter::Empty;
    } else if (config.size_filter == "Tiny") {
      sizeFilter = SizeFilter::Tiny;
    } else if (config.size_filter == "Small") {
      sizeFilter = SizeFilter::Small;
    } else if (config.size_filter == "Medium") {
      sizeFilter = SizeFilter::Medium;
    } else if (config.size_filter == "Large") {
      sizeFilter = SizeFilter::Large;
    } else if (config.size_filter == "Huge") {
      sizeFilter = SizeFilter::Huge;
    } else if (config.size_filter == "Massive") {
      sizeFilter = SizeFilter::Massive;
    } else {
      // "None" or unknown value - default to None
      sizeFilter = SizeFilter::None;
    }
  } else {
    sizeFilter = SizeFilter::None;
  }
  
  // Mark input as changed to trigger search
  MarkInputChanged();
}

void GuiState::ApplyShowAllPreset() {
  extensionInput.Clear();
  filenameInput.Clear();
  pathInput.SetValue("pp:**");
  foldersOnly = false;
  caseSensitive = false;
  timeFilter = TimeFilter::None;
  sizeFilter = SizeFilter::None;
  MarkInputChanged();
}
