#pragma once

#include <string>

#include "api/GeminiApiUtils.h"

namespace ai_provider {

// Provider-neutral aliases: SearchConfig / result shape is shared across
// all LLM providers. The inner {"version","search_config"} JSON contract
// is provider-independent; only transport + envelope differ.
using gemini_api_utils::SearchConfig;
using AiResult = gemini_api_utils::GeminiApiResult;

// Configuration for one LLM provider invocation. All strings empty means
// "use provider default". Secrets are never stored here — only the env
// var name; the key is read at call time via GetEnvVarString.
struct ProviderConfig {
  std::string provider_id;
  std::string base_url;
  std::string model;
  std::string api_key_env;
  int timeout_seconds = gemini_api_utils::kDefaultTimeoutSeconds;
};

namespace ai_provider_defaults {

// Provider ids (single source of truth; DRY).
inline constexpr const char* kProviderGemini = "gemini";
inline constexpr const char* kProviderOpenAiCompatible = "openai-compatible";

// Env var names (secrets via env only, never persisted in settings JSON).
inline constexpr const char* kEnvGeminiApiKey = "GEMINI_API_KEY";
inline constexpr const char* kEnvOpencodeApiKey = "OPENCODE_API_KEY";
inline constexpr const char* kEnvGenericAiApiKey = "AI_API_KEY";

// Default endpoints / models.
inline constexpr const char* kDefaultOpenAiBaseUrl = "https://opencode.ai/inference/openai/v1";
inline constexpr const char* kDefaultOpenAiChatPath = "/chat/completions";
// Default model: free, unlimited, and served to third-party clients on the
// inference gateway (verified 200). NOTE: the legacy zen/go gateway gates
// free models to OpenCode-only (HTTP 403 FreeTierError) and requires
// x-opencode-session — hence the inference default + session header.
inline constexpr const char* kDefaultOpenAiModel = "space-bunny-free";

// Settings defaults for AppSettings::AiProviderSettings.
inline constexpr const char* kDefaultProviderId = kProviderGemini;

}  // namespace ai_provider_defaults

}  // namespace ai_provider
