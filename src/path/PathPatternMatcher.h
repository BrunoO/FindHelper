#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "utils/CompilerMacros.h"
#include "utils/StringSearch.h"

namespace path_pattern {

// Tokens for the simplified path glob pattern:
//  - Literal characters
//  - '?'           : single non-separator
//  - '*'           : zero or more non-separator characters (within one segment)
//  - '**'          : zero or more of any character (including separators)
//  - '/' or '\'    : matches either path separator (cross-platform)
enum class SimpleTokKind : std::uint8_t {  // NOLINT(performance-enum-size) - explicit uint8_t; checker may still flag in some contexts
  kLiteral,     // NOLINT(readability-identifier-naming) - project convention: k prefix for enum constants
  kSeparator,   // NOLINT(readability-identifier-naming) - matches '/' or '\'
  kAnyNonSep,   // NOLINT(readability-identifier-naming) - matches '?' (single non-separator)
  kStarNonSep,  // NOLINT(readability-identifier-naming) - matches '*' (zero or more non-separators within segment)
  kStarAny,     // NOLINT(readability-identifier-naming) - matches '**' (zero or more characters across segments)
};

struct SimpleToken {
  SimpleTokKind kind = SimpleTokKind::kLiteral;
  char literal = '\0';
};

// Why a pattern is or is not usable. Anything other than kOk means the pattern
// cannot be matched: every lookup against it returns false.
enum class CompileStatus : std::uint8_t {  // NOLINT(performance-enum-size) - checker still flags; size explicit for ABI
  kOk = 0U,             // NOLINT(readability-identifier-naming) - project k prefix for enum constants
  kMovedFrom,           // NOLINT(readability-identifier-naming) - the object was moved from and must not be matched against
};

// Maximum number of tokens the simple (bitmask NFA) compile path can hold.
// HARD limit, not a tuning knob: the NFA tracks positions in a single 64-bit
// StateMask, so token i needs bit i and the accept state needs bit n. Patterns
// above it or patterns requiring segment semantics take the backtracking matcher.
inline constexpr std::size_t kMaxSimpleTokens = 64;

// Minimum length of a literal run to be worth a prefilter check.
// Shorter runs reject too rarely to pay for the extra scan.
inline constexpr std::size_t kMinRequiredSubstringLength = 3;

// Pre-compiled path pattern for efficient repeated matching.
// Compile once with CompilePathPattern(), then match many paths.
struct CompiledPathPattern {  // NOSONAR(cpp:S3624) NOLINT(cppcoreguidelines-pro-type-member-init) - retained suppression
  // The original pattern string (without anchors).
  std::string pattern_string;

  bool case_insensitive = false;

  // Why the pattern is (not) usable. Prefer IsValid() over comparing to kOk.
  CompileStatus status = CompileStatus::kOk;

  // True when the pattern compiled and can be matched.
  [[nodiscard]] bool IsValid() const noexcept {
    return status == CompileStatus::kOk;
  }

  // True if pattern is pure literal (no wildcards: *, ?, **)
  // For literal-only patterns, we use direct string comparison instead of NFA/backtracking.
  bool is_literal_only = false;

  // True if pattern matches all inputs unconditionally (e.g. "**" or empty).
  bool is_all_pass = false;

  // True if pattern requires segment semantics (double-star slash, trailing double-star) or exceeds kMaxSimpleTokens.
  bool needs_backtracking = false;

  // Fixed inline token storage for patterns with <= kMaxSimpleTokens tokens
  // (zero heap allocation, cache-local).
  std::array<SimpleToken, kMaxSimpleTokens> inline_tokens{};

  // Number of tokens in the compiled pattern.
  std::size_t token_count = 0;

  // Cold overflow token storage used only when token_count > kMaxSimpleTokens.
  std::vector<SimpleToken> overflow_tokens;

  [[nodiscard]] const SimpleToken* TokensData() const noexcept LIFETIMEBOUND {
    return token_count <= kMaxSimpleTokens ? inline_tokens.data() : overflow_tokens.data();
  }

  // Pre-computed epsilon closure mask for star positions in the NFA path.
  std::uint64_t epsilon_mask = 0;

  // Optimization: fast rejection if a required substring is missing.
  // For case-insensitive patterns, the substring is stored in lowercase.
  std::string required_substring;

  string_search::SimdSubstringHints required_substring_hints{};

  bool has_required_substring = false;

  bool required_substring_is_prefix = false;

  bool required_substring_is_suffix = false;

  // Additional required literals (AND-ed with required_substring).
  std::vector<std::string> extra_required_substrings;

  std::vector<string_search::SimdSubstringHints> extra_required_hints;

  // Trailing-literal constraint.
  std::string required_suffix;

  bool has_required_suffix = false;

  // Default constructor
  CompiledPathPattern() = default;

  // Destructor
  ~CompiledPathPattern() = default;

  // Disable copy
  CompiledPathPattern(const CompiledPathPattern&) = delete;
  CompiledPathPattern& operator=(const CompiledPathPattern&) = delete;

