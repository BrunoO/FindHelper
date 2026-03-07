#pragma once

#include <future>  // NOLINT - false positive on macOS: <future> header exists and compiles fine
#include <string>
#include <string_view>
#include <vector>

namespace gemini_api_utils {

// Default timeout for API calls (seconds)
constexpr int kDefaultTimeoutSeconds = 30;

// Environment variable name for enabling API calls in tests (test-only, not used in production code)
// If this variable is NOT set, tests will skip API calls (default behavior).
// If this variable IS set (to any value), tests will make real API calls.
// This constant allows easy renaming of the environment variable if needed
constexpr const char* kGeminiApiTestEnableEnvVar = "GEMINI_API_TEST_ENABLE";

// Search configuration structure parsed from JSON
struct SearchConfig {
  std::string filename;         // Optional: filename pattern (glob, path pattern with pp: or auto-detected ^/$/.+*, or std::regex with rs:)
  std::vector<std::string> extensions;  // Optional: array of extensions (without dots)
  std::string path;             // Optional: path pattern (prefer pp: prefix)
  bool folders_only = false;   // Optional: default false
  bool case_sensitive = false;  // Optional: default false
  std::string time_filter;      // Optional: "None", "Today", "ThisWeek", "ThisMonth", "ThisYear", "Older"
  std::string size_filter;      // Optional: "None", "Empty", "Tiny", "Small", "Medium", "Large", "Huge", "Massive"
};

// Result structure for API calls
struct GeminiApiResult {
  bool success = false;
  std::string error_message;   // Error description if success == false
  SearchConfig search_config;  // Full search configuration from JSON
};

// Call the Google Gemini API and return the raw JSON response string.
// This is used for the new JSON-based API where we need to parse the response ourselves.
//
// Parameters:
//   prompt: The prompt text to send to the API
//   api_key: Google AI API key for authentication
//   timeout_seconds: Request timeout in seconds (default: kDefaultTimeoutSeconds)
//
// Returns:
//   Pair of (success bool, response_json_string). On error, success is false and
//   response_json_string contains the error message.
//
// Note: This function makes a synchronous HTTP request and may block.
std::pair<bool, std::string> CallGeminiApiRaw(std::string_view prompt,
                                               std::string_view api_key,
                                               int timeout_seconds = kDefaultTimeoutSeconds);

// Validate that a string is a valid path pattern format.
// Checks that it starts with "pp:" and contains a pattern after the prefix.
//
// Parameters:
//   pattern: The pattern string to validate (e.g., "pp:**/*.txt")  // NOSONAR - Example pattern in comment
//
// Returns:
//   true if the pattern has valid format, false otherwise
bool ValidatePathPatternFormat(std::string_view pattern);

// Get the Gemini API key from the GEMINI_API_KEY environment variable.
//
// Returns:
//   API key string if found, empty string if not set
//
// Note: The environment variable name is "GEMINI_API_KEY"
std::string GetGeminiApiKeyFromEnv();

// Build a prompt for the Gemini API requesting a complete search configuration in JSON format.
//
// Parameters:
//   user_description: Natural language description of files to search for
//                     (e.g., "find all C++ source files modified this week that are larger than 1MB")
//
// Returns:
//   Complete prompt string ready to send to Gemini API requesting JSON output
std::string BuildSearchConfigPrompt(std::string_view user_description);

// Parse JSON response from Gemini API and extract search configuration.
//
// Parameters:
//   json_response: Raw JSON response string from Gemini API
//
// Returns:
//   GeminiApiResult with success status and SearchConfig in search_config field
//
// The function expects the response in format:
//   {
//     "candidates": [{
//       "content": {
//         "parts": [{
//           "text": "{ \"version\": \"1.0\", \"search_config\": { ... } }"
//         }]
//       }
//     }]
//   }
//
// The search_config JSON should match the SearchConfig structure:
//   {
//     "path": "pp:src/**/*.cpp",       // Optional: path pattern matching FULL paths (directory + filename + extension)  // NOSONAR(cpp:S1103) - Example JSON in comment, not comment delimiter
//                                       // Prefer pp: prefix for simple patterns, use rs: for regex (alternation, exclusions)
//     "folders_only": false,            // Optional: default false
//     "case_sensitive": false,          // Optional: default false
//     "time_filter": "ThisWeek",        // Optional: "None", "Today", "ThisWeek", "ThisMonth", "ThisYear", "Older"
//     "size_filter": "Large"            // Optional: "None", "Empty", "Tiny", "Small", "Medium", "Large", "Huge", "Massive"
//   }
// NOTE: BuildSearchConfigPrompt may generate path, filename, and extensions when complementary
// (path = folder scope, filename = name pattern, extensions = type). All combine with AND.
GeminiApiResult ParseSearchConfigJson(std::string_view json_response);

// Main entry point: Generate a complete search configuration from user description.
// Uses API key from environment variable (GEMINI_API_KEY) if api_key is empty.
// Combines prompt building, API call, and response parsing.
//
// Parameters:
//   user_description: Natural language description of files to search for
//   api_key: Google AI API key for authentication (empty string to use env var)
//   timeout_seconds: Request timeout in seconds (default: kDefaultTimeoutSeconds)
//
// Returns:
//   GeminiApiResult with success status and SearchConfig in search_config field
//
// Example:
//   auto result = GenerateSearchConfigFromDescription(
//       "find all C++ source files modified this week that are larger than 1MB", "");
//   if (result.success) {
//     // Use result.search_config to apply to GuiState
//   }
GeminiApiResult GenerateSearchConfigFromDescription(
    std::string_view user_description,
    std::string_view api_key = "",
    int timeout_seconds = kDefaultTimeoutSeconds);

// Async wrapper for GenerateSearchConfigFromDescription.
// Returns a future that will contain the result when the API call completes.
//
// Parameters:
//   user_description: Natural language description of files to search for
//   api_key: Google AI API key for authentication (empty string to use env var)
//   timeout_seconds: Request timeout in seconds (default: kDefaultTimeoutSeconds)
//
// Returns:
//   std::future<GeminiApiResult> that will contain the result when ready
std::future<GeminiApiResult> GenerateSearchConfigAsync(
    std::string_view user_description,
    std::string_view api_key = "",
    int timeout_seconds = kDefaultTimeoutSeconds);

} // namespace gemini_api_utils


