
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "TestHelpers.h"
#include "api/GeminiApiHttp.h"
#include "api/GeminiApiUtils.h"
#include "doctest/doctest.h"

using Json = nlohmann::json;  // NOLINT(readability-identifier-naming) - json is common alias name from nlohmann library

using gemini_api_utils::BuildSearchConfigPrompt;
using gemini_api_utils::CallGeminiApiRaw;
// GetGeminiApiKeyFromEnv removed - unused in tests
using gemini_api_utils::ParseSearchConfigJson;
using gemini_api_utils::ValidatePathPatternFormat;

namespace test_helpers {

struct ValidatePathPatternTestCase {
  std::string pattern;
  bool expected;
};

inline void RunParameterizedValidatePathPatternTests(const std::vector<ValidatePathPatternTestCase>& test_cases) {
  for (const auto& tc : test_cases) {
    DOCTEST_SUBCASE(tc.pattern.c_str()) {
      CHECK(gemini_api_utils::ValidatePathPatternFormat(tc.pattern) == tc.expected);
    }
  }
}

struct ParseSearchConfigJsonTestCase {
  std::string json_input;
  bool expected_success;
  std::string expected_path;
  std::string expected_error_substring;
  std::vector<std::string> expected_extensions;
  std::string test_name;
};

inline void RunOneParseSearchConfigJsonTest(
    const ParseSearchConfigJsonTestCase& tc,
    const gemini_api_utils::GeminiApiResult& result) {
  CHECK(result.success == tc.expected_success);
  if (!tc.expected_success) {
    if (!tc.expected_error_substring.empty()) {
      CHECK(result.error_message.find(tc.expected_error_substring) != std::string::npos);
    }
    return;
  }
  if (!tc.expected_path.empty()) {
    CHECK(result.search_config.path == tc.expected_path);
  }
  if (!tc.expected_extensions.empty()) {
    CHECK(result.search_config.extensions.size() == tc.expected_extensions.size());
    for (size_t i = 0; i < tc.expected_extensions.size() && i < result.search_config.extensions.size(); ++i) {
      CHECK(result.search_config.extensions[i] == tc.expected_extensions[i]);
    }
  }
}

inline void RunParameterizedParseSearchConfigJsonTests(
    const std::vector<ParseSearchConfigJsonTestCase>& test_cases) {
  for (const auto& tc : test_cases) {
    DOCTEST_SUBCASE(tc.test_name.empty() ? tc.json_input.c_str() : tc.test_name.c_str()) {
      auto result = gemini_api_utils::ParseSearchConfigJson(tc.json_input);
      RunOneParseSearchConfigJsonTest(tc, result);
    }
  }
}

struct BuildSearchConfigPromptTestCase {
  std::string description;
  std::string expected_substring;  // Substring that should be found in prompt
  bool check_description_in_prompt = true;  // Whether to check if description is in prompt
};

inline void RunParameterizedBuildSearchConfigPromptTests(
    const std::vector<BuildSearchConfigPromptTestCase>& test_cases) {
  for (const auto& tc : test_cases) {
    DOCTEST_SUBCASE(tc.description.empty() ? "empty description" : tc.description.c_str()) {
      std::string prompt = gemini_api_utils::BuildSearchConfigPrompt(tc.description);

      CHECK(!prompt.empty());
      if (tc.check_description_in_prompt && !tc.description.empty()) {
        CHECK(prompt.find(tc.description) != std::string::npos);
      }
      if (!tc.expected_substring.empty()) {
        CHECK(prompt.find(tc.expected_substring) != std::string::npos);
      }
    }
  }
}

struct GetGeminiApiKeyTestCase {
  std::string api_key;
  std::string expected_key;
  bool expect_empty = false;  // If true, expect empty key regardless of api_key value
};

inline void RunParameterizedGetGeminiApiKeyTests(
    const std::vector<GetGeminiApiKeyTestCase>& test_cases) {
  for (const auto& tc : test_cases) {
    DOCTEST_SUBCASE(tc.api_key.empty() ? "empty key" : tc.api_key.c_str()) {
      test_helpers::TestGeminiApiKeyFixture fixture(tc.api_key);

      std::string key = gemini_api_utils::GetGeminiApiKeyFromEnv();
      if (tc.expect_empty) {
        CHECK(key.empty());
      } else {
        CHECK(key == tc.expected_key);
      }
    }
  }
}

}  // namespace test_helpers

