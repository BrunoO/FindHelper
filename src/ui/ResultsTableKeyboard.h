#pragma once

/**
 * @file ui/ResultsTableKeyboard.h
 * @brief Keyboard shortcut handling for the results table.
 *
 * This header exposes the keyboard dispatcher used by ResultsTable::Render.
 * Implementation lives in ResultsTableKeyboard.cpp.
 */

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
void HandleResultsTableKeyboardShortcuts(
  GuiState& state,
  const std::vector<SearchResult>& display_results,
  const FileIndex& file_index,
  GLFWwindow* glfw_window,
  bool shift,
  [[maybe_unused]] NativeWindowHandle native_window,
  IncrementalSearchState& incremental_search,
  float current_table_scroll_y,
  const std::vector<SearchResult>& base_results);

}  // namespace ui

