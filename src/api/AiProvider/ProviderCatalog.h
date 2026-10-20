#pragma once

/**
 * @file api/AiProvider/ProviderCatalog.h
 * @brief Curated list of AI services the user can pick in the UI.
 *
 * The app implements two *protocols* (`gemini`, `openai-compatible`) but users
 * think in terms of *services* (Google Gemini, OpenCode, OpenAI, Ollama).
 * The catalog bridges the two: every entry is a recipe that fills in the
 * connection fields already stored in `AppSettings::aiProvider`, so the
 * settings JSON stays the contract and the user never has to author it.
 *
 * A preset introduces no new storage concept and no new protocol — it only
 * names a combination of `providerId` / `baseUrl` / `model` / `apiKeyEnv`.
 */

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "api/AiProvider/AiTypes.h"
#include "core/Settings.h"

namespace ai_provider {

// One curated service the user can select.
//
// The text fields are plain C strings (never string_view): they are passed to
// ImGui, which needs a null-terminated label, and the catalog is static data
// with static storage duration.
struct ProviderPreset {
  // Stable id persisted as `aiProvider.presetId` (never shown to the user).
  const char* id = nullptr;
  // Human-readable service name shown in the picker and in the AI panel header summary.
  const char* label = nullptr;
  // One-line hint rendered under the label.
  const char* description = nullptr;
  // Protocol id written to `aiProvider.providerId`.
  const char* provider_id = nullptr;
  // Endpoint written to `aiProvider.baseUrl` ("" = provider default).
  const char* base_url = nullptr;
  // Model written to `aiProvider.model` ("" = provider default).
  const char* model = nullptr;
  // Environment variable holding the API key ("" = the provider needs no key).
  const char* api_key_env = nullptr;
  // False for keyless local servers: they must not be reported as "no key".
  bool needs_api_key = true;
  // True for the custom entry: selecting it keeps the user's own endpoint,
  // model and key-variable values instead of overwriting them.
  bool keeps_connection_details = false;
};

// Preset ids (single source of truth for the persisted `presetId`).
namespace preset_defaults {
inline constexpr const char* kPresetGemini = "gemini";
inline constexpr const char* kPresetOpenCode = "opencode";
inline constexpr const char* kPresetOpenAi = "openai";
inline constexpr const char* kPresetOllama = "ollama";
inline constexpr const char* kPresetCustom = "custom";
}  // namespace preset_defaults

// Availability of the API key for a provider.
enum class ApiKeyStatus : std::uint8_t {
  NotRequired,  // the provider works without a key (local servers)
  Found,        // a key is present in the environment
  Missing,      // the provider needs a key and none was found
};

// The curated catalog in display order (the custom entry is always last).
[[nodiscard]] const std::vector<ProviderPreset>& PresetCatalog();

// Catalog entry with the given id, or nullptr when the id is unknown.
[[nodiscard]] const ProviderPreset* FindPresetById(std::string_view preset_id);

// The entry a configuration resolves to. Never null: an unknown or absent
// selection resolves to the custom entry.
[[nodiscard]] const ProviderPreset& ResolvePreset(const ProviderConfig& config);

// Short summary for the AI panel header ("OpenCode · deepseek-v4.1-flash").
[[nodiscard]] std::string ProviderSummary(const ProviderConfig& config);

// Writes an entry's connection details into the settings group. The custom
// entry keeps the user's own endpoint/model/key values. The timeout is a user
// preference and is never touched.
void ApplyPreset(AiProviderSettings& settings, const ProviderPreset& preset);

// Key status of the provider a configuration resolves to.
[[nodiscard]] ApiKeyStatus ResolveApiKeyStatus(const ProviderConfig& config);

// Key status of a catalog entry, independent of the current selection.
[[nodiscard]] ApiKeyStatus ResolvePresetApiKeyStatus(const ProviderPreset& preset);

// Name of the environment variable that holds the API key: the configured one
// when set, else the provider default. Empty when the provider needs no key.
[[nodiscard]] std::string_view KeyVariableHint(const ProviderConfig& config);

}  // namespace ai_provider
