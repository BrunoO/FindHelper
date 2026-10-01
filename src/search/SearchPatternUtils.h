#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>

#include <string>
#include <string_view>
#include <variant>

#include "path/PathPatternMatcher.h"
#include "search/SearchContext.h"
#include "utils/RegexAliases.h"
#include "utils/StdRegexUtils.h"
#include "utils/StringSearch.h"
#include "utils/StringUtils.h"

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
  Substring,   // default - substring search
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
    const auto prefix = pattern.substr(0, 3);
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

// Zero-copy view of the pattern after stripping an explicit rs:/pp:/fz: prefix.
// The returned view refers into @p input (no allocation).
[[nodiscard]] inline std::string_view ExtractPatternView(std::string_view input) {
  if (input.size() >= 3 &&
      (input.substr(0, 3) == "rs:" || input.substr(0, 3) == "pp:" ||
       input.substr(0, 3) == "fz:")) {
    return input.substr(3);
  }
  return input;
}

namespace search_pattern_utils_detail {

struct StdRegexMatcherState {
  const regex_t* compiled_regex_ = nullptr;  // NOLINT(readability-identifier-naming) - project convention: snake_case_ members
  bool requires_full_match_ = false;          // NOLINT(readability-identifier-naming) - project convention: snake_case_ members
  bool match_at_start_ = false;               // NOLINT(readability-identifier-naming) - start-anchored only (^...); regex_search with match_continuous
  bool case_sensitive_ = false;               // NOLINT(readability-identifier-naming) - project convention: snake_case_ members
  std::string prefilter_;                     // NOLINT(readability-identifier-naming) - project convention: snake_case_ members
  string_search::SimdSubstringHints prefilter_hints_{};  // NOLINT(readability-identifier-naming) - project convention: snake_case_ members
};

inline bool RunStdRegexMatcher(const StdRegexMatcherState& state, std::string_view text) {
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
  if (state.match_at_start_) {
    return regex_search(begin, end, *state.compiled_regex_, regex_constants::match_continuous);
  }
  return regex_search(begin, end, *state.compiled_regex_);
}

}  // namespace search_pattern_utils_detail

// Matcher that matches everything (empty query or no filter)
struct AllPassMatcher {
  constexpr bool operator()(std::string_view /*text*/) const noexcept { return true; }
};

// Matcher that matches nothing (invalid / uncompiled pattern)
struct EmptyMatcher {
  constexpr bool operator()(std::string_view /*text*/) const noexcept { return false; }
};

// Substring matcher (supports case-sensitive & case-insensitive with SIMD hints)
struct SubstringMatcher {
  std::string pattern;
  string_search::SimdSubstringHints hints{};
  bool case_sensitive = false;

  bool operator()(std::string_view text) const {
    if (case_sensitive) {
      return string_search::ContainsSubstring(text, pattern, hints);
    }
    return string_search::ContainsSubstringI(text, pattern, hints);
  }
};

// Prefix matcher (e.g. "prefix*")
struct GlobPrefixMatcher {
  std::string literal;
  bool case_sensitive = false;

  bool operator()(std::string_view text) const {
    if (text.size() < literal.size()) {
      return false;
    }
    if (case_sensitive) {
      return text.substr(0, literal.size()) == literal;
    }
    return string_search::string_search_detail::CheckPrefix<string_search::string_search_detail::CaseInsensitive>(
        text, literal);
  }
};

// Suffix matcher (e.g. "*suffix")
struct GlobSuffixMatcher {
  std::string literal;
  bool case_sensitive = false;

  bool operator()(std::string_view text) const {
    if (text.size() < literal.size()) {
      return false;
    }
    const std::string_view tail = text.substr(text.size() - literal.size());
    if (case_sensitive) {
      return tail == literal;
    }
    for (size_t i = 0; i < literal.size(); ++i) {
      if (string_search::ToLowerChar(static_cast<unsigned char>(tail[i])) !=  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - i bounded by literal.size() == tail.size()
          string_search::ToLowerChar(static_cast<unsigned char>(literal[i]))) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - i bounded by literal.size()
        return false;
      }
    }
    return true;
  }
};