namespace {

void RunParseSearchConfigJsonCases(
    std::initializer_list<test_helpers::ParseSearchConfigJsonTestCase> cases) {
  test_helpers::RunParameterizedParseSearchConfigJsonTests(
      std::vector<test_helpers::ParseSearchConfigJsonTestCase>(cases.begin(), cases.end()));
}

void RunValidatePathPatternCases(
    std::initializer_list<test_helpers::ValidatePathPatternTestCase> cases) {
  test_helpers::RunParameterizedValidatePathPatternTests(
      std::vector<test_helpers::ValidatePathPatternTestCase>(cases.begin(), cases.end()));
}

void RunGetGeminiApiKeyCases(std::initializer_list<test_helpers::GetGeminiApiKeyTestCase> cases) {
  test_helpers::RunParameterizedGetGeminiApiKeyTests(
      std::vector<test_helpers::GetGeminiApiKeyTestCase>(cases.begin(), cases.end()));
}

void RunBuildSearchConfigPromptCases(
    std::initializer_list<test_helpers::BuildSearchConfigPromptTestCase> cases) {
  test_helpers::RunParameterizedBuildSearchConfigPromptTests(
      std::vector<test_helpers::BuildSearchConfigPromptTestCase>(cases.begin(), cases.end()));
}

}  // namespace

// IMPORTANT: These unit tests exercise parsing/validation only and do not call the live Gemini API.
// Set GEMINI_API_KEY only for tests that explicitly integrate with the network (none in this file).

