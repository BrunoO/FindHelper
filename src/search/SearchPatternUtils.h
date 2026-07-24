#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
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

namespace search_pattern_utils {
// Maximum pattern length to prevent ReDoS (Regular Expression Denial of Service) attacks
// Patterns longer than this will be rejected to prevent catastrophic backtracking
constexpr size_t kMaxPatternLength = 1000;

// Pattern type enumeration
enum class PatternType : std::uint8_t {  // NOLINT(performance-enum-size) - explicit uint8_t; checker may still flag in some contexts
  StdRegex,    // rs: prefix - full ECMAScript regex
  PathPattern, // pp: prefix OR auto-detected (contains ^, $, **, [], {}, \d, \w) - advanced path pattern
  Fuzzy,       // fz: prefix - fuzzy matching (subsequence)
  Glob,        // contains * or ? - wildcard pattern (matches substrings)
  Substring    // default - substring search
};

// Detect the type of pattern based on its content
// Detection priority:
// 1. Explicit prefixes (rs:, pp:, fz:) - highest priority
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
    if (prefix == "fz:") {
      return PatternType::Fuzzy;
    }
  }

  // 2. Advanced PathPattern auto-detect (no prefix, but clearly using PathPattern features)
  const bool has_anchor_start = !pattern.empty() && pattern.front() == '^';
  const bool has_anchor_end = !pattern.empty() && pattern.back() == '$';
  const bool has_double_star = pattern.find("**") != std::string_view::npos;
  const bool has_char_class =
      pattern.find('[') != std::string_view::npos ||
      pattern.find(']') != std::string_view::npos;
  const bool has_quantifier =
      pattern.find('{') != std::string_view::npos ||
      pattern.find('}') != std::string_view::npos;

  if (const bool has_d_or_w_escape =
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
      (input.substr(0, 3) == "rs:" || input.substr(0, 3) == "pp:" ||
       input.substr(0, 3) == "fz:")) {
    return std::string(input.substr(3));
  }
  return std::string(input);
}

// Type traits for matcher text type (bounded std::string_view in hot paths).
template<typename TextType>
struct TextTypeTraits;

template<>
struct TextTypeTraits<std::string_view> {
  using ReturnType = lightweight_callable::LightweightCallable<bool, std::string_view>;

  static bool ContainsSubstringCaseSensitive(std::string_view text, std::string_view pattern) {
    return string_search::ContainsSubstring(text, pattern);
  }

  static bool ContainsSubstringCaseInsensitive(std::string_view text, std::string_view pattern) {
    return string_search::ContainsSubstringI(text, pattern);
  }
};

namespace search_pattern_utils_detail {

struct StdRegexMatcherState {
  const regex_t* compiled_regex_ = nullptr;
  bool requires_full_match_ = false;
  bool case_sensitive_ = false;
  std::string prefilter_;
  string_search::SimdSubstringHints prefilter_hints_{};
};

template<typename TextType>
bool RunStdRegexMatcher(const StdRegexMatcherState& state, TextType text) {
  if (!state.prefilter_.empty()) {
    if (state.case_sensitive_) {
      if (!string_search::ContainsSubstring(text, state.prefilter_, state.prefilter_hints_)) {
        return false;
      }
    } else if (!string_search::ContainsSubstringI(text, state.prefilter_, state.prefilter_hints_)) {
      return false;
    }
  }
  const auto begin = text.begin();
  const auto end = text.end();
  if (state.requires_full_match_) {
    return regex_match(begin, end, *state.compiled_regex_);
  }
  return regex_search(begin, end, *state.compiled_regex_);
}

}  // namespace search_pattern_utils_detail

