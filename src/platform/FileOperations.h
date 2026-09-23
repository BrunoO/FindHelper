#pragma once

#include <string>
#include <string_view>

#include "utils/ClipboardUtils.h"
#include "utils/Logger.h"
#include "utils/PlatformTypes.h"

// Forward declaration
struct GLFWwindow;

/**
 * FileOperations - Cross-platform file operation utilities
 *
 * This module provides a set of utility functions for common file operations that
 * integrate with platform-specific APIs (Windows Shell API on Windows, NSWorkspace
 * on macOS, xdg-open/gio on Linux). It handles opening files with their default
 * applications, navigating to files in file managers (Explorer on Windows, Finder
 * on macOS, default file manager on Linux), clipboard operations, and safe file
 * deletion (Recycle Bin on Windows, Trash on macOS/Linux).
 *
 * THREADING MODEL:
 * - All functions in this module are synchronous and blocking. They are designed
 *   to be called directly from the UI thread in response to user actions
 *   (double-clicks, keyboard shortcuts, button clicks).
 * - The operations use platform-specific APIs which are inherently synchronous and
 *   can block for varying durations:
 *   * Opening files: Typically fast (<100ms) but can be slower if the default
 *     application takes time to launch or if file associations are being resolved.
 *   * Clipboard operations: Usually very fast (<10ms) but require the UI thread
 *     on Windows (OpenClipboard requires an HWND from the calling thread).
 *   * File deletion: Can take longer (100-500ms) especially for large files or
 *     when the Recycle Bin/Trash needs to process the operation.
 * - No explicit thread synchronization is needed because these functions are only
 *   called from the UI thread. However, the blocking nature means the UI may
 *   briefly freeze during these operations, particularly for file deletion.
 *
 * DESIGN TRADE-OFFS:
 * 1. Synchronous execution: We chose simplicity over async complexity. Making these
 *    operations asynchronous would require callback mechanisms, state management,
 *    and more complex error handling. For typical use cases, the blocking time is
 *    acceptable (most operations complete in <100ms), and users expect immediate
 *    feedback for actions like opening files.
 *
 * 2. Minimal error handling: Functions log errors but don't provide detailed error
 *    codes or user-facing error messages. This keeps the API simple, but means
 *    the UI layer cannot provide specific feedback about failures (e.g., "File is
 *    locked" vs "Access denied"). The trade-off is acceptable because most failures
 *    are rare and platforms typically provide their own error dialogs for file operations.
 *
 * 3. Platform-specific implementation: These functions use platform-specific APIs
 *    (Windows Shell API on Windows, NSWorkspace/NSPasteboard on macOS). The API
 *    is unified across platforms, but the implementation differs.
 *
 * 4. No operation cancellation: Once an operation starts, there's no way to cancel
 *    it. This is a limitation of the underlying platform APIs.
 *
 * 5. String encoding: All functions accept UTF-8 strings (std::string) and convert
 *    to platform-specific string types internally. This matches the application's
 *    internal string representation but requires conversion overhead.
 *
 * WINDOWS API REFERENCES:
 * - ShellExecuteW: https://docs.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shellexecutew
 * - SHFileOperationW: https://docs.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shfileoperationw
 * - ILCreateFromPathW: https://docs.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-ilcreatefrompathw
 * - SHOpenFolderAndSelectItems: https://docs.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shopenfolderandselectitems
 * - OpenClipboard: https://docs.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-openclipboard
 * - SetClipboardData: https://docs.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setclipboarddata
 *
 * macOS API REFERENCES:
 * - NSWorkspace: https://developer.apple.com/documentation/appkit/nsworkspace
 * - NSPasteboard: https://developer.apple.com/documentation/appkit/nspasteboard
 * - NSFileManager: https://developer.apple.com/documentation/foundation/nsfilemanager
 *
 * LINUX API REFERENCES:
 * - xdg-open: https://www.freedesktop.org/wiki/Software/xdg-utils/
 * - FreeDesktop.org Trash Specification: https://specifications.freedesktop.org/trash-spec/trashspec-1.0.html
 * - X11 Clipboard: xclip/xsel utilities
 * - Wayland Clipboard: wl-clipboard utility
 */
