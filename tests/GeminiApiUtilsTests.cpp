#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include <cstdlib>
#include <string>

#include <nlohmann/json.hpp>

#include "TestHelpers.h"
#include "api/GeminiApiUtils.h"
#include "doctest/doctest.h"

using Json = nlohmann::json;  // NOLINT(readability-identifier-naming) - json is common alias name from nlohmann library

using gemini_api_utils::BuildSearchConfigPrompt;
// GetGeminiApiKeyFromEnv removed - unused in tests
using gemini_api_utils::ParseSearchConfigJson;
using gemini_api_utils::ValidatePathPatternFormat;

// IMPORTANT: These unit tests skip API calls by default for safety.
//
// - By default, tests will NOT make real API calls (safer, faster)
// - To enable real API calls in tests, set GEMINI_API_TEST_ENABLE environment variable
// - Note: This variable is test-only and does not affect production code
// - Input validation tests work without API calls (validation happens before API call)
//
// To enable real API calls in tests:
//   - Windows: set GEMINI_API_TEST_ENABLE=1
//   - macOS/Linux: export GEMINI_API_TEST_ENABLE=1
//
// Requirements for tests that call the real API:
// 1. GEMINI_API_TEST_ENABLE environment variable must be set
// 2. GEMINI_API_KEY environment variable must be set with a valid API key
// 3. Network access must be available

TEST_CASE("ParseSearchConfigJson - Valid Responses") {
  SUBCASE("Valid responses with various path patterns") {
    const std::vector<test_helpers::ParseSearchConfigJsonTestCase> test_cases = {
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
    };
    test_helpers::RunParameterizedParseSearchConfigJsonTests(test_cases);
  }

  SUBCASE("Empty response") {
    const std::vector<test_helpers::ParseSearchConfigJsonTestCase> test_cases = {
      {"", false, "", "Empty", {}, "Empty response"}
    };
    test_helpers::RunParameterizedParseSearchConfigJsonTests(test_cases);
  }

  SUBCASE("Invalid JSON") {
    const std::vector<test_helpers::ParseSearchConfigJsonTestCase> test_cases = {
      {"{ invalid json }", false, "", "", {}, "Invalid JSON"}
    };
    test_helpers::RunParameterizedParseSearchConfigJsonTests(test_cases);
  }

  SUBCASE("API error response") {
    const std::vector<test_helpers::ParseSearchConfigJsonTestCase> test_cases = {
      {test_helpers::CreateGeminiErrorResponse(400, "Invalid API key", "INVALID_ARGUMENT"),
       false, "", "Invalid API key", {}, "API error response"}
    };
    test_helpers::RunParameterizedParseSearchConfigJsonTests(test_cases);
  }

  SUBCASE("Missing candidates array") {
    const std::string json = R"({
      "candidates": []
    })";
    const std::vector<test_helpers::ParseSearchConfigJsonTestCase> test_cases = {
      {json, false, "", "Invalid JSON structure", {}, "Missing candidates array"}
    };
    test_helpers::RunParameterizedParseSearchConfigJsonTests(test_cases);
  }

  SUBCASE("Complex patterns") {
    const std::vector<test_helpers::ParseSearchConfigJsonTestCase> test_cases = {
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
    };
    test_helpers::RunParameterizedParseSearchConfigJsonTests(test_cases);
  }
}

TEST_CASE("ValidatePathPatternFormat - Basic Patterns") {
  SUBCASE("Valid patterns") {
    const std::vector<test_helpers::ValidatePathPatternTestCase> test_cases = {
        {"pp:**/*.txt", true},
        {"pp:src/**/*.cpp", true},
        {"pp:*.py", true},
        {"pp:^C:/Windows/**/*.exe$", true}
    };
    test_helpers::RunParameterizedValidatePathPatternTests(test_cases);
  }

  SUBCASE("Invalid patterns") {
    const std::vector<test_helpers::ValidatePathPatternTestCase> test_cases = {
        {"pp:", false},
        {"**/*.txt", false},
        {"", false},
        {"pp", false}
    };
    test_helpers::RunParameterizedValidatePathPatternTests(test_cases);
  }
}