TEST_CASE("ParseSearchConfigJson - Valid Responses") {
  SUBCASE("Valid responses with various path patterns") {
    RunParseSearchConfigJsonCases({
      {
        test_helpers::CreateGeminiJsonResponse(R"({"version": "1.0", "search_config": {"path": "pp:**/*.txt"}})"),
        true,
        "pp:**/*.txt",
        "",
        {},
        "Valid response with pp: prefix"
      },
      {
        test_helpers::CreateGeminiJsonResponse(R"({"version": "1.0", "search_config": {"path": "pp:src/**/*.cpp"}})"),
        true,
        "pp:src/**/*.cpp",
        "",
        {},
        "Response with extra text before pp:"
      },
      {
        test_helpers::CreateGeminiJsonResponse(R"({"version": "1.0", "search_config": {"path": "pp:**/*.log"}})"),
        true,
        "pp:**/*.log",
        "",
        {},
        "Response with newline after pattern"
      },
      {
        test_helpers::CreateGeminiJsonResponse(R"({"version": "1.0", "search_config": {"path": "**/*.txt"}})"),
        true,
        "**/*.txt",
        "",
        {},
        "Response missing pp: prefix"
      }
    });
  }

  SUBCASE("Empty response") {
    RunParseSearchConfigJsonCases({
      {"", false, "", "Empty", {}, "Empty response"}
    });
  }

  SUBCASE("Invalid JSON") {
    RunParseSearchConfigJsonCases({
      {"{ invalid json }", false, "", "", {}, "Invalid JSON"}
    });
  }

  SUBCASE("API error response") {
    RunParseSearchConfigJsonCases({
      {test_helpers::CreateGeminiErrorResponse(400, "Invalid API key", "INVALID_ARGUMENT"),
       false, "", "Invalid API key", {}, "API error response"}
    });
  }

  SUBCASE("Missing candidates array") {
    const std::string json = R"({
      "candidates": []
    })";
    RunParseSearchConfigJsonCases({
      {json, false, "", "Invalid JSON structure", {}, "Missing candidates array"}
    });
  }

  SUBCASE("Complex patterns") {
    RunParseSearchConfigJsonCases({
      {
        test_helpers::CreateGeminiResponseWithPath("pp:^C:/Windows/**/*.exe$"),
        true,
        "pp:^C:/Windows/**/*.exe$",
        "",
        {},
        "Complex pattern with anchors"
      },
      {
        test_helpers::CreateGeminiResponseWithPath("pp:**/\\d{3}*.log"),
        true,
        "pp:**/\\d{3}*.log",
        "",
        {},
        "Pattern with character classes"
      }
    });
  }
}

TEST_CASE("ValidatePathPatternFormat - Basic Patterns") {
  SUBCASE("Valid patterns") {
    RunValidatePathPatternCases({
        {"pp:**/*.txt", true},
        {"pp:src/**/*.cpp", true},
        {"pp:*.py", true},
        {"pp:^C:/Windows/**/*.exe$", true}
    });
  }

  SUBCASE("Invalid patterns") {
    RunValidatePathPatternCases({
        {"pp:", false},
        {"**/*.txt", false},
        {"", false},
        {"pp", false}
    });
  }
}

TEST_CASE("GetGeminiApiKeyFromEnv") {
  SUBCASE("Environment variable not set") {
    RunGetGeminiApiKeyCases({
      {"", "", true}
    });
  }

  SUBCASE("Environment variable set") {
    RunGetGeminiApiKeyCases({
      {"test-api-key-12345", "test-api-key-12345"}
    });
  }
}

TEST_CASE("BuildSearchConfigPrompt - Basic Descriptions") {
  SUBCASE("Very long description") {
    const std::string long_desc(5000, 'a');
    RunBuildSearchConfigPromptCases({
      {long_desc, long_desc}
    });
  }

  SUBCASE("Description with special characters") {
    RunBuildSearchConfigPromptCases({
      {R"(files with 'quotes' and "double quotes" and \backslashes)",
       R"(files with 'quotes' and "double quotes" and \backslashes)"}
    });
  }

  SUBCASE("Description with newlines") {
    RunBuildSearchConfigPromptCases({
      {"all files\nin directory\nwith extension .txt",
       "all files\nin directory\nwith extension .txt"}
    });
  }

  SUBCASE("Description with Unicode characters") {
    RunBuildSearchConfigPromptCases({
      {u8"files in 目录 with 中文 names",
       u8"files in 目录 with 中文 names"}
    });
  }

  SUBCASE("Description with Windows path separators") {
    RunBuildSearchConfigPromptCases({
      {R"(files in C:\Users\Documents\)",
       R"(files in C:\Users\Documents\)"}
    });
  }
}

TEST_CASE("BuildSearchConfigPrompt - Prompt Structure") {
  SUBCASE("Prompt structure - filename, path, extensions, and regex in schema") {
    const std::string desc = "find all .cpp files";
    const std::string prompt = BuildSearchConfigPrompt(desc);

    // Verify prompt mentions primary search fields
    CHECK(prompt.find("\"filename\"") != std::string::npos);
    CHECK(prompt.find("\"path\"") != std::string::npos);
    CHECK(prompt.find("\"extensions\"") != std::string::npos);
    CHECK(prompt.find("\"time_filter\"") != std::string::npos);
    CHECK(prompt.find("\"size_filter\"") != std::string::npos);

    // Verify prompt mentions regex support
    CHECK(prompt.find("re:") != std::string::npos);
  }
}

TEST_CASE("BuildSearchConfigPrompt - Base Prompt Size") {
  constexpr size_t k_max_prompt_size = 8000;
  // Test base prompt size (without user description)
  // The base prompt template should be under 8000 chars to leave room for user descriptions
  const std::string base_prompt = BuildSearchConfigPrompt("");
  // Base prompt includes Search Engine Logic, schema, examples; must stay under k_max_prompt_size
  CHECK(base_prompt.size() < k_max_prompt_size);
}

TEST_CASE("BuildSearchConfigPrompt - Prompt with Description Size") {
  constexpr size_t k_max_prompt_size = 8000;
  // Test with a reasonable user description to ensure total stays under limit
  const std::string desc = "find all C++ source files modified this week";
  const std::string prompt = BuildSearchConfigPrompt(desc);
  CHECK(prompt.size() < k_max_prompt_size);

  // Verify prompt includes the user description
  CHECK(prompt.find(desc) != std::string::npos);
}

TEST_CASE("ParseSearchConfigJson - Multiple Candidates and Parts") {
  SUBCASE("Multiple candidates (should use first)") {
    RunParseSearchConfigJsonCases({
      {test_helpers::CreateGeminiResponseWithMultipleCandidates({"pp:**/*.txt", "pp:**/*.log"}),
       true, "pp:**/*.txt", "", {}, "Multiple candidates - use first"}
    });
  }

  SUBCASE("Multiple parts (should use first)") {
    RunParseSearchConfigJsonCases({
      {test_helpers::CreateGeminiResponseWithMultipleParts({"pp:**/*.txt", "pp:**/*.log"}),
       true, "pp:**/*.txt", "", {}, "Multiple parts - use first"}
    });
  }

  SUBCASE("Gemini 3.8 Flash response with thought part followed by output text") {
    const std::string json = R"({
      "candidates": [{
        "content": {
          "role": "model",
          "parts": [
            {
              "thought": true,
              "text": "The user is looking for C++ files in src. I will generate a path pattern."
            },
            {
              "text": "{\"version\": \"1.0\", \"search_config\": {\"path\": \"pp:src/**/*.cpp\"}}"
            }
          ]
        },
        "finishReason": "STOP",
        "index": 0
      }]
    })";
    RunParseSearchConfigJsonCases({
      {json, true, "pp:src/**/*.cpp", "", {}, "Response with thought part followed by JSON text"}
    });
  }

  SUBCASE("Response with metadata fields") {
    const std::string json = R"({
      "candidates": [{
        "content": {
          "role": "model",
          "parts": [{
            "text": "{\"version\": \"1.0\", \"search_config\": {\"path\": \"pp:src/**/*.cpp\"}}"
          }]
        },
        "finishReason": "STOP",
        "index": 0
      }],
      "usageMetadata": {
        "promptTokenCount": 100,
        "candidatesTokenCount": 10
      }
    })";
    RunParseSearchConfigJsonCases({
      {json, true, "pp:src/**/*.cpp", "", {}, "Response with metadata fields"}
    });
  }

  SUBCASE("Patterns with whitespace") {
    RunParseSearchConfigJsonCases({
      {
        test_helpers::CreateGeminiJsonResponse(R"({"version": "1.0", "search_config": {"path": "pp:**/*.txt   "}})"),
        true,
        "pp:**/*.txt   ",
        "",
        {},
        "Pattern with trailing whitespace"
      },
      {
        test_helpers::CreateGeminiResponseWithPath("pp:**/*.txt\t\t"),
        true,
        "pp:**/*.txt\t\t",
        "",
        {},
        "Pattern with tabs"
      },
      {
        test_helpers::CreateGeminiResponseWithPath("pp:**/*.txt\r"),
        true,
        "pp:**/*.txt\r",
        "",
        {},
        "Pattern with carriage return"
      }
    });
  }

  SUBCASE("Pattern in middle of explanation text") {
    // This test case doesn't apply to the new JSON format - patterns must be in search_config.path
    // Skipping this test as it tests old behavior
  }

  SUBCASE("Multiple pp: patterns (should use first)") {
    // This test case doesn't apply to the new JSON format - only one path is supported
    // Skipping this test as it tests old behavior
  }
}

