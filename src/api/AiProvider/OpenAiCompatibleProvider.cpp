#include "api/AiProvider/OpenAiCompatibleProvider.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <future>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "api/AiProvider/LlmHttp.h"
#include "api/GeminiApiHttp.h"
#include "api/GeminiApiUtils.h"
#include "utils/Logger.h"

using nlohmann::json;

namespace ai_provider {

namespace {
// Appends actionable guidance when the gateway rejects free-tier models for
// third-party clients (HTTP 403 FreeTierError). Keeps the raw body intact.
std::string AnnotateFreeTierError(const std::string& error_message) {
  if (error_message.find("FreeTierError") == std::string::npos) {
    return error_message;
  }
  std::string annotated = error_message;
  annotated += " - free models only work inside OpenCode; set aiProvider.model to a subscription model (e.g. ";
  annotated += ai_provider_defaults::kDefaultOpenAiModel;
  annotated += ")";
  return annotated;
}

// Retry policy: 1 initial attempt + 2 retries; timeout_seconds stays
// per-attempt (documented); worst case ~3x timeout + 3s backoff on the
// worker thread (UI stays responsive, button shows Generating...).
constexpr int kMaxAttempts = 3;
constexpr int kRetryBaseDelayMs = 1000;
constexpr int kRetryMaxDelayMs = 8000;

// Outcome of a single attempt: done=true carries the terminal result;
// done=false means the failure is retriable (result holds the last error).
struct AttemptOutcome {
  bool done = true;
  AiResult result;
};

// Error envelope: {"error": "msg"} or {"error": {"message": "msg"}}.
std::string ExtractEnvelopeErrorMessage(const json& response_json) {
  std::string message = "Unknown error";
  if (const auto& error = response_json.at("error"); error.is_string()) {
    message = error.get<std::string>();
  } else if (error.is_object() && error.contains("message") && error.at("message").is_string()) {
    message = error.at("message").get<std::string>();
  }
  return message;
}

// Content parts envelope: concatenate text parts, ignore non-text parts.
std::string CombineContentParts(const json& content) {
  std::string combined;
  for (const auto& part : content) {
    if (part.is_object() && part.contains("text") && part.at("text").is_string()) {
      combined += part.at("text").get<std::string>();
    } else if (part.is_string()) {
      combined += part.get<std::string>();
    }
  }
  return combined;
}

// Choices envelope probe: present=false means no usable choices array (caller
// tries the next shape); otherwise ok carries the outcome and text the text
// or the error message.
struct ChoicesOutcome {
  bool present = false;
  bool ok = false;
  std::string text;
};

ChoicesOutcome ExtractChoicesText(const json& response_json) {
  ChoicesOutcome outcome;
  if (!response_json.contains("choices") || !response_json.at("choices").is_array() ||
      response_json.at("choices").empty()) {
    return outcome;
  }
  outcome.present = true;
  const auto& choice = response_json.at("choices").at(0);
  if (choice.contains("message") && choice.at("message").contains("content")) {
    if (const auto& content = choice.at("message").at("content"); content.is_string()) {
      if (std::string text = content.get<std::string>(); !text.empty()) {
        outcome.ok = true;
        outcome.text = std::move(text);
        return outcome;
      }
    } else if (content.is_array() && !content.empty()) {
      // Some gateways return content parts; concatenate text parts.
      if (std::string combined = CombineContentParts(content); !combined.empty()) {
        outcome.ok = true;
        outcome.text = std::move(combined);
        return outcome;
      }
    }
  }
  // /responses-style fallback inside choices wrapper.
  if (choice.contains("text") && choice.at("text").is_string()) {
    outcome.ok = true;
    outcome.text = choice.at("text").get<std::string>();
    return outcome;
  }
  outcome.text = "Invalid JSON structure: missing 'choices[0].message.content'";
  return outcome;
}

AttemptOutcome TryRequestOnce(const std::string& url,
                              const std::vector<std::string>& headers,
                              const std::string& body,
                              int timeout_seconds) {
  AttemptOutcome outcome;
  auto [ok, payload] = llm_http::CallLlmHttpPost(url, headers, body, timeout_seconds);
  if (!ok) {
    outcome.result.error_message = AnnotateFreeTierError(payload);
    outcome.done = !OpenAiCompatibleProvider::IsRetriableError(payload);
    return outcome;
  }
  auto [extract_ok, text] = OpenAiCompatibleProvider::ExtractAssistantText(payload);
  if (!extract_ok) {
    outcome.result.error_message = std::move(text);
    outcome.done = !OpenAiCompatibleProvider::IsRetriableError(outcome.result.error_message);
    return outcome;
  }
  // Inner-JSON parse failures are terminal: retrying cannot fix model output.
  outcome.result = gemini_api_utils::ParseSearchConfigJson(text);
  outcome.done = true;
  return outcome;
}
}  // namespace

OpenAiCompatibleProvider::OpenAiCompatibleProvider(ProviderConfig config)
    : config_(std::move(config)) {}

std::string OpenAiCompatibleProvider::ProviderId() const {
  return {ai_provider_defaults::kProviderOpenAiCompatible};
}

std::string OpenAiCompatibleProvider::DefaultEnvVarName() const {
  if (!config_.api_key_env.empty()) {
    return config_.api_key_env;
  }
  return {ai_provider_defaults::kEnvOpencodeApiKey};
}

std::string OpenAiCompatibleProvider::BuildRequestBody(
    std::string_view prompt, std::string_view model) {
  const std::string escaped = gemini_api_utils::EscapeJsonString(prompt);
  const std::string model_str(model);
  const std::string escaped_model = gemini_api_utils::EscapeJsonString(model_str);
  std::ostringstream body;
  body << R"({"model":")" << escaped_model
       << R"(","messages":[{"role":"user","content":")" << escaped << R"("}]})";
  return body.str();
}

