
#include <string>
#include <vector>

#include "api/AiProvider/AiTypes.h"
#include "api/AiProvider/OpenAiCompatibleProvider.h"
#include "api/AiProvider/ProviderCatalog.h"
#include "api/AiProvider/ProviderFactory.h"
#include "core/Settings.h"
#include "doctest/doctest.h"
#include "ui/AiProviderWindow.h"

using ai_provider::OpenAiCompatibleProvider;
using ai_provider::ProviderConfig;

namespace {

// RAII helper that sets an environment variable for the duration of a scope
// and restores the previous value on destruction. An empty value clears it.
class ScopedEnvVar {
 public:
  ScopedEnvVar(std::string_view name, std::string_view value)
      : name_(name),
        previous_(gemini_api_utils::GetEnvVarString(name)),
        was_set_(!previous_.empty()) {
    Apply(value);
  }
  ~ScopedEnvVar() {
    Apply(was_set_ ? previous_ : std::string_view{});
  }
  ScopedEnvVar(const ScopedEnvVar&) = delete;
  ScopedEnvVar& operator=(const ScopedEnvVar&) = delete;

 private:
  void Apply(std::string_view value) {
#ifdef _WIN32
    const std::string value_str(value);
    _putenv_s(name_.c_str(), value_str.c_str());
#else
    if (value.empty()) {
      unsetenv(name_.c_str());
    } else {
      const std::string value_str(value);
      setenv(name_.c_str(), value_str.c_str(), 1);
    }
#endif
  }
  std::string name_;
  std::string previous_;
  bool was_set_;
};

}  // namespace

TEST_CASE("OpenAiCompatibleProvider - EndpointUrl") {
  SUBCASE("Default base URL") {
    ProviderConfig config;
    CHECK(OpenAiCompatibleProvider::EndpointUrl(config) ==
          "https://opencode.ai/zen/go/v1/chat/completions");
  }
  SUBCASE("Custom base URL with trailing slash") {
    ProviderConfig config;
    config.base_url = "http://localhost:11434/v1/";
    CHECK(OpenAiCompatibleProvider::EndpointUrl(config) ==
          "http://localhost:11434/v1/chat/completions");
  }
}

TEST_CASE("OpenAiCompatibleProvider - BuildRequestBody") {
  const std::string body = OpenAiCompatibleProvider::BuildRequestBody("find reports", "space-bunny-free");
  CHECK(body.find("\"model\":\"space-bunny-free\"") != std::string::npos);
  CHECK(body.find("\"role\":\"user\"") != std::string::npos);
  CHECK(body.find("find reports") != std::string::npos);
}

TEST_CASE("OpenAiCompatibleProvider - BuildHeaders") {
  SUBCASE("With key") {
    const auto headers = OpenAiCompatibleProvider::BuildHeaders("test-key");
    REQUIRE(headers.size() == 2);
    CHECK(headers[0] == "Content-Type: application/json");
    CHECK(headers[1] == "Authorization: Bearer test-key");
  }
  SUBCASE("Empty key omits Authorization (local servers need no auth)") {
    const auto headers = OpenAiCompatibleProvider::BuildHeaders("");
    REQUIRE(headers.size() == 1);
    CHECK(headers[0] == "Content-Type: application/json");
  }
}