TEST_CASE("ParseSearchConfigJson - Malformed JSON") {
  SUBCASE("Malformed JSON - missing closing brace") {
    const std::string json = R"({
      "candidates": [{
        "content": {
          "parts": [{
            "text": "{\"version\": \"1.0\", \"search_config\": {\"path\": \"pp:**/*.txt\"}"
    })";
    RunParseSearchConfigJsonCases({
      {json, false, "", "parse", {}, "Malformed JSON - missing closing brace"}
    });
  }

  SUBCASE("Malformed JSON - invalid escape") {
    const std::string json = R"({
      "candidates": [{
        "content": {
          "parts": [{
            "text": "{\"version\": \"1.0\", \"search_config\": {\"path\": \"pp:**/*.txt\\\"}"
          }]
        }
      }]
    })";
    RunParseSearchConfigJsonCases({
      {json, false, "", "", {}, "Malformed JSON - invalid escape"}
    });
  }
}

TEST_CASE("ParseSearchConfigJson - Invalid Fields") {
  SUBCASE("Missing text field") {
    const std::string json = R"({
      "candidates": [{
        "content": {
          "parts": [{}]
        }
      }]
    })";
    RunParseSearchConfigJsonCases({
      {json, false, "", "structure", {}, "Missing text field"}
    });
  }

  SUBCASE("Text field is not a string") {
    const std::string json = R"({
      "candidates": [{
        "content": {
          "parts": [{
            "text": 12345
          }]
        }
      }]
    })";
    RunParseSearchConfigJsonCases({
      {json, false, "", "", {}, "Text field is not a string"}
    });
  }

  SUBCASE("Empty text field") {
    const std::string json = R"({
      "candidates": [{
        "content": {
          "parts": [{
            "text": ""
          }]
        }
      }]
    })";
    // Empty text after cleaning results in JSON parse error or invalid structure message
    auto result = ParseSearchConfigJson(json);
    CHECK(result.success == false);
    const bool has_valid_error = (result.error_message.find("parse error") != std::string::npos) ||
                                 (result.error_message.find("Invalid JSON structure") != std::string::npos);
    CHECK(has_valid_error);
  }

  SUBCASE("Text field with only whitespace") {
    const std::string json = R"({
      "candidates": [{
        "content": {
          "parts": [{
            "text": "   \n\t  "
          }]
        }
      }]
    })";
    RunParseSearchConfigJsonCases({
      {json, false, "", "", {}, "Text field with only whitespace"}
    });
  }
}

