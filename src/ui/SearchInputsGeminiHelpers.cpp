/**
 * @file ui/SearchInputsGeminiHelpers.cpp
 * @brief Implementation of Gemini API helper functions for search inputs
 *
 * This file contains helper functions extracted from SearchInputs.cpp to reduce
 * file size and improve maintainability. These functions handle Gemini API
 * integration, including future cleanup, API calls, and result processing.
 */

#include "ui/SearchInputsGeminiHelpers.h"

#include <chrono>
#include <future>
#include <string>

#include <GLFW/glfw3.h>

#include "api/GeminiApiUtils.h"
#include "gui/GuiState.h"
#include "gui/UIActions.h"
#include "utils/AsyncUtils.h"
#include "utils/ClipboardUtils.h"
#include "utils/Logger.h"

namespace ui {

namespace {

constexpr int kShortMessageDisplaySeconds = 3;
constexpr int kDefaultMessageDisplaySeconds = 5;
constexpr int kLongMessageDisplaySeconds = 10;

// Helper function to safely get and clean up a future, handling all exceptions
inline void SafeGetAndCleanupFuture(std::future<gemini_api_utils::GeminiApiResult>& future) {  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables,readability-identifier-naming) - function in anonymous namespace
  async_utils::ExecuteWithLogCatch(
    [&future]() {
      if (future.valid()) {
        future.get();  // Discard result, just clean up
      }
    },
    "Error during future cleanup");
  future = std::future<gemini_api_utils::GeminiApiResult>();
}
}  // anonymous namespace

void CleanupGeminiFuture(GuiState& state) {
  if (state.gemini_api_future_.valid()) {
    auto status = state.gemini_api_future_.wait_for(std::chrono::milliseconds(0));
    if (status == std::future_status::ready) {
      // Future is ready but not processed - clean it up now
      SafeGetAndCleanupFuture(state.gemini_api_future_);
    } else if (status == std::future_status::timeout) {
      // Future is still running - we need to wait for it to complete
      // This blocks the main thread, but:
      // - The button is disabled, so this shouldn't happen in normal use
      // - It's necessary to prevent memory leaks from abandoned futures
      // - The wait is typically brief (just waiting for HTTP response)
      state.gemini_api_future_.wait();
      SafeGetAndCleanupFuture(state.gemini_api_future_);
    }
  }
}

void StartGeminiApiCall(GuiState& state) {
  CleanupGeminiFuture(state);
  state.gemini_api_call_in_progress_ = true;
  state.gemini_error_message_ = "";
  state.gemini_api_future_ = gemini_api_utils::GenerateSearchConfigAsync(
    std::string_view(state.gemini_description_input_.data()), "",
    30);  // NOLINT(readability-magic-numbers) - Timeout value in seconds is self-explanatory
}

bool HasSearchConfig(const gemini_api_utils::SearchConfig& config) {
  return !config.filename.empty() || !config.extensions.empty() || !config.path.empty() ||
         config.folders_only || config.case_sensitive ||
         (config.time_filter != "None" && !config.time_filter.empty()) ||
         (config.size_filter != "None" && !config.size_filter.empty());
}

void ApplySearchConfigResult(GuiState& state, const gemini_api_utils::GeminiApiResult& result) {
  if (HasSearchConfig(result.search_config)) {
    // Full search config - apply it to all search parameters
    state.ApplySearchConfig(result.search_config);
  } else if (!result.search_config.path.empty()) {
    // Path pattern only - populate Path Search field
    state.pathInput.SetValue(result.search_config.path);
    state.gemini_error_message_ = "✅ Path pattern applied successfully!";
    state.gemini_error_display_time_ = std::chrono::steady_clock::now() + std::chrono::seconds(kShortMessageDisplaySeconds);
  } else {
    state.gemini_error_message_ =
      "Warning: Clipboard contains valid JSON but no search configuration";
    state.gemini_error_display_time_ = std::chrono::steady_clock::now() + std::chrono::seconds(kDefaultMessageDisplaySeconds);
  }
}

void HandleHelpMeSearchButton(GuiState& state, UIActions* actions, bool is_index_building,
                              bool is_api_call_in_progress, bool has_description) {
  if (is_index_building || is_api_call_in_progress) {
    return;
  }

  if (has_description) {
    // Description provided - trigger AI search
    StartGeminiApiCall(state);
  } else {
    // No description - trigger manual search with current filters
    if (actions != nullptr) {
      actions->TriggerManualSearch(state);
    }
  }
}

