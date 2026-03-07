#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>

#include "path/PathPatternMatcher.h"
#include "search/SearchContext.h"
#include "utils/LightweightCallable.h"
#include "utils/RegexAliases.h"
#include "utils/SimpleRegex.h"
#include "utils/StdRegexUtils.h"
#include "utils/StringSearch.h"
#include "utils/StringUtils.h"
#include <string>
#include <string_view>
#include <type_traits>

namespace search_pattern_utils {
// Maximum pattern length to prevent ReDoS (Regular Expression Denial of Service) attacks
// Patterns longer than this will be rejected to prevent catastrophic backtracking
constexpr size_t kMaxPatternLength = 1000;

// Pattern type enumeration
enum class PatternType : std::uint8_t {  // NOLINT(performance-enum-size) - explicit uint8_t; checker may still flag in some contexts
  StdRegex,    // rs: prefix - full ECMAScript regex
  PathPattern, // pp: prefix OR auto-detected (contains ^, $, **, [], {}, \d, \w) - advanced path pattern
  Glob,        // contains * or ? - wildcard pattern (matches substrings)
  Substring    // default - substring search
};

// Detect the type of pattern based on its content
// Detection priority:
// 1. Explicit prefixes (rs:, pp:) - highest priority
// 2. Advanced PathPattern features (^, $, **, [], {}, \d, \w) - auto-detect
// 3. Glob patterns (*, ?) - simple wildcards
// 4. Substring - default fallback
inline PatternType DetectPatternType(std::string_view pattern) {
  // 1. Explicit prefixes (highest priority)
  if (pattern.size() >= 3) {
    auto prefix = pattern.substr(0, 3);
    if (prefix == "rs:") {
      return PatternType::StdRegex;
    }
    if (prefix == "pp:") {
      return PatternType::PathPattern;
    }
  }

  // 2. Advanced PathPattern auto-detect (no prefix, but clearly using PathPattern features)
  const bool has_anchor_start = !pattern.empty() && pattern.front() == '^';
  const bool has_anchor_end = !pattern.empty() && pattern.back() == '$';
  const bool has_double_star = pattern.find("**") != std::string_view::npos;  // NOLINT(cppcoreguidelines-init-variables)
  const bool has_char_class =  // NOLINT(cppcoreguidelines-init-variables)
      pattern.find('[') != std::string_view::npos ||
      pattern.find(']') != std::string_view::npos;
  const bool has_quantifier =  // NOLINT(cppcoreguidelines-init-variables)
      pattern.find('{') != std::string_view::npos ||
      pattern.find('}') != std::string_view::npos;

  if (const bool has_d_or_w_escape =  // NOLINT(cppcoreguidelines-init-variables)
          pattern.find("\\d") != std::string_view::npos ||
          pattern.find("\\w") != std::string_view::npos;
      has_anchor_start || has_anchor_end || has_double_star ||
      has_char_class || has_quantifier || has_d_or_w_escape) {
    return PatternType::PathPattern;
  }

  // 3. Glob detection (simple wildcards)
  if (pattern.find('*') != std::string_view::npos ||
      pattern.find('?') != std::string_view::npos) {
    return PatternType::Glob;
  }

  // 4. Default: substring
  return PatternType::Substring;
}

// Extract the actual pattern (remove prefix if present)
inline std::string ExtractPattern(std::string_view input) {
  if (input.size() >= 3 &&
      (input.substr(0, 3) == "rs:" || input.substr(0, 3) == "pp:")) {
    return std::string(input.substr(3));
  }
  return std::string(input);
}


// Match a pattern against text (for SearchWorker use)
inline bool MatchPattern(std::string_view pattern, std::string_view text,
                         bool case_sensitive) {
  if (pattern.empty()) {
    return true; // Empty pattern matches everything
  }

  switch (const PatternType type = DetectPatternType(pattern); type) {  // NOLINT(cppcoreguidelines-init-variables) - Variable initialized in C++17 init-statement
  case PatternType::StdRegex: {
    const std::string regex_pattern = ExtractPattern(pattern);
    // Empty pattern after extracting "rs:" prefix is invalid
    if (regex_pattern.empty()) {
      return false;
    }
    // ReDoS protection: Limit pattern length to prevent catastrophic backtracking
    // Note: A proper timeout mechanism would require more complex implementation
    // (e.g., running regex in a separate thread with timeout). This length limit
    // provides basic protection against obviously malicious patterns.
    if (regex_pattern.length() > kMaxPatternLength) {
      return false;
    }
    // Use strict regex matching (no routing to SimpleRegex or string search)
    // User explicitly requested std::regex with rs: prefix, so require valid regex
    return std_regex_utils::RegexMatchStrict(regex_pattern, text, case_sensitive);
  }

  case PatternType::PathPattern: {
    const std::string path_pattern = ExtractPattern(pattern);
    auto options = case_sensitive
                       ? path_pattern::MatchOptions::kNone
                       : path_pattern::MatchOptions::kCaseInsensitive;
    return path_pattern::PathPatternMatches(path_pattern, text, options);
  }

  case PatternType::Glob:
    return case_sensitive ? simple_regex::GlobMatch(pattern, text)
                          : simple_regex::GlobMatchI(pattern, text);

  case PatternType::Substring: {
    if (case_sensitive) {
      return string_search::ContainsSubstring(text, pattern);
    }
    // Case-insensitive: use ContainsSubstringI (no full ToLower copies; uses AVX2/prefix/reverse paths)
    return string_search::ContainsSubstringI(text, pattern);
  }
  }

  return false; // Should never reach here
}

// Type traits for handling differences between const char* and std::string_view
template<typename TextType>
struct TextTypeTraits;

template<>
struct TextTypeTraits<const char*> {
  using ReturnType = lightweight_callable::LightweightCallable<bool, const char*>;
  
