#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace path_utils {

// ============================================================================
// Path Separators
// ============================================================================

#ifdef _WIN32
inline constexpr char kPathSeparator = '\\';
inline constexpr const char* kPathSeparatorStr = "\\";
#else
inline constexpr char kPathSeparator = '/';
inline constexpr const char* kPathSeparatorStr = "/";
#endif  // _WIN32

// ============================================================================
// Root Path Constants
// ============================================================================

/**
 * @brief Get the default volume root path for the current platform
 *
 * Windows: "C:\\" (default, can be overridden)
 * macOS/Linux: "/"
 */
std::string GetDefaultVolumeRootPath();

/**
 * @brief Get a view of the default volume root path without allocating
 *
 * Returns a std::string_view into internal storage on Windows (g_defaultVolumeRootPath)
 * or a string literal on macOS/Linux. This is useful for performance-sensitive
 * code paths (e.g. path recomputation) that only need to read the root prefix.
 */
std::string_view GetDefaultVolumeRootPathView();

/**
 * @brief Set the default volume root path (Windows only)
 *
 * Used when the monitored volume is changed via command line or settings.
 *
 * @param root New root path (e.g., "D:\\")
 */
void SetDefaultVolumeRootPath(std::string_view root);

/**
 * @brief Get the default user root path for the current platform
 *
 * Windows: "C:\\Users\\"
 * macOS/Linux: "/Users/" or "/home/" depending on system
 */
std::string GetDefaultUserRootPath();

// ============================================================================
// User Home Directory
// ============================================================================

/**
 * @brief Get the user's home directory path
 *
 * Windows: Uses USERPROFILE environment variable, falls back to default
 * macOS/Linux: Uses HOME environment variable, falls back to default
 *
 * @return User home directory path (e.g., "C:\\Users\\John" or "/Users/john")
 */
std::string GetUserHomePath();

// ============================================================================
// Special Folders
// ============================================================================

/**
 * @brief Get the Desktop folder path
 *
 * Windows: Uses FOLDERID_Desktop or falls back to USERPROFILE\\Desktop
 * macOS: Uses ~/Desktop
 * Linux: Uses ~/Desktop
 *
 * @return Desktop folder path
 */
std::string GetDesktopPath();

/**
 * @brief Get the Downloads folder path
 *
 * Windows: Uses FOLDERID_Downloads (Known Folder API), falls back to USERPROFILE\\Downloads
 * macOS: Uses ~/Downloads
 * Linux: Uses ~/Downloads
 *
 * @return Downloads folder path
 */
std::string GetDownloadsPath();

/**
 * @brief Get the Trash/Recycle Bin folder path
 *
 * Windows: Uses FOLDERID_RecycleBinFolder
 * macOS: Uses ~/.Trash
 * Linux: Uses ~/.local/share/Trash/files
 *
 * @return Trash/Recycle Bin folder path
 */
std::string GetTrashPath();

// ============================================================================
// Path Joining
// ============================================================================

/**
 * @brief Get the directory containing the current executable
 *
 * Used to resolve relative paths (e.g. --index-from-file) when the process
 * working directory is not the project root (e.g. when launching the .app from Finder).
 *
 * Windows: Uses GetModuleFileNameA; macOS: _NSGetExecutablePath; Linux: readlink /proc/self/exe.
 * All platforms return the path with a trailing separator (e.g. "C:\\dir\\" or "/dir/").
 *
 * @return Directory path with trailing separator, or empty string on failure
 */
std::string GetExecutableDirectory();

/**
 * @brief Get the parent directory of the given path
 *
 * Returns the path without a trailing separator (unlike GetExecutableDirectory).
 * When chaining with GetParentDirectory (e.g. walking up from GetExecutableDirectory()),
 * the result has no trailing separator.
 *
 * @param path Path (file or directory)
 * @return Parent path without trailing separator (e.g. "/a/b" from "/a/b/c"), or empty if path has no parent
 */
std::string GetParentDirectory(std::string_view path);

