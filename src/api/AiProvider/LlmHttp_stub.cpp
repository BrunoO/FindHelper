#include "api/AiProvider/LlmHttp.h"

#include <string>
#include <vector>

namespace ai_provider::llm_http {

std::pair<bool, std::string> CallLlmHttpPost(
    std::string_view /*url*/,
    const std::vector<std::string>& /*headers*/,
    std::string_view /*body*/,
    int /*timeout_seconds*/) {
  return {false, "LLM HTTP not supported on this platform (libcurl unavailable)"};
}

}  // namespace ai_provider::llm_http