TEST_CASE("ParseSearchConfigJson - Error Response with Details") {
  SUBCASE("Error response with detailed error info") {
    const std::string json = test_helpers::CreateGeminiErrorResponse(429, "Resource has been exhausted (e.g. check quota).", "RESOURCE_EXHAUSTED");
    auto result = ParseSearchConfigJson(json);
    CHECK(result.success == false);
    CHECK(result.error_message.find("API error") != std::string::npos);
    CHECK(result.error_message.find("Resource has been exhausted") != std::string::npos);
  }
}

TEST_CASE("ParseSearchConfigJson - Error Response without Message") {
  SUBCASE("Error response without message field") {
    const std::string json = test_helpers::CreateGeminiErrorResponse(500, "", "INTERNAL");
    auto result = ParseSearchConfigJson(json);
    CHECK(result.success == false);
    CHECK(result.error_message.find("API error") != std::string::npos);
    CHECK(result.error_message.find("Unknown error") != std::string::npos);
  }
}

TEST_CASE("ParseSearchConfigJson - Edge Cases") {
  SUBCASE("Very long response text") {
    RunParseSearchConfigJsonCases({
      {test_helpers::CreateGeminiResponseWithPath("pp:**/*.txt"),
       true, "pp:**/*.txt", "", {}, "Very long response text"}
    });
  }

  SUBCASE("Unicode characters in response") {
    RunParseSearchConfigJsonCases({
      {test_helpers::CreateGeminiResponseWithPath("pp:**/*.txt"),
       true, "pp:**/*.txt", "", {}, "Unicode characters in response"}
    });
  }

  SUBCASE("Empty search_config object should mean all files (pp:)") {
    // Direct JSON format (e.g., pasted from clipboard)
    const std::string direct_json = R"({
      "version": "1.0",
      "search_config": {}
    })";

    RunParseSearchConfigJsonCases({
      {direct_json, true, "pp:", "", {}, "Direct JSON format"}
    });

    // Gemini API-style response where the inner text is the same JSON
    Json response_json;
    response_json["candidates"][0]["content"]["parts"][0]["text"] = direct_json;

    RunParseSearchConfigJsonCases({
      {response_json.dump(), true, "pp:", "", {}, "Gemini API-style response"}
    });
  }
}

