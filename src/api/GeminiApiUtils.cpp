#include "api/GeminiApiUtils.h"

#include <cstdlib>
#include <cstring>
#include <future>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "api/GeminiApiHttp.h"
#include "path/PathUtils.h"
#include "utils/Logger.h"
#include "utils/StringUtils.h"

using json = nlohmann::json;  // NOLINT(readability-identifier-naming) - match nlohmann library convention

namespace gemini_api_utils {

// Removed unused function ShouldSkipApiCall - was not used anywhere

// Maximum sizes for input validation
constexpr size_t kMaxPromptSize = static_cast<size_t>(100) * 1024;  // 100KB
// Note: kMaxResponseSize is defined in platform-specific files (GeminiApiHttp_*.cpp)
constexpr int kMinTimeoutSeconds = 1;
constexpr int kMaxTimeoutSeconds = 300;  // 5 minutes

// UTF-8 BOM constants
constexpr unsigned char kUtf8BomByte1 = 0xEF;
constexpr unsigned char kUtf8BomByte2 = 0xBB;
constexpr unsigned char kUtf8BomByte3 = 0xBF;
constexpr size_t kUtf8BomSize = 3;

// Markdown code block constants
constexpr size_t kMarkdownCodeBlockMinSize = 6;  // Minimum size for ```...```
constexpr size_t kMarkdownJsonPrefixSize = 7;  // Size of "```json" prefix

// Helper function to process and add a token to extensions (removes leading dot, trims)
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables,readability-identifier-naming) - Static function, not a global variable. Naming follows function convention.
static void ProcessAndAddToken(std::string_view token_view, std::vector<std::string>& extensions) {
  std::string token = Trim(token_view);
  if (token.empty()) {
    return;
  }
  // Remove leading dot if present
  if (token[0] == '.') {
    token.erase(0, 1);
  }
  extensions.push_back(token);
}

// Helper function to validate inputs for CallGeminiApi
// Returns an optional GeminiApiResult if validation fails, otherwise std::nullopt
std::optional<GeminiApiResult> ValidateApiInputs(std::string_view prompt,
                                                 std::string_view api_key,
                                                 int timeout_seconds) {
    if (prompt.empty()) {
        LOG_ERROR_BUILD("Gemini API call failed: prompt is empty");
        return GeminiApiResult{false, "", "Prompt is empty"};
    }
    if (prompt.size() > kMaxPromptSize) {
        LOG_ERROR_BUILD("Gemini API call failed: prompt too large (" << prompt.size() << " bytes, max " << kMaxPromptSize << ")");
        return GeminiApiResult{false, "", "Prompt is too large (max 100KB)"};
    }
    if (api_key.empty()) {
        LOG_ERROR_BUILD("Gemini API call failed: API key is empty");
        return GeminiApiResult{false, "", "API key is empty"};
    }
    if (timeout_seconds < kMinTimeoutSeconds || timeout_seconds > kMaxTimeoutSeconds) {
        LOG_ERROR_BUILD("Gemini API call failed: invalid timeout (" << timeout_seconds << " seconds, must be " << kMinTimeoutSeconds << "-" << kMaxTimeoutSeconds << ")");
        return GeminiApiResult{false, "", "Invalid timeout value (must be 1-300 seconds)"};
    }
    return std::nullopt; // Indicates validation passed
}

std::pair<bool, std::string> CallGeminiApiRaw(std::string_view prompt,
                                               std::string_view api_key,
                                               int timeout_seconds) {
  // Input validation
  if (auto validation_result = ValidateApiInputs(prompt, api_key, timeout_seconds)) {
    return {false, validation_result->error_message};
  }

  // Call platform-specific HTTP client (returns raw JSON)
  return CallGeminiApiHttpPlatform(prompt, api_key, timeout_seconds);
}

