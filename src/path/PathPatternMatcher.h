#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "utils/StringSearch.h"

namespace path_pattern {

// Forward declaration for type-erased storage
struct Pattern;

// Custom deleter for type-erased Pattern storage
// Defined in .cpp file where Pattern is complete to avoid incomplete type issues
// This allows using unique_ptr<void, PatternDeleter> for RAII while maintaining type erasure
struct PatternDeleter {
  void operator()(void* ptr) const noexcept;
};

// Type alias for type-erased Pattern storage with RAII
// Zero runtime overhead: deleter is compile-time type, not runtime function pointer
using PatternPtr = std::unique_ptr<void, PatternDeleter>;

// Matching options for path patterns.
enum class MatchOptions : std::uint8_t {  // NOLINT(performance-enum-size) - checker still flags; size explicit for ABI
  kNone = 0U,  // NOLINT(readability-identifier-naming) - project k prefix for enum constants
  kCaseInsensitive = 1U << 0U,  // NOLINT(readability-identifier-naming) - 0U avoids hicpp-signed-bitwise
};

// Why a pattern is or is not usable. Anything other than kOk means the pattern
// cannot be matched: every lookup against it returns false, which is
// indistinguishable from "matched nothing" unless the reason is surfaced.
enum class CompileStatus : std::uint8_t {  // NOLINT(performance-enum-size) - checker still flags; size explicit for ABI
  kOk = 0U,             // NOLINT(readability-identifier-naming) - project k prefix for enum constants
  kTooManyAtoms,        // NOLINT(readability-identifier-naming) - pattern needs more atoms than kMaxAtoms (very long patterns)
  kUnterminatedClass,   // NOLINT(readability-identifier-naming) - '[' with no closing ']'
  kMovedFrom,           // NOLINT(readability-identifier-naming) - not a compile failure: the object was moved from and must not be matched against
};

// Maximum number of tokens the simple (bitmask NFA) compile path can hold.
// HARD limit, not a tuning knob: the NFA tracks positions in a single 64-bit
// StateMask, so token i needs bit i and the accept state needs bit n. Patterns
// above it are not rejected - CompilePathPattern retries them on the
// backtracking matcher, which has no position-mask constraint
// (kMaxPatternAtoms).
inline constexpr std::size_t kMaxSimpleTokens = 64;

// Maximum number of atoms the backtracking / advanced compile paths may hold.
// This is a storage budget, not an architectural limit: Atom is 272 bytes (the
// CharClass bitmap dominates), so this is ~34 KiB per compiled pattern and a
// search holds at most two (filename + path). A pattern needing more is
// reported as CompileStatus::kTooManyAtoms rather than silently truncated.
inline constexpr std::size_t kMaxPatternAtoms = 128;

// Minimum length of a literal run to be worth a prefilter check.
// Shorter runs reject too rarely to pay for the extra scan.
inline constexpr std::size_t kMinRequiredSubstringLength = 3;

// Pre-compiled path pattern for efficient repeated matching.
// Compile once with CompilePathPattern(), then match many paths.
//
// Implementation note: Opaque storage is sized to hold internal types.
// SimpleToken is 2 bytes, Atom is ~264 bytes (due to CharClass bitmap).
// We use fixed sizes here to avoid exposing internal types.
struct CompiledPathPattern {  // NOSONAR(cpp:S3624) NOLINT(cppcoreguidelines-pro-type-member-init,hicpp-member-init) - default ctor default-initializes members; destructor cleans up RAII
  // The original pattern string (normalized for simple patterns).

  std::string pattern_string;

  bool case_insensitive = false;

  // Why the pattern is (not) usable. Prefer IsValid() over comparing to kOk.
  CompileStatus status = CompileStatus::kOk;

  // True when the pattern compiled and can be matched. A pattern that did not
  // compile matches nothing, so a caller reporting "no results" should check
  // this first: an invalid pattern is a user-input error, not a search result.
  [[nodiscard]] bool IsValid() const noexcept {
    return status == CompileStatus::kOk;
  }

  // True if pattern uses advanced features (char classes, quantifiers, etc.)

  bool uses_advanced = false;

  // True if pattern is pure literal (no wildcards: *, ?, **)
  // For literal-only patterns, we use direct string comparison instead of DFA/NFA

  bool is_literal_only = false;

  // NFA support
  // Each SimpleToken is 2 bytes (kind + literal).
  static constexpr std::size_t kSimpleTokenSize = 2;

  alignas(8) std::array<std::uint8_t,
                        kMaxSimpleTokens *
                          kSimpleTokenSize> simple_tokens_storage{};  // NOLINT(readability-identifier-naming) - Fixed-size array for performance-critical pattern matching (hot path)

  std::size_t simple_token_count = 0;
  // Pre-computed epsilon closure mask for star positions.

  std::uint64_t epsilon_mask = 0;

  // For advanced patterns: cached compiled pattern to avoid re-parsing.
  // Opaque pointer to Pattern structure (allocated on heap).
  // Only valid when uses_advanced == true and IsValid() is true.
  // Uses unique_ptr<void, PatternDeleter> for RAII - zero runtime overhead, automatic cleanup

  PatternPtr cached_pattern_ = nullptr;  // NOLINT(readability-identifier-naming) - RAII-managed type-erased storage

  bool anchor_start = false;

  bool anchor_end = false;

  // Optimization: fast rejection if a required substring is missing.
  // This helps significantly for patterns like "**" + wildcard + "substring" + wildcard + "...".
  // For case-insensitive patterns, the substring is stored in lowercase.

  std::string required_substring;

  string_search::SimdSubstringHints required_substring_hints{};