std::string OpenAiCompatibleProvider::EndpointUrl(const ProviderConfig& config) {
  std::string base = config.base_url.empty()
                         ? ai_provider_defaults::kDefaultOpenAiBaseUrl
                         : config.base_url;
  while (!base.empty() && base.back() == '/') {
    base.pop_back();
  }
  return base + ai_provider_defaults::kDefaultOpenAiChatPath;
}

std::vector<std::string> OpenAiCompatibleProvider::BuildHeaders(std::string_view api_key) {
  std::vector<std::string> headers;
  headers.reserve(3);
  headers.emplace_back("Content-Type: application/json");
  // Local servers (Ollama) need no key: omit the header rather than sending
  // an empty bearer. Hosted providers answer 401, surfaced terminally.
  if (!api_key.empty()) {
    headers.emplace_back("Authorization: Bearer " + std::string(api_key));
  }
  return headers;
}

bool OpenAiCompatibleProvider::NeedsSessionHeader(std::string_view url) {
  return url.find("opencode.ai") != std::string_view::npos;
}

std::string OpenAiCompatibleProvider::StableSessionId() {
  static const std::string session_id = [] {
    std::random_device device;
    std::mt19937_64 engine(device());
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < 4; ++i) {
      oss << std::setw(16) << engine();
    }
    return oss.str();
  }();
  return session_id;
}

std::string OpenAiCompatibleProvider::BuildSessionHeader() {
  return "x-opencode-session: " + StableSessionId();
}

bool OpenAiCompatibleProvider::IsRetriableError(std::string_view error_message) {
  if (error_message.find("Network error:") != std::string_view::npos ||
      error_message.find("Request timeout") != std::string_view::npos) {
    return true;
  }
  if (constexpr std::string_view kHttpErrorPrefix = "HTTP error";
      error_message.compare(0, kHttpErrorPrefix.size(), kHttpErrorPrefix) == 0) {
    // Tolerates "HTTP error <code>: ..." and "HTTP error: <code> - ...".
    std::string_view rest = error_message.substr(kHttpErrorPrefix.size());
    while (!rest.empty() && (rest.front() < '0' || rest.front() > '9')) {
      rest.remove_prefix(1);
    }
    int code = 0;
    while (!rest.empty() && rest.front() >= '0' && rest.front() <= '9') {
      code = code * 10 + (rest.front() - '0');
      rest.remove_prefix(1);
    }
    // 429 is rate limiting; 5xx is a server-side fault. Both are transient, so a
    // retry can plausibly succeed. HTTP status codes are three digits, so the 5xx
    // class is exactly 500-599.
    constexpr int kHttpTooManyRequests = 429;
    constexpr int kHttpServerErrorFirst = 500;
    constexpr int kHttpServerErrorLast = 599;
    return code == kHttpTooManyRequests ||
           (code >= kHttpServerErrorFirst && code <= kHttpServerErrorLast);
  }
  // 200-envelope API errors with transient markers.
  static constexpr std::array<std::string_view, 7> kRetriableMarkers = {
      "server_error", "Upstream request failed", "overloaded",
      "rate_limit", "Rate limit", "temporarily unavailable", "try again later"};
  const auto is_retriable_marker = [&error_message](std::string_view marker) {
    return error_message.find(marker) != std::string_view::npos;
  };
  return std::any_of(kRetriableMarkers.begin(), kRetriableMarkers.end(),
                     is_retriable_marker);
}

