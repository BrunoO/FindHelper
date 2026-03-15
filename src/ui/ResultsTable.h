#pragma once

/**
 * @file ui/ResultsTable.h
 * @brief Search results table rendering component
 *
 * This component handles rendering the search results table with:
 * - Base columns: Filename, Size, Last Modified, Full Path, Extension
 * - Optional folder statistics columns: Matched Files, Matched Size
 * - Column sorting with async attribute loading
 * - Row selection and interaction
 * - Double-click to open/reveal
 * - Right-click context menu (Windows)
 * - Drag-and-drop (Windows)
 * - Delete key handling
 * - Delete confirmation popup
 * - Virtual scrolling
 * - Lazy loading of file metadata
 * - Time filter application
 */

#include <cstddef>  // For size_t
#include <string>
#include <string_view>

#include "search/SearchTypes.h"   // For SearchResult
#include "utils/PlatformTypes.h"  // For NativeWindowHandle

// Forward declarations
class GuiState;
class FileIndex;
class ThreadPool;
class FolderSizeAggregator;
struct GLFWwindow;

namespace ui {

/**
 * @class ResultsTable
 * @brief Static utility class for search results table rendering
 *
 * Renders a sortable, scrollable table with base columns:
 * - Filename: Double-click to open, right-click for context menu (Windows), Ctrl+C to copy filename, drag to copy (Windows and macOS)
 * - Size: File size with lazy loading (shows "..." while loading)
 * - Last Modified: Modification time with lazy loading (shows "..." while loading)
 * - Full Path: Truncated with ellipsis, double-click to reveal in Finder/Explorer
 * - Extension: File extension
 *
 * When folder statistics are enabled via GUI state, two additional columns appear:
 * - Matched Files: Recursive count of displayed files under a folder
 * - Matched Size: Recursive total size of displayed files under a folder
 *
 * Features:
 * - Virtual scrolling for performance (ImGuiListClipper)
 * - Column sorting (persists across searches)
 * - Row selection (Delete key to delete selected file)
 * - Drag-and-drop from filename/path columns (Windows and macOS)
 * - Lazy loading of file sizes and modification times
 * - Time filter application (if active)
 * - Pending deletion highlighting (grayed out files)
 */
class ResultsTable {
 public:
  /**
   * @brief Renders the search results table with sorting and interaction
   *
   * Displays a sortable, scrollable table with 5 columns. Handles sorting,
   * selection, drag-and-drop, and delete operations.
   *
   * @param state GUI state containing search results, selection, and filter state (modified by user
   * interactions)
   * @param native_window Platform-specific window handle (HWND on Windows, nullptr on macOS)
   * @param glfw_window GLFW window handle for clipboard and drag-drop
   * @param thread_pool Thread pool for parallel sorting operations
   * @param file_index File index for lazy loading file metadata (size, modification time)
   * @param show_path_hierarchy_indentation When true, indent filenames by folder depth
   */
  static void Render(GuiState& state, [[maybe_unused]] NativeWindowHandle native_window,
                     GLFWwindow* glfw_window, ThreadPool& thread_pool, FileIndex& file_index,
                     FolderSizeAggregator* aggregator = nullptr,
                     bool show_path_hierarchy_indentation = true);

  /**
   * @brief Gets formatted file size display text for a search result
   *
   * Returns cached display string or formats on-demand. Handles special cases:
   * - Directories: Returns "Folder"
   * - Not loaded: Returns "..." (loading indicator)
   * - Load failed: Returns "N/A" (access denied)
   *
   * @param result Search result containing file size and display string
   * @return Formatted size string (e.g., "1.5 MB") or special indicator
   */
  static const char* GetSizeDisplayText(const SearchResult& result);

  /**
   * @brief Gets formatted modification time display text for a search result
   *
   * Returns cached display string or formats on-demand. Handles special cases:
   * - Directories: Returns "Folder"
   * - Not loaded: Returns "..." (loading indicator)
   * - Load failed: Returns "N/A" (access denied)
   *
   * @param result Search result containing modification time and display string
   * @return Formatted time string (e.g., "2024-01-15 14:30:00") or special indicator
   */
  static const char* GetModTimeDisplayText(const SearchResult& result);

  /**
   * @brief Extract directory path from SearchResult
   *
   * Uses filename string_view offset to efficiently extract directory portion
   * without string allocation. Returns empty string_view for root files.
   *
   * @param result Search result
   * @return Directory path as string_view (empty if root file)
   */
  static std::string_view GetDirectoryPath(const SearchResult& result) {
    // If filename starts at the beginning of fullPath, there's no directory (root file)
    if (result.filename_offset == 0U) {
      return {};
    }

    // Directory path is everything before the separator immediately preceding the filename.
    // fullPath = directory + separator + filename[+extension]
    // Therefore directory_length = filename_offset - 1 (exclude that separator).
    const size_t directory_length = result.filename_offset - 1U;
    return result.fullPath.substr(0, directory_length);
  }

  /**
   * @brief Truncates a path at the beginning to fit within max width
   *
   * Uses ImGui text width calculation to determine how many characters
   * can fit, then truncates from the beginning and adds "..." prefix.
   *
   * @param path Full path to truncate
   * @param max_width Maximum width in pixels
   * @return Truncated path with "..." prefix if needed
   */
  static std::string TruncatePathAtBeginning(std::string_view path, float max_width);

  /**
   * @brief Renders the Full Path column with ellipsis at the beginning
   *
   * Shows directory path (without filename) in the column to avoid duplication
   * with the Filename column. Shows full path in tooltip on hover. Handles
   * selection, double-click, and Ctrl+C. Returns true if selection changed.
   *
   * @param result Search result containing the full path
   * @param is_selected Whether this row is currently selected
   * @param row Row index in the table
   * @param state GUI state (modified by selection changes)
   * @param native_window Platform-specific window handle for file operations
   * @return true if selection changed, false otherwise
   */
  static bool RenderPathColumnWithEllipsis(const SearchResult& result, bool is_selected, int row,
                                           GuiState& state, NativeWindowHandle native_window,
                                           GLFWwindow* glfw_window, float max_width);
};

/**
 * @brief Copy marked result paths to clipboard (newline-separated).
 * Used by W shortcut and "Copy Paths" button; exposed for ImGui test engine hook.
 */
void CopyMarkedPathsToClipboard(const GuiState& state, GLFWwindow* glfw_window,
                                const FileIndex& file_index);

/**
 * @brief Copy marked result filenames to clipboard (newline-separated).
 * Used by Shift+W shortcut; exposed for ImGui test engine hook.
 */
void CopyMarkedFilenamesToClipboard(const GuiState& state, GLFWwindow* glfw_window,
                                    const FileIndex& file_index);

}  // namespace ui
