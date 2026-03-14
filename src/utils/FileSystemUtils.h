#pragma once

#include <array>  // NOLINT(clang-diagnostic-error) - System header, unavoidable on macOS (header-only analysis limitation)
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

#include "utils/FileAttributeConstants.h"
#include "utils/FileTimeTypes.h"
#include "utils/Logger.h"

#ifdef _WIN32
// must be after FileTimeTypes.h (which includes windows.h on Windows)
#include <propkey.h>  // For PKEY_Size
#include <shlwapi.h>  // For SHCreateItemFromParsingName  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem
#include <shobjidl.h> // For IShellItem2  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem
#elif defined(__APPLE__) || defined(__unix__)
// macOS/Unix: POSIX APIs for file attributes
#include <cerrno>      // For errno (error codes)
#include <ctime>       // For struct timespec, localtime()
#include <sys/stat.h>  // For stat()
#endif  // _WIN32

// Forward declaration - Utf8ToWide is in StringUtils.h
// FileSystemUtils.h depends on StringUtils.h for encoding conversion
#include "utils/StringUtils.h"

namespace file_system_utils_constants {
constexpr uint32_t kFiletimeLowWordMask = 0xFFFFFFFFU;
constexpr int64_t kFiletimeIntervalsPerSecond = 10000000LL;
constexpr int64_t kNanosecondsPer100ns = 100;
constexpr double kBytesPerKb = 1024.0;
}  // namespace file_system_utils_constants

// ============================================================================
// Time Conversion Helpers (macOS/Unix)
// ============================================================================

#if defined(__APPLE__) || defined(__unix__)
/**
 * @brief Converts struct timespec (Unix time) to FILETIME (Windows time format)
 *
 * Unix epoch: 1970-01-01 00:00:00 UTC
 * Windows epoch: 1601-01-01 00:00:00 UTC
 * Difference: 11644473600 seconds
 *
 * @param ts timespec structure containing seconds and nanoseconds since Unix epoch
 * @return FILETIME structure in Windows format (100-nanosecond intervals since Windows epoch)
 */
inline FILETIME TimespecToFileTime(const struct timespec &ts) {
  constexpr int64_t nanoseconds_per_100ns = 100;       // Conversion factor: nanoseconds to 100-nanosecond intervals
  constexpr int64_t filetime_per_second = 10000000LL;   // 100-nanosecond intervals per second
  int64_t total_100ns = (static_cast<int64_t>(ts.tv_sec) + file_time_constants::kEpochDiffSeconds) * filetime_per_second;  // NOSONAR(cpp:S1905) - Cast ensures portability (time_t size varies by platform)

  // Add nanoseconds (convert to 100-nanosecond intervals)
  // Note: We truncate to 100-ns precision (FILETIME doesn't support full nanosecond precision)
  total_100ns += ts.tv_nsec / nanoseconds_per_100ns;

  // Split into high and low parts (FILETIME structure uses two 32-bit values)
  const auto total_100ns_u = static_cast<uint64_t>(total_100ns);
  FILETIME ft;
  ft.dwLowDateTime = static_cast<uint32_t>(total_100ns_u & file_system_utils_constants::kFiletimeLowWordMask);
  ft.dwHighDateTime = static_cast<uint32_t>((total_100ns_u >> 32u) & file_system_utils_constants::kFiletimeLowWordMask);

  return ft;
}

/**
 * @brief Converts FILETIME (Windows time format) to struct timespec (Unix time)
 *
 * Reverse conversion of TimespecToFileTime().
 * Used for formatting FILETIME values on macOS/Unix.
 *
 * @param ft FILETIME structure in Windows format
 * @return timespec structure containing seconds and nanoseconds since Unix epoch
 */
inline struct timespec FileTimeToTimespec(const FILETIME &ft) {
  // Combine high and low parts into 64-bit value
  const uint64_t total_100ns = (static_cast<uint64_t>(ft.dwHighDateTime) << 32u) |  // NOSONAR(cpp:S1905) - Cast needed for 64-bit bit shift operation
                               static_cast<uint64_t>(ft.dwLowDateTime);  // NOSONAR(cpp:S1905) - Cast needed for 64-bit bit shift operation

  // Convert from 100-nanosecond intervals to seconds (explicit cast: result fits int64_t for valid FILETIME)
  const auto total_seconds = static_cast<int64_t>(total_100ns / file_system_utils_constants::kFiletimeIntervalsPerSecond);
  const int64_t unix_seconds = total_seconds - file_time_constants::kEpochDiffSeconds;

  // Extract remaining 100-ns intervals and convert to nanoseconds
  const uint64_t remaining_100ns = total_100ns % file_system_utils_constants::kFiletimeIntervalsPerSecond;
  auto nanoseconds = static_cast<int64_t>(remaining_100ns * file_system_utils_constants::kNanosecondsPer100ns);

  struct timespec ts = {};
  ts.tv_sec = unix_seconds;
  ts.tv_nsec = nanoseconds;

  return ts;
}
#endif  // defined(__APPLE__) || defined(__unix__)

