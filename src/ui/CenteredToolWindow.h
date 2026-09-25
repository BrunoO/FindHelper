#pragma once

/**
 * @file ui/CenteredToolWindow.h
 * @brief Helpers for centered, resizable tool windows (Help, Search Help, etc.)
 *
 * Reduces duplication between HelpWindow and SearchHelpWindow by providing:
 * - SetupCenteredToolWindow: set next window position (center) and size
 * - RenderToolWindowFooterSeparator: spacing + separator before footer actions
 * - AlignToolWindowFooterButtons: right-align a row of fixed-width buttons
 * - RenderToolWindowCloseButton: separator + close button that sets *p_open = false
 */

namespace ui::detail {

/**
 * Sets the next ImGui window position (centered on viewport) and size.
 * Call before ImGui::Begin() (or WindowGuard). Uses ImGuiCond_FirstUseEver.
 */
void SetupCenteredToolWindow(float width, float height);

/**
 * Renders spacing and a horizontal separator before a tool-window footer action row.
 * Call at the end of scrollable content, before Save/Close or other footer buttons.
 */
void RenderToolWindowFooterSeparator();

/**
 * Sets the cursor X so the next @p button_count buttons (each @p button_width wide) align
 * to the right edge of the content region. Call immediately before the first footer button.
 */
void AlignToolWindowFooterButtons(int button_count, float button_width);

/**
 * Renders a separator and a "Close" button; when clicked, sets *p_open = false.
 * Call at the end of window content, inside the WindowGuard block.
 */
void RenderToolWindowCloseButton(bool* p_open);

}  // namespace ui::detail
