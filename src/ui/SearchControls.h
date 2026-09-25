#pragma once

/**
 * @file ui/SearchControls.h
 * @brief Search controls rendering component
 *
 * This component handles rendering the search controls row, including the Search button,
 * filter checkboxes (Folders Only, Case-Insensitive, Auto-refresh), Clear All button,
 * (Help is in the application bar.)
 */

// Forward declarations
class GuiState;

namespace ui {
  class UIActions;

/**
 * @class SearchControls
 * @brief Static utility class for search controls rendering
 *
 * Renders the search controls row with:
 * - Search button (manual trigger, bypasses debounce)
 * - Folders Only checkbox
 * - Case-Insensitive checkbox (with tooltip)
 * - Auto-refresh checkbox (with tooltip, disabled when index building)
 * - Clear All button
 * - Help is toggled from the application bar (keyboard shortcuts window)
 */
class SearchControls {
public:
  /**
   * @brief Renders the search controls row
   *
   * Displays the Search button, filter checkboxes (Folders Only,
   * Case-Insensitive, Auto-refresh), and Clear All button.
   * Help is toggled from the application bar. Disables controls when index is building.
   *
   * @param state GUI state containing filter settings (modified by checkboxes and buttons)
   * @param search_controller Search controller for triggering manual searches
   * @param search_worker Search worker for executing searches
   * @param is_index_building Whether the index is currently being built (disables search)
   */
  static void Render(const GuiState &state,
                     UIActions* actions,
                     bool is_index_building);
};

} // namespace ui

