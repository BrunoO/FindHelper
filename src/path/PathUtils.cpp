/**
 * @file PathUtils.cpp
 * @brief Implementation of cross-platform path manipulation utilities
 *
 * This file implements path utility functions that work on both Windows and
 * macOS/Linux. It provides platform-specific implementations for common
 * path operations like getting user directories, normalizing paths, and
 * handling path separators.
 *
 * FUNCTIONALITY:
 * - Path separator handling: Windows (\) vs Unix (/)
 * - User directory access: Home, Desktop, Downloads, Trash
 * - Volume root paths: Default drive/root directory
 * - Path normalization: Consistent separators and formatting
 * - Cross-platform compatibility: Stubs for non-Windows platforms
 *
 * PLATFORM-SPECIFIC:
 * - Windows: Uses SHGetKnownFolderPath API for user directories
 * - macOS/Linux: Uses environment variables and standard paths
 * - All functions return UTF-8 strings for consistency
 *
 * USAGE:
 * - Used throughout the application for file path operations
 * - Critical for cross-platform compatibility
 * - Provides consistent path format regardless of platform
 *
 * @see PathUtils.h for function declarations
 * @see PathConstants.h for path separator constants
 * @see FileOperations.cpp for path usage examples
 */

#include "path/PathUtils.h"

#include <array>
#include <cstdlib>
#include <cstring>  // For strlen, memcpy
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX  // Prevent min/max macro conflicts with Windows.h
#endif            // NOMINMAX
#include <windows.h>  // NOSONAR(cpp:S3806) - Must be first: provides types/macros for Shell headers
#include <knownfolders.h>  // NOSONAR(cpp:S3806) - After windows.h (FOLDERID_*, SHGetKnownFolderPath)
#include <shlobj_core.h>  // NOSONAR(cpp:S3806) - After windows.h (Shell API)
#endif  // _WIN32

#ifdef __APPLE__
#include <mach-o/dyld.h>  // _NSGetExecutablePath
#endif  // __APPLE__

#ifdef __linux__
#include <unistd.h>  // readlink
#endif  // __linux__

#include "utils/StringUtils.h"  // For WideToUtf8 (cross-platform, has stubs)