// Combined structure for file attributes (size and modification time)
// Allows loading both in a single system call for better performance
struct FileAttributes {
  uint64_t fileSize;  // NOLINT(readability-identifier-naming) - public API uses camelCase
  FILETIME lastModificationTime;  // NOLINT(readability-identifier-naming)
  bool success;
};

// Combined function to get both file size and modification time in one system call.
// Contract: On success, result.success == true and result.fileSize is the logical size
// (0 only for true zero-byte files). On failure, result.success == false; consumers must
// not treat result.fileSize as valid (do not cache 0 as a successful size).
inline FileAttributes GetFileAttributes(std::string_view path) {
  // Initialize with failure sentinels - correct semantics for "not yet loaded" vs "failed"
  FileAttributes result = {kFileSizeFailed, kFileTimeFailed, false};

#ifdef _WIN32
  std::wstring w_path = Utf8ToWide(path);

  // 1. Try GetFileAttributesEx first (fast, no download)
  // This returns BOTH size and modification time in one call
  if (WIN32_FILE_ATTRIBUTE_DATA file_info; GetFileAttributesExW(w_path.c_str(), GetFileExInfoStandard, &file_info)) {
    // Extract size (even if 0 - 0 is a valid file size)
    ULARGE_INTEGER file_size;
    file_size.LowPart = file_info.nFileSizeLow;
    file_size.HighPart = file_info.nFileSizeHigh;
    result.fileSize = file_size.QuadPart;

    // Extract modification time
    result.lastModificationTime = file_info.ftLastWriteTime;
    result.success = true;
    return result;
  }

  // 2. Fallback: IShellItem2 for cloud files (slower, but robust)
  // This also allows us to get both properties in one call
  IShellItem2 *p_item = nullptr;
  HRESULT hr = SHCreateItemFromParsingName(w_path.c_str(), nullptr, IID_PPV_ARGS(&p_item));
  if (SUCCEEDED(hr)) {
    // Get size
    ULONGLONG size = 0;
    hr = p_item->GetUInt64(PKEY_Size, &size);
    if (SUCCEEDED(hr)) {
      result.fileSize = size;
    }

    // Get modification time
    FILETIME ft = {};
    hr = p_item->GetFileTime(PKEY_DateModified, &ft);
    if (SUCCEEDED(hr)) {
      result.lastModificationTime = ft;
      result.success = true;
    }

    p_item->Release();
  }
#elif defined(__APPLE__) || defined(__unix__)
  // macOS/Unix implementation using stat()
  // stat() returns both file size and modification time in a single system call
  struct stat file_stat = {};

  // Convert path to C string (stat requires null-terminated string)
  std::string path_str(path);

  // Path conversion: Handle Windows paths from index files
  // Windows paths (e.g., "C:\Users\...") need to be converted to Unix paths (e.g., "/Users/...")
  // This is useful when loading index files generated on Windows
  if (path_str.length() >= 3 && path_str[1] == ':' && (path_str[2] == '\\' || path_str[2] == '/')) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - indices 1 and 2 are guarded by length() >= 3 check
    // Windows path detected (e.g., "C:\Users\..." or "C:/Users/...")
    LOG_INFO_BUILD("[GetFileAttributes] Windows path detected: " + path_str);

    // Convert drive letter to Unix path
    // C: -> /, other drives -> /Volumes/DriveName
    auto drive_letter = static_cast<char>(std::toupper(static_cast<unsigned char>(path_str[0])));  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - index 0 is guarded by length() >= 3 check above
    std::string unix_path;

    if (drive_letter == 'C') {
      // C: drive maps to root
      unix_path = "/";
    } else {
      // Other drives map to /Volumes/DriveName
      unix_path = "/Volumes/";
      unix_path += drive_letter;
    }

    // Convert backslashes to forward slashes and append rest of path
    for (size_t i = 3; i < path_str.length(); ++i) {
      if (path_str[i] == '\\') {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - loop-guarded: i < path_str.length()
        unix_path += '/';
      } else {
        unix_path += path_str[i];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - loop-guarded: i < path_str.length()
      }
    }

    path_str = unix_path;
    LOG_INFO_BUILD("[GetFileAttributes] Path converted to: " + path_str);
  }

  if (const int stat_result = stat(path_str.c_str(), &file_stat); stat_result == 0) {
    // Success: extract file size
    result.fileSize = static_cast<uint64_t>(file_stat.st_size);

    // Extract modification time and convert to FILETIME
    // Platform-specific: macOS uses st_mtimespec, Linux uses st_mtim
    struct timespec mtime_ts = {};
#ifdef __APPLE__
    // macOS: use st_mtimespec (struct timespec with nanoseconds)
    mtime_ts = file_stat.st_mtimespec;
#elif defined(__linux__)
    // Linux: use st_mtim (struct timespec with nanoseconds)
    mtime_ts = file_stat.st_mtim;
#else
    // Other Unix: fallback to st_mtime (seconds only, nanoseconds = 0)
    mtime_ts.tv_sec = file_stat.st_mtime;
    mtime_ts.tv_nsec = 0;
#endif  // __APPLE__

    // Convert to FILETIME format
    result.lastModificationTime = TimespecToFileTime(mtime_ts);
    result.success = true;
  } else {
    // Failure: file doesn't exist, permission denied, etc.
    LOG_ERROR_BUILD("[GetFileAttributes] stat() FAILED for: " + path_str + " (errno=" + std::to_string(errno) + ")");
    // Return sentinel values to indicate failure (already initialized, but be explicit)
    result.fileSize = kFileSizeFailed;
    result.lastModificationTime = kFileTimeFailed;
    result.success = false;
  }
