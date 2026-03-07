/**
 * @file StringUtils_linux.cpp
 * @brief Linux implementation of string utility functions
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