void HandleGeneratePromptButton(GuiState& state, GLFWwindow* window, UIActions* actions,
                                bool is_index_building, bool is_api_call_in_progress,
                                bool has_description) {
  if (is_index_building || is_api_call_in_progress) {
    return;
  }

  if (has_description && window != nullptr) {
    // Description provided - build prompt and copy to clipboard
    const std::string prompt = gemini_api_utils::BuildSearchConfigPrompt(
      std::string_view(state.gemini_description_input_.data()));
    if (clipboard_utils::SetClipboardText(window, prompt)) {
      // Show success message
      state.gemini_error_message_ =
        "✅ Prompt copied to clipboard!\n\nNext steps:\n1. Paste the prompt into your AI assistant "
        "(Copilot, ChatGPT, etc.)\n2. Copy the AI's JSON response\n3. Click 'Paste Prompt from "
        "Clipboard' to apply the configuration";
      state.gemini_error_display_time_ =
        std::chrono::steady_clock::now() + std::chrono::seconds(kLongMessageDisplaySeconds);
    } else {
      state.gemini_error_message_ = "Failed to copy prompt to clipboard";
      state.gemini_error_display_time_ = std::chrono::steady_clock::now() + std::chrono::seconds(kDefaultMessageDisplaySeconds);
    }
  } else if (!has_description && actions != nullptr) {
    // No description - trigger manual search with current filters
    actions->TriggerManualSearch(state);
  } else if (has_description && window == nullptr) {
    state.gemini_error_message_ = "Error: Window handle not available for clipboard operations";
    state.gemini_error_display_time_ = std::chrono::steady_clock::now() + std::chrono::seconds(kDefaultMessageDisplaySeconds);
  }
}

void HandlePasteFromClipboardButton(GuiState& state, GLFWwindow* window) {
  if (window == nullptr) {
    state.gemini_error_message_ = "Error: Window handle not available for clipboard operations";
    state.gemini_error_display_time_ = std::chrono::steady_clock::now() + std::chrono::seconds(kDefaultMessageDisplaySeconds);
    return;
  }

  const std::string clipboard_text = clipboard_utils::GetClipboardText(window);
  if (clipboard_text.empty()) {
    state.gemini_error_message_ = "Clipboard is empty or contains no text";
    state.gemini_error_display_time_ = std::chrono::steady_clock::now() + std::chrono::seconds(kShortMessageDisplaySeconds);
    return;
  }

  // Parse JSON from clipboard
  auto result = gemini_api_utils::ParseSearchConfigJson(clipboard_text);
  if (result.success) {
    ApplySearchConfigResult(state, result);
    if (HasSearchConfig(result.search_config)) {
      state.gemini_error_message_ = "✅ Configuration applied successfully!";
      state.gemini_error_display_time_ = std::chrono::steady_clock::now() + std::chrono::seconds(kShortMessageDisplaySeconds);
    }
    // Keep the description input so user can regenerate or modify
  } else {
    // Error: show error message
    state.gemini_error_message_ = "Failed to parse JSON from clipboard: " + result.error_message;
    state.gemini_error_display_time_ = std::chrono::steady_clock::now() + std::chrono::seconds(kDefaultMessageDisplaySeconds);
  }
}

void ProcessGeminiApiResult(GuiState& state) {
  state.gemini_api_call_in_progress_ = false;

  try {
    auto result = state.gemini_api_future_.get();

    // Reset the future to invalid state to free resources (CRITICAL: must
    // happen even on error)
    state.gemini_api_future_ = std::future<gemini_api_utils::GeminiApiResult>();

    if (result.success) {
      ApplySearchConfigResult(state, result);
      // Keep the description input so user can regenerate or modify
    } else {
      // Error: show error message
      state.gemini_error_message_ = result.error_message;
      state.gemini_error_display_time_ = std::chrono::steady_clock::now() + std::chrono::seconds(kDefaultMessageDisplaySeconds);
    }
  } catch (
    const std::exception& e) {  // NOSONAR(cpp:S1181) - Catch-all needed: future.get() can throw
                                // various exception types (future_error, system_error, etc.)
    // Exception occurred - still need to clean up the future
    state.gemini_api_future_ = std::future<gemini_api_utils::GeminiApiResult>();
    (void)e;  // Suppress unused variable warning in Release builds
    LOG_ERROR_BUILD("Exception in Gemini API result processing: " << e.what());
    state.gemini_error_message_ = "Unexpected error processing API response";
    state.gemini_error_display_time_ = std::chrono::steady_clock::now() + std::chrono::seconds(kDefaultMessageDisplaySeconds);
  } catch (...) {  // NOSONAR(cpp:S2738) - Catch-all for any other exception - still need to clean
                   // up the future
    state.gemini_api_future_ = std::future<gemini_api_utils::GeminiApiResult>();
    LOG_ERROR_BUILD("Unknown exception in Gemini API result processing");
    state.gemini_error_message_ = "Unexpected error processing API response";
    state.gemini_error_display_time_ = std::chrono::steady_clock::now() + std::chrono::seconds(kDefaultMessageDisplaySeconds);
  }
}

}  // namespace ui
