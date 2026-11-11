#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include "path/PathPatternMatcher.h"
#include "search/SearchContext.h"
#include "search/SearchPrefixes.h"
#include "search/rx/RxCompiler.h"
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
  StdRegex,    // re: prefix - full ECMAScript regex
  PathPattern, // pp: prefix OR auto-detected (contains ^, $, **) - fast glob path pattern
  Fuzzy,       // fz: prefix - fuzzy matching (subsequence)
  Glob,        // contains * or ? - wildcard pattern (matches substrings)
  Substring,   // default - substring search
  Rx,          // rx: prefix - S-expression structured regex
};

// Detect the type of pattern based on its content
// Detection priority:
// 1. Explicit prefixes (re:, pp:, fz:, rx:) - highest priority
// 2. PathPattern features (^, $, **) - auto-detect
// 3. Glob patterns (*, ?) - simple wildcards
// 4. Substring - default fallback
inline PatternType DetectPatternType(std::string_view pattern) {
  // 1. Explicit prefixes (highest priority). Tokens live in SearchPrefixes.h so
  // this dispatch cannot drift from StripPrefix / HasExplicitPrefix.
  if (pattern.size() >= prefix::kTokenLength) {
    const auto token = pattern.substr(0, prefix::kTokenLength);
    if (token == prefix::kRegex) {
      return PatternType::StdRegex;
    }
    if (token == prefix::kPathPattern) {
      return PatternType::PathPattern;
    }
    if (token == prefix::kFuzzy) {
      return PatternType::Fuzzy;
    }
    if (token == prefix::kRx) {
      return PatternType::Rx;
    }
  }

  // 2. PathPattern auto-detect (no prefix, but clearly using PathPattern features)
  if (const bool is_path_pattern =
          (!pattern.empty() && (pattern.front() == '^' || pattern.back() == '$')) ||
          pattern.find("**") != std::string_view::npos;
      is_path_pattern) {
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

// Zero-copy view of the pattern after stripping an explicit re:/pp:/fz: prefix.
// The returned view refers into @p input (no allocation).
[[nodiscard]] inline std::string_view ExtractPatternView(std::string_view input) {
  return StripPrefix(input);
}

// Extract the actual pattern (remove prefix if present)
inline std::string ExtractPattern(std::string_view input) {
  return std::string(ExtractPatternView(input));
}

namespace matcher_test_hooks {
inline bool& SkipLiteralPrefilterFlag() {
  static thread_local bool s_flag = false;
  return s_flag;
}

[[nodiscard]] inline bool IsSkipLiteralPrefilterActive() {
  return SkipLiteralPrefilterFlag();
}

class ScopedSkipLiteralPrefilter {
public:
  ScopedSkipLiteralPrefilter() : prev_(SkipLiteralPrefilterFlag()) {
    SkipLiteralPrefilterFlag() = true;
  }
  ~ScopedSkipLiteralPrefilter() {
    SkipLiteralPrefilterFlag() = prev_;
  }
  ScopedSkipLiteralPrefilter(const ScopedSkipLiteralPrefilter&) = delete;
  ScopedSkipLiteralPrefilter& operator=(const ScopedSkipLiteralPrefilter&) = delete;
  ScopedSkipLiteralPrefilter(ScopedSkipLiteralPrefilter&&) = delete;
  ScopedSkipLiteralPrefilter& operator=(ScopedSkipLiteralPrefilter&&) = delete;
private:
  bool prev_ = false;
};
}  // namespace matcher_test_hooks

namespace search_pattern_utils_detail {

struct StdRegexMatcherState {
  const regex_t* compiled_regex_ = nullptr;  // NOLINT(readability-identifier-naming) - project convention: snake_case_ members
  bool requires_full_match_ = false;          // NOLINT(readability-identifier-naming) - project convention: snake_case_ members
  bool match_at_start_ = false;               // NOLINT(readability-identifier-naming) - start-anchored only (^...); regex_search with match_continuous
  bool case_sensitive_ = false;               // NOLINT(readability-identifier-naming) - project convention: snake_case_ members
  std::string prefilter_;                     // NOLINT(readability-identifier-naming) - project convention: snake_case_ members
  string_search::SimdSubstringHints prefilter_hints_{};  // NOLINT(readability-identifier-naming) - project convention: snake_case_ members
  // Required literal suffixes for end-anchored patterns with no required literal
  // (e.g. re:.*\.(cpp|h)$). Lowercased when the search is case-insensitive.
  std::vector<std::string> suffix_prefilter_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_ members
  // Required literal prefixes for start-anchored patterns (e.g. re:^(get|set)_).
  // Lowercased when the search is case-insensitive.
  std::vector<std::string> prefix_prefilter_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_ members
  // Required single-class character run for patterns with no literal at all
  // (e.g. re:\d{4} needs four consecutive digits). `class_run_min_length_ == 0`
  // disables the gate. Only built when no literal/prefix/suffix gate applies.
  std::array<bool, 256> class_run_members_{};  // NOLINT(readability-identifier-naming) - project convention: snake_case_ members
  size_t class_run_min_length_ = 0;            // NOLINT(readability-identifier-naming) - project convention: snake_case_ members
};

// Case-(in)sensitive "does text end with suffix" test. `suffix` is stored
// lowercased for case-insensitive matchers.
inline bool EndsWithAscii(std::string_view text, std::string_view suffix, bool case_sensitive) {
  if (suffix.size() > text.size()) {
    return false;
  }
  const size_t offset = text.size() - suffix.size();
  for (size_t i = 0; i < suffix.size(); ++i) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - offset + suffix.size() <= text.size()
    const char text_char = text[offset + i];
    const char lhs = case_sensitive
                         ? text_char
                         : string_search::ToLowerChar(static_cast<unsigned char>(text_char));
    if (lhs != suffix[i]) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - i < suffix.size()
      return false;
    }
  }
  return true;
}

// Case-(in)sensitive "does text end with any of the required suffixes" test.
inline bool EndsWithAnySuffix(std::string_view text,
                              const std::vector<std::string>& suffixes,
                              bool case_sensitive) {
  const auto ends_with_suffix = [&text, case_sensitive](const std::string& suffix) {
    return EndsWithAscii(text, suffix, case_sensitive);
  };
  return std::any_of(suffixes.begin(), suffixes.end(), ends_with_suffix);
}

// True when text contains a line terminator, i.e. when a line-oriented '$' may
// hold somewhere other than the end of the input.
inline bool ContainsLineTerminator(std::string_view text) {
  return text.find_first_of("\r\n") != std::string_view::npos;
}

// Precondition of the matcher layer, asserted (debug builds) at every production
// site that hands text to a matcher: ParallelSearchEngine.cpp CheckMatchers,
// ParallelSearchEngine.h MatchesPatterns, SearchResultUtils.cpp
// ScanRecycleSnapshot. Matcher input is an indexed name or path, which contains
// no line terminator on the supported platforms (Win32 forbids control
// characters in names outright).
//
// Why it matters: '$' is line-oriented under FastLibsBoost but anchors strictly
// under std::regex (see regex_traits::kDollarIsLineOriented), so line-spanning
// text is the one input where the exact extension fast path cannot decide with a
// suffix compare. SuffixAnchorHolds falls back to the regex there, so a violated
// precondition costs a regex call instead of a wrong row - but the assumption
// should be loud in development, not silent. Calls to this helper are compiled
// out in release builds; note that unit tests drive the matchers directly, so
// they are deliberately not covered by the assertion.
inline void AssertSingleLineMatcherInput([[maybe_unused]] std::string_view text) {
  assert(!ContainsLineTerminator(text) &&
         "matcher input must be an indexed name/path: expected no line terminator");
}

// Pre-filter form of the '$' suffix-anchor test: true when text *may* satisfy
// the anchor. It must never reject a genuine match; over-accepting only costs a
// regex call.
//   - ECMAScript '$' anchors strictly at the end of the input, so "ends with a
//     required suffix" is exact and anything else can be rejected outright.
//   - A line-oriented '$' (see regex_traits::kDollarIsLineOriented) also matches
//     before any line terminator, so a text spanning lines may end a match for
//     reasons no suffix compare can see. Accept it and let the regex decide.
inline bool TextMayEndWithAnySuffix(std::string_view text,
                                    const std::vector<std::string>& suffixes,
                                    bool case_sensitive) {
  if (EndsWithAnySuffix(text, suffixes, case_sensitive)) {
    return true;
  }
  return regex_traits::kDollarIsLineOriented && ContainsLineTerminator(text);
}

// Exact form of the same test, for callers where the answer *is* the decision
// and no regex runs afterwards (the extension fast path).
//   - Without a line terminator in the text, '$' can only hold at the end of the
//     input under either engine, so the suffix compare is exact.
//   - Under a line-oriented '$' a text spanning lines cannot be decided that
//     way; fall back to the regex this fast path replaces. Production input is
//     single-line (AssertSingleLineMatcherInput), so this branch is exercised
//     only by tests and by a release build that somehow sees a line-spanning
//     name: it is a correctness net, not a supported path.
inline bool SuffixAnchorHolds(std::string_view text,
                              const std::vector<std::string>& suffixes,
                              bool case_sensitive,
                              std::string_view extension_pattern) {
  if (!regex_traits::kDollarIsLineOriented || !ContainsLineTerminator(text)) {
    return EndsWithAnySuffix(text, suffixes, case_sensitive);
  }
  return std_regex_utils::RegexMatchStrict(extension_pattern, text, case_sensitive);
}

// Case-(in)sensitive "does text start with prefix" test. Prefixes are stored
// lowercased for case-insensitive matchers. No leading-newline tolerance: a
// non-multiline '^' (and match_continuous) anchors the match at index 0.
inline bool StartsWithAscii(std::string_view text, std::string_view prefix, bool case_sensitive) {
  if (prefix.size() > text.size()) {
    return false;
  }
  for (size_t i = 0; i < prefix.size(); ++i) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - i < prefix.size() <= text.size()
    const char text_char = text[i];
    const char lhs = case_sensitive
                         ? text_char
                         : string_search::ToLowerChar(static_cast<unsigned char>(text_char));
    if (lhs != prefix[i]) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - i < prefix.size()
      return false;
    }
  }
  return true;
}