namespace file_operations {

namespace internal {
  /**
   * @brief Validates path input for file operations
   *
   * Checks that the path is not empty and does not contain embedded null characters
   * (security concern). Logs warnings/errors for invalid paths.
   *
   * @param path Path to validate
   * @param operation Name of the operation (for logging)
   * @return true if path is valid, false otherwise
   */
  inline bool ValidatePath(std::string_view path, const char* operation) {
    if (path.empty()) {
      LOG_WARNING(std::string(operation) + " called with empty path");
      return false;
    }
    // Check for embedded nulls (security concern)
    if (path.find('\0') != std::string_view::npos) {
      LOG_ERROR(std::string(operation) + " called with path containing null character");
      return false;
    }
    return true;
  }

  // Convert string_view to string and validate in one step.
  // Returns the converted string; sets valid=false and logs on failure.
  // Use at the top of each platform function: const std::string path = ToValidatedPath(full_path, "Func", ok); if (!ok) return;
  inline std::string ToValidatedPath(std::string_view full_path, const char* operation, bool& valid) {
    std::string path(full_path);
    valid = ValidatePath(path, operation);
    return path;
  }

  /**
   * @brief Security validation for paths used with ShellExecute
   *
   * Rejects potentially unsafe paths that could be used for privilege escalation
   * or arbitrary code execution. This is a defense-in-depth measure.
   *
   * Security checks:
   * - Rejects UNC paths (\\server\share) to prevent network access
   * - Rejects path traversal sequences (..) to prevent directory escape
   * - Rejects embedded nulls (already checked by ValidatePath, but double-check)
   *
   * Note: File paths come from the USN Journal/MFT (trusted source), but this
   * validation adds an extra layer of protection against malicious paths.
   *
   * @param path Path to validate
   * @return true if path is safe for ShellExecute, false otherwise
   */
  inline bool IsPathSafe(std::string_view path) {
    // Reject UNC paths (network shares) - could be used to access remote resources
    if (path.size() >= 2 && path[0] == '\\' && path[1] == '\\') {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - bounds checked by size() >= 2
      return false;
    }

    // Reject path traversal sequences - could be used to escape intended directory
    if (path.find("..") != std::string_view::npos) {
      return false;
    }

    // Reject embedded nulls (should already be caught by ValidatePath, but double-check)
    if (path.find('\0') != std::string_view::npos) {
      return false;
    }

    return true;
  }
} // namespace internal
  // Open file with default application
  // Falls back to "edit" verb if "open" fails (Windows only)
  void OpenFileDefault(std::string_view full_path);

  // Open parent folder in file manager and select the file
  // Windows: Opens Explorer and selects the file
  // macOS: Reveals file in Finder
  // Linux: Opens parent directory in default file manager
  void OpenParentFolder(std::string_view full_path);

  // Copy file path to clipboard (using GLFW clipboard utilities)
  // NOTE: Must be called from the UI thread
  inline void CopyPathToClipboard(struct GLFWwindow* window, std::string_view full_path) {
    const std::string path(full_path);
    if (!internal::ValidatePath(path, "CopyPathToClipboard")) {
      return;
    }
    if (clipboard_utils::SetClipboardText(window, path)) {
      LOG_INFO("Copied path to clipboard: " + path);
    } else {
      LOG_ERROR("Failed to copy path to clipboard: " + path);
    }
  }

  // Delete file to Recycle Bin (Windows) or Trash (macOS/Linux)
  // Returns true on success, false on failure (file not found, access denied, etc.)
  // Logs error details on failure
  bool DeleteFileToRecycleBin(const std::string &full_path);

}

