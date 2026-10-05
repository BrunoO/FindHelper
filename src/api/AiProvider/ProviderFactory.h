#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "api/AiProvider/AiTypes.h"
#include "api/AiProvider/ILlmProvider.h"

struct AppSettings;

namespace ai_provider {

// Creates the provider for provider_id ("gemini" default, "openai-compatible").
// Unknown ids fall back to Gemini for backward compatibility.
[[nodiscard]] std::unique_ptr<ILlmProvider> CreateProvider(std::string_view provider_id);

// Creates the provider described by config (used by UI/async paths).
[[nodiscard]] std::unique_ptr<ILlmProvider> CreateProviderForConfig(const ProviderConfig& config);

// Resolves effective ProviderConfig from AppSettings (aiProvider group) with
// provider defaults for empty fields. Never includes the secret itself.
[[nodiscard]] ProviderConfig ResolveProviderConfig(const AppSettings& settings);

// Reads the API key for config from env (config.api_key_env, then provider
// default, then generic AI_API_KEY). Returns empty when unset.
[[nodiscard]] std::string ResolveApiKey(const ProviderConfig& config,
                                        std::string_view provider_default_env);

// True for the two supported ids.
[[nodiscard]] bool IsKnownProviderId(std::string_view provider_id);

// Short display name for UI surfaces (status bar). Gemini keeps its name;
// OpenAI-compatible providers show the configured model (or a generic label
// when no model is set).
[[nodiscard]] std::string DisplayName(const ProviderConfig& config);

}  // namespace ai_provider
