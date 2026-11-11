#pragma once

/**
 * @file ui/AiSearchInputsLogic.h
 * @brief Pure (ImGui-free, GuiState-free) decisions of the "Help Me Search" UI.
 *
 * The AI search inputs mix three things: ImGui widgets, GUI state mutation and
 * small decisions about *what should happen* when the user acts or when an AI
 * response arrives. Only the last part is here, so it can be unit-tested
 * without a window, a GLFW context or a populated GuiState.
 *
 * `SearchInputsGeminiHelpers` performs the side effects (starting the call,
 * applying the config, triggering the search) using these predicates.
 */

#include <cstdint>

#include "api/GeminiApiUtils.h"

namespace ui::ai_search_inputs {

/// What the user's press of the "Help Me Search" button should do.
enum class HelpMeSearchAction : std::uint8_t {
  /// No description typed: fall through to a plain manual search.
  ManualSearch,
  /// Description typed and no call in flight: start the AI call.
  StartAiCall,
  /// An AI call is already running: ignore the press (no duplicate call).
  IgnoreInFlight,
  /// The index is still building: refuse, the search cannot run yet.
  BlockedByIndexBuild,
};

/// Decides the "Help Me Search" behaviour from the current UI flags.
///
/// Precedence: an in-flight call wins over everything (re-pressing must never
/// dispatch a second request or re-enter the pipeline), then the index-build
/// guard, then the description check.
[[nodiscard]] constexpr HelpMeSearchAction ResolveHelpMeSearchAction(bool is_index_building,
                                                                    bool is_api_call_in_progress,
                                                                    bool has_description) {
  if (is_api_call_in_progress) {
    return HelpMeSearchAction::IgnoreInFlight;
  }
  if (is_index_building) {
    return HelpMeSearchAction::BlockedByIndexBuild;
  }
  if (!has_description) {
    return HelpMeSearchAction::ManualSearch;
  }
  return HelpMeSearchAction::StartAiCall;
}

/// What should happen once an AI response has been received.
enum class AiResultAction : std::uint8_t {
  /// Success: apply the config and run the search once.
  ApplyConfigAndSearch,
  /// Failure: report the provider error, do not touch the results.
  ReportError,
};

/// Decides how to handle a completed AI call. Kept trivial but explicit so the
/// "apply exactly once, then search exactly once" contract is asserted by a
/// test rather than implied by the call site.
[[nodiscard]] constexpr AiResultAction ResolveAiResultAction(
    const gemini_api_utils::GeminiApiResult& result) {
  return result.success ? AiResultAction::ApplyConfigAndSearch : AiResultAction::ReportError;
}

}  // namespace ui::ai_search_inputs
