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
#include <utility>

#include <GLFW/glfw3.h>

#include "api/GeminiApiUtils.h"
#include "api/AiProvider/ProviderFactory.h"
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
    [&future] {
      if (future.valid()) {
        (void)future.get();  // Discard result, just clean up
      }
    },
    "Error during future cleanup");
  future = std::future<gemini_api_utils::GeminiApiResult>();
}

// Single choke point for AI-assistant messages: keeps error_message,
// message_severity, and error_display_time consistent. Warning/Error are
// sticky (rendered until dismissed); Success auto-expires. Empty text
// always maps to None so nothing renders.
inline void SetAiMessage(GuiState& state, std::string message, AiMessageSeverity severity,  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables,readability-identifier-naming,cpp:S1238) - by value + move below: callers pass temporaries, avoids extra copy vs const-ref
                         int display_seconds) {
  state.gemini.error_message = std::move(message);
  if (state.gemini.error_message.empty()) {
    state.gemini.message_severity = AiMessageSeverity::None;
  } else {
    state.gemini.message_severity = severity;
  }
  state.gemini.error_display_time =
      std::chrono::steady_clock::now() + std::chrono::seconds(display_seconds);
}
}  // anonymous namespace

void CleanupGeminiFuture(GuiState& state) {
  if (state.gemini.api_future.valid()) {
    if (const auto status = state.gemini.api_future.wait_for(std::chrono::milliseconds(0));
        status == std::future_status::ready) {
      // Future is ready but not processed - clean it up now
      SafeGetAndCleanupFuture(state.gemini.api_future);
    } else if (status == std::future_status::timeout) {
      // Future is still running - we need to wait for it to complete
      // This blocks the main thread, but:
      // - The button is disabled, so this shouldn't happen in normal use
      // - It's necessary to prevent memory leaks from abandoned futures
      // - The wait is typically brief (just waiting for HTTP response)
      state.gemini.api_future.wait();
      SafeGetAndCleanupFuture(state.gemini.api_future);
    }
  }
}

void StartAiSearchCall(GuiState& state, const ai_provider::ProviderConfig& config) {
  CleanupGeminiFuture(state);
  state.gemini.api_call_in_progress = true;
  state.gemini.api_provider_display_name = ai_provider::DisplayName(config);
  SetAiMessage(state, "", AiMessageSeverity::None, 0);
  auto provider = ai_provider::CreateProviderForConfig(config);
  const std::string default_env = provider->DefaultEnvVarName();
  const std::string api_key = ai_provider::ResolveApiKey(config, default_env);
  state.gemini.api_future = provider->GenerateSearchConfigAsync(
    std::string_view(state.gemini.description_input.data()), api_key,
    config.timeout_seconds);
}

bool HasSearchConfig(const gemini_api_utils::SearchConfig& config) {
  return !config.filename.empty() || !config.extensions.empty() || !config.path.empty() ||
         config.folders_only || config.files_only || config.case_sensitive ||
         (config.time_filter != "None" && !config.time_filter.empty()) ||
         (config.size_filter != "None" && !config.size_filter.empty());
}

void ApplySearchConfigResult(GuiState& state, const gemini_api_utils::GeminiApiResult& result) {
  if (HasSearchConfig(result.search_config)) {
    // Full search config - apply it to all search parameters
    state.ApplySearchConfig(result.search_config);
  } else if (!result.search_config.path.empty()) {
    // Path pattern only - populate Path Search field
    state.searchCriteria.path_input.SetValue(result.search_config.path);
    SetAiMessage(state, "✅ Path pattern applied successfully!", AiMessageSeverity::Success,
                 kShortMessageDisplaySeconds);
  } else {
    SetAiMessage(state, "Warning: Clipboard contains valid JSON but no search configuration",
                 AiMessageSeverity::Warning, kDefaultMessageDisplaySeconds);
  }
}