#else
  // Other platforms: stub (for tests or unsupported platforms)
  (void)path;  // Suppress unused parameter warning
#endif  // _WIN32

  return result;
}

// Helper function to get file size
// Uses GetFileAttributesExW first, then falls back to IShellItem2
// OPTIMIZED: Now uses combined GetFileAttributes() internally for consistency
inline uint64_t GetFileSize(std::string_view path) {
  const FileAttributes attrs = GetFileAttributes(path);
  return attrs.fileSize;
}

// Helper function to get file modification time
// Uses GetFileAttributesExW first, then falls back to IShellItem2
// OPTIMIZED: Now uses combined GetFileAttributes() internally for consistency
inline FILETIME GetFileModificationTime(std::string_view path) {
  const FileAttributes attrs = GetFileAttributes(path);
  return attrs.success ? attrs.lastModificationTime : kFileTimeFailed;
}

#ifdef _WIN32
/**
 * @brief Check if a file is likely a cloud file (requires IShellItem2)
 *
 * Cloud files (OneDrive, SharePoint) often fail GetFileAttributesExW and require
 * IShellItem2 which is slow. This function quickly checks if GetFileAttributesExW
 * would fail, indicating a potential cloud file.
 *
 * @param path File path to check
 * @return True if file is likely a cloud file (GetFileAttributesExW fails)
 */
inline bool IsLikelyCloudFile(std::string_view path) {
  std::wstring w_path = Utf8ToWide(path);
  WIN32_FILE_ATTRIBUTE_DATA file_info;
  // If GetFileAttributesExW fails, it's likely a cloud file
  return !GetFileAttributesExW(w_path.c_str(), GetFileExInfoStandard, &file_info);
}

/**
 * @brief Check if a file path or filename contains "OneDrive" (case-insensitive)
 *
 * OneDrive files often have incomplete metadata in the MFT and should be handled
 * via lazy loading instead. This function provides a simple heuristic to detect
 * OneDrive files by checking if the path/filename contains "OneDrive".
 *
 * PERFORMANCE: Uses case-insensitive search without string allocation to avoid
 * performance penalty during initial index population (100k-500k files).
 * Optimized to avoid ToLower() allocation overhead by doing character-by-character
 * comparison inline.
 *
 * @param path_or_filename File path or filename to check
 * @return True if path/filename contains "OneDrive" (case-insensitive)
 */
inline bool IsOneDriveFile(std::string_view path_or_filename) {
  // Case-insensitive search for "onedrive" without allocating a new string
  // Use simple character-by-character comparison to avoid ToLower() allocation overhead
  constexpr std::string_view onedrive_lower = "onedrive";
  constexpr size_t onedrive_len = onedrive_lower.size();

  if (path_or_filename.size() < onedrive_len) {
    return false;
  }

  // Slide a window through the string, comparing case-insensitively
  // Early exit optimization: only check if we have enough characters remaining
  for (size_t i = 0; i <= path_or_filename.size() - onedrive_len; ++i) {
    bool matches = true;
    for (size_t j = 0; j < onedrive_len; ++j) {
      const auto c1 = static_cast<unsigned char>(path_or_filename[i + j]);
      const auto c2 = static_cast<unsigned char>(onedrive_lower[j]);
      if (std::tolower(c1) != c2) {
        matches = false;
        break;
      }
    }
    if (matches) {
      return true;
    }
  }

  return false;
}
#else
// Non-Windows: stat() works for all files, no cloud file detection needed
inline bool IsLikelyCloudFile(std::string_view /*path*/) {
  return false;
}

