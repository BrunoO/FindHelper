#pragma once

/**
 * @file ui/AiProviderWindow.h
 * @brief Dedicated "AI Settings" window: pick the AI provider for AI-assisted search.
 *
 * The window lists the curated provider catalog (Google Gemini, OpenCode,
 * OpenAI, Ollama, Custom endpoint) with a live API-key status per entry, and
 * exposes the connection details of the selected entry. It is launched from
 * the toolbar and from the provider chip in the AI-Assisted Search header.
 */

#include "core/Settings.h"

struct AppSettings;
struct GLFWwindow;

namespace ui {
class UIActions;
}

namespace ui::ai_provider_window {

/**
 * @brief Owns the working copy of the AI provider settings (editor scratch state).
 *
 * The window edits a *copy* of AppSettings::aiProvider so Close can discard
 * edits; that copy has to outlive a frame because the window is immediate-mode.
 * This is UI-thread-only state: it is written exclusively by Render() on the UI
 * thread and is not part of GuiState (see the FIELD OWNERSHIP MAP there, which
 * covers search/results state, not tool-window editor scratch).
 *
 * Declared here rather than as a function-local static so the state is visible
 * and reachable instead of being hidden inside the window's own translation
 * unit, and so the lifecycle below can be unit-tested with a local instance.
 */
struct AiProviderEditorState {
  /// Settings being edited; applied to AppSettings only on Save.
  AiProviderSettings working{};
  /// Whether the window was open on the previous frame. The working copy is
  /// re-synced from settings on the first frame after (re)opening, so a
  /// discarded edit can never resurface.
  bool previous_open = false;

  /**
   * @brief Syncs the working copy for this frame.
   *
   * On the first frame after (re)opening the working copy is taken from
   * @p settings, so edits discarded by Close cannot reappear; on later frames
   * the in-progress edits are kept.
   */
  void SyncForFrame(const AppSettings& settings) {
    if (previous_open) {
      return;  // In-progress edits of an open window are kept across frames.
    }
    working = settings.aiProvider;
    previous_open = true;
  }

  /**
   * @brief Applies the edited working copy to @p settings (the Save action).
   *
   * Close does the opposite: it simply drops the working copy, which
   * SyncForFrame() then rebuilds from settings on the next open.
   */
  void ApplyTo(AppSettings& settings) const { settings.aiProvider = working; }

  /**
   * @brief Records that the window is (still) open.
   *
   * Called after a Save or Close so the next open re-syncs the working copy.
   */
  void MarkClosed() { previous_open = false; }
};

/**
 * @brief Renders the AI Settings window.
 *
 * Edits a working copy of the AI provider settings; the changes reach the
 * live settings only when the user presses Save (Close discards them).
 *
 * @param p_open Pointer to the window visibility flag (modified when closed)
 * @param settings Application settings (updated on Save)
 * @param actions UI actions interface (used to persist the settings)
 * @param glfw_window GLFW window handle (for clipboard operations, may be nullptr)
 */
void Render(bool* p_open, AppSettings& settings, ui::UIActions* actions = nullptr,
            GLFWwindow* glfw_window = nullptr);

}  // namespace ui::ai_provider_window