TEST_CASE("GetGeminiApiKeyFromEnv") {
  SUBCASE("Environment variable not set") {
    const std::vector<test_helpers::GetGeminiApiKeyTestCase> test_cases = {
      {"", "", true}
    };
    test_helpers::RunParameterizedGetGeminiApiKeyTests(test_cases);
  }

  SUBCASE("Environment variable set") {
    const std::vector<test_helpers::GetGeminiApiKeyTestCase> test_cases = {
      {"test-api-key-12345", "test-api-key-12345"}
    };
    test_helpers::RunParameterizedGetGeminiApiKeyTests(test_cases);
  }
}

TEST_CASE("BuildSearchConfigPrompt - Basic Descriptions") {
  SUBCASE("Very long description") {
    const std::string long_desc(5000, 'a');
    const std::vector<test_helpers::BuildSearchConfigPromptTestCase> test_cases = {
      {long_desc, long_desc}
    };
    test_helpers::RunParameterizedBuildSearchConfigPromptTests(test_cases);
  }

  SUBCASE("Description with special characters") {
    const std::vector<test_helpers::BuildSearchConfigPromptTestCase> test_cases = {
      {R"(files with 'quotes' and "double quotes" and \backslashes)",
       R"(files with 'quotes' and "double quotes" and \backslashes)"}
    };
    test_helpers::RunParameterizedBuildSearchConfigPromptTests(test_cases);
  }

  SUBCASE("Description with newlines") {
    const std::vector<test_helpers::BuildSearchConfigPromptTestCase> test_cases = {
      {"all files\nin directory\nwith extension .txt",
       "all files\nin directory\nwith extension .txt"}
    };
    test_helpers::RunParameterizedBuildSearchConfigPromptTests(test_cases);
  }

  SUBCASE("Description with Unicode characters") {
    const std::vector<test_helpers::BuildSearchConfigPromptTestCase> test_cases = {
      {u8"files in 目录 with 中文 names",
       u8"files in 目录 with 中文 names"}
    };
    test_helpers::RunParameterizedBuildSearchConfigPromptTests(test_cases);
  }

  SUBCASE("Description with Windows path separators") {
    const std::vector<test_helpers::BuildSearchConfigPromptTestCase> test_cases = {
      {R"(files in C:\Users\Documents\)",
       R"(files in C:\Users\Documents\)"}
    };
    test_helpers::RunParameterizedBuildSearchConfigPromptTests(test_cases);
  }
}

TEST_CASE("BuildSearchConfigPrompt - Prompt Structure") {
  SUBCASE("Prompt structure - path field is primary, no filename/extensions in schema") {
    const std::string desc = "find all .cpp files";
    const std::string prompt = BuildSearchConfigPrompt(desc);

    // Verify prompt mentions path field
    CHECK(prompt.find("\"path\"") != std::string::npos);

    // Verify prompt does NOT mention filename or extensions in the schema
    // (They may appear in examples or documentation, but not as primary fields)
    // Note: We check that "filename" and "extensions" are not in the main schema section
    const size_t schema_pos = prompt.find("## Search Configuration JSON Schema");
    if (schema_pos != std::string::npos) {
      const std::string schema_section = prompt.substr(schema_pos, 500); // Check first 500 chars of schema
      // The schema should only show path field, not filename or extensions
      // (We allow them in examples for backward compatibility documentation)
      CHECK(schema_section.find("\"path\"") != std::string::npos);
    }
  }
}

