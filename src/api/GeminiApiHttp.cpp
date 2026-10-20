#include "api/GeminiApiHttp.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "api/AiProvider/LlmHttp.h"

namespace gemini_api_utils {

std::pair<bool, std::string> CallGeminiApiHttpPlatform(
    std::string_view prompt,
    std::string_view api_key,
    int timeout_seconds,
    std::string_view model) {
  const std::string body = BuildGeminiRequestBody(prompt);
  const std::vector<std::string> headers = {
      "Content-Type: application/json",
      "x-goog-api-key: " + std::string(api_key),
  };
  return ai_provider::llm_http::CallLlmHttpPost(
      BuildGeminiApiUrl(model), headers, body, timeout_seconds);
}

}  // namespace gemini_api_utils