  bool has_required_substring = false;
  // If true, the required substring must appear at the start (after anchors).
  // Note: For now we only use general substring search (not prefix optimized) to keep it simple,
  // unless we find it's a prefix.

  bool required_substring_is_prefix = false;
  // If true, the required substring must appear at the end of the path.
  // This enables faster suffix checks (e.g., for extension-based patterns like "**" + "*.cpp").

  bool required_substring_is_suffix = false;

  // Additional required literals (AND-ed with required_substring): every other
  // safe-literal run of length >= kMinRequiredSubstringLength besides the
  // primary. Stored lowercase when case_insensitive, with hoisted SIMD hints.

  std::vector<std::string> extra_required_substrings;

  std::vector<string_search::SimdSubstringHints> extra_required_hints;

  // Trailing-literal constraint: when the pattern ends with a safe run of
  // length >= kMinRequiredSubstringLength that is not already the primary
  // suffix, the path must end with it (checked with endswith, cheaper and
  // stronger than a contains scan).

  std::string required_suffix;

  string_search::SimdSubstringHints required_suffix_hints{};

  bool has_required_suffix = false;

  // Default constructor
  CompiledPathPattern() = default;

  // Destructor: RAII members (unique_ptr, string) handle cleanup automatically
  ~CompiledPathPattern() = default;

  // Disable copy (Pattern is large, ~16KB)
  CompiledPathPattern(const CompiledPathPattern&) = delete;
  CompiledPathPattern& operator=(const CompiledPathPattern&) = delete;

  // Enable move
  CompiledPathPattern(CompiledPathPattern&& other) noexcept;
  CompiledPathPattern& operator=(CompiledPathPattern&& other) noexcept;
};

// Compile a pattern for repeated use. Returns a compiled pattern.
// If the pattern is invalid, compiled.status tells why (see CompileStatus) and
// IsValid() returns false.
CompiledPathPattern CompilePathPattern(std::string_view pattern,
                                       MatchOptions options = MatchOptions::kNone);

// Human-readable explanation of a status, for logs and user-facing messages.
// Always returns a non-empty string ("ok" for kOk), so it is safe to stream
// unconditionally. Only non-kOk statuses are worth surfacing to a user.
[[nodiscard]] const char* DescribeCompileStatus(CompileStatus status);

// Match a pre-compiled pattern against a path.
// This is faster than PathPatternMatches() when matching many paths.
bool PathPatternMatches(const CompiledPathPattern& compiled, std::string_view path);

// Returns true if the given path matches the pattern.
//
// Pattern language (v1, prototype):
// - Literals: ordinary characters match themselves.
// - Separators: '/' and backslash are interchangeable (either matches either).
// - Wildcards (always wildcards, in every position - never repetition):
//   ?   : matches exactly one non-separator character.
//   *   : matches zero or more non-separator characters (within one segment).
//   **  : matches zero or more characters, including separators.
//   double-star-slash : matches zero or more whole segments (boundary kept:
//         a "report*" folder matches "report_q1" but not "my_report";
//         use star-report-star for contains).
// - Character classes:
//   [abc]      : one of 'a', 'b', or 'c'.
//   [a-z0-9_]  : ranges and sets.
//   [^a-z]     : negated class.
// - Shorthands:
//   \d : [0-9]
//   \w : [A-Za-z0-9_]
// - Repetition of a single atom (literal, class, shorthand):
//   atom+      : 1 or more
//   atom{m}    : exactly m
//   atom{m,}   : m or more
//   atom{m,n}  : between m and n inclusive
//   Note: repetition is spelled with '+' or braces only. '?' and '*' are
//         wildcards everywhere, so "atom?" is an atom plus one arbitrary
//         character and "atom*" is an atom plus any run of characters; write
//         atom{0,1} and atom{0,} for the optional / zero-or-more readings.
//         A '{' that does not parse as one of the four forms above is a literal
//         '{' (and the text after it is matched literally too), so "a{x}b"
//         matches "a{x}b" - the same reading the simple compile path gives.
// - Anchors:
//   ^ : path start
//   $ : path end
//
// Unsupported (by design, for simplicity and speed):
//   - Capturing groups, alternation, backreferences, lookaround.
//
// By default (no anchors) the pattern is treated as if ^pattern$ was used
// (i.e., it must match the whole path).
//
// NOTE: For matching many paths, prefer CompilePathPattern() + the overload
// that takes CompiledPathPattern, which avoids repeated pattern parsing.
bool PathPatternMatches(std::string_view pattern, std::string_view path,
                        MatchOptions options = MatchOptions::kNone);

/**
 * @brief Append each "safe" literal run from @p pattern into @p out_runs (views into @p pattern).
 *
 * Safe literals match ExtractRequiredSubstring: alnum, '.', '_', '-'.
 * Wildcards, separators, classes, escapes, and anchors break runs.
 * Used for match highlighting (all runs) and shares the same literal definition as prefilter.
 */
void AppendLiteralRuns(std::string_view pattern, std::vector<std::string_view>& out_runs);

/**
 * @brief Highlight PathPattern / pp: matches by painting every literal run found in @p text.
 *
 * Walks literal runs left-to-right (same order as the pattern), finding each in @p text
 * after the previous match — analogous to FindGlobLiteralSpans. Adjacent hits are merged.
 * Patterns with no safe literals yield no spans.
 */
[[nodiscard]] bool FindPathPatternSpans(std::string_view pattern, std::string_view text,
                                        bool case_sensitive,
                                        std::vector<string_search::MatchSpan>& out_spans);

}  // namespace path_pattern