// Infix / contains glob matcher (e.g. "*substring*")
struct GlobContainsMatcher {
  std::string literal;
  string_search::SimdSubstringHints hints{};
  bool case_sensitive = false;

  bool operator()(std::string_view text) const {
    if (case_sensitive) {
      return string_search::ContainsSubstring(text, literal, hints);
    }
    return string_search::ContainsSubstringI(text, literal, hints);
  }
};

// General wildcard glob matcher (e.g. "a*b?c")
struct GeneralGlobMatcher {
  std::shared_ptr<path_pattern::CompiledPathPattern> compiled_pattern;

  bool operator()(std::string_view text) const {
    if (!compiled_pattern || !compiled_pattern->valid) {
      return false;
    }
    return path_pattern::PathPatternMatches(*compiled_pattern, text);
  }
};

// Standard Regex matcher (`rs:` or `vs:`)
struct StdRegexMatcher {
  std::shared_ptr<search_pattern_utils_detail::StdRegexMatcherState> state;

  bool operator()(std::string_view text) const {
    return state && search_pattern_utils_detail::RunStdRegexMatcher(*state, text);
  }
};

// Path pattern matcher (`pp:`)
struct PathPatternMatcher {
  std::shared_ptr<path_pattern::CompiledPathPattern> compiled_pattern;

  bool operator()(std::string_view text) const {
    return compiled_pattern && compiled_pattern->valid &&
           path_pattern::PathPatternMatches(*compiled_pattern, text);
  }
};

// Fuzzy matcher (`fz:`)
struct FuzzyMatcher {
  std::string pattern;
  bool case_sensitive = false;

  bool operator()(std::string_view text) const {
    return case_sensitive ? string_search::FuzzyMatch(text, pattern)
                          : string_search::FuzzyMatchI(text, pattern);
  }
};

// Concrete variant representing any pattern matcher in FindHelper.
// Value-semantics, zero-heap-allocation, fully inlinable via std::visit.
struct PatternMatcher : std::variant<  // NOSONAR(cpp:S110) - depth comes from std::variant stdlib internals; single domain level
    AllPassMatcher,
    EmptyMatcher,
    SubstringMatcher,
    GlobPrefixMatcher,
    GlobSuffixMatcher,
    GlobContainsMatcher,
    GeneralGlobMatcher,
    StdRegexMatcher,
    PathPatternMatcher,
    FuzzyMatcher
> {
  using variant::variant;

  bool operator()(std::string_view text) const {
    return std::visit([text](const auto& m) { return m(text); }, *this);
  }

  explicit operator bool() const noexcept {
    return !std::holds_alternative<AllPassMatcher>(*this);
  }
};

