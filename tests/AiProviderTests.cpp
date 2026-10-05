#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include <string>
#include <vector>

#include "api/AiProvider/AiTypes.h"
#include "api/AiProvider/OpenAiCompatibleProvider.h"
#include "api/AiProvider/ProviderFactory.h"
#include "core/Settings.h"
#include "doctest/doctest.h"

using ai_provider::OpenAiCompatibleProvider;
using ai_provider::ProviderConfig;

TEST_CASE("OpenAiCompatibleProvider - EndpointUrl") {
  SUBCASE("Default base URL") {
    ProviderConfig config;
    CHECK(OpenAiCompatibleProvider::EndpointUrl(config) ==
          "https://opencode.ai/inference/openai/v1/chat/completions");
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
  }
  SUBCASE("OpenAI-compatible with model default") {
    AppSettings settings;
    settings.aiProvider.providerId = "openai-compatible";
    const ProviderConfig config = ai_provider::ResolveProviderConfig(settings);
    CHECK(config.provider_id == "openai-compatible");
    CHECK(config.model == "space-bunny-free");
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
