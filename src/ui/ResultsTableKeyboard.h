#pragma once

/**
 * @file ui/ResultsTableKeyboard.h
 * @brief Keyboard shortcut handling for the results table.
 *
 * This header exposes the keyboard dispatcher used by ResultsTable::Render.
 * Implementation lives in ResultsTableKeyboard.cpp.
 */

#include <string>
#include <string_view>
#include <vector>

#include "search/SearchTypes.h"   // For SearchResult
#include "utils/PlatformTypes.h"  // For NativeWindowHandle

// Forward declarations to avoid pulling heavy headers into all callers.
class GuiState;
class FileIndex;
struct GLFWwindow;

namespace ui {

class IncrementalSearchState;

// Render the inline "Filter in results" prompt/status line above the results table.
void RenderIncrementalSearchPrompt(const IncrementalSearchState& incremental_search);

// Render a compact status line when an inline "Filter in results" is active but the prompt is hidden.
void RenderIncrementalSearchFilterStatus(const IncrementalSearchState& incremental_search);

/**
 * @brief Report a clipboard-copy outcome as a status-bar toast.
 *
 * Success shows success_message via ExportWorkflowState::NotifySuccess (5s
 * auto-expiry in the status bar); failure shows an error toast instead.
 * Use when the copy itself happens elsewhere (e.g. validated helpers).
 * UI thread only.
 *
 * @param state GUI state receiving the toast (modified)
 * @param ok True when the clipboard copy succeeded
 * @param success_message Toast text on success
 */
void NotifyClipboardResult(GuiState& state, bool ok, std::string success_message);

/**
 * @brief Copy text to the clipboard and report the result as a status-bar toast.
 *
 * Centralizes the clipboard+toast pattern so copy handlers stay one-liners.
 * UI thread only.
 *
 * @param state GUI state receiving the toast (modified)
 * @param glfw_window GLFW window handle for clipboard access
 * @param text Text to copy (UTF-8)
 * @param success_message Toast text on success
 * @return true on success, false on failure
 */
[[nodiscard]] bool CopyTextToClipboardWithToast(GuiState& state, GLFWwindow* glfw_window,
                                  std::string_view text, std::string success_message);

/**
 * @brief Handle all keyboard shortcuts for the results table.
 *
 * This function is a pure refactor of the previous in-file implementation in ResultsTable.cpp.
 * It must preserve existing behaviour exactly.
 *
 * @param state           GUI state (selection, marks, notifications)
 * @param display_results Currently displayed results (matches the table contents)
 * @param file_index      File index for path lookups and bulk operations
 * @param glfw_window     GLFW window handle for clipboard interactions
 * @param shift           Whether Shift is currently pressed
 * @param native_window   Native window handle (HWND on Windows, nullptr elsewhere)
 */
void HandleResultsTableKeyboardShortcuts(  // NOSONAR(cpp:S107) - Stable public dispatcher API; params bundled internally via ShortcutFrame (see definition)
  GuiState& state,
  const std::vector<SearchResult>& display_results,
  const FileIndex& file_index,
  GLFWwindow* glfw_window,
  bool shift,
  [[maybe_unused]] NativeWindowHandle native_window,
  IncrementalSearchState& incremental_search,
  float current_table_scroll_y,
  const std::vector<SearchResult>& base_results);

/**
 * @brief Reset internal keyboard debounce states for testing.
 *
 * Clears the 50ms release cooldown tracking for M/T/U keys so tests can
 * execute back-to-back without sleeping for the cooldown duration.
 */
void ResetResultsTableKeyboardDebounceForTesting();

}  // namespace ui

