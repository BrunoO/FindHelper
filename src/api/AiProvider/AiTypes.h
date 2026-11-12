#pragma once

#include <string>

#include "api/GeminiApiHttp.h"
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
  // Id of the catalog entry the user picked (see ProviderCatalog.h). Empty =
  // not chosen yet; the service is then recognised from the endpoint.
  std::string preset_id;
};

namespace ai_provider_defaults {

// Provider ids (single source of truth; DRY).
inline constexpr const char* kProviderGemini = "gemini";
inline constexpr const char* kProviderOpenAiCompatible = "openai-compatible";

// Env var names (secrets via env only, never persisted in settings JSON).
inline constexpr const char* kEnvGeminiApiKey = "GEMINI_API_KEY";
inline constexpr const char* kEnvOpencodeApiKey = "OPENCODE_API_KEY";
inline constexpr const char* kEnvGenericAiApiKey = "AI_API_KEY";
inline constexpr const char* kEnvOpenAiApiKey = "OPENAI_API_KEY";

// Default endpoints / models.
inline constexpr const char* kDefaultGeminiModel = gemini_api_utils::kGeminiModel;
inline constexpr const char* kDefaultOpenAiBaseUrl = "https://opencode.ai/zen/go/v1";
inline constexpr const char* kDefaultOpenAiChatPath = "/chat/completions";
// Default model: a paid OpenCode Go model (verified 200 on the zen/go gateway,
// 2026-10-20). Free models no longer work for third-party clients: the
// inference gateway answers FreeTierError ("only from within OpenCode") and
// space-bunny-free is deprecated (HTTP 410). The zen/go gateway requires the
// x-opencode-session header (sent automatically for that URL).
inline constexpr const char* kDefaultOpenAiModel = "deepseek-v4.1-flash";
// Valid per-attempt timeout range in seconds. GeminiApiUtils.h owns the numbers
// (they are needed there for validation and for its default-argument value); the
// aliases below keep one definition while giving provider code a short name.
inline constexpr int kMinTimeoutSeconds = gemini_api_utils::kMinTimeoutSeconds;
inline constexpr int kMaxTimeoutSeconds = gemini_api_utils::kMaxTimeoutSeconds;

// Settings defaults for AppSettings::AiProviderSettings.
inline constexpr const char* kDefaultProviderId = kProviderGemini;

}  // namespace ai_provider_defaults

}  // namespace ai_provider
