#include "api/AiProvider/ProviderCatalog.h"

#include <string>
#include <string_view>

#include "api/GeminiApiUtils.h"

namespace ai_provider {
namespace {

// OpenAI service endpoint/model (catalog preset).
constexpr const char* kOpenAiServiceBaseUrl = "https://api.openai.com/v1";
constexpr const char* kOpenAiServiceModel = "gpt-4o-mini";
// Local server endpoint/model (catalog preset); no API key is required.
constexpr const char* kLocalOllamaBaseUrl = "http://localhost:11434/v1";
constexpr const char* kLocalOllamaModel = "llama3.1";

// Endpoint comparison ignoring a trailing slash.
std::string_view TrimTrailingSlash(std::string_view url) {
  while (!url.empty() && url.back() == '/') {
    url.remove_suffix(1);
  }
  return url;
}

// True when the named environment variable holds a non-empty value.
bool HasKeyInEnv(std::string_view env_name) {
  return !env_name.empty() && !gemini_api_utils::GetEnvVarString(env_name).empty();
}

// The custom entry, the last catalog entry by contract (see PresetCatalog()).
const ProviderPreset& CustomPreset() {
  return PresetCatalog().back();
}

}  // namespace

const std::vector<ProviderPreset>& PresetCatalog() {
  static const std::vector<ProviderPreset> kPresets = {
      {preset_defaults::kPresetGemini, "Google Gemini", "Google's built-in endpoint",
       ai_provider_defaults::kProviderGemini, "", ai_provider_defaults::kDefaultGeminiModel,
       ai_provider_defaults::kEnvGeminiApiKey, true, false,},
      {preset_defaults::kPresetOpenCode, "OpenCode", "opencode.ai/zen/go · OpenCode Go subscription",
       ai_provider_defaults::kProviderOpenAiCompatible, ai_provider_defaults::kDefaultOpenAiBaseUrl,
       ai_provider_defaults::kDefaultOpenAiModel, ai_provider_defaults::kEnvOpencodeApiKey, true,
       false,},
      {preset_defaults::kPresetOpenAi, "OpenAI", "api.openai.com/v1 · gpt-4o-mini",
       ai_provider_defaults::kProviderOpenAiCompatible, kOpenAiServiceBaseUrl,
       kOpenAiServiceModel, ai_provider_defaults::kEnvOpenAiApiKey, true,
       false,},
      {preset_defaults::kPresetOllama, "Ollama (local)", "localhost:11434/v1 · llama3.1 · no key",
       ai_provider_defaults::kProviderOpenAiCompatible, kLocalOllamaBaseUrl,
       kLocalOllamaModel, "", false, false,},
      {preset_defaults::kPresetCustom, "Custom endpoint", "Any OpenAI-compatible server",
       ai_provider_defaults::kProviderOpenAiCompatible, "", "", "", true, true,},
  };
  return kPresets;
}

const ProviderPreset* FindPresetById(std::string_view preset_id) {
  for (const auto& preset : PresetCatalog()) {
    if (preset_id == preset.id) {
      return &preset;
    }
  }
  return nullptr;
}

const ProviderPreset& ResolvePreset(const ProviderConfig& config) {
  // A stored preset id wins: it survives a non-default port or a customised
  // model, which matching on the endpoint alone could not.
  if (const ProviderPreset* preset = FindPresetById(config.preset_id); preset != nullptr) {
    return *preset;
  }
  // No (or an unknown) stored id: recognise the service from its default endpoint so
  // configuration files written before the catalog existed still resolve.
  if (config.provider_id == ai_provider_defaults::kProviderGemini) {
    return *FindPresetById(preset_defaults::kPresetGemini);
  }
  // Only the catalog's exact default endpoints are recognised; anything else
  // is a custom endpoint (a lookalike host must not be mislabelled).
  if (const std::string_view url = TrimTrailingSlash(config.base_url); !url.empty()) {
    for (const auto& preset : PresetCatalog()) {
      if (!preset.keeps_connection_details && url == TrimTrailingSlash(preset.base_url)) {
        return preset;
      }
    }
  }
  return CustomPreset();
}

std::string ProviderSummary(const ProviderConfig& config) {
  std::string summary{ResolvePreset(config).label};
  if (!config.model.empty()) {
    summary += " · ";
    summary += config.model;
  }
  return summary;
}

void ApplyPreset(AiProviderSettings& settings, const ProviderPreset& preset) {
  settings.presetId = preset.id;
  if (preset.keeps_connection_details) {
    // The custom entry keeps the user's own endpoint, model and key variable.
    return;
  }
  settings.providerId = preset.provider_id;
  settings.baseUrl = preset.base_url;
  settings.model = preset.model;
  settings.apiKeyEnv = preset.api_key_env;
}

ApiKeyStatus ResolvePresetApiKeyStatus(const ProviderPreset& preset) {
  if (!preset.needs_api_key) {
    return ApiKeyStatus::NotRequired;
  }
  if (HasKeyInEnv(preset.api_key_env) ||
      HasKeyInEnv(ai_provider_defaults::kEnvGenericAiApiKey)) {
    return ApiKeyStatus::Found;
  }
  return ApiKeyStatus::Missing;
}

ApiKeyStatus ResolveApiKeyStatus(const ProviderConfig& config) {
  const ProviderPreset& preset = ResolvePreset(config);
  if (!preset.needs_api_key) {
    return ApiKeyStatus::NotRequired;
  }
  if (HasKeyInEnv(config.api_key_env)) {
    return ApiKeyStatus::Found;
  }
  return ResolvePresetApiKeyStatus(preset);
}

std::string_view KeyVariableHint(const ProviderConfig& config) {
  const ProviderPreset& preset = ResolvePreset(config);
  if (!preset.needs_api_key) {
    return {};
  }
  if (!config.api_key_env.empty()) {
    return config.api_key_env;
  }
  if (preset.api_key_env[0] != '\0') {
    return preset.api_key_env;
  }
  return ai_provider_defaults::kEnvGenericAiApiKey;
}

}  // namespace ai_provider
