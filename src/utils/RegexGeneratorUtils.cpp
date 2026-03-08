/**
 * @file utils/RegexGeneratorUtils.cpp
 * @brief Regex generator helpers (escaping and pattern generation from templates)
 */

#include "utils/RegexGeneratorUtils.h"

namespace regex_generator_utils {

std::string EscapeRegexSpecialChars(std::string_view text) {
  std::string result;
  result.reserve(text.size() * 2);
  for (const char c : text) {
    if (c == '.' || c == '\\' || c == '+' || c == '*' || c == '?' || c == '^' || c == '$' ||
        c == '[' || c == ']' || c == '{' || c == '}' || c == '(' || c == ')' || c == '|') {
      result += '\\';
    }
    result += c;
  }
  return result;
}

std::string GenerateRegexPattern(RegexTemplateType template_type, std::string_view param1,
                                 std::string_view param2) {
  (void)param2;
  switch (template_type) {
    // NOLINTNEXTLINE(bugprone-branch-clone) - Cases have similar structure but different pattern generation logic (intentional design)
    case RegexTemplateType::StartsWith: {
      if (param1.empty()) {
        return "";
      }
      return std::string("^") + EscapeRegexSpecialChars(param1) + ".*";
    }

    case RegexTemplateType::EndsWith: {
      if (param1.empty()) {
        return "";
      }
      return std::string(".*") + EscapeRegexSpecialChars(param1) + "$";
    }

    case RegexTemplateType::Contains: {
      if (param1.empty()) {
        return "";
      }
      return std::string(".*") + EscapeRegexSpecialChars(param1) + ".*";
    }

    case RegexTemplateType::DoesNotContain: {
      if (param1.empty()) {
        return "";
      }
      return std::string("^(?!.*") + EscapeRegexSpecialChars(param1) + ").*$";
    }

    case RegexTemplateType::FileExtension: {
      if (param1.empty()) {
        return "";
      }
      return std::string(".*\\.(") + std::string(param1) + ")$";
    }

    case RegexTemplateType::NumericPattern:
      return ".*\\d+.*";

    case RegexTemplateType::DatePattern:
      // NOLINTNEXTLINE(modernize-raw-string-literal) - Escaped string literal is clearer than raw string for regex pattern
      return ".*\\d{4}[-_]\\d{2}[-_]\\d{2}.*";

    case RegexTemplateType::Custom:
      return std::string(param1);

    default:
      return "";
  }
}

}  // namespace regex_generator_utils