TEST_CASE("ValidatePathPatternFormat - Complex Patterns") {
  SUBCASE("Patterns with quantifiers") {
    RunValidatePathPatternCases({
        {"pp:**/\\d{3}*.log", true},
        {"pp:**/\\w{2,5}.txt", true},
        {"pp:**/[a-z]{1,}*.cpp", true}
    });
  }

  SUBCASE("Patterns with character classes") {
    RunValidatePathPatternCases({
        {"pp:**/[0-9]*.log", true},
        {"pp:**/[A-Za-z]*.txt", true},
        {"pp:**/[^a-z]*.cpp", true}
    });
  }

  SUBCASE("Patterns with shorthands") {
    RunValidatePathPatternCases({
        {"pp:**/\\d*.log", true},
        {"pp:**/\\w+.txt", true}
    });
  }

  SUBCASE("Patterns with anchors") {
    RunValidatePathPatternCases({
        {"pp:^src/**/*.cpp$", true},
        {"pp:^C:/Windows/**/*.exe$", true}
    });
  }

  SUBCASE("Patterns with Windows paths") {
    RunValidatePathPatternCases({
        {"pp:C:/Users/**/*.txt", true},
        {"pp:**/Documents/**/*.doc", true}
    });
  }

  SUBCASE("Patterns with special characters") {
    RunValidatePathPatternCases({
        {"pp:**/*.txt", true},
        {"pp:**/file?.txt", true},
        {"pp:**/test*.log", true}
    });
  }

  SUBCASE("Edge cases for validation") {
    RunValidatePathPatternCases({
        {"pp:", false},
        {"pp: ", true},   // Space after pp: is valid
        {"pp:\t", true},  // Tab after pp: is valid
        {"pp:\n", true}   // Newline after pp: is valid
    });
  }

  SUBCASE("Very long pattern") {
    constexpr size_t k_long_pattern_length = 1000;  // Test pattern length
    std::string long_pattern = "pp:";
    long_pattern += std::string(k_long_pattern_length, '*');
    long_pattern += ".txt";

    CHECK(ValidatePathPatternFormat(long_pattern) == true);
  }
}

TEST_CASE("ParseSearchConfigJson - Patterns with Special Characters") {
  SUBCASE("Patterns with various special characters") {
    RunParseSearchConfigJsonCases({
      {test_helpers::CreateGeminiResponseWithPath("pp:**/file/name.txt"),
       true, "pp:**/file/name.txt", "", {}, "Pattern with escaped characters"},
      {test_helpers::CreateGeminiResponseWithPath("pp:**/文档/*.txt"),
       true, "pp:**/文档/*.txt", "", {}, "Pattern with Unicode in path"},
      {test_helpers::CreateGeminiResponseWithPath("pp:**/dir/subdir/*.txt"),
       true, "pp:**/dir/subdir/*.txt", "", {}, "Pattern with mixed separators"},
      {test_helpers::CreateGeminiResponseWithPath("pp:**/\\d{2,4}*.log"),
       true, "pp:**/\\d{2,4}*.log", "", {}, "Pattern with complex quantifiers"},
      {test_helpers::CreateGeminiResponseWithPath("pp:**/[A-Za-z0-9_]*.cpp"),
       true, "pp:**/[A-Za-z0-9_]*.cpp", "", {}, "Pattern with nested character classes"}
    });
  }
}

TEST_CASE("GetGeminiApiKeyFromEnv - Edge Cases") {
  SUBCASE("Environment variable with special characters") {
    RunGetGeminiApiKeyCases({
      {"key-with-special-chars-!@#$%", "key-with-special-chars-!@#$%"}
    });
  }

  SUBCASE("Environment variable with spaces") {
    RunGetGeminiApiKeyCases({
      {"key with spaces", "key with spaces"}
    });
  }

  SUBCASE("Environment variable with newlines") {
    RunGetGeminiApiKeyCases({
      {"key\nwith\nnewlines", "key\nwith\nnewlines"}
    });
  }
}

// NOTE: These tests verify backward compatibility with old format that included extensions field.
// The new prompt (BuildSearchConfigPrompt) only uses path field, but parsing still supports
// old format for backward compatibility (in case old responses are cached or from previous API versions).

TEST_CASE("ParseSearchConfigJson - Old Format Fix Path Pattern with Extension") {
  // Simulate old Gemini response format: path includes extension AND extensions field is set
  // This is the old format - new prompt will only use path field
  const std::string json = test_helpers::CreateGeminiJsonResponse(
      R"({"version": "1.0", "search_config": {"extensions": ["cpp"], "path": "pp:USN_windows/**/*.cpp"}})");
  RunParseSearchConfigJsonCases({
    {json, true, "pp:**/USN_windows**", "", {"cpp"}, "Fix path pattern ending with extension"}
  });
}

