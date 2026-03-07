#pragma once

// FileTimeTypes.h - FILETIME type definitions and constants
// Extracted from FileSystemUtils.h for better separation of concerns
// This allows FileIndex.h to use FILETIME types without pulling in file I/O code

#include <cstdint>

// Conditional Windows includes
#ifdef _WIN32
#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem
#else
// For non-Windows platforms (tests and macOS app): use stub definitions
#include "tests/WindowsTypesStub.h"
#endif  // _WIN32

// Windows FILETIME epoch: 1601-01-01 00:00:00 UTC. Unix epoch: 1970-01-01 00:00:00 UTC.
namespace file_time_constants {
constexpr int64_t kEpochDiffSeconds = 11644473600LL;
}  // namespace file_time_constants

// Sentinel values for FILETIME (modification time)
const FILETIME kFileTimeNotLoaded = {UINT32_MAX, UINT32_MAX};  // NOLINT(cert-err58-cpp) - trivial aggregate init, no throw
const FILETIME kFileTimeFailed = {1, 0};  // NOLINT(cert-err58-cpp) - trivial aggregate init, no throw

// Helper function to check if FILETIME is sentinel (not loaded yet)
inline bool IsSentinelTime(const FILETIME &ft) {
  return ft.dwHighDateTime == UINT32_MAX && ft.dwLowDateTime == UINT32_MAX;
}

// Helper function to check if FILETIME load failed
inline bool IsFailedTime(const FILETIME &ft) {
  return ft.dwHighDateTime == 0 && ft.dwLowDateTime == 1;
}

// Helper function to check if FILETIME is valid (not sentinel, not failed)
inline bool IsValidTime(const FILETIME &ft) {
  return !IsSentinelTime(ft) && !IsFailedTime(ft);
}

// Helper function to check if FILETIME is the Windows epoch (1601-01-01) or very close to it
// This date is often returned for protected system files when access is denied
// The epoch is 1601-01-01 00:00:00 UTC, but timezone conversion may shift it slightly
inline bool IsEpochTime(const FILETIME &ft) {
  // Windows FILETIME epoch is 1601-01-01 00:00:00 UTC = 0x0000000000000000
  // Check for exact epoch
  if (ft.dwHighDateTime == 0 && ft.dwLowDateTime == 0) {
    return true;
  }
  
  // Check if the date is in year 1601 (accounts for timezone offsets)
  // FILETIME for 1601-01-01 00:00:00 UTC = 0x0000000000000000
  // FILETIME for 1602-01-01 00:00:00 UTC ≈ 0x00000000002A69C0 (31,536,000,000,000 = 100ns intervals in a year)
  // With timezone offset (e.g., UTC+1), 1601-01-01 01:00:00 local = 1601-01-01 00:00:00 UTC
  // So we check if dwHighDateTime is 0 and dwLowDateTime is within the year 1601 range
  // Adding some margin for timezone offsets (up to ±14 hours = ±50,400,000,000,000 100ns intervals)
  if (ft.dwHighDateTime == 0) {
    // Check if within year 1601 (with timezone margin)
    // Max value for 1601-12-31 23:59:59 UTC + 14 hours = approximately 0x00000000002A69C0 + 0x00000000002EE824
    const uint64_t kYear1601Max = 0x0000000000595200ULL;
    if (ft.dwLowDateTime < kYear1601Max) {
      return true;  // Within year 1601 (accounting for timezone)
    }
  }
  
  return false;
}

// Cross-platform CompareFileTime function
// On Windows, uses Windows API CompareFileTime
// On non-Windows, provides stub implementation
#ifndef _WIN32
// CompareFileTime stub: returns -1 if ft1 < ft2, 0 if equal, 1 if ft1 > ft2
// LONG is defined in WindowsTypesStub.h for non-Windows platforms
inline LONG CompareFileTime(const FILETIME* ft1, const FILETIME* ft2) {
  if (ft1 == nullptr || ft2 == nullptr) {
    return 0;
  }
  const uint64_t time1 = (static_cast<uint64_t>(ft1->dwHighDateTime) << 32U) | static_cast<uint64_t>(ft1->dwLowDateTime);  // NOSONAR(cpp:S1905) - Cast needed for 64-bit bit shift
  const uint64_t time2 = (static_cast<uint64_t>(ft2->dwHighDateTime) << 32U) | static_cast<uint64_t>(ft2->dwLowDateTime);  // NOSONAR(cpp:S1905) - Cast needed for 64-bit bit shift
  if (time1 < time2) {
    return -1;
  }
  if (time1 > time2) {
    return 1;
  }
  return 0;
}
#else
// On Windows, CompareFileTime is provided by windows.h (included above)
// No need to define it here
#endif  // _WIN32
