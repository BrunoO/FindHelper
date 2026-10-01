#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "utils/FileAttributeConstants.h"
#include "utils/FileTimeTypes.h"

#if defined(__APPLE__) || defined(__unix__)
#include <ctime>  // For struct timespec
#endif  // defined(__APPLE__) || defined(__unix__)

namespace file_system_utils_constants {
constexpr uint32_t kFiletimeLowWordMask = 0xFFFFFFFFU;
constexpr int64_t kFiletimeIntervalsPerSecond = 10000000LL;
constexpr int64_t kNanosecondsPer100ns = 100;
constexpr double kBytesPerKb = 1024.0;
}  // namespace file_system_utils_constants

#if defined(__APPLE__) || defined(__unix__)
/**
 * @brief Converts struct timespec (Unix time) to FILETIME (Windows time format)
 */
FILETIME TimespecToFileTime(const struct timespec &ts);

/**
 * @brief Converts FILETIME (Windows time format) to struct timespec (Unix time)
 */
struct timespec FileTimeToTimespec(const FILETIME &ft);
#endif  // defined(__APPLE__) || defined(__unix__)

// Combined structure for file attributes (size and modification time)
// Allows loading both in a single system call for better performance
struct FileAttributes {
  uint64_t fileSize{kFileSizeFailed};  // NOLINT(readability-identifier-naming) - public API uses camelCase
  FILETIME lastModificationTime{kFileTimeFailed};  // NOLINT(readability-identifier-naming)
  bool success{false};  // NOLINT(readability-identifier-naming) - public API uses camelCase
};

// Combined function to get both file size and modification time in one system call.
// Contract: On success, result.success == true and result.fileSize is the logical size
// (0 only for true zero-byte files). On failure, result.success == false; consumers must
// not treat result.fileSize as valid (do not cache 0 as a successful size).
FileAttributes GetFileAttributes(const char* path);
FileAttributes GetFileAttributes(std::string_view path);

// Helper function to get file size
uint64_t GetFileSize(std::string_view path);
uint64_t GetFileSize(const char* path);

// Helper function to get file modification time
FILETIME GetFileModificationTime(std::string_view path);
FILETIME GetFileModificationTime(const char* path);

// Helper function to format file size as human-readable string
std::string FormatFileSize(uint64_t bytes);

// Format a file count as a locale-independent comma-separated integer (e.g., 1200000 → "1,200,000").
std::string FormatFileCount(uint64_t count);

// Helper function to format FILETIME as human-readable string
// Format: "YYYY-MM-DD HH:MM"
std::string FormatFileTime(const FILETIME &ft);
