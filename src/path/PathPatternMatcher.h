#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

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

// Maximum number of tokens/atoms a pattern can have.
inline constexpr std::size_t kMaxPatternTokens = 64;

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

  bool valid = false;

  // True if pattern uses advanced features (char classes, quantifiers, etc.)

  bool uses_advanced = false;

  // True if pattern is pure literal (no wildcards: *, ?, **)
  // For literal-only patterns, we use direct string comparison instead of DFA/NFA

  bool is_literal_only = false;

  // For simple patterns: DFA state table (faster than NFA simulation).
  // DFA states are stored as a transition table: state_table[state * 256 + char] = next_state
  // Invalid state is represented as 0xFFFF (max uint16_t value).
  static constexpr std::size_t kMaxDfaStates = 256;  // Reasonable limit for simple patterns
  static constexpr std::uint16_t kInvalidDfaState = 0xFFFF;
  static constexpr std::size_t kDfaCharTableSize =
    256;  // One entry per possible char value (0-255)

  // DFA state table: dfa_table[state_index * 256 + char] = next_state_index
  // Only allocated if uses_advanced == false and dfa_state_count > 0

  std::unique_ptr<std::uint16_t[]> dfa_table_;  // NOSONAR(cpp:S5945) NOLINT(readability-identifier-naming,cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays) - unique_ptr<T[]> is correct for dynamic array; project snake_case_

  std::size_t dfa_state_count_ = 0;  // NOLINT(readability-identifier-naming)
  std::uint16_t dfa_start_state_ = 0;  // NOLINT(readability-identifier-naming)
  std::uint16_t dfa_accept_state_ = kInvalidDfaState;  // NOLINT(readability-identifier-naming)

  // Legacy NFA support (fallback if DFA construction fails or is too large)
  // Each SimpleToken is 2 bytes (kind + literal).
  static constexpr std::size_t kSimpleTokenSize = 2;

  alignas(8) std::array<std::uint8_t,
                        kMaxPatternTokens *
                          kSimpleTokenSize> simple_tokens_storage{};  // NOLINT(readability-identifier-naming) - Fixed-size array for performance-critical pattern matching (hot path)

  std::size_t simple_token_count = 0;
  // Pre-computed epsilon closure mask for star positions.

  std::uint64_t epsilon_mask = 0;

  bool use_dfa_ = false;  // NOLINT(readability-identifier-naming) - True if DFA is available, false to use NFA fallback

  // For advanced patterns: cached compiled pattern to avoid re-parsing.
  // Opaque pointer to Pattern structure (allocated on heap).
  // Only valid when uses_advanced == true and valid == true.
  // Uses unique_ptr<void, PatternDeleter> for RAII - zero runtime overhead, automatic cleanup

  PatternPtr cached_pattern_ = nullptr;  // NOLINT(readability-identifier-naming) - RAII-managed type-erased storage

  bool anchor_start = false;

  bool anchor_end = false;

  // Optimization: fast rejection if a required substring is missing.
  // This helps significantly for patterns like "**" + wildcard + "substring" + wildcard + "...".
  // For case-insensitive patterns, the substring is stored in lowercase.

  std::string required_substring;

  bool has_required_substring = false;
  // If true, the required substring must appear at the start (after anchors).
  // Note: For now we only use general substring search (not prefix optimized) to keep it simple,
  // unless we find it's a prefix.

  bool required_substring_is_prefix = false;
  // If true, the required substring must appear at the end of the path.
  // This enables faster suffix checks (e.g., for extension-based patterns like "**" + "*.cpp").

  bool required_substring_is_suffix = false;

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
// If the pattern is invalid, compiled.valid will be false.
CompiledPathPattern CompilePathPattern(std::string_view pattern,
                                       MatchOptions options = MatchOptions::kNone);

// Match a pre-compiled pattern against a path.
// This is faster than PathPatternMatches() when matching many paths.
bool PathPatternMatches(const CompiledPathPattern& compiled, std::string_view path);

// Returns true if the given path matches the pattern.
//
// Pattern language (v1, prototype):
// - Literals: ordinary characters match themselves.
// - Separators: '/' and '\\' match only themselves.
// - Wildcards:
//   ?   : matches exactly one non-separator character.
//   *   : matches zero or more non-separator characters.
//   **  : matches zero or more characters, including separators.
// - Character classes:
//   [abc]      : one of 'a', 'b', or 'c'.
//   [a-z0-9_]  : ranges and sets.
//   [^a-z]     : negated class.
// - Shorthands:
//   \d : [0-9]
//   \w : [A-Za-z0-9_]
// - Quantifiers on a single atom (literal, class, shorthand):
//   atom?      : 0 or 1
//   atom*      : 0 or more
//   atom+      : 1 or more
//   atom{m}    : exactly m
//   atom{m,}   : m or more
//   atom{m,n}  : between m and n inclusive
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

}  // namespace path_pattern