inline bool TextMayStartWithAnyPrefix(std::string_view text,
                                      const std::vector<std::string>& prefixes,
                                      bool case_sensitive) {
  return std::any_of(prefixes.begin(), prefixes.end(),
                     [text, case_sensitive](const std::string& prefix) {
                       return StartsWithAscii(text, prefix, case_sensitive);
                     });
}

// True when `text` contains at least `min_length` consecutive characters from
// `members`. This is a necessary (never sufficient) condition for a pattern
// whose matches must contain that run, so a false result safely rejects the row.
inline bool TextMayContainClassRun(std::string_view text,
                                   const std::array<bool, 256>& members,
                                   size_t min_length) {
  if (min_length == 0) {
    return true;
  }
  // Raw pointer table lookup: this runs once per search row, so keep it free of
  // std::array bounds-checking machinery (indices are unsigned char, 0-255).
  const bool* const member_table = members.data();
  size_t streak = 0;
  for (const char ch : text) {
    if (!member_table[static_cast<unsigned char>(ch)]) {
      streak = 0;
      continue;
    }
    ++streak;
    if (streak >= min_length) {
      return true;
    }
  }
  return false;
}

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
  if (!state.suffix_prefilter_.empty() &&
      !TextMayEndWithAnySuffix(text, state.suffix_prefilter_, state.case_sensitive_)) {
    return false;
  }
  if (!state.prefix_prefilter_.empty() &&
      !TextMayStartWithAnyPrefix(text, state.prefix_prefilter_, state.case_sensitive_)) {
    return false;
  }
  if (!TextMayContainClassRun(text, state.class_run_members_, state.class_run_min_length_)) {
    return false;
  }
  const auto *const begin = text.begin();
  const auto *const end = text.end();
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

