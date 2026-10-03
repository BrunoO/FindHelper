/**
 * @file StringUtils.cpp
 * @brief Windows implementation of string utility functions
 */

#include "utils/StringUtils.h"

#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem

void WideToUtf8Assign(const wchar_t* wide, size_t wide_len, std::string& out) {
  if (wide == nullptr || wide_len == 0) {
    out.clear();
    return;
  }

  const auto wchar_count = static_cast<int>(wide_len);
  const int size_needed =
      WideCharToMultiByte(CP_UTF8, 0, wide, wchar_count, nullptr, 0, nullptr, nullptr);
  if (size_needed <= 0) {
    out.clear();
    return;
  }

  out.resize(static_cast<size_t>(size_needed));
  const int bytes_written = WideCharToMultiByte(CP_UTF8, 0, wide, wchar_count, out.data(),
                                                size_needed, nullptr, nullptr);
  if (bytes_written <= 0) {
    out.clear();
    return;
  }
  out.resize(static_cast<size_t>(bytes_written));
  // When cbMultiByte > 0, bytes_written may or may not count the written '\0' (MSVC
  // varies with explicit cchWideChar). Strip only a trailing null, never a data byte.
  if (!out.empty() && out.back() == '\0') {
    out.pop_back();
  }
}

std::string WideToUtf8(std::wstring_view wstr) {
  std::string result;
  WideToUtf8Assign(wstr.data(), wstr.size(), result);
  return result;
}

namespace string_utils_win_detail {

[[nodiscard]] std::wstring ConvertNarrowToWide(std::string_view str, UINT code_page) {
  if (str.empty()) {
    return {};
  }
  const int size_needed =
      MultiByteToWideChar(code_page, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
  if (size_needed <= 0) {
    return {};
  }
  std::wstring wide(static_cast<size_t>(size_needed), L'\0');
  const int chars_written = MultiByteToWideChar(code_page, 0, str.data(), static_cast<int>(str.size()),
                                               wide.data(), size_needed);
  if (chars_written <= 0) {
    return {};
  }
  wide.resize(static_cast<size_t>(chars_written));
  if (!wide.empty() && wide.back() == L'\0') {
    wide.pop_back();
  }
  return wide;
}

}  // namespace string_utils_win_detail

std::wstring Utf8ToWide(std::string_view str) {
  if (str.empty()) {
    return {};
  }

  if (std::wstring converted = string_utils_win_detail::ConvertNarrowToWide(str, CP_UTF8); !converted.empty()) {
    return converted;
  }
  // Fallback for ANSI paths from Win32 A APIs (GetTempFileNameA, GetModuleFileNameA, etc.).
  return string_utils_win_detail::ConvertNarrowToWide(str, CP_ACP);
}