  static bool ContainsSubstringCaseSensitive(const char* text, std::string_view pattern) {
    // strstr is safe here: text is null-terminated (from std::string), pattern is bounded by string_view
    return strstr(text, pattern.data()) != nullptr;  // NOSONAR(cpp:S7132) NOLINT(bugprone-suspicious-stringview-data-usage) - text null-terminated, pattern bounded
  }
  
  static bool ContainsSubstringCaseInsensitive(const char* text, std::string_view pattern) {
    return string_search::StrStrCaseInsensitive(text, pattern.data()) != nullptr;  // NOLINT(bugprone-suspicious-stringview-data-usage) - pattern bounded by string_view
  }
  
  static std::string ConvertToStdString(const char* text) {
    return std::string{text};
  }
};

template<>
struct TextTypeTraits<std::string_view> {
  using ReturnType = lightweight_callable::LightweightCallable<bool, std::string_view>;
  
  static bool ContainsSubstringCaseSensitive(std::string_view text, std::string_view pattern) {
    return string_search::ContainsSubstring(text, pattern);
  }
  
  static bool ContainsSubstringCaseInsensitive(std::string_view text, std::string_view pattern) {
    return string_search::ContainsSubstringI(text, pattern);
  }
  
  static std::string ConvertToStdString(std::string_view text) {
    return std::string{text};
  }
};

// Helpers for CreateMatcherImpl: each handles one pattern type to keep complexity per function under Sonar limit.
template<typename TextType>
inline typename TextTypeTraits<TextType>::ReturnType CreateMatcherImplStdRegex(
    std::string_view pattern, bool case_sensitive) {
  using Traits = TextTypeTraits<TextType>;
  const std::string regex_pattern = ExtractPattern(pattern);
  if (regex_pattern.empty()) {
    return [](TextType) { return false; };
  }
  if (regex_pattern.length() > kMaxPatternLength) {
    return [](TextType) { return false; };
  }
  const regex_t* compiled_regex =
      std_regex_utils::GetCache().GetRegex(regex_pattern, case_sensitive);
  if (compiled_regex == nullptr) {
    return [](TextType) { return false; };
  }
  const bool requires_full_match = std_regex_utils::detail::RequiresFullMatch(regex_pattern);
  return [compiled_regex, requires_full_match](TextType text) {
    const std::string text_str = Traits::ConvertToStdString(text);
    if (requires_full_match) {
      return regex_match(text_str, *compiled_regex);
    }
    return regex_search(text_str, *compiled_regex);
  };
}

template<typename TextType>
inline typename TextTypeTraits<TextType>::ReturnType CreateMatcherImplPathPattern(
    std::string_view pattern, bool case_sensitive) {
  const std::string path_pattern = ExtractPattern(pattern);
  if (path_pattern.empty()) {
    return [](TextType) { return true; };
  }
  auto options = case_sensitive
                     ? path_pattern::MatchOptions::kNone
                     : path_pattern::MatchOptions::kCaseInsensitive;
  auto compiled_ptr = std::make_shared<path_pattern::CompiledPathPattern>(
      path_pattern::CompilePathPattern(path_pattern, options));
  return [compiled_ptr](TextType text) {
    return path_pattern::PathPatternMatches(*compiled_ptr, text);
  };
}

template<typename TextType>
inline typename TextTypeTraits<TextType>::ReturnType CreateMatcherImplGlob(
    std::string_view pattern, bool case_sensitive) {
  const std::string pattern_str(pattern);
  if (case_sensitive) {
    return [pattern_str](TextType text) {  // NOLINT(bugprone-exception-escape) - matcher may throw from regex/glob
      return simple_regex::GlobMatch(pattern_str, text);
    };
  }
  return [pattern_str](TextType text) {  // NOLINT(bugprone-exception-escape)
    return simple_regex::GlobMatchI(pattern_str, text);
  };
}

template<typename TextType>
inline typename TextTypeTraits<TextType>::ReturnType CreateMatcherImplSubstring(
    std::string_view pattern, bool case_sensitive, std::string_view pattern_lower) {
  if (case_sensitive) {
    if constexpr (std::is_same_v<TextType, const char*>) {
      const std::string pattern_str(pattern);
      return [pattern_str](const char* text) {  // NOLINT(bugprone-exception-escape)
        return std::string_view(text).find(pattern_str) != std::string_view::npos;
      };
    }
    const std::string_view pattern_view(pattern);
    return [pattern_view](std::string_view text) {  // NOLINT(bugprone-exception-escape)
      return string_search::ContainsSubstring(text, pattern_view);
    };
  }
  const std::string lower_pattern =
      pattern_lower.empty() ? ToLower(pattern) : std::string(pattern_lower);
  if constexpr (std::is_same_v<TextType, const char*>) {
    return [lower_pattern](const char* text) {  // NOLINT(bugprone-exception-escape)
      return (string_search::StrStrCaseInsensitive(
                  text, lower_pattern.c_str()) != nullptr);
    };
  }
  return [lower_pattern](std::string_view text) {  // NOLINT(bugprone-exception-escape)
    return string_search::ContainsSubstringI(text, lower_pattern);
  };
}

// Common implementation for creating matchers (eliminates duplication)
template<typename TextType>
inline typename TextTypeTraits<TextType>::ReturnType
CreateMatcherImpl(std::string_view pattern, bool case_sensitive,
                  std::string_view pattern_lower = "") {
  if (pattern.empty()) {
    return [](TextType) { return true; };
  }
  switch (const PatternType type = DetectPatternType(pattern); type) {  // NOLINT(cppcoreguidelines-init-variables) - Variable initialized in C++17 init-statement
  case PatternType::StdRegex:
    return CreateMatcherImplStdRegex<TextType>(pattern, case_sensitive);
  case PatternType::PathPattern:
    return CreateMatcherImplPathPattern<TextType>(pattern, case_sensitive);
  case PatternType::Glob:
    return CreateMatcherImplGlob<TextType>(pattern, case_sensitive);
  case PatternType::Substring:
    return CreateMatcherImplSubstring<TextType>(pattern, case_sensitive, pattern_lower);
  }
  return [](TextType) { return false; };
}

// Create a filename matcher function (for FileIndex use)
// Returns a function that matches a C-string filename against the pattern
// Uses lightweight callable wrapper for better performance in hot paths
inline lightweight_callable::LightweightCallable<bool, const char *>
CreateFilenameMatcher(std::string_view pattern, bool case_sensitive,
                      std::string_view pattern_lower = "") {
  return CreateMatcherImpl<const char*>(pattern, case_sensitive, pattern_lower);
}

// Create a path matcher function (for FileIndex use)
// Returns a function that matches a string_view path against the pattern
// Uses lightweight callable wrapper for better performance in hot paths
inline lightweight_callable::LightweightCallable<bool, std::string_view>
CreatePathMatcher(std::string_view pattern, bool case_sensitive,  // NOLINT(readability-identifier-naming,cppcoreguidelines-avoid-non-const-global-variables) - PascalCase API
                  std::string_view pattern_lower = "") {
  return CreateMatcherImpl<std::string_view>(pattern, case_sensitive, pattern_lower);
}

// Create a filename matcher using a pre-compiled PathPattern (optimization:
// compile once before threads, pass to all threads).
// This avoids recompiling the same pattern in each thread.
inline lightweight_callable::LightweightCallable<bool, const char *>
CreateFilenameMatcher(  // NOLINT(readability-identifier-naming,cppcoreguidelines-avoid-non-const-global-variables) - PascalCase API; checker misidentifies function as global
    const std::shared_ptr<path_pattern::CompiledPathPattern>& compiled_pattern) {
  if (!compiled_pattern || !compiled_pattern->valid) {
    return [](const char *) { return false; };
  }

  return [compiled_pattern](const char *filename) {
    return path_pattern::PathPatternMatches(*compiled_pattern, filename);
  };
}

// Create a path matcher using a pre-compiled PathPattern (optimization:
// compile once before threads, pass to all threads).
// This avoids recompiling the same pattern in each thread.
inline lightweight_callable::LightweightCallable<bool, std::string_view>
CreatePathMatcher(  // NOLINT(readability-identifier-naming,cppcoreguidelines-avoid-non-const-global-variables) - PascalCase API; checker misidentifies function as global
    const std::shared_ptr<path_pattern::CompiledPathPattern>& compiled_pattern) {
  if (!compiled_pattern || !compiled_pattern->valid) {
    return [](std::string_view) { return false; };
  }

  return [compiled_pattern](std::string_view dir_path) {
    return path_pattern::PathPatternMatches(*compiled_pattern, dir_path);
  };
}

/**
 * @brief Check if extension matches using heterogeneous lookup (no allocation in hot path)
 *
 * Uses ExtensionSet (std::set with std::less<void>) for transparent find(string_view).
 * Case-sensitive: find(ext_view) directly. Case-insensitive: lowercase into stack buffer, then find.
 *
 * This function is shared between ParallelSearchEngine and FileIndex to eliminate
 * code duplication.
 *
 * @param ext_view Extension string view to check
 * @param extension_set Set of allowed extensions (already lowercased for case-insensitive)
 * @param case_sensitive True for case-sensitive matching, false for case-insensitive
 * @return True if extension matches, false otherwise
 */
inline bool ExtensionMatches(std::string_view ext_view,
                             const ExtensionSet& extension_set,
                             bool case_sensitive) {
  if (ext_view.empty()) {
    return (extension_set.find(std::string_view("")) != extension_set.end());  // NOLINT(readability-container-contains) - C++17: no .contains()
  }
  if (case_sensitive) {
    return (extension_set.find(ext_view) != extension_set.end());  // NOLINT(readability-container-contains) - C++17: no .contains()
  }
  // Case-insensitive: lowercase into stack buffer (no heap allocation in hot path)
  constexpr size_t k_max_ext_len = 255;
  if (ext_view.size() > k_max_ext_len) {
    return false;
  }
  std::array<char, k_max_ext_len> lower_buf{};
  size_t i = 0;  // NOLINT(misc-const-correctness) - incremented in loop
  for (const char c : ext_view) {
    lower_buf[i] = string_search::ToLowerChar(static_cast<unsigned char>(c));  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index) - i bounded by ext_view.size() <= k_max_ext_len
    ++i;
  }
  const std::string_view lower_view(lower_buf.data(), ext_view.size());
  return (extension_set.find(lower_view) != extension_set.end());  // NOLINT(readability-container-contains) - C++17: no .contains()
}

}  // namespace search_pattern_utils
