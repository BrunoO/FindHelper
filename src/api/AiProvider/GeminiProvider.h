#pragma once

#include <future>
#include <string>
#include <string_view>
#include <utility>

#include "api/AiProvider/AiTypes.h"
#include "api/AiProvider/ILlmProvider.h"

namespace ai_provider {

// Gemini adapter: preserves existing behavior verbatim by forwarding to
// gemini_api_utils::GenerateSearchConfigAsync (same URL, x-goog-api-key
// header, candidates[] envelope, validation, timeouts).
class GeminiProvider final : public ILlmProvider {
 public:
  // `model` empty = gemini_api_utils::kGeminiModel.
  explicit GeminiProvider(std::string model = {}) : model_(std::move(model)) {}

  [[nodiscard]] std::string ProviderId() const override;
  [[nodiscard]] std::string DefaultEnvVarName() const override;

  std::future<AiResult> GenerateSearchConfigAsync(
      std::string_view description,
      std::string_view api_key,
      int timeout_seconds) override;

 private:
  std::string model_;
};

}  // namespace ai_provider