TEST_CASE("OpenAiCompatibleProvider - ExtractAssistantText") {
  SUBCASE("Valid choices response") {
    const std::string inner = R"({"version":"1.0","search_config":{"path":"pp:**/*.txt"}})";
    std::string escaped;
    for (const char c : inner) {
      if (c == '"') {
        escaped += "\\\"";
      } else {
        escaped += c;
      }
    }
    const std::string response =
        R"({"choices":[{"message":{"content":")" + escaped + R"("}}]})";
    const auto [ok, text] = OpenAiCompatibleProvider::ExtractAssistantText(response);
    CHECK(ok);
    CHECK(text.find("search_config") != std::string::npos);
  }
  SUBCASE("Error response") {
    const std::string response = R"({"error":{"message":"Invalid API key"}})";
    const auto [ok, text] = OpenAiCompatibleProvider::ExtractAssistantText(response);
    CHECK(!ok);
    CHECK(text.find("Invalid API key") != std::string::npos);
  }
  SUBCASE("Missing choices") {
    const std::string response = R"({"foo":"bar"})";
    const auto [ok, text] = OpenAiCompatibleProvider::ExtractAssistantText(response);
    CHECK(!ok);
  }
  SUBCASE("Empty response") {
    const auto [ok, text] = OpenAiCompatibleProvider::ExtractAssistantText("");
    CHECK(!ok);
  }
}

TEST_CASE("OpenAiCompatibleProvider - SessionHeader") {
  SUBCASE("Needed for opencode endpoints") {
    CHECK(OpenAiCompatibleProvider::NeedsSessionHeader("https://opencode.ai/zen/go/v1/chat/completions"));
    CHECK(OpenAiCompatibleProvider::NeedsSessionHeader("https://opencode.ai/zen/v1/chat/completions"));
  }
  SUBCASE("Not needed elsewhere") {
    CHECK(!OpenAiCompatibleProvider::NeedsSessionHeader("http://localhost:11434/v1/chat/completions"));
    CHECK(!OpenAiCompatibleProvider::NeedsSessionHeader("https://api.openai.com/v1/chat/completions"));
  }
  SUBCASE("Stable within process") {
    const std::string first = OpenAiCompatibleProvider::StableSessionId();
    const std::string second = OpenAiCompatibleProvider::StableSessionId();
    CHECK(!first.empty());
    CHECK(first == second);
    const std::string header = OpenAiCompatibleProvider::BuildSessionHeader();
    CHECK(header.find("x-opencode-session: ") == 0);
    CHECK(header.find(first) != std::string::npos);
  }
}

TEST_CASE("ProviderFactory - CreateProvider") {
  SUBCASE("Gemini default") {
    auto provider = ai_provider::CreateProvider("gemini");
    CHECK(provider->ProviderId() == "gemini");
    CHECK(provider->DefaultEnvVarName() == "GEMINI_API_KEY");
  }
  SUBCASE("OpenAI-compatible") {
    auto provider = ai_provider::CreateProvider("openai-compatible");
    CHECK(provider->ProviderId() == "openai-compatible");
  }
  SUBCASE("Unknown falls back to Gemini") {
    auto provider = ai_provider::CreateProvider("does-not-exist");
    CHECK(provider->ProviderId() == "gemini");
  }
}

TEST_CASE("OpenAiCompatibleProvider - RetryPolicy") {
  SUBCASE("Transport faults are retriable") {
    CHECK(OpenAiCompatibleProvider::IsRetriableError("Network error: connection reset"));
    CHECK(OpenAiCompatibleProvider::IsRetriableError("Network error: Timeout was reached"));
    CHECK(OpenAiCompatibleProvider::IsRetriableError("Request timeout"));
  }
  SUBCASE("HTTP 429 and 5xx are retriable") {
    CHECK(OpenAiCompatibleProvider::IsRetriableError("HTTP error 429: quota exceeded"));
    CHECK(OpenAiCompatibleProvider::IsRetriableError("HTTP error 500: internal"));
    CHECK(OpenAiCompatibleProvider::IsRetriableError("HTTP error 502: bad gateway"));
    CHECK(OpenAiCompatibleProvider::IsRetriableError("HTTP error 503: unavailable"));
    CHECK(OpenAiCompatibleProvider::IsRetriableError("HTTP error: 503 - unavailable"));
  }
  SUBCASE("HTTP 4xx are terminal") {
    CHECK(!OpenAiCompatibleProvider::IsRetriableError("HTTP error 400: bad request"));
    CHECK(!OpenAiCompatibleProvider::IsRetriableError("HTTP error 401: unauthorized"));
    CHECK(!OpenAiCompatibleProvider::IsRetriableError("HTTP error 403: forbidden"));
    CHECK(!OpenAiCompatibleProvider::IsRetriableError("HTTP error 404: not found"));
  }
  SUBCASE("Transient envelope errors are retriable") {
    CHECK(OpenAiCompatibleProvider::IsRetriableError(
        "API error: Upstream request failed: Model is unavailable."));
    CHECK(OpenAiCompatibleProvider::IsRetriableError("API error: server_error"));
    CHECK(OpenAiCompatibleProvider::IsRetriableError("API error: overloaded, try again later"));
  }
  SUBCASE("Config errors are terminal") {
    CHECK(!OpenAiCompatibleProvider::IsRetriableError(
        "API error: OpenCode's free tier can only be used from within OpenCode"));
    CHECK(!OpenAiCompatibleProvider::IsRetriableError("API key is empty"));
    CHECK(!OpenAiCompatibleProvider::IsRetriableError("Invalid JSON structure: missing 'choices'"));
    CHECK(!OpenAiCompatibleProvider::IsRetriableError("Response too large (max 1MB)"));
    CHECK(!OpenAiCompatibleProvider::IsRetriableError(""));
  }
  SUBCASE("Backoff doubles with cap") {
    CHECK(OpenAiCompatibleProvider::RetryDelayMs(1) == 1000);
    CHECK(OpenAiCompatibleProvider::RetryDelayMs(2) == 2000);
    CHECK(OpenAiCompatibleProvider::RetryDelayMs(3) == 4000);
    CHECK(OpenAiCompatibleProvider::RetryDelayMs(99) == 8000);
  }
}

TEST_CASE("ProviderFactory - DisplayName") {
  SUBCASE("Gemini") {
    ProviderConfig config;
    config.provider_id = "gemini";
    CHECK(ai_provider::DisplayName(config) == "Gemini");
  }
  SUBCASE("OpenAI-compatible shows model") {
    ProviderConfig config;
    config.provider_id = "openai-compatible";
    config.model = "space-bunny-free";
    CHECK(ai_provider::DisplayName(config) == "space-bunny-free");
  }
  SUBCASE("OpenAI-compatible without model") {
    ProviderConfig config;
    config.provider_id = "openai-compatible";
    CHECK(ai_provider::DisplayName(config) == "OpenAI-compatible");
  }
  SUBCASE("Unknown falls back to Gemini label") {
    ProviderConfig config;
    config.provider_id = "bogus";
    CHECK(ai_provider::DisplayName(config) == "Gemini");
  }
}

TEST_CASE("ProviderFactory - ResolveProviderConfig") {
  SUBCASE("Defaults to Gemini") {
    AppSettings settings;
    const ProviderConfig config = ai_provider::ResolveProviderConfig(settings);
    CHECK(config.provider_id == "gemini");
    CHECK(std::string_view(ai_provider::ai_provider_defaults::kDefaultGeminiModel) == "gemini-flash-lite-latest");
  }
  SUBCASE("OpenAI-compatible with model default") {
    AppSettings settings;
    settings.aiProvider.providerId = "openai-compatible";
    const ProviderConfig config = ai_provider::ResolveProviderConfig(settings);
    CHECK(config.provider_id == "openai-compatible");
    CHECK(config.model == "deepseek-v4.1-flash");
  }
  SUBCASE("Unknown provider falls back") {
    AppSettings settings;
    settings.aiProvider.providerId = "bogus";
    const ProviderConfig config = ai_provider::ResolveProviderConfig(settings);
    CHECK(config.provider_id == "gemini");
  }
  SUBCASE("Custom timeout passes through") {
    AppSettings settings;
    settings.aiProvider.providerId = "openai-compatible";
    settings.aiProvider.timeoutSeconds = 120;
    const ProviderConfig config = ai_provider::ResolveProviderConfig(settings);
    CHECK(config.timeout_seconds == 120);
  }
  SUBCASE("Zero timeout means default") {
    AppSettings settings;
    settings.aiProvider.providerId = "openai-compatible";
    const ProviderConfig config = ai_provider::ResolveProviderConfig(settings);
    CHECK(config.timeout_seconds == gemini_api_utils::kDefaultTimeoutSeconds);
  }
  SUBCASE("IsKnownProviderId") {
    CHECK(ai_provider::IsKnownProviderId("gemini"));
    CHECK(ai_provider::IsKnownProviderId("openai-compatible"));
    CHECK(!ai_provider::IsKnownProviderId("other"));
  }
}

TEST_CASE("ProviderCatalog - catalog structure") {
  const auto& presets = ai_provider::PresetCatalog();
  REQUIRE(presets.size() >= 5);

  SUBCASE("Every entry names a known protocol id") {
    for (const auto& preset : presets) {
      CHECK(ai_provider::IsKnownProviderId(preset.provider_id));
    }
  }
  SUBCASE("Ids are unique") {
    for (size_t i = 0; i < presets.size(); ++i) {
      for (size_t j = i + 1; j < presets.size(); ++j) {
        CHECK(presets[i].id != presets[j].id);
      }
    }
  }
  SUBCASE("The custom entry exists") {
    CHECK(ai_provider::FindPresetById(ai_provider::preset_defaults::kPresetCustom) != nullptr);
  }
  SUBCASE("Unknown ids are not found") {
    CHECK(ai_provider::FindPresetById("nope") == nullptr);
  }
  SUBCASE("The keyless local entry needs no key") {
    const auto* ollama =
        ai_provider::FindPresetById(ai_provider::preset_defaults::kPresetOllama);
    REQUIRE(ollama != nullptr);
    CHECK(!ollama->needs_api_key);
    CHECK(ollama->api_key_env[0] == '\0');
  }
}

TEST_CASE("ProviderCatalog - ResolvePreset") {
  SUBCASE("A stored preset id wins over the endpoint") {
    ProviderConfig config;
    config.preset_id = ai_provider::preset_defaults::kPresetOllama;
    config.provider_id = "openai-compatible";
    // A local server on a non-default port must still resolve to the local
    // entry instead of the custom one.
    config.base_url = "http://localhost:1234/v1";
    CHECK(std::string_view(ai_provider::ResolvePreset(config).id) == ai_provider::preset_defaults::kPresetOllama);
  }
  SUBCASE("A lookalike host is not mistaken for a known service") {
    ProviderConfig config;
    config.provider_id = "openai-compatible";
    config.base_url = "https://localhost.example.com/v1";
    CHECK(std::string_view(ai_provider::ResolvePreset(config).id) == ai_provider::preset_defaults::kPresetCustom);
    config.base_url = "https://proxy.example.com/api.openai.com/v1";
    CHECK(std::string_view(ai_provider::ResolvePreset(config).id) == ai_provider::preset_defaults::kPresetCustom);
  }
  SUBCASE("Without a stored id the service is derived from the endpoint") {
    ProviderConfig config;
    config.provider_id = "openai-compatible";
    config.base_url = "https://opencode.ai/zen/go/v1";
    CHECK(std::string_view(ai_provider::ResolvePreset(config).id) == ai_provider::preset_defaults::kPresetOpenCode);

    config.base_url = "https://api.openai.com/v1";
    CHECK(std::string_view(ai_provider::ResolvePreset(config).id) == ai_provider::preset_defaults::kPresetOpenAi);

    config.base_url = "http://localhost:11434/v1";
    CHECK(std::string_view(ai_provider::ResolvePreset(config).id) == ai_provider::preset_defaults::kPresetOllama);
  }
  SUBCASE("The Gemini protocol resolves to the Gemini entry") {
    ProviderConfig config;
    config.provider_id = "gemini";
    CHECK(std::string_view(ai_provider::ResolvePreset(config).id) == ai_provider::preset_defaults::kPresetGemini);
  }
  SUBCASE("An unknown endpoint resolves to the custom entry") {
    ProviderConfig config;
    config.provider_id = "openai-compatible";
    config.base_url = "https://example.invalid/v1";
    CHECK(std::string_view(ai_provider::ResolvePreset(config).id) == ai_provider::preset_defaults::kPresetCustom);
  }
  SUBCASE("An unknown stored id falls back to derivation") {
    ProviderConfig config;
    config.preset_id = "stale-preset";
    config.provider_id = "openai-compatible";
    config.base_url = "https://api.openai.com/v1";
    CHECK(std::string_view(ai_provider::ResolvePreset(config).id) == ai_provider::preset_defaults::kPresetOpenAi);
  }
}

TEST_CASE("ProviderCatalog - ApplyPreset") {
  SUBCASE("Writes the connection details") {
    AiProviderSettings settings;
    const auto* opencode =
        ai_provider::FindPresetById(ai_provider::preset_defaults::kPresetOpenCode);
    REQUIRE(opencode != nullptr);
    ai_provider::ApplyPreset(settings, *opencode);
    CHECK(settings.presetId == ai_provider::preset_defaults::kPresetOpenCode);
    CHECK(settings.providerId == "openai-compatible");
    CHECK(settings.baseUrl == "https://opencode.ai/zen/go/v1");
    CHECK(settings.model == "deepseek-v4.1-flash");
    CHECK(settings.apiKeyEnv == "OPENCODE_API_KEY");
  }
  SUBCASE("The custom entry keeps the user's own values") {
    AiProviderSettings settings;
    settings.providerId = "openai-compatible";
    settings.baseUrl = "https://my-gateway.invalid/v1";
    settings.model = "my-model";
    settings.apiKeyEnv = "MY_KEY";
    const auto* custom =
        ai_provider::FindPresetById(ai_provider::preset_defaults::kPresetCustom);
    REQUIRE(custom != nullptr);
    ai_provider::ApplyPreset(settings, *custom);
    CHECK(settings.presetId == ai_provider::preset_defaults::kPresetCustom);
    CHECK(settings.baseUrl == "https://my-gateway.invalid/v1");
    CHECK(settings.model == "my-model");
    CHECK(settings.apiKeyEnv == "MY_KEY");
  }
  SUBCASE("The timeout is a user preference and is never touched") {
    AiProviderSettings settings;
    settings.timeoutSeconds = 120;
    const auto* ollama = ai_provider::FindPresetById(ai_provider::preset_defaults::kPresetOllama);
    REQUIRE(ollama != nullptr);
    ai_provider::ApplyPreset(settings, *ollama);
    CHECK(settings.timeoutSeconds == 120);
  }
}

TEST_CASE("ProviderCatalog - labels and summary") {
  SUBCASE("Summary includes the model") {
    ProviderConfig config;
    config.preset_id = ai_provider::preset_defaults::kPresetOpenCode;
    config.model = "space-bunny-free";
    CHECK(ai_provider::ProviderSummary(config) == "OpenCode · space-bunny-free");
  }
  SUBCASE("Summary omits an empty model") {
    ProviderConfig config;
    config.preset_id = ai_provider::preset_defaults::kPresetGemini;
    config.model.clear();
    CHECK(ai_provider::ProviderSummary(config) == "Google Gemini");
  }
}

TEST_CASE("ProviderCatalog - API key status") {
  SUBCASE("A keyless provider never reports a missing key") {
    const auto* ollama = ai_provider::FindPresetById(ai_provider::preset_defaults::kPresetOllama);
    REQUIRE(ollama != nullptr);
    CHECK(ai_provider::ResolvePresetApiKeyStatus(*ollama) ==
          ai_provider::ApiKeyStatus::NotRequired);

    ProviderConfig config;
    config.preset_id = ai_provider::preset_defaults::kPresetOllama;
    config.provider_id = "openai-compatible";
    config.base_url = "http://localhost:11434/v1";
    CHECK(ai_provider::ResolveApiKeyStatus(config) == ai_provider::ApiKeyStatus::NotRequired);
  }
  SUBCASE("A missing key is reported") {
    const ScopedEnvVar clear_generic(ai_provider::ai_provider_defaults::kEnvGenericAiApiKey, "");
    const ScopedEnvVar clear_openai(ai_provider::ai_provider_defaults::kEnvOpenAiApiKey, "");
    ProviderConfig config;
    config.preset_id = ai_provider::preset_defaults::kPresetOpenAi;
    config.provider_id = "openai-compatible";
    config.base_url = "https://api.openai.com/v1";
    config.api_key_env = "OPENAI_API_KEY";
    CHECK(ai_provider::ResolveApiKeyStatus(config) == ai_provider::ApiKeyStatus::Missing);
  }
  SUBCASE("A configured key is reported") {
    const ScopedEnvVar clear_generic(ai_provider::ai_provider_defaults::kEnvGenericAiApiKey, "");
    const ScopedEnvVar set_openai(ai_provider::ai_provider_defaults::kEnvOpenAiApiKey, "test-key");
    ProviderConfig config;
    config.preset_id = ai_provider::preset_defaults::kPresetOpenAi;
    config.provider_id = "openai-compatible";
    config.base_url = "https://api.openai.com/v1";
    config.api_key_env = "OPENAI_API_KEY";
    CHECK(ai_provider::ResolveApiKeyStatus(config) == ai_provider::ApiKeyStatus::Found);
  }
}

TEST_CASE("ProviderFactory - ResolveProviderConfig carries the preset id") {
  AppSettings settings;
  settings.aiProvider.presetId = "opencode";
  const ProviderConfig config = ai_provider::ResolveProviderConfig(settings);
  CHECK(config.preset_id == "opencode");
  CHECK(std::string_view(ai_provider::ResolvePreset(config).id) == ai_provider::preset_defaults::kPresetOpenCode);
}

TEST_CASE("Timeout bounds have a single source of truth") {
  SUBCASE("The provider aliases the Gemini bounds") {
    static_assert(ai_provider::ai_provider_defaults::kMinTimeoutSeconds ==
                      gemini_api_utils::kMinTimeoutSeconds,
                  "ai_provider must not redefine the minimum timeout");
    static_assert(ai_provider::ai_provider_defaults::kMaxTimeoutSeconds ==
                      gemini_api_utils::kMaxTimeoutSeconds,
                  "ai_provider must not redefine the maximum timeout");
    CHECK(ai_provider::ai_provider_defaults::kMinTimeoutSeconds == gemini_api_utils::kMinTimeoutSeconds);
    CHECK(ai_provider::ai_provider_defaults::kMaxTimeoutSeconds == gemini_api_utils::kMaxTimeoutSeconds);
  }
  SUBCASE("The default timeout is inside the accepted range") {
    CHECK(gemini_api_utils::kDefaultTimeoutSeconds >= gemini_api_utils::kMinTimeoutSeconds);
    CHECK(gemini_api_utils::kDefaultTimeoutSeconds <= gemini_api_utils::kMaxTimeoutSeconds);
  }
  SUBCASE("The shared message is built from the bounds, not hard-coded") {
    const std::string message = gemini_api_utils::BuildTimeoutRangeMessage();
    CHECK(message.find(std::to_string(gemini_api_utils::kMinTimeoutSeconds)) != std::string::npos);
    CHECK(message.find(std::to_string(gemini_api_utils::kMaxTimeoutSeconds)) != std::string::npos);
    // A literal "1-300" would keep passing after a bound changed, so assert the
    // numbers are actually composed from the constants.
    CHECK(message == "Invalid timeout value (must be " +
                         std::to_string(gemini_api_utils::kMinTimeoutSeconds) + "-" +
                         std::to_string(gemini_api_utils::kMaxTimeoutSeconds) + " seconds)");
  }
}

TEST_CASE("AiProviderEditorState - working copy lifecycle") {
  AppSettings settings;
  settings.aiProvider.presetId = "gemini";
  settings.aiProvider.model = "persisted-model";
  settings.aiProvider.timeoutSeconds = 42;

  SUBCASE("The first frame after opening copies the persisted settings") {
    ui::ai_provider_window::AiProviderEditorState editor;
    editor.SyncForFrame(settings);
    CHECK(editor.working.model == "persisted-model");
    CHECK(editor.working.presetId == "gemini");
    CHECK(editor.working.timeoutSeconds == 42);
  }
  SUBCASE("Later frames keep the in-progress edit") {
    ui::ai_provider_window::AiProviderEditorState editor;
    editor.SyncForFrame(settings);
    editor.working.model = "being-typed";
    // A second frame while still open must not re-sync (the user's edit would
    // be thrown away on every frame).
    editor.SyncForFrame(settings);
    CHECK(editor.working.model == "being-typed");
  }
  SUBCASE("Save applies the working copy to settings") {
    ui::ai_provider_window::AiProviderEditorState editor;
    editor.SyncForFrame(settings);
    editor.working.model = "saved-model";
    editor.ApplyTo(settings);
    CHECK(settings.aiProvider.model == "saved-model");
  }
  SUBCASE("Close discards the edit and reopening re-syncs from settings") {
    ui::ai_provider_window::AiProviderEditorState editor;
    editor.SyncForFrame(settings);
    editor.working.model = "discarded-model";

    // Close: nothing is written back; the state is marked closed.
    editor.MarkClosed();
    CHECK(settings.aiProvider.model == "persisted-model");

    // Reopening rebuilds the working copy from the persisted settings, so the
    // discarded edit cannot reappear.
    editor.SyncForFrame(settings);
    CHECK(editor.working.model == "persisted-model");
    CHECK(editor.working.model != "discarded-model");
  }
  SUBCASE("A closed state marks itself closed again idempotently") {
    ui::ai_provider_window::AiProviderEditorState editor;
    editor.SyncForFrame(settings);
    editor.MarkClosed();
    editor.MarkClosed();
    CHECK_FALSE(editor.previous_open);
  }
}

