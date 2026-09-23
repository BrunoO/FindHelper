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
  const auto nanoseconds = static_cast<int64_t>(remaining_100ns * file_system_utils_constants::kNanosecondsPer100ns);

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
  bool success;  // NOLINT(readability-identifier-naming) - public API uses camelCase
};

// Combined function to get both file size and modification time in one system call.
// Contract: On success, result.success == true and result.fileSize is the logical size
// (0 only for true zero-byte files). On failure, result.success == false; consumers must
// not treat result.fileSize as valid (do not cache 0 as a successful size).
FileAttributes GetFileAttributes(const char* path);
FileAttributes GetFileAttributes(std::string_view path);

// Log a failed stat() with the right level: ENOENT is expected churn during index
// passes (transient files created/deleted by other processes, e.g. build artifacts) —
// release-silent; other errnos (EACCES, EIO, ...) stay visible at ERROR level.
inline void LogStatFailure(const char* target_path, int stat_errno) {
  if (stat_errno == ENOENT) {
    LOG_DEBUG_BUILD("[GetFileAttributes] stat() not found (transient or deleted): " << target_path);
  } else {
    LOG_ERROR_BUILD("[GetFileAttributes] stat() FAILED for: " << target_path << " (errno=" << stat_errno << ")");
  }
}

#if defined(__APPLE__) || defined(__unix__)
inline FileAttributes GetFileAttributesFromCStr(const char* path) {
  FileAttributes result = {kFileSizeFailed, kFileTimeFailed, false};
  if (path == nullptr || path[0] == '\0') {
    return result;
  }

  struct stat file_stat = {};
  const char* target_path = path;
  std::string unix_path;

  // Path conversion: Handle Windows paths from index files
  // Windows paths (e.g., "C:\Users\...") need to be converted to Unix paths (e.g., "/Users/...")
  // This is useful when loading index files generated on Windows
  // Windows path detection: drive letter conversion needs the letter on both branches.
  if (const auto drive_letter = static_cast<char>(std::toupper(static_cast<unsigned char>(path[0])));
      path[1] == ':' && (path[2] == '\\' || path[2] == '/')) {
    LOG_INFO_BUILD("[GetFileAttributes] Windows path detected: " << path);

    if (drive_letter == 'C') {
      unix_path = "/";
    } else {
      unix_path = "/Volumes/";
      unix_path += drive_letter;
    }

    for (size_t i = 3; path[i] != '\0'; ++i) {
      if (path[i] == '\\') {
        unix_path += '/';
      } else {
        unix_path += path[i];
      }
    }

    target_path = unix_path.c_str();
    LOG_INFO_BUILD("[GetFileAttributes] Path converted to: " << target_path);
  }

  if (const int stat_result = stat(target_path, &file_stat); stat_result == 0) {
    result.fileSize = static_cast<uint64_t>(file_stat.st_size);

    struct timespec mtime_ts = {};
#ifdef __APPLE__
    mtime_ts = file_stat.st_mtimespec;
#elif defined(__linux__)
    mtime_ts = file_stat.st_mtim;
#else
    mtime_ts.tv_sec = file_stat.st_mtime;
    mtime_ts.tv_nsec = 0;
#endif  // __APPLE__

    result.lastModificationTime = TimespecToFileTime(mtime_ts);
    result.success = true;
  } else {
    // Capture errno immediately: later calls (logging) may clobber it.
    const int stat_errno = errno;
    LogStatFailure(target_path, stat_errno);
    result.fileSize = kFileSizeFailed;
    result.lastModificationTime = kFileTimeFailed;
    result.success = false;
  }

  return result;
}
#endif  // defined(__APPLE__) || defined(__unix__)

inline FileAttributes GetFileAttributes(const char* path) {
#ifdef _WIN32
  return GetFileAttributes(std::string_view(path != nullptr ? path : ""));
#elif defined(__APPLE__) || defined(__unix__)
  return GetFileAttributesFromCStr(path);
#else
  (void)path;
  return {kFileSizeFailed, kFileTimeFailed, false};
#endif  // _WIN32
}

