#include "api/AiProvider/GeminiProvider.h"

#include <future>
#include <string>
#include <string_view>

#include "api/AiProvider/AiTypes.h"
#include "api/GeminiApiUtils.h"

namespace ai_provider {

std::string GeminiProvider::ProviderId() const {
  return {ai_provider_defaults::kProviderGemini};
}

std::string GeminiProvider::DefaultEnvVarName() const {
  return {ai_provider_defaults::kEnvGeminiApiKey};
}

std::future<AiResult> GeminiProvider::GenerateSearchConfigAsync(
    std::string_view description,
    std::string_view api_key,
    int timeout_seconds) {
  // Forward by value into async boundary (caller-owned views must not dangle).
  const std::string desc_str(description);
  const std::string key_str(api_key);
  return std::async(std::launch::async, [desc_str, key_str, timeout_seconds] {  // NOSONAR(cpp:S8460) - single user-triggered network call; future stored, polled per-frame, cleaned on all paths
    return gemini_api_utils::GenerateSearchConfigFromDescription(
        desc_str, key_str, timeout_seconds);
  });
}

}  // namespace ai_provider
