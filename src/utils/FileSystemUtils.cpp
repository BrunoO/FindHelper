#include "utils/FileSystemUtils.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <windows.h>  // NOSONAR(cpp:S3806) - Must be first: provides types for Windows/Shell headers
#include <objbase.h>  // NOLINT(llvm-include-order) - must follow windows.h on MSVC
#include <propsys.h>  // NOLINT(llvm-include-order) - must follow windows.h on MSVC; defines PROPERTYKEY
#include <propkey.h>  // NOLINT(llvm-include-order) - for PKEY_Size, PKEY_DateModified
#include <shlwapi.h>  // NOLINT(llvm-include-order) - for SHCreateItemFromParsingName
#include <shobjidl.h> // NOLINT(llvm-include-order) - for IShellItem2
#elif defined(__APPLE__) || defined(__unix__)
#include <cctype>
#include <cerrno>
#include <ctime>
#include <sys/stat.h>
#endif  // _WIN32

#include "utils/FileAttributeConstants.h"
#include "utils/FileTimeTypes.h"
#include "utils/Logger.h"
#include "utils/StringUtils.h"

#if defined(__APPLE__) || defined(__unix__)
FILETIME TimespecToFileTime(const struct timespec &ts) {
  constexpr int64_t nanoseconds_per_100ns = 100;
  constexpr int64_t filetime_per_second = 10000000LL;
  int64_t total_100ns = (static_cast<int64_t>(ts.tv_sec) + file_time_constants::kEpochDiffSeconds) * filetime_per_second;  // NOSONAR(cpp:S1905)

  total_100ns += ts.tv_nsec / nanoseconds_per_100ns;

  const auto total_100ns_u = static_cast<uint64_t>(total_100ns);
  FILETIME ft;
  ft.dwLowDateTime = static_cast<uint32_t>(total_100ns_u & file_system_utils_constants::kFiletimeLowWordMask);
  ft.dwHighDateTime = static_cast<uint32_t>((total_100ns_u >> 32u) & file_system_utils_constants::kFiletimeLowWordMask);

  return ft;
}

struct timespec FileTimeToTimespec(const FILETIME &ft) {
  const uint64_t total_100ns = (static_cast<uint64_t>(ft.dwHighDateTime) << 32u) |  // NOSONAR(cpp:S1905)
                               static_cast<uint64_t>(ft.dwLowDateTime);

  const auto total_seconds = static_cast<int64_t>(total_100ns / file_system_utils_constants::kFiletimeIntervalsPerSecond);
  const int64_t unix_seconds = total_seconds - file_time_constants::kEpochDiffSeconds;

  const uint64_t remaining_100ns = total_100ns % file_system_utils_constants::kFiletimeIntervalsPerSecond;
  const auto nanoseconds = static_cast<int64_t>(remaining_100ns * file_system_utils_constants::kNanosecondsPer100ns);

  struct timespec ts = {};
  ts.tv_sec = unix_seconds;
  ts.tv_nsec = nanoseconds;

  return ts;
}

namespace {

void LogStatFailure(const char* target_path, int stat_errno) {
  if (stat_errno == ENOENT) {
    LOG_DEBUG_BUILD("[GetFileAttributes] stat() not found (transient or deleted): " << target_path);
  } else {
    LOG_ERROR_BUILD("[GetFileAttributes] stat() FAILED for: " << target_path << " (errno=" << stat_errno << ")");
  }
}

FileAttributes GetFileAttributesFromCStr(const char* path) {
  FileAttributes result = {kFileSizeFailed, kFileTimeFailed, false};
  if (path == nullptr || path[0] == '\0') {
    return result;
  }

  struct stat file_stat = {};
  const char* target_path = path;
  std::string unix_path;

  // Path conversion: Handle Windows paths from index files
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
    const int stat_errno = errno;
    LogStatFailure(target_path, stat_errno);
    result.fileSize = kFileSizeFailed;
    result.lastModificationTime = kFileTimeFailed;
    result.success = false;
  }

  return result;
}

}  // namespace
#endif  // defined(__APPLE__) || defined(__unix__)

FileAttributes GetFileAttributes(const char* path) {
#ifdef _WIN32
  return GetFileAttributes(std::string_view(path != nullptr ? path : ""));
#elif defined(__APPLE__) || defined(__unix__)
  return GetFileAttributesFromCStr(path);
#else
  (void)path;
  return {kFileSizeFailed, kFileTimeFailed, false};
#endif  // _WIN32
}

