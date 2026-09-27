#pragma once

#include <sstream>
#include <string>  // NOLINT(clang-diagnostic-error) - System header, unavoidable on macOS (header-only analysis limitation)
#include <string_view>
#include <utility>

namespace gemini_api_utils {

// Shared HTTP constants used by all platform implementations.
// Defined here (not in anonymous namespace) so each platform .cpp can use them
// without redeclaring.
inline constexpr size_t kMaxResponseSize = size_t{1024} * 1024;  // 1 MB
inline constexpr int kHttpStatusOk = 200;

// Shared helper: basic JSON string escaping for prompt text.
// Defined inline here to avoid duplicating it across all platform implementations.
inline std::string EscapeJsonString(std::string_view str) {
  std::ostringstream oss;
  for (const char c : str) {
    switch (c) {
    case '"':
      oss << "\\\"";  // NOSONAR(cpp:S3628) - Raw string literal would be less readable for JSON escaping
      break;
    case '\\':
      oss << "\\\\";  // NOSONAR(cpp:S3628) - Raw string literal would be less readable for JSON escaping
      break;
    case '\n':
      oss << "\\n";
      break;
    case '\r':
      oss << "\\r";
      break;
    case '\t':
      oss << "\\t";
      break;
    default:
      oss << c;
      break;
    }
  }
  return oss.str();
}

// Builds the JSON request body for a Gemini API call from the given prompt.
// Escapes the prompt and wraps it in the expected {"contents":[...]} envelope.
inline std::string BuildGeminiRequestBody(std::string_view prompt) {
  const std::string escaped_prompt = EscapeJsonString(prompt);
  std::ostringstream json_body;
  json_body << R"({"contents":[{"parts":[{"text":")" << escaped_prompt
            << R"("}]}]})";
  return json_body.str();
}

// Platform-specific HTTP client function for Gemini API
// Returns (success, response_body_or_error_message)
// This function is implemented in platform-specific files:
// - GeminiApiHttp_win.cpp (Windows)
// - GeminiApiHttp_mac.mm (macOS)
// - GeminiApiHttp_linux.cpp (Linux)
std::pair<bool, std::string> CallGeminiApiHttpPlatform(
    std::string_view prompt,
    std::string_view api_key,
    int timeout_seconds);

} // namespace gemini_api_utils