void HandleHelpMeSearchButton(GuiState& state, UIActions* actions, bool is_index_building,
                              bool is_api_call_in_progress, bool has_description,
                              const ai_provider::ProviderConfig& provider_config) {
  if (is_index_building || is_api_call_in_progress) {
    return;
  }

  if (has_description) {
    // Description provided - trigger AI search via the configured provider
    StartAiSearchCall(state, provider_config);
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
      std::string_view(state.gemini.description_input.data()));
    if (clipboard_utils::SetClipboardText(window, prompt)) {
      // Show success message
      SetAiMessage(state,
                   "✅ Prompt copied to clipboard!\n\nNext steps:\n1. Paste the prompt into your AI assistant "
                   "(Copilot, ChatGPT, etc.)\n2. Copy the AI's JSON response\n3. Click 'Paste Prompt from "
                   "Clipboard' to apply the configuration",
                   AiMessageSeverity::Success, kLongMessageDisplaySeconds);
    } else {
      SetAiMessage(state, "Failed to copy prompt to clipboard", AiMessageSeverity::Error,
                   kDefaultMessageDisplaySeconds);
    }
  } else if (!has_description && actions != nullptr) {
    // No description - trigger manual search with current filters
    actions->TriggerManualSearch(state);
  } else if (has_description && window == nullptr) {
    SetAiMessage(state, "Error: Window handle not available for clipboard operations",
                 AiMessageSeverity::Error, kDefaultMessageDisplaySeconds);
  }
}

void HandlePasteFromClipboardButton(GuiState& state, GLFWwindow* window) {
  if (window == nullptr) {
    SetAiMessage(state, "Error: Window handle not available for clipboard operations",
                 AiMessageSeverity::Error, kDefaultMessageDisplaySeconds);
    return;
  }

  const std::string clipboard_text = clipboard_utils::GetClipboardText(window);
  if (clipboard_text.empty()) {
    SetAiMessage(state, "Clipboard is empty or contains no text", AiMessageSeverity::Error,
                 kShortMessageDisplaySeconds);
    return;
  }

  // Parse JSON from clipboard
  if (const auto result = gemini_api_utils::ParseSearchConfigJson(clipboard_text);
      result.success) {
    ApplySearchConfigResult(state, result);
    if (HasSearchConfig(result.search_config)) {
      SetAiMessage(state, "✅ Configuration applied successfully!", AiMessageSeverity::Success,
                   kShortMessageDisplaySeconds);
    }
    // Keep the description input so user can regenerate or modify
  } else {
    // Error: show error message
    SetAiMessage(state, "Failed to parse JSON from clipboard: " + result.error_message,
                 AiMessageSeverity::Error, kDefaultMessageDisplaySeconds);
  }
}

void ProcessGeminiApiResult(GuiState& state) {
  state.gemini.api_call_in_progress = false;

  try {
    const auto result = state.gemini.api_future.get();

    // Reset the future to invalid state to free resources (CRITICAL: must
    // happen even on error)
    state.gemini.api_future = std::future<gemini_api_utils::GeminiApiResult>();

    if (result.success) {
      ApplySearchConfigResult(state, result);
      // Keep the description input so user can regenerate or modify
    } else {
      // Error: show error message (sticky until dismissed, with Copy support)
      SetAiMessage(state, result.error_message, AiMessageSeverity::Error,
                   kDefaultMessageDisplaySeconds);
    }
  } catch (
    const std::exception& e) {  // NOSONAR(cpp:S1181) - Catch-all needed: future.get() can throw
                                // various exception types (future_error, system_error, etc.)
    // Exception occurred - still need to clean up the future
    state.gemini.api_future = std::future<gemini_api_utils::GeminiApiResult>();
    (void)e;  // Suppress unused variable warning in Release builds
    LOG_ERROR_BUILD("Exception in Gemini API result processing: " << e.what());
    SetAiMessage(state, "Unexpected error processing API response", AiMessageSeverity::Error,
                 kDefaultMessageDisplaySeconds);
  } catch (...) {  // NOSONAR(cpp:S2738) - Catch-all for any other exception - still need to clean
                   // up the future
    state.gemini.api_future = std::future<gemini_api_utils::GeminiApiResult>();
    LOG_ERROR_BUILD("Unknown exception in Gemini API result processing");
    SetAiMessage(state, "Unexpected error processing API response", AiMessageSeverity::Error,
                 kDefaultMessageDisplaySeconds);
  }
}

}  // namespace ui
