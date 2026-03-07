#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <iterator>

#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// ============================================================================
// Cross-Platform String Utilities (strcpy_s/strcat_s replacements)
// ============================================================================

#ifdef _WIN32
#define strcpy_safe strcpy_s
#define strcat_safe strcat_s
#else
/**
 * @brief Cross-platform strcpy_s replacement for non-Windows
 *
 * Safely copies a null-terminated string from src to dest, ensuring null-termination
 * and preventing buffer overruns. On Windows, this is a macro that maps to strcpy_s.
 *
 * @param dest Destination buffer
 * @param dest_size Size of destination buffer
 * @param src Source string
 */
inline void strcpy_safe(char* dest, size_t dest_size, const char* src) {  // NOLINT(readability-identifier-naming) - API name used project-wide (AGENTS.md)
  if (dest_size == 0) {
    return;
  }
  const size_t src_len = strlen(src);  // NOSONAR(cpp:S1081) - Safe: utility function assumes null-terminated C string input (standard contract)
  const size_t copy_len = (src_len < dest_size - 1) ? src_len : dest_size - 1;
  memcpy(dest, src, copy_len);
  dest[copy_len] = '\0';  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic) - required for null-termination
}

/**
 * @brief Cross-platform strcat_s replacement for non-Windows
 *
 * Safely appends a null-terminated string from src to dest, ensuring null-termination
 * and preventing buffer overruns. On Windows, this is a macro that maps to strcat_s.
 *
 * @param dest Destination buffer (must be null-terminated)
 * @param dest_size Size of destination buffer
 * @param src Source string to append
 */
inline void strcat_safe(char* dest, size_t dest_size, const char* src) {  // NOLINT(readability-identifier-naming) - API name used project-wide (AGENTS.md)
  if (dest_size == 0) {
    return;
  }
  const size_t dest_len = strlen(dest);  // NOSONAR(cpp:S1081) - Safe: utility function assumes null-terminated C string input (standard contract)
  if (dest_len >= dest_size - 1) {
    return;  // No room
  }
  const size_t src_len = strlen(src);  // NOSONAR(cpp:S1081) - Safe: utility function assumes null-terminated C string input (standard contract)
  const size_t available = dest_size - dest_len - 1;
  const size_t copy_len = (src_len < available) ? src_len : available;
  memcpy(dest + dest_len, src, copy_len);  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic) - append position
  dest[dest_len + copy_len] = '\0';  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic) - null-termination
}
#endif  // _WIN32

// Helper function to convert UTF-16 (wide string) to UTF-8
// Windows: Uses WideCharToMultiByte API
// macOS/Linux: Stub implementation (for tests)
std::string WideToUtf8(const std::wstring& wstr);

// Helper function to convert UTF-8 to UTF-16 (wide string)
// Windows: Uses MultiByteToWideChar API
// macOS/Linux: Stub implementation (for tests)
std::wstring Utf8ToWide(std::string_view str);

// Helper function to convert string to lowercase
inline std::string ToLower(std::string_view str) {
  std::string result;
  result.reserve(str.size());
  std::transform(str.begin(), str.end(), std::back_inserter(result),
                 [](unsigned char c) { return std::tolower(c); });
  return result;
}

// Helper function to trim whitespace from both ends of a string
inline std::string Trim(std::string_view str) {
  if (str.empty()) {
    return {};
  }

  const size_t first = str.find_first_not_of(" \t");
  if (first == std::string_view::npos) {
    return {};
  }

  const size_t last = str.find_last_not_of(" \t");
  return std::string{str.substr(first, last - first + 1)};
}

// Helper function to extract filename from path
inline std::string GetFilename(std::string_view path) {
  if (const size_t pos = path.find_last_of("\\/"); pos != std::string_view::npos) {
    return std::string(path.substr(pos + 1));
  }
  return std::string(path);
}

// Helper function to extract extension from filename
inline std::string GetExtension(std::string_view filename) {
  if (const size_t pos = filename.find_last_of('.');
      pos != std::string_view::npos && pos < filename.length() - 1) {
    return std::string(filename.substr(pos + 1));
  }
  return {};
}

// Parse extensions with configurable delimiter (default is semicolon)
inline std::vector<std::string> ParseExtensions(std::string_view input, char delimiter = ';') {
  std::vector<std::string> extensions;
  // Reserve capacity to avoid reallocations (estimate: ~4-8 extensions typical)
  extensions.reserve(8);

  // Parse directly from string_view (more efficient than stringstream)
  size_t start = 0;
  while (start < input.size()) {
    // Find next delimiter or end of string
    size_t end = input.find(delimiter, start);
    if (end == std::string_view::npos) {
      end = input.size();
    }

    // Extract token
    const std::string_view token_view = input.substr(start, end - start);

    // Trim whitespace
    if (std::string token = Trim(token_view); !token.empty()) {
      // Remove leading dot if present
      if (token[0] == '.') {
        token.erase(0, 1);
      }
      extensions.push_back(ToLower(token));
    }

    // Move to next token (skip delimiter)
    start = end + 1;
  }

  return extensions;
}

/**
 * @brief Format number with thousand separators (e.g., 1,234,567)
 *
 * @param count Number to format
 * @return Formatted string
 */
inline std::string FormatNumberWithSeparators(size_t count) {
  std::string num_str = std::to_string(count);
  std::string result;
  result.reserve(num_str.size() + ((num_str.size() + 2) / 3));

  // Build from right to left using += then reverse (avoids temporary string concatenation)
  int comma_count = 0;
  for (int i = static_cast<int>(num_str.length()) - 1; i >= 0; --i) {
    if (comma_count > 0 && comma_count % 3 == 0) {
      result += ',';
    }
    result += num_str[static_cast<size_t>(i)];
    ++comma_count;
  }
  std::reverse(result.begin(), result.end());
  return result;
}

/**
 * @brief Format memory bytes into human-readable string (e.g., "15 MB")
 *
 * Converts a byte count into a formatted string with appropriate unit (B, KB, MB, GB).
 * Uses binary (1024-based) units for consistency with system conventions.
 * Values are rounded to the nearest integer (no decimal precision).
 *
 * @param bytes Memory size in bytes
 * @return Formatted string (e.g., "0 B", "2 KB", "15 MB", "2 GB")
 */
inline std::string FormatMemory(size_t bytes) {
  if (bytes == 0) {
    return "0 B";
  }

  const size_t kKB = 1024;
  const size_t kMB = kKB * 1024;
  const size_t kGB = kMB * 1024;

  std::ostringstream oss;

  if (bytes >= kGB) {
    // Round to nearest integer: add half the divisor before dividing
    const size_t gb = (bytes + kGB / 2) / kGB;
    oss << gb << " GB";
  } else if (bytes >= kMB) {
    // Round to nearest integer: add half the divisor before dividing
    const size_t mb = (bytes + kMB / 2) / kMB;
    oss << mb << " MB";
  } else if (bytes >= kKB) {
    // Round to nearest integer: add half the divisor before dividing
    const size_t kb = (bytes + kKB / 2) / kKB;
    oss << kb << " KB";
  } else {
    oss << bytes << " B";
  }

  return oss.str();
}