std::string GetGeminiApiKeyFromEnv() {
#ifdef _WIN32
  // Use RAII wrapper to safely manage memory from _dupenv_s (replaces manual free() calls)
  struct EnvKeyDeleter {
    void operator()(char* ptr) const noexcept {
      if (ptr != nullptr) {
        free(ptr);  // NOSONAR(cpp:S1231) - Required by _dupenv_s C API, custom deleter for unique_ptr is correct pattern
      }
    }
  };
  using EnvKeyPtr = std::unique_ptr<char, EnvKeyDeleter>;
  
  char *env_key = nullptr;
  size_t len = 0;
  if (errno_t err = _dupenv_s(&env_key, &len, "GEMINI_API_KEY"); err != 0 || env_key == nullptr || len == 0) {
    // Wrap in unique_ptr for automatic cleanup even in error path (RAII - eliminates manual free() calls)
    if (env_key != nullptr) {
      EnvKeyPtr env_key_guard(env_key);
    }
    return std::string();
  }
  // Wrap in unique_ptr for automatic cleanup (RAII - eliminates manual free() calls)
  EnvKeyPtr env_key_guard(env_key);
  std::string result(env_key);
  return result;
#else
  if (const char *env_key = std::getenv("GEMINI_API_KEY"); env_key != nullptr) {  // NOLINT(cppcoreguidelines-init-variables,concurrency-mt-unsafe) - C++17 init-statement; getenv result copied immediately, config-style read
    return {env_key};
  }
  return {};
#endif  // _WIN32
}

bool ValidatePathPatternFormat(std::string_view pattern) {
  // Must start with "pp:"
  if (pattern.size() < 3 || pattern.substr(0, 3) != "pp:") {
    return false;
  }

  // Must have content after "pp:"
  if (pattern.size() <= 3) {
    return false;
  }

  return true;
}

// Removed unused function ExtractTextFromGeminiResponse - was not used anywhere

/**
 * @brief Parse extensions from JSON value (supports both array and comma-separated string formats)
 * 
 * Handles two formats:
 * - Array format: ["cpp", "hpp", "cxx"]
 * - String format: "cpp, hpp, cxx" (comma-separated)
 * 
 * @param extensions_value JSON value containing extensions (array or string)
 * @return Vector of extension strings (without leading dots, trimmed)
 */
std::vector<std::string> ParseExtensionsFromJson(const json& extensions_value) {
  std::vector<std::string> extensions;
  
  // OPTIMIZATION: Pre-allocate vector to avoid reallocations during parsing
  // Typical API responses have 5-20 extensions, estimate conservatively
  if (extensions_value.is_array()) {
    // Array format: ["cpp", "hpp"] - use exact size
    extensions.reserve(extensions_value.size());
    for (const auto &ext : extensions_value) {
      if (ext.is_string()) {
        extensions.push_back(ext.get<std::string>());
      }
    }
  } else if (extensions_value.is_string()) {
    // String format: "cpp, hpp" (comma-separated)
    // Estimate: typical extensions are 3-5 chars, with comma/space ~10 chars per extension
    const std::string ext_str = extensions_value.get<std::string>();
    constexpr size_t k_estimated_chars_per_extension = 10;  // NOLINT(readability-magic-numbers) - Estimate: 3-5 char ext + comma/space
    constexpr size_t k_safety_margin = 5;  // NOLINT(readability-magic-numbers) - Safety margin for reserve estimate
    extensions.reserve((ext_str.size() / k_estimated_chars_per_extension) + k_safety_margin);  // NOLINT(readability-math-missing-parentheses) - Parentheses already added
    
    // Parse comma-separated values
    size_t start = 0;
    while (start < ext_str.size()) {
      // Find next comma or end of string
      size_t end = ext_str.find(',', start);
      if (end == std::string::npos) {
        end = ext_str.size();
      }
      
      // Extract token and process it
      const std::string_view token_view = std::string_view(ext_str).substr(start, end - start);
      ProcessAndAddToken(token_view, extensions);
      
      // Move to next token (skip comma)
      start = end + 1;
    }
  }
  
  return extensions;
}

