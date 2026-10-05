#pragma once

#include <future>
#include <memory>
#include <string>
#include <string_view>

#include "api/AiProvider/AiTypes.h"

namespace ai_provider {

// Strategy interface for LLM search-config providers. Each adapter knows
// its endpoint, auth scheme, request body, and envelope extraction; prompt
// building (BuildSearchConfigPrompt) and inner-JSON parsing
// (ParseSearchConfigJson) stay common in gemini_api_utils.
class ILlmProvider {
 public:
  virtual ~ILlmProvider() = default;

  [[nodiscard]] virtual std::string ProviderId() const = 0;
  [[nodiscard]] virtual std::string DefaultEnvVarName() const = 0;

  virtual std::future<AiResult> GenerateSearchConfigAsync(
      std::string_view description,
      std::string_view api_key,
      int timeout_seconds) = 0;
};

}  // namespace ai_provider
