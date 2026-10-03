#include "api/AiProvider/ProviderFactory.h"

#include <memory>
#include <string>
#include <string_view>

#include "api/AiProvider/GeminiProvider.h"
#include "api/AiProvider/OpenAiCompatibleProvider.h"
#include "api/GeminiApiUtils.h"
#include "core/Settings.h"

namespace ai_provider {

bool IsKnownProviderId(std::string_view provider_id) {
  return provider_id == ai_provider_defaults::kProviderGemini ||
         provider_id == ai_provider_defaults::kProviderOpenAiCompatible;
}

std::unique_ptr<ILlmProvider> CreateProvider(std::string_view provider_id) {
  if (provider_id == ai_provider_defaults::kProviderOpenAiCompatible) {
    ProviderConfig config;
    config.provider_id = ai_provider_defaults::kProviderOpenAiCompatible;
    return std::make_unique<OpenAiCompatibleProvider>(config);
  }
  return std::make_unique<GeminiProvider>();
}

std::unique_ptr<ILlmProvider> CreateProviderForConfig(const ProviderConfig& config) {
  if (config.provider_id == ai_provider_defaults::kProviderOpenAiCompatible) {
    return std::make_unique<OpenAiCompatibleProvider>(config);
  }
  return std::make_unique<GeminiProvider>();
}

ProviderConfig ResolveProviderConfig(const AppSettings& settings) {
  ProviderConfig config;
  config.provider_id = settings.aiProvider.providerId.empty()
                           ? ai_provider_defaults::kDefaultProviderId
                           : settings.aiProvider.providerId;
  if (!IsKnownProviderId(config.provider_id)) {
    config.provider_id = ai_provider_defaults::kProviderGemini;
  }
  config.base_url = settings.aiProvider.baseUrl;
  config.model = settings.aiProvider.model;
  config.api_key_env = settings.aiProvider.apiKeyEnv;
  config.timeout_seconds = gemini_api_utils::kDefaultTimeoutSeconds;
  if (settings.aiProvider.timeoutSeconds > 0) {
    // Range is validated by the provider (1-300); out-of-range surfaces a
    // clear terminal error instead of silently clamping.
    config.timeout_seconds = settings.aiProvider.timeoutSeconds;
  }
  if (config.provider_id == ai_provider_defaults::kProviderOpenAiCompatible &&
      config.model.empty()) {
    config.model = ai_provider_defaults::kDefaultOpenAiModel;
  }
  return config;
}

std::string DisplayName(const ProviderConfig& config) {
  if (config.provider_id == ai_provider_defaults::kProviderOpenAiCompatible) {
    if (!config.model.empty()) {
      return config.model;
    }
    return {"OpenAI-compatible"};
  }
  return {"Gemini"};
}

std::string ResolveApiKey(const ProviderConfig& config,
                          std::string_view provider_default_env) {
  if (!config.api_key_env.empty()) {
    if (const std::string from_custom = gemini_api_utils::GetEnvVarString(config.api_key_env);
        !from_custom.empty()) {
      return from_custom;
    }
  }
  if (!provider_default_env.empty()) {
    if (const std::string from_default = gemini_api_utils::GetEnvVarString(provider_default_env);
        !from_default.empty()) {
      return from_default;
    }
  }
  return gemini_api_utils::GetEnvVarString(ai_provider_defaults::kEnvGenericAiApiKey);
}

}  // namespace ai_provider