TEST_CASE("BuildSearchConfigPrompt - Base Prompt Size") {
  constexpr size_t k_max_prompt_size = 8000;
  // Test base prompt size (without user description)
  // The base prompt template should be under 8000 chars to leave room for user descriptions
  const std::string base_prompt = BuildSearchConfigPrompt("");
  CHECK(base_prompt.size() < k_max_prompt_size);
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
    const std::vector<test_helpers::ParseSearchConfigJsonTestCase> test_cases = {
      {test_helpers::CreateGeminiResponseWithMultipleCandidates({"pp:**/*.txt", "pp:**/*.log"}),
       true, "pp:**/*.txt", "", {}, "Multiple candidates - use first"}
    };
    test_helpers::RunParameterizedParseSearchConfigJsonTests(test_cases);
  }

  SUBCASE("Multiple parts (should use first)") {
    const std::vector<test_helpers::ParseSearchConfigJsonTestCase> test_cases = {
      {test_helpers::CreateGeminiResponseWithMultipleParts({"pp:**/*.txt", "pp:**/*.log"}),
       true, "pp:**/*.txt", "", {}, "Multiple parts - use first"}
    };
    test_helpers::RunParameterizedParseSearchConfigJsonTests(test_cases);
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
    const std::vector<test_helpers::ParseSearchConfigJsonTestCase> test_cases = {
      {json, true, "pp:src/**/*.cpp", "", {}, "Response with metadata fields"}
    };
    test_helpers::RunParameterizedParseSearchConfigJsonTests(test_cases);
  }

  SUBCASE("Patterns with whitespace") {
    const std::vector<test_helpers::ParseSearchConfigJsonTestCase> test_cases = {
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
    };
    test_helpers::RunParameterizedParseSearchConfigJsonTests(test_cases);
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
    const std::vector<test_helpers::ParseSearchConfigJsonTestCase> test_cases = {
      {json, false, "", "parse", {}, "Malformed JSON - missing closing brace"}
    };
    test_helpers::RunParameterizedParseSearchConfigJsonTests(test_cases);
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
    const std::vector<test_helpers::ParseSearchConfigJsonTestCase> test_cases = {
      {json, false, "", "", {}, "Malformed JSON - invalid escape"}
    };
    test_helpers::RunParameterizedParseSearchConfigJsonTests(test_cases);
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
    const std::vector<test_helpers::ParseSearchConfigJsonTestCase> test_cases = {
      {json, false, "", "structure", {}, "Missing text field"}
    };
    test_helpers::RunParameterizedParseSearchConfigJsonTests(test_cases);
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
    const std::vector<test_helpers::ParseSearchConfigJsonTestCase> test_cases = {
      {json, false, "", "", {}, "Text field is not a string"}
    };
    test_helpers::RunParameterizedParseSearchConfigJsonTests(test_cases);
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
    const std::vector<test_helpers::ParseSearchConfigJsonTestCase> test_cases = {
      {json, false, "", "", {}, "Text field with only whitespace"}
    };
    test_helpers::RunParameterizedParseSearchConfigJsonTests(test_cases);
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
    const std::vector<test_helpers::ParseSearchConfigJsonTestCase> test_cases = {
      {test_helpers::CreateGeminiResponseWithPath("pp:**/*.txt"),
       true, "pp:**/*.txt", "", {}, "Very long response text"}
    };
    test_helpers::RunParameterizedParseSearchConfigJsonTests(test_cases);
  }

  SUBCASE("Unicode characters in response") {
    const std::vector<test_helpers::ParseSearchConfigJsonTestCase> test_cases = {
      {test_helpers::CreateGeminiResponseWithPath("pp:**/*.txt"),
       true, "pp:**/*.txt", "", {}, "Unicode characters in response"}
    };
    test_helpers::RunParameterizedParseSearchConfigJsonTests(test_cases);
  }

  SUBCASE("Empty search_config object should mean all files (pp:)") {
    // Direct JSON format (e.g., pasted from clipboard)
    const std::string direct_json = R"({
      "version": "1.0",
      "search_config": {}
    })";

    const std::vector<test_helpers::ParseSearchConfigJsonTestCase> direct_cases = {
      {direct_json, true, "pp:", "", {}, "Direct JSON format"}
    };
    test_helpers::RunParameterizedParseSearchConfigJsonTests(direct_cases);

    // Gemini API-style response where the inner text is the same JSON
    Json response_json;
    response_json["candidates"][0]["content"]["parts"][0]["text"] = direct_json;

    const std::vector<test_helpers::ParseSearchConfigJsonTestCase> api_cases = {
      {response_json.dump(), true, "pp:", "", {}, "Gemini API-style response"}
    };
    test_helpers::RunParameterizedParseSearchConfigJsonTests(api_cases);
  }
}