namespace path_utils {

// ============================================================================
// Path Separators (defined in header as inline constexpr)
// ============================================================================

// ============================================================================
// Root Path Constants
// ============================================================================

#ifdef _WIN32
static std::string g_defaultVolumeRootPath = "C:\\";
#endif  // _WIN32

std::string GetDefaultVolumeRootPath() {
#ifdef _WIN32
  return g_defaultVolumeRootPath;
#else
  return "/";
#endif  // _WIN32
}

std::string_view GetDefaultVolumeRootPathView() {
#ifdef _WIN32
  return g_defaultVolumeRootPath;
#else
  return "/";
#endif  // _WIN32
}

std::string GetDefaultUserRootPath() {
#ifdef _WIN32
  return JoinPath(GetDefaultVolumeRootPath(), "Users") + kPathSeparatorStr;
#else
  // Try to detect system convention
  // Most Unix systems use /home/, macOS uses /Users/
  // We'll default to /Users/ for macOS compatibility
  return "/Users/";
#endif  // _WIN32
}

// ============================================================================
// User Home Directory
// ============================================================================

std::string GetUserHomePath() {
#ifdef _WIN32
  std::array<char, 32767> user_profile = {0};
  if (DWORD len = GetEnvironmentVariableA("USERPROFILE", user_profile.data(),
                                          static_cast<DWORD>(user_profile.size()));
      len != 0 && len < user_profile.size()) {
    return std::string(user_profile.data());
  }
  return GetDefaultUserRootPath();
#else
  // macOS/Linux: Use HOME environment variable
  // NOLINTNEXTLINE(concurrency-mt-unsafe) - getenv one-time init
  if (const char* home = std::getenv("HOME"); home != nullptr) {
    return {home};
  }
  return GetDefaultUserRootPath();
#endif  // _WIN32
}

// Helper to get path relative to user home directory (eliminates duplicate code)
// Returns empty string if user home is not available (caller should handle fallback)
static std::string GetUserHomeRelativePath(std::string_view relative_path) {
  if (const std::string user_home = GetUserHomePath(); user_home != GetDefaultUserRootPath()) {
    return JoinPath(user_home, relative_path);
  }
  return {};  // Empty string indicates user home not available
}

#ifdef _WIN32
// Helper to get path from Windows Known Folder API (eliminates duplicate code)
// Returns empty string if API call fails or path is empty (caller should handle fallback)
static std::string GetKnownFolderPath(REFKNOWNFOLDERID folder_id) {
  PWSTR wide_path = nullptr;
  if (HRESULT hr = SHGetKnownFolderPath(folder_id, 0, nullptr, &wide_path);
      SUCCEEDED(hr) && wide_path) {
    // Convert wide string to UTF-8
    std::string path = WideToUtf8(std::wstring(wide_path));
    CoTaskMemFree(wide_path);
    if (!path.empty()) {
      return path;
    }
  }
  return std::string();  // Empty string indicates API call failed or path is empty
}
#endif  // _WIN32

// ============================================================================
// Special Folders
// ============================================================================

std::string GetDesktopPath() {
#ifdef _WIN32
  // Try Windows Known Folder API first
  if (std::string path = GetKnownFolderPath(FOLDERID_Desktop); !path.empty()) {
    return path;
  }

  // Fallback: Use USERPROFILE\Desktop
  if (std::string desktop_path = GetUserHomeRelativePath("Desktop"); !desktop_path.empty()) {
    return desktop_path;
  }

  // Final fallback: Use default user root
  return JoinPath(GetDefaultUserRootPath(), "Public\\Desktop");
#else
  // macOS/Linux: Use ~/Desktop
  if (std::string desktop_path = GetUserHomeRelativePath("Desktop"); !desktop_path.empty()) {
    return desktop_path;
  }
  return "/Users";  // Minimal fallback
#endif  // _WIN32
}

std::string GetDownloadsPath() {
#ifdef _WIN32
  // Try Windows Known Folder API first (handles localized folder names, e.g. "Téléchargements")
  if (std::string path = GetKnownFolderPath(FOLDERID_Downloads); !path.empty()) {
    return path;
  }
#endif  // _WIN32

  if (std::string downloads_path = GetUserHomeRelativePath("Downloads"); !downloads_path.empty()) {
    return downloads_path;
  }
  return GetDefaultUserRootPath();
}

std::string GetTrashPath() {
#ifdef _WIN32
  // Try Windows Known Folder API
  if (std::string path = GetKnownFolderPath(FOLDERID_RecycleBinFolder); !path.empty()) {
    return path;
  }
  return GetDefaultUserRootPath();  // Fallback
#else
  // macOS: Use ~/.Trash
  // Linux: Use ~/.local/share/Trash/files (but we'll use .Trash for simplicity)
  if (std::string trash_path = GetUserHomeRelativePath(".Trash"); !trash_path.empty()) {
    return trash_path;
  }
  return GetDefaultUserRootPath();
#endif  // _WIN32
}

// ============================================================================
// Executable and path resolution (for relative --index-from-file when CWD differs)
// ============================================================================

constexpr size_t kMaxExecutablePathLen = 4096U;

std::string GetExecutableDirectory() {
#ifdef _WIN32
  std::array<char, kMaxExecutablePathLen> module_path = {};
  if (DWORD result = GetModuleFileNameA(nullptr, module_path.data(),
                                        static_cast<DWORD>(module_path.size()));
      result > 0 && result < module_path.size()) {
    std::string path(module_path.data());
    if (const size_t last_slash = path.find_last_of("\\/"); last_slash != std::string::npos) {
      return path.substr(0, last_slash + 1);
    }
    return path;
  }
  return {};
#elif defined(__APPLE__)
  std::array<char, kMaxExecutablePathLen> path = {};
  if (auto size = static_cast<uint32_t>(path.size());
      _NSGetExecutablePath(path.data(), &size) != 0) {
    return {};
  }
  std::string exe_path(path.data());
  if (const size_t last_slash = exe_path.find_last_of('/'); last_slash != std::string::npos) {
    return exe_path.substr(0, last_slash + 1);
  }
  return exe_path;
#elif defined(__linux__)
  std::array<char, kMaxExecutablePathLen> path = {};
  const ssize_t count = readlink("/proc/self/exe", path.data(), path.size() - 1);
  if (count <= 0 || static_cast<size_t>(count) >= path.size()) {
    return {};
  }
  path[static_cast<size_t>(count)] = '\0';
  std::string exe_path(path.data());
  if (const size_t last_slash = exe_path.find_last_of('/'); last_slash != std::string::npos) {
    return exe_path.substr(0, last_slash + 1);
  }
  return exe_path;
#else
  return {};
#endif  // _WIN32
}

std::string GetParentDirectory(std::string_view path) {
  if (path.empty()) {
    return {};
  }
  const size_t last_slash = path.find_last_of("/\\");
  if (last_slash == std::string_view::npos) {
    return {};
  }
  if (last_slash == 0) {
    return std::string(path.substr(0, 1));
  }
  return std::string(path.substr(0, last_slash));
}

bool IsPathAbsolute(std::string_view path) {
#ifdef _WIN32
  if (path.size() >= 3U && path[1] == ':' &&
      (path[2] == '\\' || path[2] == '/')) {
    return true;
  }
  if (path.size() >= 2U && path[0] == '\\' && path[1] == '\\') {
    return true;
  }
  return false;
#else
  return !path.empty() && path[0] == '/';  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - guarded by !path.empty()
#endif  // _WIN32
}

// ============================================================================
// Path Joining
// ============================================================================

std::string JoinPath(std::string_view base, std::string_view component) {
  if (base.empty()) {
    return std::string(component);
  }
  if (component.empty()) {
    return std::string(base);
  }

  // Check if base ends with separator
  const bool base_has_sep =
    (!base.empty() && (base.back() == kPathSeparator || base.back() == '/' || base.back() == '\\'));

  // Check if component starts with separator
  const bool comp_has_sep =
    (!component.empty() && (component.front() == kPathSeparator || component.front() == '/' ||
                            component.front() == '\\'));

  const bool trim_leading = (base_has_sep && comp_has_sep);
  const bool add_sep = (!base_has_sep && !comp_has_sep);
  const std::string component_part = (add_sep ? kPathSeparatorStr : "") +
                                     std::string(trim_leading ? component.substr(1) : component);
  return std::string(base) + component_part;
}

std::string JoinPath(const std::vector<std::string>& components) {
  if (components.empty()) {
    return "";
  }
  if (components.size() == 1) {
    return components[0];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - guarded by size() == 1
  }

  std::string result = components[0];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - guarded by size() >= 2
  for (size_t i = 1; i < components.size(); ++i) {
    result = JoinPath(result, components[i]);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - i < components.size()
  }
  return result;
}

// ============================================================================
// Path Utilities (existing)
// ============================================================================

// Constants for root detection
namespace {
#ifdef _WIN32
// Windows path constants (used by DetectDriveLetterRoot / DetectUncPathRoot)
constexpr size_t kMinDriveLetterLength = 3;   // "C:\"
constexpr size_t kDriveLetterPrefixLength = 3;  // "C:\"
constexpr size_t kMinUncPathLength = 2;       // "\\"
constexpr size_t kUncPathStartOffset = 2;    // Position after "\\"
#endif  // _WIN32

// Unix path constants
constexpr size_t kUnixRootLength = 1;  // "/"  // NOSONAR(cpp:S1481) - Actually used on line 302
constexpr size_t kUnixFirstComponentStart =
  1;  // Position after "/"  // NOSONAR(cpp:S1481) - Actually used on line 306
}  // namespace

/**
 * @brief Detects Windows drive letter root prefix (e.g., "C:\")
 *
 * @param path Path to check
 * @return Pair of (root_prefix, root_length), or ({}, 0) if no drive letter found
 */
#ifdef _WIN32
static std::pair<std::string_view, size_t> DetectDriveLetterRoot(std::string_view path) {
  // Windows drive letters can use either backslash (C:\) or forward slash (C:/)
  if (path.length() >= kMinDriveLetterLength && path[1] == ':' &&
      (path[2] == '\\' || path[2] == '/')) {
    return {path.substr(0, kDriveLetterPrefixLength), kDriveLetterPrefixLength};
  }
  return {std::string_view{}, 0};
}

/**
 * @brief Detects Windows UNC path root prefix (e.g., "\\server\share\")
 *
 * @param path Path to check
 * @return Pair of (root_prefix, root_length), or ({}, 0) if no UNC path found
 */
static std::pair<std::string_view, size_t> DetectUncPathRoot(std::string_view path) {
  if (path.length() < kMinUncPathLength || path[0] != '\\' || path[1] != '\\') {
    return {std::string_view{}, 0};
  }

  size_t first_slash = path.find('\\', kUncPathStartOffset);
  if (first_slash == std::string_view::npos) {
    return {std::string_view{}, 0};
  }

  if (size_t second_slash = path.find('\\', first_slash + 1);
      second_slash != std::string_view::npos) {
    // UNC path with trailing backslash: \\server\share\ (backslash escaped)
    return {path.substr(0, second_slash + 1), second_slash + 1};
  }

  if (first_slash < path.length() - 1) {
    // UNC path without trailing backslash: \\server\share
    return {path.substr(0, first_slash + 1), first_slash + 1};
  }

  return {std::string_view{}, 0};
}
#endif  // _WIN32

/**
 * @brief Detects Unix root directory prefix (e.g., "/" or "/Users/")
 *
 * @param path Path to check
 * @return Pair of (root_prefix, root_length), or ({}, 0) if no root found
 */
#ifndef _WIN32
static std::pair<std::string_view, size_t> DetectUnixRoot(std::string_view path) {
  if (path.empty() || path[0] != '/') {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - guarded by path.empty()
    return {std::string_view{}, 0};
  }

  // Preserve root "/" and optionally first component (e.g., "/Users/")
  // This helps identify which volume/mount point the file is on
  std::string_view root_prefix = "/";
  size_t root_length = kUnixRootLength;

  // Optionally preserve first path component for better context
  // (e.g., "/Users/" or "/home/" or "/Volumes/")
  if (const size_t first_slash = path.find('/', kUnixFirstComponentStart);
      first_slash != std::string_view::npos) {
    root_prefix = path.substr(0, first_slash + 1);  // "/Users/"
    root_length = first_slash + 1;
  }

  return {root_prefix, root_length};
}
#endif  // _WIN32

/**
 * @brief Detects and extracts root prefix from a path
 *
 * Platform-specific root detection:
 * - Windows: Drive letters (C:\) and UNC paths (\\server\share\)
 * - Unix: Root directory (/) and first component (/Users/)
 *
 * @param path Path to analyze
 * @return Pair of (root_prefix, root_length), or ({}, 0) if no root found
 */
static std::pair<std::string_view, size_t> DetectRootPrefix(std::string_view path) {
#ifdef _WIN32
  // Try drive letter first (most common case)
  if (auto [prefix, length] = DetectDriveLetterRoot(path); length > 0) {
    return {prefix, length};
  }

  // Try UNC path
  if (auto [prefix, length] = DetectUncPathRoot(path); length > 0) {
    return {prefix, length};
  }
#else
  // Unix root detection
  if (auto [prefix, length] = DetectUnixRoot(path); length > 0) {
    return {prefix, length};
  }
#endif  // _WIN32

  return {std::string_view{}, 0};
}

/**
 * @brief Finds the optimal truncation point in a path using binary search
 *
 * Uses binary search to find the longest suffix that fits within available width.
 * Then attempts to break at a path separator for cleaner display.
 *
 * @param remaining_path Path to truncate (already has root removed)
 * @param available_width Maximum width available for the truncated path
 * @param text_width_calc Function to calculate text width
 * @return Starting position for truncation (0 = no truncation needed)
 */
static size_t FindTruncationPoint(
  std::string_view remaining_path, float available_width,
  const TextWidthCalculator& text_width_calc) {  // NOSONAR(cpp:S1238, cpp:S5213) - std::function
                                                  // provides type erasure for callbacks, template
                                                  // would break API
  // Binary search for the optimal starting position
  size_t left = 0;
  size_t right = remaining_path.length();
  size_t best_pos = right;

  while (left < right) {
    const size_t mid = left + ((right - left) / 2);
    const std::string_view suffix = remaining_path.substr(mid);
    const float suffix_width = text_width_calc(suffix);

    if (suffix_width <= available_width) {
      best_pos = mid;
      right = mid;
    } else {
      left = mid + 1;
    }
  }

  // Try to break at a path separator for cleaner display
  // Look for the first separator at or after best_pos
  constexpr const char* separators = "\\/";
  if (const size_t separator_pos = remaining_path.find_first_of(separators, best_pos);
      separator_pos != std::string_view::npos && separator_pos < remaining_path.length() - 1) {
    // Verify the path from separator still fits
    const std::string_view suffix = remaining_path.substr(separator_pos + 1);
    const float suffix_width = text_width_calc(suffix);
    if (suffix_width <= available_width) {
      best_pos = separator_pos + 1;
    }
  }

  return best_pos;
}

std::string TruncatePathAtBeginning(
  std::string_view path, float max_width,
  const TextWidthCalculator&
    text_width_calc) {  // NOSONAR(cpp:S1238, cpp:S5213) - std::function provides type erasure for
                        // callbacks, template would break API
  // If column is too narrow (or invalid), return ellipsis only
  if (max_width <= 0) {
    return "...";
  }

  const std::string_view ellipsis = "...";

  // Calculate text width using provided calculator
  // If it fits, return as-is
  if (const float full_width = text_width_calc(path); full_width <= max_width) {
    return std::string(path);
  }

  // Step 1: Calculate ellipsis width first
  const float ellipsis_width = text_width_calc(ellipsis);

  // If max_width is less than or equal to ellipsis width, return ellipsis only
  // (This check must come before root detection to match test expectations)
  if (max_width <= ellipsis_width) {
    return std::string(ellipsis);
  }

  // Step 2: Detect and extract root prefix (drive letter, UNC path, or root directory)
  auto [root_prefix, root_length] = DetectRootPrefix(path);

  // Step 3: Calculate available width for remaining path
  float available_width = max_width - ellipsis_width;

  // If root is detected, subtract its width from available width
  if (root_length > 0) {
    const float root_width = text_width_calc(root_prefix);
    available_width -= root_width;

    // If root + ellipsis doesn't fit, return ellipsis only (not root + ellipsis)
    // This matches the behavior when max_width <= ellipsis_width
    if (available_width <= 0) {
      return std::string(ellipsis);
    }
  } else {
    // No root detected (relative path) - use existing behavior
    // If even ellipsis doesn't fit, just return ellipsis
    if (available_width <= 0) {
      return std::string(ellipsis);
    }
  }

  // Step 4: Truncate remaining path (from root_length onwards)
  const std::string_view remaining_path = (root_length > 0) ? path.substr(root_length) : path;

  // Find optimal truncation point using binary search
  const size_t best_pos = FindTruncationPoint(remaining_path, available_width, text_width_calc);

  // If no suffix fits (best_pos == remaining_path.length()), return ellipsis only
  // This can happen when available_width is too small to fit even a single character
  if (best_pos >= remaining_path.length()) {
    return std::string(ellipsis);
  }

  // Step 5: Combine root + ellipsis + truncated remaining path
  std::string result;
  if (root_length > 0) {
    // Preserve the original root prefix (including its separator: \ or /)
    result.reserve(root_length + ellipsis.length() + remaining_path.length() - best_pos);
    result = std::string(root_prefix);
    result.append(ellipsis);
    result.append(remaining_path.substr(best_pos));
  } else {
    // Relative path - use existing behavior
    result.reserve(ellipsis.length() + remaining_path.length() - best_pos);
    result = std::string(ellipsis);
    result.append(remaining_path.substr(best_pos));
  }

  // Verify the result actually fits within max_width
  // If it doesn't (due to rounding errors or very small available_width),
  // return ellipsis only to match test expectations
  constexpr float kWidthTolerance =
    0.1F;
  if (const float result_width = text_width_calc(result);
      result_width > max_width + kWidthTolerance) {
    // Result doesn't fit - return ellipsis only
    return std::string(ellipsis);
  }

  return result;
}

void SetDefaultVolumeRootPath(std::string_view root) {
#ifdef _WIN32
  g_defaultVolumeRootPath = std::string(root);
#else
  (void)root;  // No-op on other platforms
#endif  // _WIN32
}

}  // namespace path_utils
