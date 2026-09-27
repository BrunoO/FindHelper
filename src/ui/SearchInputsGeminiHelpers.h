#pragma once

/**
 * @file ui/SearchInputsGeminiHelpers.h
 * @brief Helper functions for Gemini API integration in search inputs
 *
 * This file contains helper functions extracted from SearchInputs.cpp to reduce
 * file size and improve maintainability. These functions handle Gemini API
 * integration, including future cleanup, API calls, and result processing.
 */

#include "api/GeminiApiUtils.h"
#include "gui/GuiState.h"
#include <GLFW/glfw3.h>
#include <chrono>

namespace ui {
  class UIActions;

/**
 * @brief Cleanup Gemini API future
 *
 * Waits for and cleans up the Gemini API future to prevent memory leaks.
 *
 * @param state GUI state containing the future
 */
void CleanupGeminiFuture(GuiState& state);

/**
 * @brief Start a new Gemini API call
 *
 * Cleans up any existing future and starts a new search config API call.
 *
 * @param state GUI state (modified)
 */
void StartGeminiApiCall(GuiState& state);

/**
 * @brief Check if search config has meaningful content
 *
 * Determines if a search config contains any actual search parameters.
 *
 * @param config Search config to check
 * @return True if config has meaningful content
 */
bool HasSearchConfig(const gemini_api_utils::SearchConfig& config);

/**
 * @brief Apply search config result to GUI state
 *
 * Applies a search config or path pattern to the GUI state, with appropriate
 * success/error messages.
 *
 * @param state GUI state (modified)
 * @param result Gemini API result containing search config
 */
void ApplySearchConfigResult(GuiState& state, const gemini_api_utils::GeminiApiResult& result);

/**
 * @brief Handle "Help Me Search" button click
 *
 * Handles the button click when API key is set. Triggers AI search if description
 * is provided, otherwise triggers manual search.
 *
 * @param state GUI state (modified)
 * @param search_controller Search controller for triggering searches
 * @param search_worker Search worker for executing searches
 * @param is_index_building Whether index is currently building
 * @param is_api_call_in_progress Whether API call is in progress
 * @param has_description Whether description input has content
 */
void HandleHelpMeSearchButton(GuiState& state,
                               UIActions* actions,
                               bool is_index_building,
                               bool is_api_call_in_progress,
                               bool has_description);

/**
 * @brief Handle "Generate Prompt" button click
 *
 * Handles the button click when API key is not set. Generates prompt and copies
 * to clipboard, or triggers manual search if no description.
 *
 * @param state GUI state (modified)
 * @param window GLFW window for clipboard operations
 * @param search_controller Search controller for triggering searches
 * @param search_worker Search worker for executing searches
 * @param is_index_building Whether index is currently building
 * @param is_api_call_in_progress Whether API call is in progress
 * @param has_description Whether description input has content
 */
void HandleGeneratePromptButton(GuiState& state,
                                 GLFWwindow* window,
                                 UIActions* actions,
                                 bool is_index_building,
                                 bool is_api_call_in_progress,
                                 bool has_description);

/**
 * @brief Handle "Paste Prompt from Clipboard" button click
 *
 * Retrieves JSON from clipboard, parses it, and applies search configuration.
 *
 * @param state GUI state (modified)
 * @param window GLFW window for clipboard operations
 */
void HandlePasteFromClipboardButton(GuiState& state, GLFWwindow* window);

/**
 * @brief Process completed Gemini API call
 *
 * Processes the result of a completed Gemini API call and applies the search
 * configuration to the GUI state.
 *
 * @param state GUI state (modified)
 */
void ProcessGeminiApiResult(GuiState& state);

} // namespace ui