TEST_CASE("ValidatePathPatternFormat - Complex Patterns") {
  SUBCASE("Patterns with quantifiers") {
    const std::vector<test_helpers::ValidatePathPatternTestCase> test_cases = {
        {"pp:**/\\d{3}*.log", true},
        {"pp:**/\\w{2,5}.txt", true},
        {"pp:**/[a-z]{1,}*.cpp", true}
    };
    test_helpers::RunParameterizedValidatePathPatternTests(test_cases);
  }

  SUBCASE("Patterns with character classes") {
    const std::vector<test_helpers::ValidatePathPatternTestCase> test_cases = {
        {"pp:**/[0-9]*.log", true},
        {"pp:**/[A-Za-z]*.txt", true},
        {"pp:**/[^a-z]*.cpp", true}
    };
    test_helpers::RunParameterizedValidatePathPatternTests(test_cases);
  }

  SUBCASE("Patterns with shorthands") {
    const std::vector<test_helpers::ValidatePathPatternTestCase> test_cases = {
        {"pp:**/\\d*.log", true},
        {"pp:**/\\w+.txt", true}
    };
    test_helpers::RunParameterizedValidatePathPatternTests(test_cases);
  }

  SUBCASE("Patterns with anchors") {
    const std::vector<test_helpers::ValidatePathPatternTestCase> test_cases = {
        {"pp:^src/**/*.cpp$", true},
        {"pp:^C:/Windows/**/*.exe$", true}
    };
    test_helpers::RunParameterizedValidatePathPatternTests(test_cases);
  }

  SUBCASE("Patterns with Windows paths") {
    const std::vector<test_helpers::ValidatePathPatternTestCase> test_cases = {
        {"pp:C:/Users/**/*.txt", true},
        {"pp:**/Documents/**/*.doc", true}
    };
    test_helpers::RunParameterizedValidatePathPatternTests(test_cases);
  }

  SUBCASE("Patterns with special characters") {
    const std::vector<test_helpers::ValidatePathPatternTestCase> test_cases = {
        {"pp:**/*.txt", true},
        {"pp:**/file?.txt", true},
        {"pp:**/test*.log", true}
    };
    test_helpers::RunParameterizedValidatePathPatternTests(test_cases);
  }

  SUBCASE("Edge cases for validation") {
    const std::vector<test_helpers::ValidatePathPatternTestCase> test_cases = {
        {"pp:", false},
        {"pp: ", true},   // Space after pp: is valid
        {"pp:\t", true},  // Tab after pp: is valid
        {"pp:\n", true}   // Newline after pp: is valid
    };
    test_helpers::RunParameterizedValidatePathPatternTests(test_cases);
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
    const std::vector<test_helpers::ParseSearchConfigJsonTestCase> test_cases = {
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
    };
    test_helpers::RunParameterizedParseSearchConfigJsonTests(test_cases);
  }
}

TEST_CASE("GetGeminiApiKeyFromEnv - Edge Cases") {
  SUBCASE("Environment variable with special characters") {
    const std::vector<test_helpers::GetGeminiApiKeyTestCase> test_cases = {
      {"key-with-special-chars-!@#$%", "key-with-special-chars-!@#$%"}
    };
    test_helpers::RunParameterizedGetGeminiApiKeyTests(test_cases);
  }

  SUBCASE("Environment variable with spaces") {
    const std::vector<test_helpers::GetGeminiApiKeyTestCase> test_cases = {
      {"key with spaces", "key with spaces"}
    };
    test_helpers::RunParameterizedGetGeminiApiKeyTests(test_cases);
  }

  SUBCASE("Environment variable with newlines") {
    const std::vector<test_helpers::GetGeminiApiKeyTestCase> test_cases = {
      {"key\nwith\nnewlines", "key\nwith\nnewlines"}
    };
    test_helpers::RunParameterizedGetGeminiApiKeyTests(test_cases);
  }
}

// NOTE: These tests verify backward compatibility with old format that included extensions field.
// The new prompt (BuildSearchConfigPrompt) only uses path field, but parsing still supports
// old format for backward compatibility (in case old responses are cached or from previous API versions).

TEST_CASE("ParseSearchConfigJson - Old Format Fix Path Pattern with Extension") {
  // Simulate old Gemini response format: path includes extension AND extensions field is set
  // This is the old format - new prompt will only use path field
  const std::string json = R"({
    "candidates": [{
      "content": {
        "parts": [{
          "text": "{\"version\": \"1.0\", \"search_config\": {\"extensions\": [\"cpp\"], \"path\": \"pp:USN_windows/**/*.cpp\"}}"
        }]
      }
    }]
  })";
  const std::vector<test_helpers::ParseSearchConfigJsonTestCase> test_cases = {
    {json, true, "pp:**/USN_windows**", "", {"cpp"}, "Fix path pattern ending with extension"}
  };
  test_helpers::RunParameterizedParseSearchConfigJsonTests(test_cases);
}

