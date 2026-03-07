#pragma once

/**
 * @file ui/Popups.h
 * @brief Popup dialogs rendering component
 *
 * This component handles rendering all modal popups and dialogs:
 * - Regex generator popup (for path and filename inputs)
 * - Saved search popups (save and delete confirmation)
 *
 * Keyboard shortcuts are shown in the Help window (ui/HelpWindow), toggled from the application bar.
 */

#include <cstddef> // For size_t

// Forward declarations
class GuiState;
struct AppSettings;

namespace ui {

/**
 * @class Popups
 * @brief Static utility class for popup dialog rendering
 *
 * Renders modal popups for:
 * - Regex pattern generator (with templates and test preview)
 * - Saved search management (save and delete)
 */
class Popups {
public:

  /**
   * @brief Renders the regex generator popup for path input
   *
   * Wrapper that sets up the popup window and delegates to
   * RenderRegexGeneratorPopupContent().
   *
   * @param target_buffer Character buffer where the generated regex will be
   * copied
   * @param buffer_size Size of the target buffer
   * @param state GUI state for managing popup open/close state
   */
  static void RenderRegexGeneratorPopup(char *target_buffer,
                                        size_t buffer_size, GuiState& state);

  /**
   * @brief Renders the regex generator popup for filename input
   *
   * Similar to RenderRegexGeneratorPopup but for filename input field.
   * On macOS, renders as a floating window instead of modal popup.
   *
   * @param target_buffer Character buffer where the generated regex will be
   * copied
   * @param buffer_size Size of the target buffer
   * @param state GUI state for managing popup open/close state
   */
  static void RenderRegexGeneratorPopupFilename(char *target_buffer,
                                                size_t buffer_size, GuiState& state);

  /**
   * @brief Renders saved search popups (save and delete)
   *
   * Manages two modal popups:
   * - Save Search: Input field for search name, saves current filter state
   * - Delete Saved Search: Confirmation dialog for deleting saved searches
   *
   * @param state GUI state containing current filter settings
   * @param settings Application settings containing saved searches list
   */
  static void RenderSavedSearchPopups(GuiState &state, AppSettings &settings);

  /**
   * @brief Renders regex generator popup content (shared implementation)
   *
   * Internal implementation shared between path and filename regex generators.
   * Maintains separate state per popup_id to support multiple instances.
   *
   * @param target_buffer Buffer to insert generated pattern into
   * @param buffer_size Size of the target buffer
   * @param popup_id Unique identifier for this popup instance (e.g., "path", "filename")
   * @param gui_state Optional pointer to GuiState to trigger search update on insert
   */
  static void RenderRegexGeneratorPopupContent(char *target_buffer,
                                               size_t buffer_size,
                                               const char *popup_id,
                                               bool& is_open,
                                               const char* popup_name,
                                               GuiState* gui_state = nullptr);
};

} // namespace ui

