#pragma once

#include <algorithm>
#include <cstddef>
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

// Gemini model and endpoint constants (single source of truth; DRY).
// Default model is a "-latest" alias so it tracks Google's current model without
// a code change. NOTE: "gemini-flash-latest" (and gemini-3.7/3.8-flash) hang
// without answering as of 2026-10-20, so the lite alias is the default.
inline constexpr const char* kGeminiModel = "gemini-flash-lite-latest";
inline constexpr const char* kGeminiApiUrlPrefix =
    "https://generativelanguage.googleapis.com/v1beta/models/";
inline constexpr const char* kGeminiApiUrlSuffix = ":generateContent";

// True when `model` is safe to embed in the request URL (letters, digits, '.',
// '_' and '-' only; non-empty).
[[nodiscard]] inline bool IsValidGeminiModelName(std::string_view model) {
  return !model.empty() && std::all_of(model.begin(), model.end(), [](char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
           ch == '.' || ch == '_' || ch == '-';
  });
}

// Full generateContent URL for a model; an empty model selects kGeminiModel.
[[nodiscard]] inline std::string BuildGeminiApiUrl(std::string_view model) {
  std::string url(kGeminiApiUrlPrefix);
  url += model.empty() ? std::string_view(kGeminiModel) : model;
  url += kGeminiApiUrlSuffix;
  return url;
}

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
// Escapes the prompt and wraps it in the expected {"contents":[...]} envelope with
// generationConfig configuring low thinking level and application/json format for Gemini 3.x models.
inline std::string BuildGeminiRequestBody(std::string_view prompt) {
  const std::string escaped_prompt = EscapeJsonString(prompt);
  std::ostringstream json_body;
  json_body << R"({"contents":[{"parts":[{"text":")" << escaped_prompt
            << R"("}]}],"generationConfig":{"responseMimeType":"application/json","thinkingConfig":{"thinkingLevel":"low"}}})";
  return json_body.str();
}

// HTTP client function for Gemini API (delegates to CallLlmHttpPost).
// Returns (success, response_body_or_error_message).
// `model` empty = kGeminiModel.
std::pair<bool, std::string> CallGeminiApiHttpPlatform(
    std::string_view prompt,
    std::string_view api_key,
    int timeout_seconds,
    std::string_view model = {});

} // namespace gemini_api_utils