std::string BuildSearchConfigPrompt(std::string_view user_description) {
  // Get contextual information for the prompt
  std::string os_name;
#ifdef _WIN32
  os_name = "Windows";
#elif defined(__APPLE__)
  os_name = "macOS";
#elif defined(__linux__)
  os_name = "Linux";
#else
  os_name = "Unknown";
#endif  // _WIN32

  const std::string home_path = path_utils::GetUserHomePath();
  const std::string index_root = path_utils::GetDefaultVolumeRootPath();
  const std::string desktop_path = path_utils::GetDesktopPath();
  const std::string downloads_path = path_utils::GetDownloadsPath();

  std::ostringstream prompt;
  prompt << R"(You are a file search configuration generator. Convert natural language into JSON search configuration.

## Execution Context

- **OS**: )" << os_name << R"(
- **HOME**: ")" << home_path << R"("
- **INDEX_ROOT**: ")" << index_root << R"("
- **DESKTOP**: ")" << desktop_path << R"("
- **DOWNLOADS**: ")" << downloads_path << R"PROMPT(

When the user mentions "my home folder", "home directory", "desktop", "downloads", or similar terms,
map them to these absolute paths in the generated path patterns.
Prefer patterns under INDEX_ROOT when queries are project-related or volume-specific.

## Search Engine Logic (how filters work)

The search engine has three independent filters that combine with **AND**:
- **path**: Matches the **full path** (directories + item name + extension). Use for folder scope, path-level patterns, exclusions.
- **filename**: Matches the **item name only** (last path segment, file or folder; e.g. "readme" matches readme.txt). Use for name patterns.
- **extensions**: Filters by extension (e.g. ["cpp","h"]). Use for type filter.

You **may** use path + filename + extensions together when they are **complementary** (e.g. path = folder scope, filename = name pattern, extensions = type). Do **not** duplicate the same criterion in two fields (see "Avoid redundancy" below).

## JSON Schema

{
  "version": "1.0",
  "search_config": {
    "path": "pp:src/**/*.cpp",        // Optional: full path pattern (dirs + item name + extension)
    "filename": "readme",             // Optional: item-name-only pattern (glob/pp:/rs:)
    "extensions": ["cpp", "h"],       // Optional: array of extensions (no dots)
    "folders_only": false,             // Optional: default false
    "case_sensitive": false,           // Optional: default false
    "time_filter": "ThisWeek",         // Optional: "None", "Today", "ThisWeek", "ThisMonth", "ThisYear", "Older"
    "size_filter": "Large"             // Optional: "None", "Empty", "Tiny", "Small", "Medium", "Large", "Huge", "Massive"
  }
}

## Path Pattern Rules (CRITICAL)

**Path patterns match FULL file paths.** Use path for folder scope, path-level exclusions, or when encoding everything in one pattern is simpler.

### Performance Priority (IMPORTANT)

**ALWAYS prefer pathPattern (pp:) for speed when possible. Only use regex (rs:) if pathPattern is too limited for the requirement.**

1. **First choice: pp: (PathPattern)** - Fastest option, use whenever possible
2. **Second choice: rs: (regex)** - Only use when pathPattern cannot express the pattern

**Key reasons to use regex instead of pathPattern:**
- **Alternation needed (any kind)**: Any pattern with alternatives like `(cpp|hpp|cxx|cc)`, `(src|tests)/`, `(foo|bar)_prefix` - pathPattern does NOT support alternation
- **Lookahead needed**: Exclusions like `(?!.*thirdparty)` - pathPattern does NOT support lookahead
- **Other advanced regex features**: Capturing groups, backreferences, etc.

### When to Use pp: vs rs:

- **pp: (PathPattern)** - PREFERRED for speed, use for simple patterns:
  - Single extension: `pp:**/*.cpp`
  - Folder + extension: `pp:**/src/**/*.cpp`
  - Folder only: `pp:**/folder_name**` (MUST use `**/folder_name**` format)
  - Wildcards: `**` (recursive), `*` (non-separator), `?` (single char)
  - ✅ **Platform-agnostic**: Automatically handles both forward slashes (/) and backslashes (\) - works on Windows, macOS, and Linux
  - ✅ Fastest matching performance
  - ❌ Does NOT support: alternation `(a|b)`, lookahead `(?!...)`

- **rs: (regex)** - ONLY use when pathPattern is insufficient:
  - **Alternation (REQUIRED for any alternatives)**: PathPattern does NOT support alternation `(a|b)`, so use regex for any OR-style pattern, for example: `rs:.*\\.(cpp|hpp|cxx|cc)$`, `rs:.*/(src|tests)/.*`, or `rs:.*(foo|bar)_prefix.*`
  - **Lookahead (REQUIRED for exclusions)**: PathPattern does NOT support lookahead `(?!...)`, so use regex for exclusions: `rs:^(?!.*[/\\\\]thirdparty[/\\\\]).*\\.cpp$`
  - Complex patterns requiring alternation, lookahead, or other advanced regex features
  - ⚠️ **CRITICAL for cross-platform**: When regex patterns match paths (not just filenames), you MUST use `[/\\\\]` for path separators in JSON (4 backslashes). After JSON parsing this becomes `[/\\]` which matches either `/` or `\\`. Without this, patterns will fail on Windows (uses `\`) or Unix (uses `/`).
  - ⚠️ Slower than pathPattern - only use when necessary

### Avoid redundancy (when path already encodes the same thing)

Do **not** put the same criterion in two fields. When path already encodes extension or filename, omit the duplicate.

- ✅ CORRECT: `{"path": "pp:**/*.cpp"}` - Extension in path only
- ✅ CORRECT: `{"path": "pp:**/src/**", "filename": "readme", "extensions": ["txt"]}` - Complementary: path = folder, filename = name, extensions = type
- ✅ CORRECT: `{"path": "pp:**/docs/**", "extensions": ["md"]}` - Path = folder scope, extensions = type (no extension in path)
- ❌ WRONG: `{"path": "pp:**/*.cpp", "extensions": ["cpp"]}` - Redundant: extension already in path
- ❌ WRONG: `{"path": "pp:**/src/**/readme*", "filename": "readme"}` - Redundant: filename already in path

**Rule: Use path + filename + extensions when they are complementary. Do not duplicate the same criterion.**

### Folder Pattern Format (pp: only):
- ✅ CORRECT: `pp:**/folder_name**` (matches folder and subfolders anywhere)
- ❌ WRONG: `pp:folder_name/**` or `pp:**/folder_name` (missing prefix/suffix)

### Options
- **folders_only**: true = directories only
- **case_sensitive**: true = case-sensitive search
- **time_filter**: "None", "Today", "ThisWeek", "ThisMonth", "ThisYear", "Older"
- **size_filter**: "None", "Empty", "Tiny", "Small", "Medium", "Large", "Huge", "Massive"

## Rules
1. All fields are OPTIONAL - only include relevant fields
2. Omit fields to use defaults
3. Return valid JSON only (no explanations)
4. **Combine path, filename, extensions when complementary** (e.g. path = folder, filename = name pattern, extensions = type). Do **not** duplicate the same criterion in two fields.
5. **Performance**: Prefer `pp:` (pathPattern) for speed, only use `rs:` (regex) when pathPattern cannot express the pattern

## Examples

User: "find all C++ source files modified this week that are larger than 1MB"
Response: {"version":"1.0","search_config":{"path":"rs:.*\\.(cpp|hpp|cxx|cc)$","time_filter":"ThisWeek","size_filter":"Large"}}

User: "all .cpp files in folder USN_windows that were modified today"
Response: {"version":"1.0","search_config":{"path":"pp:**/USN_windows**/*.cpp","time_filter":"Today"}}

User: "all .cpp files excluding those in thirdparty folder"
Response: {"version":"1.0","search_config":{"path":"rs:^(?!.*[/\\\\]thirdparty[/\\\\]).*\\.cpp$"}}

User: "Python files starting with 'test'"
Response: {"version":"1.0","search_config":{"path":"rs:.*[/\\\\]test.*\\.py$"}}

User: "all text files in the documents folder"
Response: {"version":"1.0","search_config":{"path":"pp:**/documents**/*.txt"}}

User: "readme files in src folder (any extension)"
Response: {"version":"1.0","search_config":{"path":"pp:**/src/**","filename":"readme*"}}

User: "C++ source files under src (path + extension filter)"
Response: {"version":"1.0","search_config":{"path":"pp:**/src/**","extensions":["cpp","h","hpp"]}}

**Note on cross-platform compatibility:**
- PathPattern (`pp:`) automatically handles both `/` and `\` separators - no special syntax needed
- Regex (`rs:`) MUST use `[/\\\\]` when matching paths (not just filenames) to work on both Windows and Unix

## User Query

)PROMPT";
  prompt << "Generate a search configuration for: " << user_description << "\n\n";
  prompt << "Response (JSON only, no explanations):";
  return prompt.str();
}

namespace {

  // Helper function to remove trailing slashes and asterisks
  void RemoveTrailingChars(std::string& str) {
    while (!str.empty() && (str.back() == '/' || str.back() == '*')) {
      str.pop_back();
    }
    while (!str.empty() && str.back() == '/') {
      str.pop_back();
    }
  }

  // Helper function to check and remove extension pattern from path
  bool TryRemoveExtensionPattern(std::string& path_content, std::string_view pattern) {
    if (path_content.size() < pattern.size()) {
      return false;
    }
    if (const size_t pos = path_content.rfind(pattern); pos != std::string::npos && pos + pattern.size() == path_content.size()) {
      LOG_DEBUG_BUILD("[Gemini] Post-processing: Removing extension pattern \"" << pattern << "\" from path");
      path_content = path_content.substr(0, pos);
      RemoveTrailingChars(path_content);
      return true;
    }
    return false;
  }

  // Helper function to remove extension patterns from path content
  bool RemoveExtensionPatterns(std::string& path_content, const std::vector<std::string>& extensions) {
    for (const auto& ext : extensions) {
      // Check for "/" + "*.ext" pattern
      if (const std::string ext_pattern = std::string("/") + "*." + ext; TryRemoveExtensionPattern(path_content, ext_pattern)) {
        return true;
      }
      // Check for ".ext" pattern
      if (const std::string ext_pattern2 = "." + ext; TryRemoveExtensionPattern(path_content, ext_pattern2)) {
        return true;
      }
    }
    return false;
  }

  // Helper function to ensure folder searches have proper prefix and suffix
  void EnsureFolderPatternFormat(std::string& path_content) {
    if (path_content.empty()) {
      return;
    }

    const std::string before_prefix = path_content;
    if (path_content.size() < 2 || path_content.substr(0, 2) != "**") {
      path_content = "**/" + path_content;
      LOG_DEBUG_BUILD("[Gemini] Post-processing: Added \"**/\" prefix to path (was: \"" << before_prefix << "\")");
    }
    const std::string before_suffix = path_content;
    while (!path_content.empty() && (path_content.back() == '*' || path_content.back() == '/')) {
      path_content.pop_back();
    }
    path_content += "**";
    if (before_suffix != path_content) {
      LOG_DEBUG_BUILD("[Gemini] Post-processing: Added \"**\" suffix to path (was: \"" << before_suffix << "\")");
    }
  }

/**
 * @brief Fix path pattern if it incorrectly includes extension when extensions are set
 *
 * Post-processes path pattern to remove extension patterns when extensions are
 * already specified separately. This prevents redundant filtering.
 *
 * @param search_config Search config to fix (modified)
 */
void FixPathPatternIfNeeded(SearchConfig& search_config) {
  if (search_config.extensions.empty() || search_config.path.empty()) {
    return; // No fix needed
  }

  std::string& path = search_config.path;
  
  // Check if path starts with "pp:" (path pattern)
  if (path.size() < 3 || path.substr(0, 3) != "pp:") {
    return; // Not a path pattern, no fix needed
  }

  std::string path_content = path.substr(3);
  const bool needs_fix = RemoveExtensionPatterns(path_content, search_config.extensions);  // NOLINT(cppcoreguidelines-init-variables) - Initialized from RemoveExtensionPatterns() return value
  EnsureFolderPatternFormat(path_content);

  if (needs_fix || !path_content.empty()) {
    path = "pp:" + path_content;
  }
}

/**
 * @brief Parse error response from JSON
 *
 * Extracts error message from Gemini API error response structure.
 *
 * @param response_json JSON response containing error
 * @return Error message string
 */
std::string ParseErrorResponse(const json& response_json) {
  std::string error_message = "API error: ";
  if (response_json["error"].contains("message")) {
    error_message += response_json["error"]["message"].get<std::string>();
  } else {
    error_message += "Unknown error";
  }
  return error_message;
}

/**
 * @brief Extract text from Gemini API candidate response
 *
 * Extracts the text content from candidates[0].content.parts[0].text structure.
 *
 * @param response_json JSON response from Gemini API
 * @return Extracted text, or empty string if not found
 */
std::string ExtractTextFromCandidates(const json& response_json) {
  if (!response_json.contains("candidates") || 
      !response_json["candidates"].is_array() || 
      response_json["candidates"].empty()) {
    return "";
  }

  try {
    // Use .at() for safer access on Windows (throws if index is invalid)
    // Check array size before accessing to avoid potential Windows-specific issues
    const auto& candidates_array = response_json["candidates"];
    if (candidates_array.empty()) {
      return "";
    }
    
    const auto& candidate = candidates_array.at(0);
    if (!candidate.contains("content")) {
      return "";
    }
    
    const auto& content = candidate["content"];
    if (!content.contains("parts") || !content["parts"].is_array() || content["parts"].empty()) {
      return "";
    }

    const auto& parts_array = content["parts"];
    if (parts_array.empty()) {
      return "";
    }
    
    const auto& part = parts_array.at(0);
    if (part.contains("text") && part["text"].is_string()) {
      return part["text"].get<std::string>();
    }
  } catch (const json::exception& e) {
    // JSON access failed (e.g., invalid index access on Windows), return empty string
    LOG_WARNING_BUILD("JSON access failed in ExtractFirstTextPart: " << e.what());
    (void)e;  // Suppress unused variable warning in Release builds
    return "";
  } catch (const std::out_of_range& e) {
    // Array index out of range (shouldn't happen with size checks, but handle gracefully)
    LOG_WARNING_BUILD("Array index out of range in ExtractFirstTextPart: " << e.what());
    (void)e;  // Suppress unused variable warning in Release builds
    return "";
  }

  return "";
}

  // Helper function to clean clipboard text (remove markdown code blocks, trim, handle BOM)
  std::string CleanClipboardText(std::string_view text) {
    std::string cleaned(text);
    
    // Remove UTF-8 BOM if present (common on Windows)
    if (cleaned.size() >= kUtf8BomSize && 
        static_cast<unsigned char>(cleaned[0]) == kUtf8BomByte1 &&
        static_cast<unsigned char>(cleaned[1]) == kUtf8BomByte2 &&
        static_cast<unsigned char>(cleaned[2]) == kUtf8BomByte3) {
      cleaned.erase(0, kUtf8BomSize);
    }
    
    // Trim whitespace
    cleaned = Trim(cleaned);
    
    // Remove markdown code blocks (```json ... ``` or ``` ... ```)
    if (cleaned.size() >= kMarkdownCodeBlockMinSize && cleaned.substr(0, 3) == "```") {
      // Check for opening ```json or ```
      size_t start_pos = 3;
      // Skip "json" if present
      if (cleaned.size() >= kMarkdownJsonPrefixSize && cleaned.substr(3, 4) == "json") {
        start_pos = kMarkdownJsonPrefixSize;
      }
      // Skip whitespace after ```
      while (start_pos < cleaned.size() && (cleaned[start_pos] == ' ' || cleaned[start_pos] == '\n' || cleaned[start_pos] == '\r')) {
        start_pos++;
      }
      
      // Find closing ```
      const size_t end_pos = cleaned.rfind("```");
      if (end_pos != std::string::npos && end_pos > start_pos) {
        // Extract content between code blocks
        cleaned = cleaned.substr(start_pos, end_pos - start_pos);
        // Trim again after extraction  // NOSONAR(cpp:S1103) - Documentation comment, not misleading
        cleaned = Trim(cleaned);
      }
    }  // NOSONAR(cpp:S1103) - Documentation comment above, not misleading
    
    return cleaned;
  }
  
  // Helper function to parse string field from JSON
  bool ParseStringField(const json& search_config, const char* field_name, std::string& output, bool& has_any_fields) {
    if (search_config.contains(field_name) && search_config[field_name].is_string()) {
      output = search_config[field_name].get<std::string>();
      if (!output.empty()) {
        has_any_fields = true;
        return true;
      }
    }
    return false;
  }

  // Helper function to parse boolean field from JSON
  bool ParseBooleanField(const json& search_config, const char* field_name, bool& output, bool& has_any_fields) {
    if (search_config.contains(field_name) && search_config[field_name].is_boolean()) {
      output = search_config[field_name].get<bool>();
      if (output) {
        has_any_fields = true;
        return true;
      }
    }
    return false;
  }

  // Helper function to parse filter field from JSON (string that must not be "None")
  bool ParseFilterField(const json& search_config, const char* field_name, std::string& output, bool& has_any_fields) {
    if (search_config.contains(field_name) && search_config[field_name].is_string()) {
      output = search_config[field_name].get<std::string>();
      if (!output.empty() && output != "None") {
        has_any_fields = true;
        return true;
      }
    }
    return false;
  }

  // Helper function to parse all fields from search_config JSON
  void ParseAllFields(const json& search_config, SearchConfig& result_config, bool& has_any_fields) {
    ParseStringField(search_config, "filename", result_config.filename, has_any_fields);
    
    // Parse extensions - handle both array and comma-separated string formats
    if (search_config.contains("extensions")) {
      result_config.extensions = ParseExtensionsFromJson(search_config["extensions"]);
      if (!result_config.extensions.empty()) {
        has_any_fields = true;
      }
    }
    
    ParseStringField(search_config, "path", result_config.path, has_any_fields);
    ParseBooleanField(search_config, "folders_only", result_config.folders_only, has_any_fields);
    ParseBooleanField(search_config, "case_sensitive", result_config.case_sensitive, has_any_fields);
    ParseFilterField(search_config, "time_filter", result_config.time_filter, has_any_fields);
    ParseFilterField(search_config, "size_filter", result_config.size_filter, has_any_fields);
  }

  // Helper function to parse search config from JSON object
  bool ParseSearchConfigFromJson(const json& config_json, GeminiApiResult& result) {
    // Validate version
    if (!config_json.contains("version") || !config_json.contains("search_config")) {
      result.error_message = "Invalid JSON structure: missing 'version' or 'search_config'";
      return false;
    }
    
    const auto &search_config = config_json["search_config"];
    bool has_any_fields = false;
    
    // Parse all fields
    ParseAllFields(search_config, result.search_config, has_any_fields);

    // Special case: completely empty search_config object
    // When Gemini returns:
    //   { "version": "1.0", "search_config": {} }
    // interpret this as "all files", which corresponds to an empty PathPattern
    // with explicit "pp:" prefix. PathPattern with no inner pattern matches
    // everything (see SearchPatternUtils::CreatePathMatcher).
    if (!has_any_fields && search_config.is_object() && search_config.empty()) {
      result.search_config.path = "pp:";
    }
    
    return true;
  }
} // namespace

