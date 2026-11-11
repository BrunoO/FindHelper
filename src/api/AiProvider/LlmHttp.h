#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ai_provider::llm_http {

// Provider-neutral HTTPS POST used by OpenAI-compatible providers.
// url: full URL (https://host[:port]/path). headers: full "Name: value"
// lines (without trailing CRLF). body: JSON request body.
// Returns {true, response_body} or {false, error_message}.
// Limits: 1MB response cap shared with Gemini path.
std::pair<bool, std::string> CallLlmHttpPost(
    std::string_view url,
    const std::vector<std::string>& headers,
    std::string_view body,
    int timeout_seconds);

}  // namespace ai_provider::llm_http