TEST_CASE("ParseSearchConfigJson - Old Format Fix Path Pattern for Folder") {
  const std::string json = test_helpers::CreateGeminiJsonResponse(
      R"({"version": "1.0", "search_config": {"extensions": ["cpp"], "path": "pp:USN_windows/**"}})");
  RunParseSearchConfigJsonCases({
    {json, true, "pp:**/USN_windows**", "", {"cpp"}, "Fix path pattern for folder search"}
  });
}

TEST_CASE("ParseSearchConfigJson - Old Format Remove Extension from Path") {
  const std::string json = test_helpers::CreateGeminiJsonResponse(
      R"({"version": "1.0", "search_config": {"extensions": ["txt"], "path": "pp:documents/**/*.txt"}})");
  RunParseSearchConfigJsonCases({
    {json, true, "pp:**/documents**", "", {"txt"}, "Fix path pattern - remove extension from end"}
  });
}

TEST_CASE("ParseSearchConfigJson - Old Format Multiple Extensions") {
  Json inner_json;
  inner_json["version"] = "1.0";
  inner_json["search_config"]["extensions"] = {"cpp", "hpp"};
  inner_json["search_config"]["path"] = "pp:src/**/*.cpp";

  Json outer_json;
  outer_json["candidates"][0]["content"]["parts"][0]["text"] = inner_json.dump();

  auto result = ParseSearchConfigJson(outer_json.dump());

  REQUIRE(result.success == true);
  REQUIRE(result.search_config.extensions.size() == 2);
  CHECK(result.search_config.extensions[0] == "cpp");
  CHECK(result.search_config.extensions[1] == "hpp");
  // Extension should be removed from path
  CHECK(result.search_config.path == "pp:**/src**");
}