/**
 * @brief Check if the path is absolute
 *
 * @param path Path to check
 * @return true if path is absolute (Windows: drive letter or UNC; Unix: starts with /)
 */
bool IsPathAbsolute(std::string_view path);

/**
 * @brief Join two path components with the appropriate separator
 *
 * Automatically handles:
 * - Trailing separators in base path
 * - Leading separators in component
 * - Platform-specific separator
 *
 * @param base Base path (e.g., "C:\\Users" or "/Users")
 * @param component Component to append (e.g., "Desktop" or "Documents")
 * @return Joined path
 */
std::string JoinPath(std::string_view base, std::string_view component);

/**
 * @brief Join multiple path components
 *
 * @param components Path components to join
 * @return Joined path
 */
std::string JoinPath(const std::vector<std::string>& components);

// ============================================================================
// Path Utilities (existing)
// ============================================================================

// Text width calculator function type
using TextWidthCalculator = std::function<float(std::string_view)>;

// Returns truncated string like "...\\Documents\\file.txt" or ".../Documents/file.txt"
std::string TruncatePathAtBeginning(std::string_view path, float max_width,
                                    const TextWidthCalculator& text_width_calc);

/**
 * @brief Get the filename from a path
 *
 * @param path Path to extract filename from
 * @return Filename component (e.g., "file.txt" from "/path/to/file.txt")
 */
/**
 * @brief Remove trailing path separators (both '/' and '\\') from a path
 *
 * Returns a zero-copy view into the original string with trailing separators
 * stripped. Used to normalise paths before storage or comparison.
 *
 * @param path Path to trim
 * @return View of path without trailing separators
 */
inline std::string_view TrimTrailingSeparators(std::string_view path) {
  size_t n = path.size();
  while (n > 0 && (path[n - 1] == '/' || path[n - 1] == '\\')) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - bounds-checked by n > 0
    --n;
  }
  return path.substr(0, n);
}

inline std::string_view GetFilename(std::string_view path) {
  // Find last separator
  // Note: We check for both separators to handle mixed paths correctly on Windows
  // and handle potential Windows paths on non-Windows systems (e.g. tests)
  if (const size_t last_slash = path.find_last_of("/\\"); last_slash != std::string_view::npos) {
    return path.substr(last_slash + 1);
  }
  return path;
}

/**
 * @brief Get the extension from a path
 *
 * @param path Path to extract extension from
 * @return Extension component without dot (e.g., "txt" from "/path/to/file.txt"), or empty if none
 */
inline std::string_view GetExtension(std::string_view path) {
  const std::string_view filename = GetFilename(path);

  // Find last dot
  if (const size_t last_dot = filename.find_last_of('.');
      last_dot != std::string_view::npos && last_dot + 1 < filename.length()) {
    // Return extension without the dot
    return filename.substr(last_dot + 1);
  }

  return {};
}

// ============================================================================
// Filename / Extension Extraction
// ============================================================================

/**
 * @brief Extract filename stem and extension from a pre-computed path
 *
 * @param path         Null-terminated path string (e.g. "C:\\Users\\file.txt")
 * @param filename_start_offset  Offset where the filename begins (after last separator)
 * @param extension_start_offset Offset of the first extension character (after the dot),
 *                               or SIZE_MAX when there is no extension
 * @param out_filename  Receives the filename stem (no dot, no extension)
 * @param out_extension Receives the extension (no dot), or is cleared when absent
 */
inline void ExtractFilenameAndExtension(const char* path,
                                        size_t filename_start_offset,
                                        size_t extension_start_offset,
                                        std::string& out_filename,
                                        std::string& out_extension) {
  const char* filename_start = path + filename_start_offset;
  if (extension_start_offset != SIZE_MAX) {
    const size_t filename_len = extension_start_offset - filename_start_offset - 1;
    out_filename.assign(filename_start, filename_len);
    out_extension.assign(path + extension_start_offset);
  } else {
    out_filename.assign(filename_start);
    out_extension.clear();
  }
}

}  // namespace path_utils