// Non-Windows: OneDrive is Windows-specific
inline bool IsOneDriveFile(std::string_view /*path_or_filename*/) {
  return false;
}
#endif  // _WIN32

namespace file_system_utils_detail {
// Resize buffer after snprintf: use written length if valid, else trim trailing nul (truncation).
inline void ResizeBufferFromSnprintf(std::string& buffer, int written) {
  if (written > 0 && static_cast<size_t>(written) < buffer.size()) {
    buffer.resize(static_cast<size_t>(written));
  } else if (written >= 0) {
    buffer.resize(buffer.size() - 1);  // Truncation occurred
  }
}
}  // namespace file_system_utils_detail

// Helper function to format file size as human-readable string
inline std::string FormatFileSize(uint64_t bytes) {
  constexpr double kBytesPerKb = file_system_utils_constants::kBytesPerKb;
  if (bytes == 0) {
    return "0 B";
  }
  if (bytes < 1024ULL) {
    return std::to_string(bytes) + " B";
  }
  if (bytes < 1024ULL * 1024) {
    const double kb = static_cast<double>(bytes) / kBytesPerKb;
    std::string buffer(32, '\0');
    const int written = std::snprintf(buffer.data(), buffer.size(), "%.1f KB", kb);
    file_system_utils_detail::ResizeBufferFromSnprintf(buffer, written);
    return buffer;
  }
  if (bytes < 1024ULL * 1024 * 1024) {
    const double mb = static_cast<double>(bytes) / (kBytesPerKb * kBytesPerKb);
    std::string buffer(32, '\0');
    const int written = std::snprintf(buffer.data(), buffer.size(), "%.1f MB", mb);
    file_system_utils_detail::ResizeBufferFromSnprintf(buffer, written);
    return buffer;
  }
  const double gb = static_cast<double>(bytes) / (kBytesPerKb * kBytesPerKb * kBytesPerKb);
  std::string buffer(32, '\0');
  const int written = std::snprintf(buffer.data(), buffer.size(), "%.2f GB", gb);
  file_system_utils_detail::ResizeBufferFromSnprintf(buffer, written);
  return buffer;
}

// Helper function to format FILETIME as human-readable string
// Format: "YYYY-MM-DD HH:MM"
inline std::string FormatFileTime(const FILETIME &ft) {  // NOSONAR(cpp:S5350) - Function has no pointer parameters; pointer-to-const rule is not applicable here
  // Check for sentinel value
  if (IsSentinelTime(ft)) {
    return "...";  // Loading indicator
  }

  // Check for failed value
  if (IsFailedTime(ft)) {
    return "N/A";  // Error indicator
  }

#ifdef _WIN32
  SYSTEMTIME st;

  // Convert to local time
  if (FILETIME localFt; !FileTimeToLocalFileTime(&ft, &localFt) ||
      !FileTimeToSystemTime(&localFt, &st)) {
    return "";  // Invalid time
  }

  // Format as "YYYY-MM-DD HH:MM"
  std::array<char, 64> buffer{};
  if (const int n = std::snprintf(buffer.data(), buffer.size(), "%04d-%02d-%02d %02d:%02d",
                                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
      n < 0 || static_cast<size_t>(n) >= buffer.size()) {
    return "";
  }
  return std::string(buffer.data());
#elif defined(__APPLE__) || defined(__unix__)
  // macOS/Unix implementation
  // Convert FILETIME to Unix timestamp
  const struct timespec ts = FileTimeToTimespec(ft);

  // Convert to local time (thread-safe)
  struct tm timeinfo_buf = {};
  const struct tm *timeinfo = nullptr;  // Safe default, assigned from localtime_r() below
  const time_t time_sec = ts.tv_sec;
  timeinfo = localtime_r(&time_sec, &timeinfo_buf);
  if (timeinfo == nullptr) {
    return "";  // Invalid time
  }

  // Format as "YYYY-MM-DD HH:MM"
  // Note: tm_year is years since 1900, tm_mon is 0-11
  std::string buffer(64, '\0');
  if (const int n = std::snprintf(buffer.data(), buffer.size(), "%04d-%02d-%02d %02d:%02d",
                                 timeinfo->tm_year + 1900,  // Add 1900 to get actual year
                                 timeinfo->tm_mon + 1,      // Add 1 (tm_mon is 0-11)
                                 timeinfo->tm_mday,
                                 timeinfo->tm_hour,
                                 timeinfo->tm_min);
      n < 0 || static_cast<size_t>(n) >= buffer.size()) {
    return "";
  }
  return buffer;
#else
  // Other platforms: stub (for tests or unsupported platforms)
  (void)ft;  // Suppress unused parameter warning
  return "";
#endif  // _WIN32
}