namespace search_pattern_utils_detail {

inline std::optional<PatternMatcher> TryGlobShapeMatcher(
    std::string_view pattern_str, bool case_sensitive) {
  if (pattern_str.find('?') != std::string_view::npos) {
    return std::nullopt;
  }
  if (pattern_str.find_first_not_of('*') == std::string_view::npos) {
    return PatternMatcher{AllPassMatcher{}};
  }
  const auto star_count =
      static_cast<size_t>(std::count(pattern_str.begin(), pattern_str.end(), '*'));  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
  if (star_count == 2 && pattern_str.front() == '*' && pattern_str.back() == '*') {
    const std::string lit(pattern_str.substr(1, pattern_str.size() - 2));
    const std::string lower = case_sensitive ? lit : ToLower(lit);
    const string_search::SimdSubstringHints hints = string_search::MakeSimdSubstringHints(lower);
    return PatternMatcher{GlobContainsMatcher{lower, hints, case_sensitive}};
  }
  if (star_count == 1 && pattern_str.back() == '*') {
    return PatternMatcher{GlobPrefixMatcher{std::string(pattern_str.substr(0, pattern_str.size() - 1)), case_sensitive}};
  }
  if (star_count == 1 && pattern_str.front() == '*') {
    return PatternMatcher{GlobSuffixMatcher{std::string(pattern_str.substr(1)), case_sensitive}};
  }
  return std::nullopt;
}

inline PatternMatcher CreateMatcherImplStdRegex(
    std::string_view pattern, bool case_sensitive) {
  const std::string regex_pattern = ExtractPattern(pattern);
  if (regex_pattern.empty() || regex_pattern.length() > kMaxPatternLength) {
    return PatternMatcher{EmptyMatcher{}};
  }
  const regex_t* compiled_regex =
      std_regex_utils::GetCache().GetRegex(regex_pattern, case_sensitive);
  if (compiled_regex == nullptr) {
    return PatternMatcher{EmptyMatcher{}};
  }
  const bool requires_full_match = std_regex_utils::detail::RequiresFullMatch(regex_pattern);
  const bool match_at_start = std_regex_utils::detail::RequiresMatchAtStart(regex_pattern);

  constexpr size_t kMinPrefilterLength = 3;
  const std::string raw_literal = std_regex_utils::detail::ExtractRequiredLiteral(regex_pattern);
  std::string prefilter;
  if (raw_literal.size() >= kMinPrefilterLength) {
    prefilter = case_sensitive ? raw_literal : ToLower(raw_literal);
  }

  auto state = std::make_shared<StdRegexMatcherState>();
  state->compiled_regex_ = compiled_regex;
  state->requires_full_match_ = requires_full_match;
  state->match_at_start_ = match_at_start;
  state->case_sensitive_ = case_sensitive;
  state->prefilter_ = std::move(prefilter);
  state->prefilter_hints_ = string_search::MakeSimdSubstringHints(state->prefilter_);
  return PatternMatcher{StdRegexMatcher{std::move(state)}};
}

inline PatternMatcher CreateMatcherImplPathPattern(
    std::string_view pattern, bool case_sensitive) {
  const std::string path_pattern = ExtractPattern(pattern);
  if (path_pattern.empty()) {
    return PatternMatcher{AllPassMatcher{}};
  }
  const auto options = case_sensitive
                     ? path_pattern::MatchOptions::kNone
                     : path_pattern::MatchOptions::kCaseInsensitive;
  auto compiled_ptr = std::make_shared<path_pattern::CompiledPathPattern>(
      path_pattern::CompilePathPattern(path_pattern, options));
  return PatternMatcher{PathPatternMatcher{std::move(compiled_ptr)}};
}

inline PatternMatcher CreateMatcherImplFuzzy(
    std::string_view pattern, bool case_sensitive) {
  const std::string fuzzy_pattern = ExtractPattern(pattern);
  if (fuzzy_pattern.empty()) {
    return PatternMatcher{AllPassMatcher{}};
  }
  return PatternMatcher{FuzzyMatcher{fuzzy_pattern, case_sensitive}};
}

inline PatternMatcher CreateMatcherImplGlob(
    std::string_view pattern, bool case_sensitive) {
  const std::string pattern_str(pattern);
  if (auto shaped = TryGlobShapeMatcher(pattern_str, case_sensitive);
      shaped.has_value()) {
    return *shaped;
  }
  const auto options = case_sensitive
                     ? path_pattern::MatchOptions::kNone
                     : path_pattern::MatchOptions::kCaseInsensitive;

  std::string path_pattern_str;
  path_pattern_str.reserve(pattern_str.size() * 2);
  for (char c : pattern_str) {
    if (c == '*') {
      path_pattern_str += "**";
    } else {
      path_pattern_str += c;
    }
  }

  if (path_pattern_str.empty() || path_pattern_str.find("**") != 0) {
    path_pattern_str.insert(0, "**");
  }
  if (path_pattern_str.size() < 2 || path_pattern_str.compare(path_pattern_str.size() - 2, 2, "**") != 0) {
    path_pattern_str += "**";
  }

  auto compiled_ptr = std::make_shared<path_pattern::CompiledPathPattern>(
      path_pattern::CompilePathPattern(path_pattern_str, options));
  return PatternMatcher{GeneralGlobMatcher{std::move(compiled_ptr)}};
}

inline PatternMatcher CreateMatcherImplSubstring(
    std::string_view pattern, bool case_sensitive, std::string_view pattern_lower) {
  if (case_sensitive) {
    const std::string pattern_str(pattern);
    const string_search::SimdSubstringHints hints = string_search::MakeSimdSubstringHints(pattern_str);
    return PatternMatcher{SubstringMatcher{pattern_str, hints, true}};
  }
  const std::string lower_pattern =
      pattern_lower.empty() ? ToLower(pattern) : std::string(pattern_lower);
  const string_search::SimdSubstringHints hints = string_search::MakeSimdSubstringHints(lower_pattern);
  return PatternMatcher{SubstringMatcher{lower_pattern, hints, false}};
}

}  // namespace search_pattern_utils_detail

