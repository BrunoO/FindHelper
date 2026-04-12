#pragma once

/**
 * @file ui/CenteredToolWindow.h
 * @brief Helpers for centered, resizable tool windows (Help, Search Help, etc.)
 *
 * Reduces duplication between HelpWindow and SearchHelpWindow by providing:
 * - SetupCenteredToolWindow: set next window position (center) and size
 * - RenderToolWindowCloseButton: separator + close button that sets *p_open = false
 */

namespace ui::detail {

/**
 * Sets the next ImGui window position (centered on viewport) and size.
 * Call before ImGui::Begin() (or WindowGuard). Uses ImGuiCond_FirstUseEver.
 */
void SetupCenteredToolWindow(float width, float height);

/**
 * Renders a separator and a "Close" button; when clicked, sets *p_open = false.
 * Call at the end of window content, inside the WindowGuard block.
 */
void RenderToolWindowCloseButton(bool* p_open);

}  // namespace ui::detail
