#pragma once

#include <string>  // NOLINT(clang-diagnostic-error) - System header, unavoidable on macOS (header-only analysis limitation)
#include <string_view>
#include <utility>

namespace gemini_api_utils {

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