FileAttributes GetFileAttributes(std::string_view path) {
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

  if (try_get_attributes(w_path)) {
    return result;
  }

  if (w_path.size() >= 2 && w_path[1] == L':' &&
      (w_path.size() < 4 || w_path[0] != L'\\' || w_path[1] != L'\\' || w_path[2] != L'?' ||
       w_path[3] != L'\\')) {
    std::wstring extended = LR"(\\?\)";
    extended.append(w_path);
    if (try_get_attributes(extended)) {
      return result;
    }
  }

  IShellItem2 *p_item = nullptr;
  HRESULT hr = SHCreateItemFromParsingName(w_path.c_str(), nullptr, IID_PPV_ARGS(&p_item));
  if (SUCCEEDED(hr)) {
    ULONGLONG size = 0;
    hr = p_item->GetUInt64(PKEY_Size, &size);
    if (SUCCEEDED(hr)) {
      result.fileSize = size;
    }

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
  result = GetFileAttributesFromCStr(path_str.c_str());
#else
  (void)path;
#endif  // _WIN32

  return result;
}

uint64_t GetFileSize(std::string_view path) {
  const FileAttributes attrs = GetFileAttributes(path);
  return attrs.fileSize;
}

uint64_t GetFileSize(const char* path) {
  const FileAttributes attrs = GetFileAttributes(path);
  return attrs.fileSize;
}

FILETIME GetFileModificationTime(std::string_view path) {
  const FileAttributes attrs = GetFileAttributes(path);
  return attrs.success ? attrs.lastModificationTime : kFileTimeFailed;
}

FILETIME GetFileModificationTime(const char* path) {
  const FileAttributes attrs = GetFileAttributes(path);
  return attrs.success ? attrs.lastModificationTime : kFileTimeFailed;
}

namespace {
namespace file_system_utils_detail {

std::string FormatSnprintfResult(const std::array<char, 32>& buf, int written) {
  if (written > 0 && static_cast<size_t>(written) < buf.size()) {
    return {buf.data(), static_cast<size_t>(written)};
  }
  if (written >= 0) {
    return {buf.data(), buf.size() - 1U};
  }
  return {};
}

std::string SnprintfKbString(double value) {
  std::array<char, 32> buf{};
  const int written = std::snprintf(buf.data(), buf.size(), "%.1f KB", value);
  return FormatSnprintfResult(buf, written);
}

std::string SnprintfMbString(double value) {
  std::array<char, 32> buf{};
  const int written = std::snprintf(buf.data(), buf.size(), "%.1f MB", value);
  return FormatSnprintfResult(buf, written);
}

std::string SnprintfGbString(double value) {
  std::array<char, 32> buf{};
  const int written = std::snprintf(buf.data(), buf.size(), "%.2f GB", value);
  return FormatSnprintfResult(buf, written);
}

}  // namespace file_system_utils_detail
}  // namespace

std::string FormatFileSize(uint64_t bytes) {
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

std::string FormatFileCount(uint64_t count) {
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

std::string FormatFileTime(const FILETIME &ft) {
  if (IsSentinelTime(ft)) {
    return "...";
  }

  if (IsFailedTime(ft)) {
    return "N/A";
  }

#ifdef _WIN32
  SYSTEMTIME st;

  if (FILETIME localFt; !FileTimeToLocalFileTime(&ft, &localFt) ||
      !FileTimeToSystemTime(&localFt, &st)) {
    return "";
  }

  std::array<char, 64> buffer{};
  if (const int n = std::snprintf(buffer.data(), buffer.size(), "%04d-%02d-%02d %02d:%02d",
                                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
      n < 0 || static_cast<size_t>(n) >= buffer.size()) {
    return "";
  }
  return std::string(buffer.data());
#elif defined(__APPLE__) || defined(__unix__)
  const struct timespec ts = FileTimeToTimespec(ft);

  struct tm timeinfo_buf = {};
  const time_t time_sec = ts.tv_sec;
  const struct tm *timeinfo = localtime_r(&time_sec, &timeinfo_buf);
  if (timeinfo == nullptr) {
    return "";
  }

  std::string buffer(64, '\0');
  const int n = std::snprintf(buffer.data(), buffer.size(), "%04d-%02d-%02d %02d:%02d",
                              timeinfo->tm_year + 1900,
                              timeinfo->tm_mon + 1,
                              timeinfo->tm_mday,
                              timeinfo->tm_hour,
                              timeinfo->tm_min);
  if (n < 0 || static_cast<size_t>(n) >= buffer.size()) {
    return "";
  }
  buffer.resize(static_cast<size_t>(n));
  return buffer;
#else
  (void)ft;
  return "";
#endif  // _WIN32
}
