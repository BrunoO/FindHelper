/**
 * @file StringUtils.cpp
 * @brief Windows implementation of string utility functions
 */

#include "utils/StringUtils.h"

#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem

std::string WideToUtf8(const std::wstring& wstr) {
  if (wstr.empty()) {
    return {};
  }

  const int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(),
                                              static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
  std::string str_to(size_needed, 0);
  WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()), str_to.data(),
                      size_needed, nullptr, nullptr);
  return str_to;
}

std::wstring Utf8ToWide(std::string_view str) {
  if (str.empty()) {
    return {};
  }

  const int size_needed =
      MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
  std::wstring wstr_to(size_needed, 0);
  MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), wstr_to.data(),
                      size_needed);
  return wstr_to;
}

