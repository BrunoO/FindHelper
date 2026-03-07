#pragma once

/**
 * @file ui/SearchInputs.h
 * @brief Search input fields rendering component
 *
 * This component handles rendering all search input fields:
 * - Path Search input (primary search bar with help and regex generator)
 * - Gemini API natural language input field
 * - Extensions input field
 * - Name input field (with help and regex generator)
 */

#include <cstddef> // For size_t

// Forward declarations
class GuiState;
struct GLFWwindow;
struct AppSettings;

namespace ui {
  class UIActions;
}

namespace ui {

/**
 * @struct InputFieldOptions
 * @brief Configuration options for RenderInputFieldWithEnter
 *
 * Groups optional parameters to keep function signature clean (max 7 params).
 * All fields have sensible defaults for common use cases.
 */
struct InputFieldOptions {
  float width = 0.0F;                    ///< Suggested width (0 = auto-calculate)
  bool show_help = false;                ///< Show help (?) button
  bool show_generator = false;           ///< Show regex generator button
  bool request_focus = false;            ///< Set keyboard focus on this field
  float reserved_right_space = 0.0F;     ///< Space to reserve for buttons on right
  const char* placeholder = nullptr;     ///< Placeholder text when field is empty
  bool is_optional = false;              ///< Mark field as optional (shows "(optional)" in lighter text)
};

/**
 * @class SearchInputs
 * @brief Static utility class for search input fields rendering
 *
 * Renders all search input fields:
 * - Path Search: Primary search bar with help (?) and regex generator ([R]) buttons
 * - AI Search: Gemini API natural language input with "Generate Pattern" and "Generate Search Config" buttons
 * - Extensions: Filter by extensions
 * - Name: Filter by name pattern (with Ctrl+F focus support, help, and regex generator)
 *
 * Handles Enter key presses to trigger manual searches.
 */
class SearchInputs {
public:
  /**
   * @brief Renders all search input fields
   *
 * Displays the main search input fields:
 * - Path Search: Primary search bar with help and regex generator
 * - AI Search: Gemini API natural language input with pattern and full config generation
 * - Extensions: Filter by extensions
 * - Name: Filter by name pattern (with Ctrl+F focus support)
   *
   * Handles Enter key presses to trigger manual searches.
   *
   * @param state GUI state containing input buffers (modified by user input)
   * @param actions UI actions interface for triggering searches
   * @param is_index_building Whether the index is currently being built (disables search)
   * @param settings Application settings (read-only, used to check UI mode)
   * @param window GLFW window handle for clipboard operations (can be nullptr)
   */
  static void Render(GuiState &state,
                     UIActions* actions,
                     bool is_index_building,
                     const AppSettings& settings,
                     GLFWwindow* window = nullptr);

  /**
   * @brief Renders AI Search input field with Gemini API integration
   *
   * Displays the AI Search input field (multi-line, 2 lines) with buttons for generating search configurations.
   * Handles both direct API calls (when API key is set) and clipboard workflow.
   * Settings and Metrics toggles are rendered on the same line as the AI Search label.
   * "Help Me Search" button triggers AI search if description is provided, otherwise triggers manual search.
   *
   * @param state GUI state containing input buffers and API state (modified by user input)
   * @param window GLFW window handle for clipboard operations (can be nullptr)
   * @param search_controller Search controller for triggering manual searches (can be nullptr)
   * @param search_worker Search worker for executing searches (can be nullptr)
   * @param is_index_building Whether the index is currently being built (disables search)
   * @param show_settings Reference to settings window visibility flag
   * @param show_metrics Reference to metrics window visibility flag
   * @param metrics_available Whether Metrics UI is available
   */
  static void RenderAISearch(GuiState &state, GLFWwindow* window = nullptr,
                              UIActions* actions = nullptr,
                              bool is_index_building = false,
                              bool *show_settings = nullptr, bool *show_metrics = nullptr,
                              bool metrics_available = false);

  /**
   * @brief Renders an input field with Enter key support and optional help/generator buttons
   *
   * Creates a labeled input field that triggers a callback when Enter is pressed.
   * Optionally displays help (?) and regex generator ([R]) buttons.
   *
   * @param label Display label for the input field
   * @param id Unique ImGui ID for the input field
   * @param buffer Character buffer for input text
   * @param buffer_size Size of the input buffer
   * @param state GUI state (used to mark input as changed)
   * @param options Optional configuration (width, buttons, placeholder, etc.)
   * @return true if Enter key was pressed, false otherwise
   */
  static bool RenderInputFieldWithEnter(const char *label, const char *id,
                                        char *buffer, size_t buffer_size,
                                        GuiState &state,
                                        const InputFieldOptions& options = {});
};

} // namespace ui