// Helpers for CreateMatcherImpl: each handles one pattern type to keep complexity per function under Sonar limit.
template<typename TextType>
inline typename TextTypeTraits<TextType>::ReturnType CreateMatcherImplStdRegex(
    std::string_view pattern, bool case_sensitive) {
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

  // Extract a required literal substring from the pattern and use it as a
  // fast pre-filter.  Any text that does not contain the literal cannot match
  // the regex, so we skip the ~1 µs regex_search call entirely.
  // A minimum length of 3 avoids the overhead on trivially short literals.
  constexpr size_t kMinPrefilterLength = 3;
  const std::string raw_literal = std_regex_utils::detail::ExtractRequiredLiteral(regex_pattern);
  std::string prefilter;
  if (raw_literal.size() >= kMinPrefilterLength) {
    prefilter = case_sensitive ? raw_literal : ToLower(raw_literal);
  }

  if (prefilter.empty()) {
    return [compiled_regex, requires_full_match](TextType text) {  // NOLINT(bugprone-exception-escape) - regex_match/search pre-validated by GetCache; no throw expected
      if (requires_full_match) {
        return regex_match(std::begin(text), std::end(text), *compiled_regex);
      }
      return regex_search(std::begin(text), std::end(text), *compiled_regex);
    };
  }

  // Single shared_ptr capture keeps the lambda within LightweightCallable SBO on all STLs (MSVC/libstdc++/libc++).
  auto state = std::make_shared<search_pattern_utils_detail::StdRegexMatcherState>();
  state->compiled_regex_ = compiled_regex;
  state->requires_full_match_ = requires_full_match;
  state->case_sensitive_ = case_sensitive;
  state->prefilter_ = std::move(prefilter);
  state->prefilter_hints_ = string_search::MakeSimdSubstringHints(state->prefilter_);
  return [state](TextType text) {  // NOLINT(bugprone-exception-escape) - regex_match/search pre-validated by GetCache; no throw expected
    return search_pattern_utils_detail::RunStdRegexMatcher(*state, text);
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
inline typename TextTypeTraits<TextType>::ReturnType CreateMatcherImplFuzzy(
    std::string_view pattern, bool case_sensitive) {
  const std::string fuzzy_pattern = ExtractPattern(pattern);
  if (fuzzy_pattern.empty()) {
    return [](TextType) { return true; };
  }
  if (case_sensitive) {
    return [fuzzy_pattern](TextType text) {  // NOLINT(bugprone-exception-escape) - FuzzyMatch performs string comparison and does not throw in practice
      return string_search::FuzzyMatch(text, fuzzy_pattern);
    };
  }
  return [fuzzy_pattern](TextType text) {  // NOLINT(bugprone-exception-escape) - FuzzyMatchI performs string comparison and does not throw in practice
    return string_search::FuzzyMatchI(text, fuzzy_pattern);
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
    const std::string_view pattern_view(pattern);
    const string_search::SimdSubstringHints hints = string_search::MakeSimdSubstringHints(pattern_view);
    return [pattern_view, hints](TextType text) {  // NOLINT(bugprone-exception-escape)
      return string_search::ContainsSubstring(text, pattern_view, hints);
    };
  }
  const std::string lower_pattern =
      pattern_lower.empty() ? ToLower(pattern) : std::string(pattern_lower);
  const string_search::SimdSubstringHints hints = string_search::MakeSimdSubstringHints(lower_pattern);
  return [lower_pattern, hints](TextType text) {  // NOLINT(bugprone-exception-escape)
    return string_search::ContainsSubstringI(text, lower_pattern, hints);
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
  switch (const PatternType type = DetectPatternType(pattern); type) {
  case PatternType::StdRegex:
    return CreateMatcherImplStdRegex<TextType>(pattern, case_sensitive);
  case PatternType::PathPattern:
    return CreateMatcherImplPathPattern<TextType>(pattern, case_sensitive);
  case PatternType::Fuzzy:
    return CreateMatcherImplFuzzy<TextType>(pattern, case_sensitive);
  case PatternType::Glob:
    return CreateMatcherImplGlob<TextType>(pattern, case_sensitive);
  case PatternType::Substring:
    return CreateMatcherImplSubstring<TextType>(pattern, case_sensitive, pattern_lower);
  }
  return [](TextType) { return false; };
}

// Create a reusable pattern matcher (canonical factory).
// Returns a function that matches a std::string_view against the pattern.
// Callers pass filename (last segment) or full path depending on context.
// Uses lightweight callable wrapper for better performance in hot paths.
inline lightweight_callable::LightweightCallable<bool, std::string_view>
CreatePatternMatcher(std::string_view pattern, bool case_sensitive,
                     std::string_view pattern_lower = "") {
  return CreateMatcherImpl<std::string_view>(pattern, case_sensitive, pattern_lower);
}

// Create a filename matcher function (for FileIndex use)
// Returns a function that matches a bounded filename std::string_view (last path segment only)
inline lightweight_callable::LightweightCallable<bool, std::string_view>
CreateFilenameMatcher(std::string_view pattern, bool case_sensitive,
                      std::string_view pattern_lower = "") {
  return CreatePatternMatcher(pattern, case_sensitive, pattern_lower);
}

// Create a path matcher function (for FileIndex use)
// Returns a function that matches a string_view path against the pattern
inline lightweight_callable::LightweightCallable<bool, std::string_view>
CreatePathMatcher(std::string_view pattern, bool case_sensitive,  // NOLINT(readability-identifier-naming,cppcoreguidelines-avoid-non-const-global-variables) - PascalCase API
                  std::string_view pattern_lower = "") {
  return CreatePatternMatcher(pattern, case_sensitive, pattern_lower);
}

// Create a pattern matcher using a pre-compiled PathPattern (optimization:
// compile once before threads, pass to all threads).
// This avoids recompiling the same pattern in each thread.
inline lightweight_callable::LightweightCallable<bool, std::string_view>
CreatePatternMatcher(  // NOLINT(readability-identifier-naming,cppcoreguidelines-avoid-non-const-global-variables) - PascalCase API; checker misidentifies function as global
    const std::shared_ptr<path_pattern::CompiledPathPattern>& compiled_pattern) {
  if (!compiled_pattern || !compiled_pattern->valid) {
    return [](std::string_view) { return false; };
  }

  return [compiled_pattern](std::string_view text) {
    return path_pattern::PathPatternMatches(*compiled_pattern, text);
  };
}

// Create a filename matcher using a pre-compiled PathPattern (optimization:
// compile once before threads, pass to all threads).
inline lightweight_callable::LightweightCallable<bool, std::string_view>
CreateFilenameMatcher(  // NOLINT(readability-identifier-naming,cppcoreguidelines-avoid-non-const-global-variables) - PascalCase API; checker misidentifies function as global
    const std::shared_ptr<path_pattern::CompiledPathPattern>& compiled_pattern) {
  return CreatePatternMatcher(compiled_pattern);
}

// Create a path matcher using a pre-compiled PathPattern (optimization:
// compile once before threads, pass to all threads).
inline lightweight_callable::LightweightCallable<bool, std::string_view>
CreatePathMatcher(  // NOLINT(readability-identifier-naming,cppcoreguidelines-avoid-non-const-global-variables) - PascalCase API; checker misidentifies function as global
    const std::shared_ptr<path_pattern::CompiledPathPattern>& compiled_pattern) {
  return CreatePatternMatcher(compiled_pattern);
}

namespace extension_set_detail {

struct SvLess {
  using is_transparent = void;  // NOLINT(readability-identifier-naming) - required ADL name for transparent comparison

  [[nodiscard]] bool operator()(std::string_view lhs, std::string_view rhs) const noexcept {
    return lhs < rhs;
  }

  [[nodiscard]] bool operator()(const std::string& lhs, std::string_view rhs) const noexcept {
    return std::string_view(lhs) < rhs;
  }

  [[nodiscard]] bool operator()(std::string_view lhs, const std::string& rhs) const noexcept {
    return lhs < std::string_view(rhs);
  }
};

[[nodiscard]] inline bool Contains(const ExtensionSet& extension_set, std::string_view key) noexcept {
  const auto it = std::lower_bound(extension_set.begin(), extension_set.end(), key, SvLess{});  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
  return it != extension_set.end() && std::string_view(*it) == key;
}

}  // namespace extension_set_detail

/**
 * @brief Check if extension matches using binary search (no allocation in hot path)
 *
 * Uses ExtensionSet (sorted vector) with transparent lower_bound(string_view).
 * Case-sensitive: lookup ext_view directly. Case-insensitive: lowercase into stack buffer, then lookup.
 *
 * This function is shared between ParallelSearchEngine and FileIndex to eliminate
 * code duplication.
 *
 * @param ext_view Extension string view to check
 * @param extension_set Sorted allowed extensions (already lowercased for case-insensitive)
 * @param case_sensitive True for case-sensitive matching, false for case-insensitive
 * @return True if extension matches, false otherwise
 */
inline bool ExtensionMatches(std::string_view ext_view,
                             const ExtensionSet& extension_set,
                             bool case_sensitive) noexcept {
  if (ext_view.empty()) {
    return extension_set_detail::Contains(extension_set, std::string_view(""));
  }
  if (case_sensitive) {
    return extension_set_detail::Contains(extension_set, ext_view);
  }
  // Case-insensitive: lowercase into stack buffer (no heap allocation in hot path)
  constexpr size_t k_max_ext_len = 255;
  if (ext_view.size() > k_max_ext_len) {
    return false;
  }
  std::array<char, k_max_ext_len> lower_buf{};
  size_t i = 0;  // NOLINT(misc-const-correctness) - incremented in loop
  for (const char c : ext_view) {
    lower_buf[i] = string_search::ToLowerChar(static_cast<unsigned char>(c));  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - i bounded by ext_view.size() <= k_max_ext_len
    ++i;
  }
  const std::string_view lower_view(lower_buf.data(), ext_view.size());
  return extension_set_detail::Contains(extension_set, lower_view);
}

}  // namespace search_pattern_utils
