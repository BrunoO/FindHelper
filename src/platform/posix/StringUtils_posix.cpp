/**
 * @file StringUtils_posix.cpp
 * @brief POSIX (macOS + Linux) stub implementation of wide-string utility functions
 *
 * On non-Windows platforms the WideToUtf8 / Utf8ToWide functions are not used
 * in production code paths (they exist to satisfy the Windows-specific API
 * surface declared in StringUtils.h). These stubs keep the build clean.
 */

#include "utils/StringUtils.h"

#include <string>
#include <string_view>

std::string WideToUtf8(const std::wstring &wstr) {
  // Non-Windows: Simple conversion (assumes wstring contains valid UTF-16 surrogate pairs)
  // For tests, this function is not used, so a stub is sufficient
  (void)wstr;
  return {};
}

std::wstring Utf8ToWide(std::string_view str) {
  // Non-Windows: Simple conversion (for tests, this function is not used)
  // For production code, this would need proper UTF-8 to UTF-16 conversion
  (void)str;
  return {};
}
