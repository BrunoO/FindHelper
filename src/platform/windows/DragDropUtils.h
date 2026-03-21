#pragma once

#include <string>
#include <string_view>
#include <vector>

/**
 * DragDropUtils - Windows Shell drag-and-drop helper
 *
 * This module provides a focused helper for initiating Windows shell drag-and-drop
 * operations from the GUI. It is designed to integrate with the ImGui-based
 * results table in main_gui.cpp and to interoperate cleanly with the Windows
 * shell (Explorer, Desktop, Outlook, etc.).
 *
 * DESIGN GOALS:
 * - Single responsibility: expose a minimal, easy-to-use API for starting a
 *   shell drag operation for one or more file system paths.
 * - Windows-only: this code uses COM and OLE drag-and-drop APIs and assumes
 *   a Windows environment. The application itself is Windows-only.
 * - UI-thread usage: DoDragDrop runs a modal loop on the calling thread and
 *   must be invoked from the UI thread. The helper functions are therefore
 *   intended to be called directly from main_gui.cpp in response to user
 *   interactions (mouse drag gestures).
 *
 * CURRENT SCOPE:
 * - Single-file and multi-file drag (via overloads).
 * - Files only: directories are rejected by the helpers.
 * - COPY semantics: the shell is asked to perform a copy (DROPEFFECT_COPY),
 *   leaving the original files untouched.
 */
namespace drag_drop {

  /**
   * StartFileDragDrop
   *
   * Initiates a Windows shell drag-and-drop operation for a single file path.
   *
   * PARAMETERS:
   * - full_path_utf8: Absolute file system path in UTF-8 encoding.
   *
   * BEHAVIOR:
   * - Validates input (non-empty, no embedded nulls).
   * - Converts the UTF-8 path to UTF-16 (std::wstring).
   * - Verifies that the path exists and refers to a regular file
   *   (directories are rejected).
   * - Creates a minimal IDataObject implementation exposing a CF_HDROP payload
   *   and a basic IDropSource, then calls DoDragDrop on the UI thread.
   *
   * RETURN VALUE:
   * - true  if DoDragDrop completed and the drop target accepted the drop
   *         (DROPEFFECT_COPY), or if the user cancelled the drag.
   * - false on immediate failure (invalid input, path missing, COM/OLE error).
   *
   * THREADING:
   * - Must be called on the thread that owns the main window and message loop.
   * - Assumes COM has been initialized for the calling thread (CoInitialize).
   */
  bool StartFileDragDrop(std::string_view full_path_utf8);

  /**
   * StartFileDragDrop (multi-file overload)
   *
   * Initiates a shell drag-and-drop operation for multiple file paths.
   * Paths that do not exist or refer to directories are skipped with a warning.
   * Returns false if no valid paths remain after filtering.
   *
   * Same threading and COM requirements as the single-path overload.
   */
  bool StartFileDragDrop(const std::vector<std::string_view>& full_paths_utf8);

} // namespace drag_drop

