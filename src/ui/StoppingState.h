#pragma once

/**
 * @file ui/StoppingState.h
 * @brief Stopping state rendering component
 *
 * This component handles rendering the stopping state UI when the application
 * is shutting down. It displays a simple "Stopping..." message in place of
 * the results table.
 */

namespace ui {

/**
 * @class StoppingState
 * @brief Static utility class for stopping state rendering
 *
 * Renders a simple stopping message when the application is closing.
 * This provides visual feedback to the user that shutdown is in progress.
 */
class StoppingState {
public:
  /**
   * @brief Renders the stopping state UI
   *
   * Displays a centered "Stopping..." message with prominent spacing.
   * This replaces the results table when the window is closing.
   */
  static void Render();
};

} // namespace ui
