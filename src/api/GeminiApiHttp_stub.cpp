#include "api/GeminiApiHttp.h"
#include <string>

namespace gemini_api_utils {

std::pair<bool, std::string> CallGeminiApiHttpPlatform(
    std::string_view /*prompt*/,
    std::string_view /*api_key*/,
    int /*timeout_seconds*/) {
    return {false, "Gemini API support not compiled (libcurl missing)"};
}

} // namespace gemini_api_utils
