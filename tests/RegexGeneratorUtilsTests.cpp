#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <string>
#include <string_view>

#include "doctest/doctest.h"
#include "utils/RegexGeneratorUtils.h"

using regex_generator_utils::EscapeRegexSpecialChars;
using regex_generator_utils::GenerateRegexPattern;
using regex_generator_utils::RegexTemplateType;

TEST_SUITE("RegexGeneratorUtils - EscapeRegexSpecialChars") {

  TEST_CASE("escapes dot") {
    CHECK(EscapeRegexSpecialChars("a.b") == "a\\.b");
  }

  TEST_CASE("escapes backslash") {
    CHECK(EscapeRegexSpecialChars("a\\b") == "a\\\\b");
  }

  TEST_CASE("escapes plus star question") {
    CHECK(EscapeRegexSpecialChars("a+b*c?") == "a\\+b\\*c\\?");
  }

  TEST_CASE("escapes anchors") {
    CHECK(EscapeRegexSpecialChars("^start") == "\\^start");
    CHECK(EscapeRegexSpecialChars("end$") == "end\\$");
  }

  TEST_CASE("escapes brackets and braces") {
    CHECK(EscapeRegexSpecialChars("[abc]") == "\\[abc\\]");
    CHECK(EscapeRegexSpecialChars("{2,5}") == "\\{2,5\\}");
  }

  TEST_CASE("escapes parentheses and pipe") {
    CHECK(EscapeRegexSpecialChars("(a|b)") == "\\(a\\|b\\)");
  }

  TEST_CASE("leaves alphanumeric unchanged") {
    CHECK(EscapeRegexSpecialChars("hello") == "hello");
    CHECK(EscapeRegexSpecialChars("file123") == "file123");
  }

  TEST_CASE("empty string returns empty") {
    CHECK(EscapeRegexSpecialChars("") == "");
  }

  TEST_CASE("multiple special chars") {
    CHECK(EscapeRegexSpecialChars(".*+?^${}()|[]") == "\\.\\*\\+\\?\\^\\$\\{\\}\\(\\)\\|\\[\\]");
  }
}

TEST_SUITE("RegexGeneratorUtils - GenerateRegexPattern") {

  TEST_CASE("StartsWith") {
    CHECK(GenerateRegexPattern(RegexTemplateType::StartsWith, "test", "") == "^test.*");
    CHECK(GenerateRegexPattern(RegexTemplateType::StartsWith, "main.", "") == "^main\\..*");
    CHECK(GenerateRegexPattern(RegexTemplateType::StartsWith, "", "") == "");
  }

  TEST_CASE("EndsWith") {
    CHECK(GenerateRegexPattern(RegexTemplateType::EndsWith, ".cpp", "") == ".*\\.cpp$");
    CHECK(GenerateRegexPattern(RegexTemplateType::EndsWith, "file", "") == ".*file$");
    CHECK(GenerateRegexPattern(RegexTemplateType::EndsWith, "", "") == "");
  }

  TEST_CASE("Contains") {
    CHECK(GenerateRegexPattern(RegexTemplateType::Contains, "part", "") == ".*part.*");
    CHECK(GenerateRegexPattern(RegexTemplateType::Contains, "a.b", "") == ".*a\\.b.*");
    CHECK(GenerateRegexPattern(RegexTemplateType::Contains, "", "") == "");
  }

  TEST_CASE("DoesNotContain") {
    CHECK(GenerateRegexPattern(RegexTemplateType::DoesNotContain, "skip", "") == "^(?!.*skip).*$");
    CHECK(GenerateRegexPattern(RegexTemplateType::DoesNotContain, "", "") == "");
  }

  TEST_CASE("FileExtension") {
    CHECK(GenerateRegexPattern(RegexTemplateType::FileExtension, "cpp", "") == ".*\\.(cpp)$");
    CHECK(GenerateRegexPattern(RegexTemplateType::FileExtension, "cpp|h|hpp", "") == ".*\\.(cpp|h|hpp)$");
    CHECK(GenerateRegexPattern(RegexTemplateType::FileExtension, "", "") == "");
  }

  TEST_CASE("NumericPattern") {
    CHECK(GenerateRegexPattern(RegexTemplateType::NumericPattern, "", "") == ".*\\d+.*");
  }

  TEST_CASE("DatePattern") {
    const std::string expected = R"(.*\d{4}[-_]\d{2}[-_]\d{2}.*)";
    CHECK(GenerateRegexPattern(RegexTemplateType::DatePattern, "", "") == expected);
  }

  TEST_CASE("Custom") {
    CHECK(GenerateRegexPattern(RegexTemplateType::Custom, "^custom$", "") == "^custom$");
    CHECK(GenerateRegexPattern(RegexTemplateType::Custom, ".*", "") == ".*");
  }
}
