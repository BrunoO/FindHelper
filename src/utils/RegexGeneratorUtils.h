#pragma once

#include <string>
#include <string_view>

namespace regex_generator_utils {

/**
 * Template types for regex pattern generation.
 * Used by the regex generator popup to provide common pattern templates.
 */
// NOLINTNEXTLINE(performance-enum-size) - Enum size optimization not needed for this small enum (8 values)
enum class RegexTemplateType {
  StartsWith,
  EndsWith,
  Contains,
  DoesNotContain,
  FileExtension,
  NumericPattern,
  DatePattern,
  Custom
};

/**
 * Escapes special regex characters in a string.
 * Prepends backslash to special regex characters to make them literal.
 *
 * @param text Input text to escape
 * @return Text with special characters escaped
 */
std::string EscapeRegexSpecialChars(std::string_view text);

/**
 * Generates a regex pattern from a template type and parameters.
 *
 * @param template_type Type of regex template to generate
 * @param param1 First parameter (text to match, pattern, etc.)
 * @param param2 Second parameter (unused for most templates)
 * @return Generated regex pattern string, or empty string on error
 */
std::string GenerateRegexPattern(RegexTemplateType template_type, std::string_view param1,
                                 std::string_view param2);

}  // namespace regex_generator_utils