GeminiApiResult ParseSearchConfigJson(std::string_view json_response) {
  GeminiApiResult result;
  
  if (json_response.empty()) {
    result.error_message = "Empty JSON response";
    return result;
  }

  // Clean clipboard text (remove markdown code blocks, trim, handle BOM)
  const std::string cleaned_json = CleanClipboardText(json_response);

  try {
    // First, try to parse as direct search config JSON (from clipboard)
    // Check if it's a direct JSON with version and search_config at root
    const json direct_json = json::parse(cleaned_json);
    
    if (direct_json.contains("version") && direct_json.contains("search_config") &&
        ParseSearchConfigFromJson(direct_json, result)) {
      // This is direct JSON format (from clipboard)
      // Apply post-processing: Fix path pattern if needed
      FixPathPatternIfNeeded(result.search_config);
      result.success = true;
      return result;
    }
    // If parsing failed, fall through to try Gemini API format
    
    // If not direct format, try Gemini API response structure
    const json& response_json = direct_json; // Reuse already parsed JSON
    
    // Handle error response first  // NOSONAR(cpp:S1103) - Documentation comment, not misleading
    if (response_json.contains("error")) {
      result.error_message = ParseErrorResponse(response_json);
      return result;
    }

    // Extract the text from candidates[0].content.parts[0].text  // NOSONAR(cpp:S1103) - Documentation comment showing JSON structure
    std::string text = ExtractTextFromCandidates(response_json);
    if (text.empty()) {
      result.error_message = "Invalid JSON structure: missing 'candidates' or 'text' field";  // NOSONAR(cpp:S1103) - String literal, not comment delimiter
      return result;
    }
    
    // Log raw Gemini response in debug builds  // NOSONAR(cpp:S1103) - Documentation comment, not misleading
    LOG_DEBUG_BUILD("[Gemini] Raw response: " << text);
    
    // Clean the text (might have markdown code blocks)
    text = CleanClipboardText(text);
    
    // Check if text is empty after cleaning
    if (text.empty()) {
      result.error_message = "Invalid JSON structure: empty text field after cleaning";
      return result;
    }
    
    // Parse the search config JSON from the text
    // Use helper function to parse search config
    if (const json config_json = json::parse(text); !ParseSearchConfigFromJson(config_json, result)) {
      return result; // Error message already set
    }
    
    // Log parsed search config before post-processing in debug builds
    LOG_DEBUG_BUILD("[Gemini] Parsed config - extensions: [" 
                    << (result.search_config.extensions.empty() ? "" : 
                        [&result]() {  // NOSONAR(cpp:S3608) - Explicit capture: result by reference
                          std::ostringstream oss;
                          for (size_t i = 0; i < result.search_config.extensions.size(); ++i) {
                            if (i > 0) oss << ", ";
                            oss << result.search_config.extensions[i];
                          }
                          return oss.str();
                        }())
                    << "], path: \"" << result.search_config.path << "\"");
    
    // Post-process: Fix path pattern if it incorrectly includes extension when extensions are set
    // NOTE: With the improved prompt, this should rarely be needed, but kept as a safety net
    // Rule: If extensions are set, path pattern must NOT end with an extension
    FixPathPatternIfNeeded(result.search_config);
    
    result.success = true;
    return result;
  } catch (const json::parse_error &e) {
    (void)e; // Suppress unused variable warning in Release builds
    result.error_message = "JSON parse error: ";
    result.error_message += e.what();
    return result;
  } catch (const json::exception &e) {
    (void)e; // Suppress unused variable warning in Release builds
    result.error_message = "JSON error: ";
    result.error_message += e.what();
    return result;
  } catch (const std::exception &e) {  // NOSONAR(cpp:S1181) - Catch-all safety net after specific exception types (json::parse_error, json::exception)
    (void)e; // Suppress unused variable warning in Release builds
    result.error_message = "Unexpected error: ";
    result.error_message += e.what();
    return result;
  }
}

