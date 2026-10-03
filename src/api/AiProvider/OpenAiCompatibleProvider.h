#pragma once

#include <future>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "api/AiProvider/AiTypes.h"
#include "api/AiProvider/ILlmProvider.h"

namespace ai_provider {

// OpenAI-compatible adapter: covers opencode-go, opencode-zen,
// OpenAI, and Ollama chat-completions endpoints.
// Transport: POST {base_url}/chat/completions, Bearer auth,
// body {"model","messages":[{"role":"user","content":prompt}]}.
// Envelope: choices[0].message.content (fallback: output_text for
// /responses-style bodies). Inner SearchConfig JSON uses the common parser.
class OpenAiCompatibleProvider final : public ILlmProvider {
 public:
  explicit OpenAiCompatibleProvider(ProviderConfig config);

  [[nodiscard]] std::string ProviderId() const override;
  [[nodiscard]] std::string DefaultEnvVarName() const override;

  std::future<AiResult> GenerateSearchConfigAsync(
      std::string_view description,
      std::string_view api_key,
      int timeout_seconds) override;

  // Pure helpers (unit-tested, no I/O).
  [[nodiscard]] static std::string BuildRequestBody(
      std::string_view prompt, std::string_view model);
  [[nodiscard]] static std::string EndpointUrl(const ProviderConfig& config);
  [[nodiscard]] static std::vector<std::string> BuildHeaders(std::string_view api_key);
  // Session header for opencode endpoints (required: Go rejects requests
  // without a stable x-opencode-session id with HTTP 400). The id is stable
  // for the process lifetime (one app run = one conversation).
  [[nodiscard]] static std::string BuildSessionHeader();
  // True when the endpoint needs the session header (opencode gateways).
  [[nodiscard]] static bool NeedsSessionHeader(std::string_view url);
  // True when a failure is worth retrying: transport faults (network,
  // timeouts), HTTP 429/5xx, and 200-envelope errors with transient markers
  // (server_error, overload, rate limits). 4xx, auth, and parse failures
  // are terminal. Pure (unit-tested).
  [[nodiscard]] static bool IsRetriableError(std::string_view error_message);
  // Backoff before the next attempt after failed_attempt (1-based): base,
  // doubling, capped. Pure (unit-tested).
  [[nodiscard]] static int RetryDelayMs(int failed_attempt);
  [[nodiscard]] static std::string StableSessionId();
  // Extracts assistant text from an OpenAI chat-completions response body.
  // Returns {true, text} or {false, error_message}.
  [[nodiscard]] static std::pair<bool, std::string> ExtractAssistantText(
      std::string_view response_body);

 private:
  ProviderConfig config_;
};

}  // namespace ai_provider
