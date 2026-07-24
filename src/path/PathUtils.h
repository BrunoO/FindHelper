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
 * Non-root parents omit the trailing separator (e.g. "/a/b" from "/a/b/c").
 * Volume-root children keep the root separator: "C:\\file.txt" → "C:\\", "/file" → "/".
 * Volume roots themselves have no parent: "C:\\" and "/" → empty (so parent walks terminate).
 * ("C:" alone is the Windows current-directory-on-drive form, not the volume root.)
 *
 * @param path Path (file or directory)
 * @return Parent directory path, or empty if path has no parent
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

/**
 * @brief True when path is exactly a volume root ("/", "\\", or "C:\\" / "C:/")
 */
[[nodiscard]] inline bool IsVolumeRootPath(std::string_view path) noexcept {
  if (path.size() == 1U && (path[0] == '/' || path[0] == '\\')) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - size == 1
    return true;
  }
  return path.size() == 3U && path[1] == ':' &&  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - size == 3
         (path[2] == '\\' || path[2] == '/');  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - size == 3
}

/**
 * @brief True when path is a Windows drive-letter index key ("C:", "D:") without separator
 *
 * DirectoryResolver and InsertPathUnderLock use this form as the parent of drive-root
 * files. Rename/Move UpdatePrefix must not treat it as a normal directory: old_prefix
 * would become "C:\\" and rewrite every path on the volume.
 */
[[nodiscard]] inline bool IsDriveLetterRootKey(std::string_view path) noexcept {
  return path.size() == 2U && path[1] == ':' &&  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - size == 2
         ((path[0] >= 'A' && path[0] <= 'Z') ||  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - size == 2
          (path[0] >= 'a' && path[0] <= 'z'));  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - size == 2
}

/**
 * @brief True when path is a single component directly under the volume root
 *
 * Examples: "C:\\file.txt", "/tmpfile". Used to refuse soft-delete cascade eviction
 * of drive-root entries even if parentID was corrupted away from MFT record 5.
 */
[[nodiscard]] inline bool IsVolumeRootChildPath(std::string_view path) noexcept {
  if (path.size() >= 3U && path[1] == ':' &&  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - size >= 3
      (path[2] == '\\' || path[2] == '/')) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - size >= 3
    if (path.size() == 3U) {
      return false;
    }
    for (size_t i = 3U; i < path.size(); ++i) {
      if (path[i] == '\\' || path[i] == '/') {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - i < size
        return false;
      }
    }
    return true;
  }
  if (!path.empty() && (path[0] == '/' || path[0] == '\\')) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - !empty
    if (path.size() == 1U) {
      return false;
    }
    for (size_t i = 1U; i < path.size(); ++i) {
      if (path[i] == '\\' || path[i] == '/') {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - i < size
        return false;
      }
    }
    return true;
  }
  return false;
}

/**
 * @brief True when an UpdatePrefix oldPrefix would rewrite an entire volume
 *
 * "C:\\" / "/" as oldPrefix matches every indexed path under that root.
 */
[[nodiscard]] inline bool IsVolumeWidePathPrefix(std::string_view prefix) noexcept {
  return IsVolumeRootPath(prefix);
}

/**
 * @brief Length of the directory prefix ending at last_separator
 *
 * By default the separator itself is excluded. Volume roots keep it so the
 * result is a valid root path ("/" or "C:\\"), not an empty string or "C:".
 *
 * @param path Full path (must be at least last_separator + 1 long)
 * @param last_separator Index of the separator immediately before the filename
 * @return Prefix length to use with path.substr(0, length)
 */
[[nodiscard]] inline size_t DirectoryPrefixLength(std::string_view path,
                                                  size_t last_separator) {
  // Keep separator when the directory portion is a volume root
  if (IsVolumeRootPath(path.substr(0, last_separator + 1U))) {
    return last_separator + 1U;
  }
  return last_separator;
}

/**
 * @brief Directory portion of a path given a precomputed filename offset
 *
 * Zero-copy view into full_path. Preserves volume-root separators so drive-root
 * files display as "C:\\" (not "C:" or empty) and Unix root files as "/".
 *
 * @param full_path Full file path
 * @param filename_offset Index of the first filename character (after last separator),
 *                        or 0 when the path has no directory separator
 * @return Directory path view, or empty if there is no directory component
 */
[[nodiscard]] inline std::string_view GetDirectoryPathView(std::string_view full_path,
                                                           size_t filename_offset) {
  if (filename_offset == 0U || filename_offset > full_path.size()) {
    return {};
  }
  const size_t last_separator = filename_offset - 1U;
  return full_path.substr(0, DirectoryPrefixLength(full_path, last_separator));
}

/**
 * @brief Directory portion of a path (finds the last separator)
 *
 * @param full_path Full file or directory path
 * @return Directory path view, or empty if there is no directory component
 */
[[nodiscard]] inline std::string_view GetDirectoryPathView(std::string_view full_path) {
  const size_t last_separator = full_path.find_last_of("/\\");
  if (last_separator == std::string_view::npos) {
    return {};
  }
  return GetDirectoryPathView(full_path, last_separator + 1U);
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