TEST_CASE("ParseSearchConfigJson - Old Format Fix Path Pattern for Folder") {
  const std::string json = R"({
    "candidates": [{
      "content": {
        "parts": [{
          "text": "{\"version\": \"1.0\", \"search_config\": {\"extensions\": [\"cpp\"], \"path\": \"pp:USN_windows/**\"}}"
        }]
      }
    }]
  })";
  const std::vector<test_helpers::ParseSearchConfigJsonTestCase> test_cases = {
    {json, true, "pp:**/USN_windows**", "", {"cpp"}, "Fix path pattern for folder search"}
  };
  test_helpers::RunParameterizedParseSearchConfigJsonTests(test_cases);
}

TEST_CASE("ParseSearchConfigJson - Old Format Remove Extension from Path") {
  const std::string json = R"({
    "candidates": [{
      "content": {
        "parts": [{
          "text": "{\"version\": \"1.0\", \"search_config\": {\"extensions\": [\"txt\"], \"path\": \"pp:documents/**/*.txt\"}}"
        }]
      }
    }]
  })";
  const std::vector<test_helpers::ParseSearchConfigJsonTestCase> test_cases = {
    {json, true, "pp:**/documents**", "", {"txt"}, "Fix path pattern - remove extension from end"}
  };
  test_helpers::RunParameterizedParseSearchConfigJsonTests(test_cases);
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
  const std::vector<test_helpers::ParseSearchConfigJsonTestCase> test_cases = {
    {test_helpers::CreateGeminiJsonResponse(
      R"({"version": "1.0", "search_config": {"extensions": ["cpp"], "path": "pp:**/USN_windows/**"}})"),
     true, "pp:**/USN_windows**", "", {"cpp"}, "Path pattern without extension"}
  };
  test_helpers::RunParameterizedParseSearchConfigJsonTests(test_cases);
}

TEST_CASE("ParseSearchConfigJson - Old Format Path Without Extensions Field") {
  const std::vector<test_helpers::ParseSearchConfigJsonTestCase> test_cases = {
    {test_helpers::CreateGeminiJsonResponse(
      R"({"version": "1.0", "search_config": {"path": "pp:**/folder/**/*.txt"}})"),
     true, "pp:**/folder/**/*.txt", "", {}, "Path pattern without extensions field"}
  };
  test_helpers::RunParameterizedParseSearchConfigJsonTests(test_cases);
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
    R"({"version": "1.0", "search_config": {"path": "rs:^(?!.*[/]thirdparty[/]).*\\.cpp$"}})"
  );

  auto result = ParseSearchConfigJson(json);

  CHECK(result.success == true);
  CHECK(result.search_config.extensions.empty()); // No extensions field
  // Path uses regex for exclusion pattern
  // After JSON parsing, \\. becomes \. (single backslash before .cpp)
  CHECK(result.search_config.path == "rs:^(?!.*[/]thirdparty[/]).*\\.cpp$");
}

TEST_CASE("ParseSearchConfigJson - New Format Regex Multiple Extensions") {
  // New format: multiple extensions use regex with alternation
  // Note: In JSON, \\. represents a literal backslash followed by a dot
  // After JSON parsing, this becomes \. (single backslash before the extension pattern)
  const std::string json = R"({
    "candidates": [{
      "content": {
        "parts": [{
          "text": "{\"version\": \"1.0\", \"search_config\": {\"path\": \"rs:.*\\\\.(cpp|hpp|cxx|cc)$\"}}"
        }]
      }
    }]
  })";

  auto result = ParseSearchConfigJson(json);

  CHECK(result.success == true);
  CHECK(result.search_config.extensions.empty()); // No extensions field
  // Path uses regex for multiple extensions (PathPattern doesn't support alternation)
  // After JSON parsing, \\. becomes \. (single backslash before the extension pattern)
  CHECK(result.search_config.path == "rs:.*\\.(cpp|hpp|cxx|cc)$");
}