  // Move operations leaving source explicitly unusable
  CompiledPathPattern(CompiledPathPattern&& other) noexcept
      : pattern_string(std::move(other.pattern_string)),
        case_insensitive(other.case_insensitive),
        status(other.status),
        is_literal_only(other.is_literal_only),
        is_all_pass(other.is_all_pass),
        needs_backtracking(other.needs_backtracking),
        inline_tokens(other.inline_tokens),
        token_count(other.token_count),
        overflow_tokens(std::move(other.overflow_tokens)),
        epsilon_mask(other.epsilon_mask),
        required_substring(std::move(other.required_substring)),
        required_substring_hints(other.required_substring_hints),
        has_required_substring(other.has_required_substring),
        required_substring_is_prefix(other.required_substring_is_prefix),
        required_substring_is_suffix(other.required_substring_is_suffix),
        extra_required_substrings(std::move(other.extra_required_substrings)),
        extra_required_hints(std::move(other.extra_required_hints)),
        required_suffix(std::move(other.required_suffix)),
        has_required_suffix(other.has_required_suffix) {
    other.status = CompileStatus::kMovedFrom;
  }

  CompiledPathPattern& operator=(CompiledPathPattern&& other) noexcept {
    if (this != &other) {
      pattern_string = std::move(other.pattern_string);
      case_insensitive = other.case_insensitive;
      status = other.status;
      is_literal_only = other.is_literal_only;
      is_all_pass = other.is_all_pass;
      needs_backtracking = other.needs_backtracking;
      inline_tokens = other.inline_tokens;
      token_count = other.token_count;
      overflow_tokens = std::move(other.overflow_tokens);
      epsilon_mask = other.epsilon_mask;
      required_substring = std::move(other.required_substring);
      required_substring_hints = other.required_substring_hints;
      has_required_substring = other.has_required_substring;
      required_substring_is_prefix = other.required_substring_is_prefix;
      required_substring_is_suffix = other.required_substring_is_suffix;
      extra_required_substrings = std::move(other.extra_required_substrings);
      extra_required_hints = std::move(other.extra_required_hints);
      required_suffix = std::move(other.required_suffix);
      has_required_suffix = other.has_required_suffix;
      other.status = CompileStatus::kMovedFrom;
    }
    return *this;
  }
};

// Compile a pattern for repeated use. Returns a compiled pattern.
CompiledPathPattern CompilePathPattern(std::string_view pattern,
                                       bool case_insensitive = false);

// Human-readable explanation of a status.
[[nodiscard]] const char* DescribeCompileStatus(CompileStatus status);

// Match a pre-compiled pattern against a path.
bool PathPatternMatches(const CompiledPathPattern& compiled, std::string_view path);

// Returns true if the given path matches the pattern.
//
// Pattern language:
// - Literals: ordinary characters match themselves. Brackets `[...]`, braces `{...}`,
//   and `+` are matched literally as ordinary characters.
// - Separators: '/' and backslash are interchangeable (either matches either).
// - Wildcards:
//   ?   : matches exactly one non-separator character.
//   *   : matches zero or more non-separator characters (within one segment).
//   **  : matches zero or more characters, including separators.
//   double-star-slash : matches zero or more whole segments (boundary kept: e.g. **/*.cpp).
//   trailing double-star : matches zero or more trailing segments (e.g. src/** matches src and src/sub).
// - Anchors:
//   ^ : optional start marker (triggers PathPattern auto-detection in unprefixed queries)
//   $ : optional end marker (triggers PathPattern auto-detection in unprefixed queries)
//
// PathPattern ALWAYS matches the entire string from start to end (^pattern$).
// Leading '^' and trailing '$' are stripped at compile time; without wildcards,
// '^foo', 'foo$', and '^foo$' all match only the exact string 'foo' (use '^foo*',
// '^foo**', '**foo$', or 're:^foo' for prefix/suffix/regex globs).
//
// NOTE: Users requiring regex, character classes, alternation, or repetition
// should use rx: (recommended structured regex) or re: (ECMAScript regex).
bool PathPatternMatches(std::string_view pattern, std::string_view path,
                        bool case_insensitive = false);

/**
 * @brief Append each "safe" literal run from @p pattern into @p out_runs (views into @p pattern).
 *
 * Safe literals match ExtractRequiredSubstring: alnum, '.', '_', '-'.
 * Wildcards, separators, and anchors break runs.
 */
void AppendLiteralRuns(std::string_view pattern, std::vector<std::string_view>& out_runs);

/**
 * @brief Highlight PathPattern / pp: matches by painting every literal run found in @p text.
 */
[[nodiscard]] bool FindPathPatternSpans(std::string_view pattern, std::string_view text,
                                        bool case_sensitive,
                                        std::vector<string_search::MatchSpan>& out_spans);

/**
 * @brief Returns true if a pattern matches any input path unconditionally.
 */
[[nodiscard]] inline bool IsAllPassPathPattern(std::string_view pattern) noexcept {
  std::string_view p = pattern;
  if (!p.empty() && p.front() == '^') {
    p.remove_prefix(1);
  }
  if (!p.empty() && p.back() == '$') {
    p.remove_suffix(1);
  }
  return p.empty() ||
         (p.size() >= 2 && p.find_first_not_of('*') == std::string_view::npos);
}

}  // namespace path_pattern