inline PatternMatcher CreateMatcherImpl(std::string_view pattern, bool case_sensitive,
                                        std::string_view pattern_lower = "") {
  if (pattern.empty()) {
    return PatternMatcher{AllPassMatcher{}};
  }
  switch (const PatternType type = DetectPatternType(pattern); type) {
  case PatternType::StdRegex:
    return search_pattern_utils_detail::CreateMatcherImplStdRegex(pattern, case_sensitive);
  case PatternType::PathPattern:
    return search_pattern_utils_detail::CreateMatcherImplPathPattern(pattern, case_sensitive);
  case PatternType::Fuzzy:
    return search_pattern_utils_detail::CreateMatcherImplFuzzy(pattern, case_sensitive);
  case PatternType::Glob:
    return search_pattern_utils_detail::CreateMatcherImplGlob(pattern, case_sensitive);
  case PatternType::Substring:
  default:
    return search_pattern_utils_detail::CreateMatcherImplSubstring(pattern, case_sensitive, pattern_lower);
  }
}

// Create a reusable pattern matcher (canonical factory).
// Returns a PatternMatcher that matches a std::string_view against the pattern.
// Callers pass filename (last segment) or full path depending on context.
inline PatternMatcher CreatePatternMatcher(std::string_view pattern, bool case_sensitive,
                                           std::string_view pattern_lower = "") {
  return CreateMatcherImpl(pattern, case_sensitive, pattern_lower);
}

// Create a filename matcher (matches bounded filename std::string_view)
inline PatternMatcher CreateFilenameMatcher(std::string_view pattern, bool case_sensitive,
                                            std::string_view pattern_lower = "") {
  return CreatePatternMatcher(pattern, case_sensitive, pattern_lower);
}

// Create a path matcher (matches string_view path against pattern)
inline PatternMatcher CreatePathMatcher(std::string_view pattern, bool case_sensitive,  // NOLINT(readability-identifier-naming,cppcoreguidelines-avoid-non-const-global-variables) - PascalCase API
                                        std::string_view pattern_lower = "") {
  return CreatePatternMatcher(pattern, case_sensitive, pattern_lower);
}

// Pre-compiled PathPattern overloads
inline PatternMatcher CreatePatternMatcher(  // NOLINT(readability-identifier-naming,cppcoreguidelines-avoid-non-const-global-variables) - PascalCase API
    const std::shared_ptr<path_pattern::CompiledPathPattern>& compiled_pattern) {
  if (!compiled_pattern || !compiled_pattern->valid) {
    return PatternMatcher{EmptyMatcher{}};
  }
  return PatternMatcher{PathPatternMatcher{compiled_pattern}};
}

inline PatternMatcher CreateFilenameMatcher(  // NOLINT(readability-identifier-naming,cppcoreguidelines-avoid-non-const-global-variables) - PascalCase API
    const std::shared_ptr<path_pattern::CompiledPathPattern>& compiled_pattern) {
  return CreatePatternMatcher(compiled_pattern);
}

inline PatternMatcher CreatePathMatcher(  // NOLINT(readability-identifier-naming,cppcoreguidelines-avoid-non-const-global-variables) - PascalCase API
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