inline FileAttributes GetFileAttributes(std::string_view path) {
  // Initialize with failure sentinels - correct semantics for "not yet loaded" vs "failed"
  FileAttributes result = {kFileSizeFailed, kFileTimeFailed, false};

#ifdef _WIN32
  const std::string_view path_sanitized = TruncateAtEmbeddedNull(path);
  std::wstring w_path = Utf8ToWide(path_sanitized);
  if (w_path.empty()) {
    return result;
  }
  for (wchar_t& ch : w_path) {
    if (ch == L'/') {
      ch = L'\\';
    }
  }

  auto try_get_attributes = [&result](const std::wstring& wpath) {
    WIN32_FILE_ATTRIBUTE_DATA file_info{};
    if (GetFileAttributesExW(wpath.c_str(), GetFileExInfoStandard, &file_info) == 0) {
      return false;
    }
    ULARGE_INTEGER file_size;
    file_size.LowPart = file_info.nFileSizeLow;
    file_size.HighPart = file_info.nFileSizeHigh;
    result.fileSize = file_size.QuadPart;
    result.lastModificationTime = file_info.ftLastWriteTime;
    result.success = true;
    return true;
  };

  // 1. Try GetFileAttributesEx first (fast, no download)
  if (try_get_attributes(w_path)) {
    return result;
  }

  // Extended-length prefix for deep paths (and some UTF-8 path edge cases on older configs).
  if (w_path.size() >= 2 && w_path[1] == L':' &&
      (w_path.size() < 4 || w_path[0] != L'\\' || w_path[1] != L'\\' || w_path[2] != L'?' ||
       w_path[3] != L'\\')) {
    std::wstring extended = LR"(\\?\)";
    extended.append(w_path);
    if (try_get_attributes(extended)) {
      return result;
    }
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
  const std::string path_str(path);
  return GetFileAttributesFromCStr(path_str.c_str());
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

inline uint64_t GetFileSize(const char* path) {
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

inline FILETIME GetFileModificationTime(const char* path) {
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
inline std::string FormatSnprintfResult(const std::array<char, 32>& buf, int written) {
  if (written > 0 && static_cast<size_t>(written) < buf.size()) {
    return {buf.data(), static_cast<size_t>(written)};
  }
  if (written >= 0) {
    return {buf.data(), buf.size() - 1U};
  }
  return {};
}

inline std::string SnprintfKbString(double value) {
  std::array<char, 32> buf{};
  const int written = std::snprintf(buf.data(), buf.size(), "%.1f KB", value);
  return FormatSnprintfResult(buf, written);
}

inline std::string SnprintfMbString(double value) {
  std::array<char, 32> buf{};
  const int written = std::snprintf(buf.data(), buf.size(), "%.1f MB", value);
  return FormatSnprintfResult(buf, written);
}

inline std::string SnprintfGbString(double value) {
  std::array<char, 32> buf{};
  const int written = std::snprintf(buf.data(), buf.size(), "%.2f GB", value);
  return FormatSnprintfResult(buf, written);
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
    return file_system_utils_detail::SnprintfKbString(kb);
  }
  if (bytes < 1024ULL * 1024 * 1024) {
    const double mb = static_cast<double>(bytes) / (kBytesPerKb * kBytesPerKb);
    return file_system_utils_detail::SnprintfMbString(mb);
  }
  const double gb = static_cast<double>(bytes) / (kBytesPerKb * kBytesPerKb * kBytesPerKb);
  return file_system_utils_detail::SnprintfGbString(gb);
}

// Format a file count as a locale-independent comma-separated integer (e.g., 1200000 → "1,200,000").
inline std::string FormatFileCount(uint64_t count) {
  const std::string digits = std::to_string(count);
  std::string formatted;
  formatted.reserve(digits.size() + ((digits.size() - 1U) / 3U));
  const size_t first_group = (digits.size() % 3U == 0U) ? 3U : (digits.size() % 3U);
  formatted.append(digits.substr(0U, first_group));
  for (size_t i = first_group; i < digits.size(); i += 3U) {
    formatted.push_back(',');
    formatted.append(digits.substr(i, 3U));
  }
  return formatted;
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
  const int n = std::snprintf(buffer.data(), buffer.size(), "%04d-%02d-%02d %02d:%02d",
                              timeinfo->tm_year + 1900,  // Add 1900 to get actual year
                              timeinfo->tm_mon + 1,      // Add 1 (tm_mon is 0-11)
                              timeinfo->tm_mday,
                              timeinfo->tm_hour,
                              timeinfo->tm_min);
  if (n < 0 || static_cast<size_t>(n) >= buffer.size()) {
    return "";
  }
  buffer.resize(static_cast<size_t>(n));
  return buffer;
#else
  // Other platforms: stub (for tests or unsupported platforms)
  (void)ft;  // Suppress unused parameter warning
  return "";
#endif  // _WIN32
}