int OpenAiCompatibleProvider::RetryDelayMs(int failed_attempt) {
  int delay_ms = kRetryBaseDelayMs;
  for (int i = 1; i < failed_attempt; ++i) {
    delay_ms = (std::min)(delay_ms * 2, kRetryMaxDelayMs);
  }
  return (std::min)(delay_ms, kRetryMaxDelayMs);
}

std::pair<bool, std::string> OpenAiCompatibleProvider::ExtractAssistantText(
    std::string_view response_body) {
  if (response_body.empty()) {
    return {false, "Empty response from API"};
  }
  try {
    const json response_json = json::parse(response_body);
    if (response_json.contains("error")) {
      return {false, "API error: " + ExtractEnvelopeErrorMessage(response_json)};
    }
    if (ChoicesOutcome choices = ExtractChoicesText(response_json); choices.present) {
      return {choices.ok, std::move(choices.text)};
    }
    if (response_json.contains("output_text") && response_json.at("output_text").is_string()) {
      return {true, response_json.at("output_text").get<std::string>()};
    }
    return {false, "Invalid JSON structure: missing 'choices' or 'output_text'"};
  } catch (const json::parse_error& e) {
    (void)e;
    std::string error_message = "JSON parse error: ";
    error_message += e.what();
    return {false, error_message};
  } catch (const json::exception& e) {
    (void)e;
    std::string error_message = "JSON error: ";
    error_message += e.what();
    return {false, error_message};
  } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - safety net after json::* types
    (void)e;
    std::string error_message = "Unexpected error: ";
    error_message += e.what();
    return {false, error_message};
  }
}

std::future<AiResult> OpenAiCompatibleProvider::GenerateSearchConfigAsync(
    std::string_view description,
    std::string_view api_key,
    int timeout_seconds) {
  ProviderConfig config = config_;
  const std::string desc_str(description);
  std::string key_str(api_key);
  if (key_str.empty()) {
    key_str = gemini_api_utils::GetEnvVarString(DefaultEnvVarName());
    if (key_str.empty() && DefaultEnvVarName() != ai_provider_defaults::kEnvGenericAiApiKey) {
      key_str = gemini_api_utils::GetEnvVarString(ai_provider_defaults::kEnvGenericAiApiKey);
    }
  }
  // Empty keys are allowed: local servers (Ollama) need no auth; hosted
  // providers answer 401, which IsRetriableError treats as terminal.
  if (!desc_str.empty() && config.model.empty()) {
    config.model = ai_provider_defaults::kDefaultOpenAiModel;
  }

  // Single user-triggered network call; future stored, polled per-frame, cleaned on all paths (mirrors GenerateSearchConfigAsync justification)
  return std::async(std::launch::async, [config, desc_str, key_str, timeout_seconds] {  // NOSONAR(cpp:S8460) - single user-triggered network call; future stored, polled per-frame, cleaned on all paths
    if (desc_str.empty()) {
      LOG_ERROR_BUILD("OpenAI-compatible call failed: user description is empty");
      AiResult result;
      result.error_message = "User description is empty";
      return result;
    }
    if (timeout_seconds < 1 || timeout_seconds > 300) {
      AiResult result;
      result.error_message = "Invalid timeout value (must be 1-300 seconds)";
      return result;
    }
    const std::string prompt = gemini_api_utils::BuildSearchConfigPrompt(desc_str);
    const std::string url = EndpointUrl(config);
    const std::string body = BuildRequestBody(prompt, config.model);
    std::vector<std::string> headers = BuildHeaders(key_str);
    if (NeedsSessionHeader(url)) {
      headers.push_back(BuildSessionHeader());
    }

    AiResult last_result;
    for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
      auto [done, outcome] = TryRequestOnce(url, headers, body, timeout_seconds);
      last_result = std::move(outcome);
      if (done || attempt == kMaxAttempts) {
        if (!done) {
          last_result.error_message += " (after " + std::to_string(kMaxAttempts) + " attempts)";
        }
        return last_result;
      }
      LOG_WARNING_BUILD("OpenAI-compatible call failed (attempt " << attempt << "/" << kMaxAttempts
                        << "): " << last_result.error_message << " - retrying");
      std::this_thread::sleep_for(std::chrono::milliseconds(RetryDelayMs(attempt)));
    }
    return last_result;
  });
}

}  // namespace ai_provider