// Glob matcher (prefix: "prefix*" or suffix: "*suffix"), shape selected by is_prefix.
struct GlobMatcher {
  std::string literal;
  bool case_sensitive = false;
  bool is_prefix = true;

  bool operator()(std::string_view text) const {
    if (text.size() < literal.size()) {
      return false;
    }
    if (is_prefix) {
      if (case_sensitive) {
        return text.substr(0, literal.size()) == literal;
      }
      return string_search::string_search_detail::CheckPrefix<string_search::string_search_detail::CaseInsensitive>(
          text, literal);
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

// Suffix-set matcher for `re:` patterns that are provably exactly an extension
// match (e.g. re:.*\.(cpp|h)$). Semantically the same as the regex, but a match
// costs an O(len) suffix compare instead of a regex call. `pattern` is kept for
// the one case a suffix compare cannot decide: a text spanning lines under a
// line-oriented '$' (see search_pattern_utils_detail::SuffixAnchorHolds).
struct ExtensionSetMatcher {
  std::string pattern;                // the re: pattern, for the regex fallback
  std::vector<std::string> suffixes;  // ".ext" (lowercased when !case_sensitive)
  bool case_sensitive = false;

  bool operator()(std::string_view text) const {
    return search_pattern_utils_detail::SuffixAnchorHolds(text, suffixes, case_sensitive, pattern);
  }
};

// Standard Regex matcher (`re:`)
struct StdRegexMatcher {
  std::shared_ptr<search_pattern_utils_detail::StdRegexMatcherState> state;

  bool operator()(std::string_view text) const {
    return state && search_pattern_utils_detail::RunStdRegexMatcher(*state, text);
  }
};

// Path pattern matcher (`pp:` and general wildcard globs)
struct PathPatternMatcher {
  std::shared_ptr<path_pattern::CompiledPathPattern> compiled_pattern;

  bool operator()(std::string_view text) const {
    return compiled_pattern && compiled_pattern->IsValid() &&
           path_pattern::PathPatternMatches(*compiled_pattern, text);
  }
};

// Fuzzy matcher (`fz:`)
struct FuzzyMatcher {
  std::string pattern;
  bool case_sensitive = false;
  bool allow_typos = false;

  bool operator()(std::string_view text) const {
    return case_sensitive ? string_search::FuzzyMatch(text, pattern, allow_typos)
                          : string_search::FuzzyMatchI(text, pattern, allow_typos);
  }
};

// Concrete variant representing any pattern matcher in FindHelper.
// Value-semantics, zero-heap-allocation, fully inlinable via std::visit.
struct PatternMatcher : std::variant<  // NOSONAR(cpp:S110) - depth comes from std::variant stdlib internals; single domain level
    AllPassMatcher,
    EmptyMatcher,
    SubstringMatcher,
    GlobMatcher,
    ExtensionSetMatcher,
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

// Matcher kind. Enumerator order matches the PatternMatcher variant order
// above for readability only; no code depends on it (MatcherKindOf maps each
// alternative explicitly).
enum class MatcherKind : std::uint8_t {  // NOLINT(performance-enum-size) - explicit uint8_t; checker may still flag in some contexts
  AllPass,    // no constraint (empty query)
  Empty,      // matches nothing (invalid pattern)
  Substring,  // literal substring (also *lit* globs)
  GlobPrefix, // lit* globs
  GlobSuffix, // *lit globs
  Extension,  // re: extension-set patterns (built as ExtensionSetMatcher)
  StdRegex,   // re: patterns
  PathPattern,  // pp: patterns and complex globs
  Fuzzy,      // fz: patterns
  GlobComplex,  // complex glob routed to the pp engine (display only; built as PathPattern)
};

// Short display label for a matcher kind (status bar, logs). Never null.
[[nodiscard]] inline const char* MatcherKindLabel(MatcherKind kind) noexcept {
  switch (kind) {
    case MatcherKind::AllPass:
      return "all";
    case MatcherKind::Empty:
      return "none";
    case MatcherKind::Substring:
      return "substr";
    case MatcherKind::GlobPrefix:
      return "prefix";
    case MatcherKind::GlobSuffix:
      return "suffix";
    case MatcherKind::Extension:
      return "ext";
    case MatcherKind::StdRegex:
      return "regex";
    case MatcherKind::PathPattern:
      return "pp";
    case MatcherKind::Fuzzy:
      return "fuzzy";
    case MatcherKind::GlobComplex:
      return "gpp";
    default:
      return "?";
  }
}

namespace search_pattern_utils_detail {

// Overloads mapping each built matcher alternative to its kind. Adding a
// PatternMatcher alternative without an overload here fails to compile at
// the MatcherKindOf call below instead of mislabeling silently.
[[nodiscard]] inline MatcherKind MatcherKindOfImpl([[maybe_unused]] const AllPassMatcher& matcher) {
  return MatcherKind::AllPass;
}
[[nodiscard]] inline MatcherKind MatcherKindOfImpl([[maybe_unused]] const EmptyMatcher& matcher) {
  return MatcherKind::Empty;
}
[[nodiscard]] inline MatcherKind MatcherKindOfImpl([[maybe_unused]] const SubstringMatcher& matcher) {
  return MatcherKind::Substring;
}
[[nodiscard]] inline MatcherKind MatcherKindOfImpl(const GlobMatcher& matcher) {
  return matcher.is_prefix ? MatcherKind::GlobPrefix : MatcherKind::GlobSuffix;
}
[[nodiscard]] inline MatcherKind MatcherKindOfImpl([[maybe_unused]] const ExtensionSetMatcher& matcher) {
  return MatcherKind::Extension;
}
[[nodiscard]] inline MatcherKind MatcherKindOfImpl([[maybe_unused]] const StdRegexMatcher& matcher) {
  return MatcherKind::StdRegex;
}
[[nodiscard]] inline MatcherKind MatcherKindOfImpl([[maybe_unused]] const PathPatternMatcher& matcher) {
  return MatcherKind::PathPattern;
}
[[nodiscard]] inline MatcherKind MatcherKindOfImpl([[maybe_unused]] const FuzzyMatcher& matcher) {
  return MatcherKind::Fuzzy;
}

}  // namespace search_pattern_utils_detail

// Exact kind of an already-built matcher (no drift possible).
// Not noexcept: std::visit can throw bad_variant_access on a valueless variant.
[[nodiscard]] inline MatcherKind MatcherKindOf(const PatternMatcher& matcher) {
  return std::visit(
      [](const auto& m) { return search_pattern_utils_detail::MatcherKindOfImpl(m); },
      matcher);
}

// Display label for an already-built matcher.
[[nodiscard]] inline const char* MatcherLabel(const PatternMatcher& matcher) {
  return MatcherKindLabel(MatcherKindOf(matcher));
}

// True when this matcher can reject a non-matching text cheaply (SIMD substring,
// O(len) prefix/suffix compare, a required-literal pre-filter, or a compiled
// path pattern with required-literal constraints). Matchers with no fast reject
// (an re: regex without a literal/suffix pre-filter, or fz:) run their full
// engine on every text and are the expensive side of a filename+path query.
[[nodiscard]] inline bool MatcherHasCheapReject(const PatternMatcher& matcher) {
  return std::visit(
      [](const auto& m) -> bool {
        using MatcherT = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<MatcherT, StdRegexMatcher>) {
          return m.state && (!m.state->prefilter_.empty() || !m.state->suffix_prefilter_.empty() ||
                             !m.state->prefix_prefilter_.empty() ||
                             m.state->class_run_min_length_ > 0);
        } else if constexpr (std::is_same_v<MatcherT, PathPatternMatcher>) {
          return m.compiled_pattern && m.compiled_pattern->has_required_substring;
        } else if constexpr (std::is_same_v<MatcherT, FuzzyMatcher>) {
          return false;  // full subsequence scan per text
        } else {
          // AllPass / Empty (no per-text work) and Substring / GlobPrefix /
          // GlobSuffix (SIMD substring or O(len) prefix/suffix compare) reject
          // cheaply.
          return true;
        }
      },
      matcher);
}

// Decide whether the path matcher should be evaluated before the filename
// matcher. Only reorders when the two sides are clearly asymmetric: the filename
// matcher has no cheap reject while the path matcher does, so path-first lets a
// selective path filter short-circuit the expensive filename work. When both (or
// neither) can reject cheaply, the existing filename-first order is kept, since
// reordering could otherwise make things slower.
[[nodiscard]] inline bool PreferPathMatcherFirst(const PatternMatcher& filename_matcher,
                                                 const PatternMatcher& path_matcher) {
  if (!static_cast<bool>(filename_matcher) || !static_cast<bool>(path_matcher)) {
    return false;  // both matchers must be present to reorder
  }
  return !MatcherHasCheapReject(filename_matcher) && MatcherHasCheapReject(path_matcher);
}

namespace search_pattern_utils_detail {

// Glob shape classification, shared by the factory (TryGlobShapeMatcher) and
// the label path (ClassifyMatcherKind) so the two cannot drift apart.
enum class GlobShape : std::uint8_t {  // NOLINT(performance-enum-size) - explicit uint8_t; checker may still flag in some contexts
  AllPass,   // only stars (matches everything)
  Contains,  // *lit* (substring)
  Prefix,    // lit* (prefix)
  Suffix,    // *lit (suffix)
  Complex,   // shaped by ? or multiple stars (needs PathPattern)
};

[[nodiscard]] inline GlobShape ClassifyGlobShape(std::string_view pattern_str) {
  if (pattern_str.find('?') != std::string_view::npos) {
    return GlobShape::Complex;
  }
  if (pattern_str.find_first_not_of('*') == std::string_view::npos) {
    return GlobShape::AllPass;
  }
  const auto star_count =
      static_cast<size_t>(std::count(pattern_str.begin(), pattern_str.end(), '*'));  //  - C++17; std::ranges requires C++20
  if (star_count == 2 && pattern_str.front() == '*' && pattern_str.back() == '*') {
    return GlobShape::Contains;
  }
  if (star_count == 1 && pattern_str.back() == '*') {
    return GlobShape::Prefix;
  }
  if (star_count == 1 && pattern_str.front() == '*') {
    return GlobShape::Suffix;
  }
  return GlobShape::Complex;
}

inline std::optional<PatternMatcher> TryGlobShapeMatcher(
    std::string_view pattern_str, bool case_sensitive) {
  switch (ClassifyGlobShape(pattern_str)) {
    case GlobShape::AllPass:
      return PatternMatcher{AllPassMatcher{}};
    case GlobShape::Contains: {
      const std::string lit(pattern_str.substr(1, pattern_str.size() - 2));
      const std::string lower = case_sensitive ? lit : ToLower(lit);
      const string_search::SimdSubstringHints hints = string_search::MakeSimdSubstringHints(lower);
      return PatternMatcher{SubstringMatcher{lower, hints, case_sensitive}};
    }
    case GlobShape::Prefix:
      return PatternMatcher{GlobMatcher{std::string(pattern_str.substr(0, pattern_str.size() - 1)), case_sensitive, true}};
    case GlobShape::Suffix:
      return PatternMatcher{GlobMatcher{std::string(pattern_str.substr(1)), case_sensitive, false}};
    case GlobShape::Complex:
    default:
      return std::nullopt;
  }
}

// Lowercase ASCII in place (normalizes pre-filter alternatives for
// case-insensitive matching).
inline void ToLowerInPlace(std::string& s) {
  for (char& c : s) {
    c = string_search::ToLowerChar(static_cast<unsigned char>(c));
  }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) - regex matcher pre-filter extraction branches
inline PatternMatcher CreateMatcherImplStdRegexPrepared(  // NOSONAR(cpp:S3776) - regex pre-filter extraction has inherent branching
    std::string_view regex_pattern, bool case_sensitive,
    std::string_view explicit_prefilter = "") {
  if (regex_pattern.empty() || regex_pattern.length() > kMaxPatternLength) {
    return PatternMatcher{EmptyMatcher{}};
  }
  // An re: pattern that is exactly an extension match needs no regex: build a
  // suffix-set matcher (same semantics, O(len) per text instead of a regex call).
  if (const auto extensions =
          std_regex_utils::detail::RecognizeExactExtensionPattern(regex_pattern);
      extensions.has_value()) {
    std::vector<std::string> suffixes;
    suffixes.reserve(extensions->size());
    for (const std::string& extension : *extensions) {
      std::string suffix = "." + extension;
      if (!case_sensitive) {
        ToLowerInPlace(suffix);
      }
      suffixes.push_back(std::move(suffix));
    }
    // `regex_pattern` is carried along for the one case the suffix compare
    // cannot decide: a text spanning lines under a line-oriented '$' (never seen
    // in a path-shaped text, so the regex fallback stays cold).
    return PatternMatcher{ExtensionSetMatcher{std::string(regex_pattern), std::move(suffixes), case_sensitive}};
  }
  const regex_t* compiled_regex =
      std_regex_utils::GetCache().GetRegex(regex_pattern, case_sensitive);
  if (compiled_regex == nullptr) {
    return PatternMatcher{EmptyMatcher{}};
  }
  const bool requires_full_match = std_regex_utils::detail::RequiresFullMatch(regex_pattern);
  const bool match_at_start = std_regex_utils::detail::RequiresMatchAtStart(regex_pattern);

  constexpr size_t kMinPrefilterLength = 3;
  // Start-anchored patterns get a stricter prefix pre-filter in place of the
  // generic substring one (in a start-anchored pattern the literal must sit at
  // index 0). Covers alternation prefixes (re:^(get|set)_) where no single
  // required literal exists.
  std::vector<std::string> prefix_prefilter =
      std_regex_utils::detail::ExtractRequiredPrefixes(regex_pattern);
  std::string prefilter;
  if (!explicit_prefilter.empty() && explicit_prefilter.size() >= kMinPrefilterLength) {
    prefilter = case_sensitive ? std::string(explicit_prefilter) : ToLower(explicit_prefilter);
  } else if (prefix_prefilter.empty()) {
    if (const std::string raw_literal = std_regex_utils::detail::ExtractRequiredLiteral(regex_pattern);
        raw_literal.size() >= kMinPrefilterLength) {
      prefilter = case_sensitive ? raw_literal : ToLower(raw_literal);
    }
  }
  if (!case_sensitive) {
    for (std::string& prefix : prefix_prefilter) {
      ToLowerInPlace(prefix);
    }
  }

  // End-anchored patterns with no required literal (e.g. re:.*\.(cpp|h)$) still
  // have a required literal-suffix set; use it to reject most rows before the
  // regex runs. Only computed when the literal pre-filter could not be built.
  std::vector<std::string> suffix_prefilter;
  if (prefilter.empty()) {
    suffix_prefilter = std_regex_utils::detail::ExtractRequiredSuffixes(regex_pattern);
    if (!case_sensitive) {
      for (std::string& suffix : suffix_prefilter) {
        ToLowerInPlace(suffix);
      }
    }
  }

  // Patterns with no literal at all (re:\d{4}) still require a run of one
  // character class, so gate on it before the regex engine runs. Only built when
  // no literal/prefix/suffix pre-filter already rejects most rows.
  std::array<bool, 256> class_run_members{};
  size_t class_run_min_length = 0;
  if (prefilter.empty() && prefix_prefilter.empty() && suffix_prefilter.empty()) {
    if (const auto run =
            std_regex_utils::detail::ExtractRequiredClassRun(regex_pattern, case_sensitive);
        run.has_value()) {
      class_run_members = run->members;
      class_run_min_length = run->min_length;
    }
  }

  auto state = std::make_shared<StdRegexMatcherState>();
  state->compiled_regex_ = compiled_regex;
  state->requires_full_match_ = requires_full_match;
  state->match_at_start_ = match_at_start;
  state->case_sensitive_ = case_sensitive;
  if (!matcher_test_hooks::IsSkipLiteralPrefilterActive()) {
    state->prefilter_ = std::move(prefilter);
    state->prefilter_hints_ = string_search::MakeSimdSubstringHints(state->prefilter_);
    state->suffix_prefilter_ = std::move(suffix_prefilter);
    state->prefix_prefilter_ = std::move(prefix_prefilter);
    state->class_run_members_ = class_run_members;
    state->class_run_min_length_ = class_run_min_length;
  }
  return PatternMatcher{StdRegexMatcher{std::move(state)}};
}

inline PatternMatcher CreateMatcherImplStdRegex(
    std::string_view pattern, bool case_sensitive) {
  const std::string regex_pattern = ExtractPattern(pattern);
  return CreateMatcherImplStdRegexPrepared(regex_pattern, case_sensitive);
}

inline PatternMatcher CreateMatcherImplRx(
    std::string_view pattern, bool case_sensitive) {
  const std::string_view rx_source = ExtractPatternView(pattern);
  if (rx_source.empty()) {
    return PatternMatcher{AllPassMatcher{}};
  }
  const rx::RxCompileResult compiled = rx::CompileRx(rx_source);
  if (!compiled.Ok()) {
    return PatternMatcher{EmptyMatcher{}};
  }
  if (compiled.regex.empty()) {
    return PatternMatcher{AllPassMatcher{}};
  }
  return CreateMatcherImplStdRegexPrepared(compiled.regex, case_sensitive, compiled.required_literal);
}

inline PatternMatcher CreateMatcherImplPathPattern(
    std::string_view pattern, bool case_sensitive) {
  const std::string path_pattern = ExtractPattern(pattern);
  if (path_pattern.empty() || path_pattern::IsAllPassPathPattern(path_pattern)) {
    return PatternMatcher{AllPassMatcher{}};
  }
  auto compiled_ptr = std::make_shared<path_pattern::CompiledPathPattern>(
      path_pattern::CompilePathPattern(path_pattern, !case_sensitive));
  return PatternMatcher{PathPatternMatcher{std::move(compiled_ptr)}};
}

inline PatternMatcher CreateMatcherImplFuzzy(
    std::string_view pattern, bool case_sensitive, bool allow_typos = true) {
  const std::string fuzzy_pattern = ExtractPattern(pattern);
  if (fuzzy_pattern.empty()) {
    return PatternMatcher{AllPassMatcher{}};
  }
  return PatternMatcher{FuzzyMatcher{fuzzy_pattern, case_sensitive, allow_typos}};
}

inline PatternMatcher CreateMatcherImplGlob(
    std::string_view pattern, bool case_sensitive) {
  const std::string pattern_str(pattern);
  if (auto shaped = TryGlobShapeMatcher(pattern_str, case_sensitive);
      shaped.has_value()) {
    return *shaped;
  }
  const bool case_insensitive = !case_sensitive;

  std::string path_pattern_str;
  path_pattern_str.reserve(pattern_str.size() * 2);
  for (const char c : pattern_str) {
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
      path_pattern::CompilePathPattern(path_pattern_str, case_insensitive));
  return PatternMatcher{PathPatternMatcher{std::move(compiled_ptr)}};
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
                                        std::string_view pattern_lower = "",
                                        bool allow_typos = true) {
  if (pattern.empty()) {
    return PatternMatcher{AllPassMatcher{}};
  }
  switch (const PatternType type = DetectPatternType(pattern); type) {
  case PatternType::StdRegex:
    return search_pattern_utils_detail::CreateMatcherImplStdRegex(pattern, case_sensitive);
  case PatternType::Rx:
    return search_pattern_utils_detail::CreateMatcherImplRx(pattern, case_sensitive);
  case PatternType::PathPattern:
    return search_pattern_utils_detail::CreateMatcherImplPathPattern(pattern, case_sensitive);
  case PatternType::Fuzzy:
    return search_pattern_utils_detail::CreateMatcherImplFuzzy(pattern, case_sensitive, allow_typos);
  case PatternType::Glob:
    return search_pattern_utils_detail::CreateMatcherImplGlob(pattern, case_sensitive);
  case PatternType::Substring:
  default:
    return search_pattern_utils_detail::CreateMatcherImplSubstring(pattern, case_sensitive, pattern_lower);
  }
}

// Classify which matcher kind CreateMatcherImpl would build for a pattern,
// without building it (no regex/pattern compilation). Mirrors the dispatch
// above branch-for-branch; the glob shapes share ClassifyGlobShape with the
// factory so the two cannot drift. Approximation: an uncompilable re: pattern
// reports StdRegex here but builds EmptyMatcher (rare; the attempted type is
// what the status bar shows).
[[nodiscard]] inline MatcherKind ClassifyMatcherKind(std::string_view pattern) {
  if (pattern.empty()) {
    return MatcherKind::AllPass;
  }
  switch (DetectPatternType(pattern)) {
    case PatternType::StdRegex: {
      const std::string regex_pattern = ExtractPattern(pattern);
      if (regex_pattern.empty() || regex_pattern.length() > kMaxPatternLength) {
        return MatcherKind::Empty;
      }
      if (std_regex_utils::detail::RecognizeExactExtensionPattern(regex_pattern).has_value()) {
        return MatcherKind::Extension;
      }
      return MatcherKind::StdRegex;
    }
    case PatternType::Rx: {
      const std::string_view rx_source = ExtractPatternView(pattern);
      if (rx_source.empty()) {
        return MatcherKind::AllPass;
      }
      const rx::RxCompileResult compiled = rx::CompileRx(rx_source);
      if (!compiled.Ok()) {
        return MatcherKind::Empty;
      }
      if (compiled.regex.empty()) {
        return MatcherKind::AllPass;
      }
      if (std_regex_utils::detail::RecognizeExactExtensionPattern(compiled.regex).has_value()) {
        return MatcherKind::Extension;
      }
      return MatcherKind::StdRegex;
    }
    case PatternType::PathPattern:
      // Bare prefix or all-pass wildcards compile to no constraint (mirrors CreateMatcherImplPathPattern).
      if (const std::string pp = ExtractPattern(pattern);
          pp.empty() || path_pattern::IsAllPassPathPattern(pp)) {
        return MatcherKind::AllPass;
      }
      return MatcherKind::PathPattern;
    case PatternType::Fuzzy:
      // Bare prefix compiles to no constraint (mirrors CreateMatcherImplFuzzy).
      if (ExtractPattern(pattern).empty()) {
        return MatcherKind::AllPass;
      }
      return MatcherKind::Fuzzy;
    case PatternType::Glob:
      switch (search_pattern_utils_detail::ClassifyGlobShape(pattern)) {
        case search_pattern_utils_detail::GlobShape::AllPass:
          return MatcherKind::AllPass;
        case search_pattern_utils_detail::GlobShape::Contains:
          return MatcherKind::Substring;
        case search_pattern_utils_detail::GlobShape::Prefix:
          return MatcherKind::GlobPrefix;
        case search_pattern_utils_detail::GlobShape::Suffix:
          return MatcherKind::GlobSuffix;
        case search_pattern_utils_detail::GlobShape::Complex:
        default:
          // Complex globs compile to the pp engine; the label keeps the
          // glob provenance (built matcher still reports PathPattern).
          return MatcherKind::GlobComplex;
      }
    case PatternType::Substring:
    default:
      return MatcherKind::Substring;
  }
}

// Create a reusable pattern matcher (canonical factory).
// Returns a PatternMatcher that matches a std::string_view against the pattern.
// Callers pass filename (last segment) or full path depending on context.
inline PatternMatcher CreatePatternMatcher(std::string_view pattern, bool case_sensitive,
                                           std::string_view pattern_lower = "",
                                           bool allow_typos = true) {
  return CreateMatcherImpl(pattern, case_sensitive, pattern_lower, allow_typos);
}

// Create a filename matcher (matches bounded filename std::string_view; enables fuzzy typos)
inline PatternMatcher CreateFilenameMatcher(std::string_view pattern, bool case_sensitive,
                                            std::string_view pattern_lower = "") {
  return CreatePatternMatcher(pattern, case_sensitive, pattern_lower, true);
}

// Create a path matcher (matches string_view path against pattern; disables fuzzy typos on paths)
inline PatternMatcher CreatePathMatcher(std::string_view pattern, bool case_sensitive,  // NOLINT(readability-identifier-naming,cppcoreguidelines-avoid-non-const-global-variables) - PascalCase API
                                        std::string_view pattern_lower = "") {
  return CreatePatternMatcher(pattern, case_sensitive, pattern_lower, false);
}

// True when either search field carries an fz: query. Pure: reads only args.
[[nodiscard]] inline bool HasFuzzyQuery(std::string_view filename_query, std::string_view path_query) {
  return DetectPatternType(filename_query) == PatternType::Fuzzy ||
         DetectPatternType(path_query) == PatternType::Fuzzy;
}

// Relevance for one result across both query fields: each present fz: part is
// scored on its own scope (basename for filename queries, full path for path
// queries) and the best part wins. Max (not average) preserves the score
// bands: an exact filename match stays 1000 even with a mediocre path part.
[[nodiscard]] inline uint16_t ComputeFuzzyRelevance(std::string_view basename, std::string_view full_path,
                                                   std::string_view filename_query,
                                                   std::string_view path_query, bool case_sensitive) {
  uint16_t score = 0;
  if (DetectPatternType(filename_query) == PatternType::Fuzzy) {
    score = (std::max)(score, string_search::ComputeFuzzyScore(basename, ExtractPattern(filename_query),
                                                               case_sensitive));
  }
  if (DetectPatternType(path_query) == PatternType::Fuzzy) {
    score = (std::max)(score, string_search::ComputeFuzzyScore(full_path, ExtractPattern(path_query),
                                                               case_sensitive));
  }
  return score;
}

// Pre-compiled PathPattern overloads
inline PatternMatcher CreatePatternMatcher(  // NOLINT(readability-identifier-naming,cppcoreguidelines-avoid-non-const-global-variables) - PascalCase API
    const std::shared_ptr<path_pattern::CompiledPathPattern>& compiled_pattern) {
  if (!compiled_pattern || !compiled_pattern->IsValid()) {
    return PatternMatcher{EmptyMatcher{}};
  }
  if (compiled_pattern->is_all_pass) {
    return PatternMatcher{AllPassMatcher{}};
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
  const auto it = std::lower_bound(extension_set.begin(), extension_set.end(), key, SvLess{});  //  - C++17; std::ranges requires C++20
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
