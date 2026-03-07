#pragma once

#include <string>
#include <string_view>

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
 * CURRENT SCOPE (FIRST ITERATION):
 * - Single-file drag only (no multi-selection).
 * - Files only: directories are rejected by the helper.
 * - COPY semantics: the shell is asked to perform a copy (DROPEFFECT_COPY),
 *   leaving the original file untouched.
 *
 * FUTURE EXTENSIONS:
 * - Multi-file drag: accept a list of paths and build a multi-entry CF_HDROP.
 * - Optional MOVE semantics: allow callers to request DROPEFFECT_MOVE where
 *   appropriate.
 * - Additional formats: add support for text/URL formats if needed by other
 *   consumers.
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

} // namespace drag_drop