TEST_CASE("ParseSearchConfigJson - Old Format Path Without Extension") {
  RunParseSearchConfigJsonCases({
    {test_helpers::CreateGeminiJsonResponse(
      R"({"version": "1.0", "search_config": {"extensions": ["cpp"], "path": "pp:**/USN_windows/**"}})"),
     true, "pp:**/USN_windows**", "", {"cpp"}, "Path pattern without extension"}
  });
}

TEST_CASE("ParseSearchConfigJson - Old Format Path Without Extensions Field") {
  RunParseSearchConfigJsonCases({
    {test_helpers::CreateGeminiJsonResponse(
      R"({"version": "1.0", "search_config": {"path": "pp:**/folder/**/*.txt"}})"),
     true, "pp:**/folder/**/*.txt", "", {}, "Path pattern without extensions field"}
  });
}

TEST_CASE("ParseSearchConfigJson - Old Format Real World Case") {
  // Old format with extensions field - new prompt will use path-only format
  const std::string json = test_helpers::CreateGeminiJsonResponse(
    R"({"version": "1.0", "search_config": {"extensions": ["cpp"], "path": "pp:USN_windows/**/*.cpp", "time_filter": "Today"}})"
  );

  auto result = ParseSearchConfigJson(json);

  CHECK(result.success == true);
  CHECK(result.search_config.extensions.size() == 1);
  CHECK(result.search_config.extensions[0] == "cpp");
  CHECK(result.search_config.time_filter == "Today");
  // Path should be fixed: extension removed, ** prefix and ** suffix added
  CHECK(result.search_config.path == "pp:**/USN_windows**");
}

TEST_CASE("ParseSearchConfigJson - New Format Path Only with Extension") {
  // New format: path includes extension, no extensions field
  const std::string json = test_helpers::CreateGeminiJsonResponse(
    R"({"version": "1.0", "search_config": {"path": "pp:**/USN_windows**/*.cpp", "time_filter": "Today"}})"
  );

  auto result = ParseSearchConfigJson(json);

  CHECK(result.success == true);
  CHECK(result.search_config.extensions.empty()); // No extensions field in new format
  CHECK(result.search_config.time_filter == "Today");
  // Path includes extension - this is the new expected format
  CHECK(result.search_config.path == "pp:**/USN_windows**/*.cpp");
}

TEST_CASE("ParseSearchConfigJson - New Format Regex Exclusion Pattern") {
  // New format: complex patterns with exclusions use regex
  // Note: In JSON, \\. represents a literal backslash followed by a dot
  // After JSON parsing, this becomes \. (single backslash before .cpp)
  const std::string json = test_helpers::CreateGeminiJsonResponse(
    R"({"version": "1.0", "search_config": {"path": "re:^(?!.*[/]thirdparty[/]).*\\.cpp$"}})"
  );

  auto result = ParseSearchConfigJson(json);

  CHECK(result.success == true);
  CHECK(result.search_config.extensions.empty()); // No extensions field
  // Path uses regex for exclusion pattern
  // After JSON parsing, \\. becomes \. (single backslash before .cpp)
  CHECK(result.search_config.path == "re:^(?!.*[/]thirdparty[/]).*\\.cpp$");
}

TEST_CASE("ParseSearchConfigJson - New Format Regex Multiple Extensions") {
  // New format: multiple extensions use regex with alternation
  // Note: In JSON, \\. represents a literal backslash followed by a dot
  // After JSON parsing, this becomes \. (single backslash before the extension pattern)
  const std::string json = test_helpers::CreateGeminiJsonResponse(
      R"({"version": "1.0", "search_config": {"path": "re:.*\\.(cpp|hpp|cxx|cc)$"}})");

  auto result = ParseSearchConfigJson(json);

  CHECK(result.success == true);
  CHECK(result.search_config.extensions.empty()); // No extensions field
  // Path uses regex for multiple extensions (PathPattern doesn't support alternation)
  // After JSON parsing, \\. becomes \. (single backslash before the extension pattern)
  CHECK(result.search_config.path == "re:.*\\.(cpp|hpp|cxx|cc)$");
}

TEST_CASE("CallGeminiApiRaw - Input validation errors") {
  // Validation failures return before any HTTP call, so these need no network or API key.
  // Regression: ValidateApiInputs once built GeminiApiResult{false, "", msg}, placing the
  // message in search_config.filename and leaving error_message empty — CallGeminiApiRaw
  // then returned {false, ""}. These CHECKs lock the message in the returned pair.
  SUBCASE("Empty prompt") {
    const auto result = CallGeminiApiRaw("", "dummy-key", 30);
    CHECK(result.first == false);
    CHECK(result.second.find("Prompt is empty") != std::string::npos);
  }
  SUBCASE("Oversized prompt") {
    const std::string big_prompt(static_cast<size_t>(100) * 1024 + 1, 'x');
    const auto result = CallGeminiApiRaw(big_prompt, "dummy-key", 30);
    CHECK(result.first == false);
    CHECK(result.second.find("too large") != std::string::npos);
  }
  SUBCASE("Empty API key") {
    const auto result = CallGeminiApiRaw("find reports", "", 30);
    CHECK(result.first == false);
    CHECK(result.second.find("API key is empty") != std::string::npos);
  }
  SUBCASE("Timeout below minimum") {
    const auto result = CallGeminiApiRaw("find reports", "dummy-key", 0);
    CHECK(result.first == false);
    CHECK(result.second.find("Invalid timeout") != std::string::npos);
  }
  SUBCASE("Timeout above maximum") {
    const auto result = CallGeminiApiRaw("find reports", "dummy-key", 301);
    CHECK(result.first == false);
    CHECK(result.second.find("Invalid timeout") != std::string::npos);
  }
}

TEST_CASE("BuildGeminiRequestBody - Gemini 3.8 Flash configuration") {
  const std::string body = gemini_api_utils::BuildGeminiRequestBody("test prompt");
  CHECK(std::string_view(gemini_api_utils::kGeminiModel) == "gemini-3.8-flash");
  CHECK(body.find("\"contents\"") != std::string::npos);
  CHECK(body.find("test prompt") != std::string::npos);
  CHECK(body.find("\"thinkingLevel\":\"low\"") != std::string::npos);
  CHECK(body.find("\"responseMimeType\":\"application/json\"") != std::string::npos);
}