GeminiApiResult GenerateSearchConfigFromDescription(
    std::string_view user_description,
    std::string_view api_key,
    int timeout_seconds) {
  
  if (user_description.empty()) {
    LOG_ERROR_BUILD("GenerateSearchConfigFromDescription failed: user description is empty");
    GeminiApiResult result;
    result.error_message = "User description is empty";
    return result;
  }

  if (user_description.size() > kMaxPromptSize) {
    LOG_ERROR_BUILD("GenerateSearchConfigFromDescription failed: user description too large (" << user_description.size() << " bytes, max " << kMaxPromptSize << ")");
    GeminiApiResult result;
    result.error_message = "User description too large (max 100KB)";
    return result;
  }

  // Use environment variable if api_key is empty
  std::string actual_api_key;
  if (api_key.empty()) {
    actual_api_key = GetGeminiApiKeyFromEnv();
    if (actual_api_key.empty()) {
      LOG_ERROR_BUILD("GenerateSearchConfigFromDescription failed: API key not provided and GEMINI_API_KEY environment variable is not set");
      GeminiApiResult result;
      result.error_message = "API key not provided and GEMINI_API_KEY environment variable is not set";
      return result;
    }
  } else {
    actual_api_key = std::string(api_key);
  }

  const std::string prompt = BuildSearchConfigPrompt(user_description);
  
  // Get raw JSON response (we'll parse it ourselves)
  auto [success, raw_json] = CallGeminiApiRaw(prompt, actual_api_key, timeout_seconds);
  
  if (!success) {
    GeminiApiResult result;
    result.error_message = raw_json; // raw_json contains error message on failure
    return result;
  }
  
  // Parse the search config JSON from the raw response
  return ParseSearchConfigJson(raw_json);
}

std::future<GeminiApiResult> GenerateSearchConfigAsync(
    std::string_view user_description,
    std::string_view api_key,
    int timeout_seconds) {
  // Capture parameters by value for async execution
  const std::string desc_str(user_description);
  const std::string key_str(api_key);
  
  // Launch async task
  return std::async(std::launch::async, [desc_str, key_str, timeout_seconds]() {  // NOLINT(bugprone-exception-escape) - exceptions propagate to std::future
    return GenerateSearchConfigFromDescription(desc_str, key_str, timeout_seconds);
  });
}

} // namespace gemini_api_utils



