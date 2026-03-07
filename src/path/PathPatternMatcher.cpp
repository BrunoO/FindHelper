/**
 * @file PathPatternMatcher.cpp
 * @brief Implementation of glob-style path pattern matching
 *
 * This file implements a high-performance path pattern matcher that supports
 * glob-style patterns for matching file paths. It's optimized for speed and
 * supports both case-sensitive and case-insensitive matching.
 *
 * PATTERN SYNTAX:
 * - Single asterisk: Matches zero or more non-separator characters
 * - Double asterisk: Matches zero or more characters including separators (recursive)
 * - Question mark: Matches exactly one non-separator character
 * - [abc]: Character class (matches a, b, or c)
 * - [^abc]: Negated character class
 * - \d: Matches digits (0-9)
 * - Literal characters: Match exactly
 *
 * PERFORMANCE:
 * - Optimized for common patterns (literal strings, simple wildcards)
 * - Uses efficient character class matching with bitmap lookups
 * - Supports both case-sensitive and case-insensitive modes
 * - Designed for high-throughput path filtering in search operations
 *
 * USAGE:
 * - Used by FileIndex for path-based filtering during search
 * - Called from LoadBalancingStrategy when processing search tasks
 * - Supports both Windows and Unix path separators
 *
 * @see PathPatternMatcher.h for function declarations
 * @see LoadBalancingStrategy.cpp for usage in search operations
 * @see SearchPatternUtils.h for pattern extraction utilities
 */

#include "path/PathPatternMatcher.h"

#include "utils/Logger.h"
#include "utils/StringSearch.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace path_pattern {

// Helper types for Pattern implementation (moved from anonymous namespace to avoid ambiguity)
struct CharClass {
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
  bool negate = false;
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
  bool has_any = false;  // when true, class matches any character
  // Simple ASCII bitmap for 256 possible characters.
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
  std::array<bool, 256> bitmap =
    {};  // Fixed-size bitmap for performance-critical pattern matching (hot path)

  [[nodiscard]] bool Matches(char c, bool /*case_insensitive*/) const {
    const auto uc = static_cast<unsigned char>(c);
    const bool in_class = has_any ? true : bitmap.at(uc);  // NOLINT(cppcoreguidelines-init-variables) - Initialized from ternary expression
    return negate ? !in_class : in_class;
  }
};

// NOLINTNEXTLINE(performance-enum-size) - int base type is acceptable for enum
enum class AtomKind {
  kLiteral,     // NOLINT(readability-identifier-naming) - k prefix is project convention for enum constants
  kQuestion,    // NOLINT(readability-identifier-naming) - k prefix is project convention for enum constants - ? (non-separator)
  kStar,        // NOLINT(readability-identifier-naming) - k prefix is project convention for enum constants - *  (non-separator, quantified as 0+)
  kDoubleStar,  // NOLINT(readability-identifier-naming) - k prefix is project convention for enum constants - ** (including separators, quantified as 0+)
  kCharClass,   // NOLINT(readability-identifier-naming) - k prefix is project convention for enum constants
  kDigit,       // NOLINT(readability-identifier-naming) - k prefix is project convention for enum constants - \d
  kWord,        // NOLINT(readability-identifier-naming) - k prefix is project convention for enum constants - \w
};

struct Atom {
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
  AtomKind kind = AtomKind::kLiteral;
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
  char literal = '\0';
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
  CharClass char_class{};
  // Quantifier bounds for this atom: min <= count <= max.
  // max == max_uint32 means "unbounded".
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
  unsigned min_count = 1;
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
  unsigned max_count = 1;

  [[nodiscard]] bool IsUnbounded() const {
    return max_count == (std::numeric_limits<unsigned>::max)();
  }
};

// Pattern structure for advanced pattern matching (with char classes, quantifiers, etc.)
struct Pattern {
  bool anchor_start = false;
  bool anchor_end = false;
  bool case_insensitive = false;
  // Sequence of atoms to match in order.
  // The matcher always tries to match the entire path (subject to anchors).
  std::array<Atom, 64>
    atoms;  // Fixed-size array for performance-critical pattern matching (hot path)
  unsigned atom_count = 0;
  bool valid = false;
};

// PatternDeleter implementation - defined here where Pattern is complete
// NOSONAR(cpp:S5008, cpp:S5025) - void* and delete are required for type-erased storage pattern.
// Pattern is complete at this point (defined above), so delete is safe.
void PatternDeleter::operator()(void* ptr) const noexcept {  // NOSONAR(cpp:S5008) - void* required for type erasure
  if (ptr != nullptr) {
    delete static_cast<Pattern*>(ptr);  // NOSONAR(cpp:S5025) - Pattern is complete here, delete is safe - NOLINT(cppcoreguidelines-owning-memory) - PatternDeleter is the owner, delete is intentional
  }
}

namespace {

inline bool IsSeparator(char c) {
  return c == '/' || c == '\\';
}

inline char ToLowerChar(unsigned char c) {
  return static_cast<char>(
    std::tolower(c));  // NOSONAR(cpp:S1905) - Required cast: std::tolower returns int, need char
}

inline bool CharsEqual(char a, char b, bool case_insensitive) {
  if (!case_insensitive) {
    return a == b;
  }
  return ToLowerChar(static_cast<unsigned char>(a)) == ToLowerChar(static_cast<unsigned char>(b));
}

bool IsDigit(char c) {
  return c >= '0' && c <= '9';
}

bool IsWordChar(unsigned char c) {
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

// --- Simple glob-style NFA for patterns without advanced features ---

// Tokens for the simple path glob subset:
//  - Literal characters
//  - '?'           : single non-separator
//  - '*'           : zero or more non-separator characters
//  - '**'          : zero or more of any character (including separators)
//  - '/' or '\'    : matches either path separator (cross-platform)
// NOLINTNEXTLINE(performance-enum-size) - Already using std::uint8_t base type; warning is false positive
enum class SimpleTokKind : std::uint8_t {
  kLiteral,     // NOLINT(readability-identifier-naming) - k prefix is project convention for enum constants
  kSeparator,   // NOLINT(readability-identifier-naming) - matches '/' or '\'
  kAnyNonSep,   // NOLINT(readability-identifier-naming) - k prefix is project convention for enum constants
  kStarNonSep,  // NOLINT(readability-identifier-naming) - k prefix is project convention for enum constants
  kStarAny,     // NOLINT(readability-identifier-naming) - k prefix is project convention for enum constants
};

// SimpleToken must be exactly 2 bytes to match
// CompiledPathPattern::kSimpleTokenSize.
struct SimpleToken {
  SimpleTokKind kind = SimpleTokKind::kLiteral;
  char literal = '\0';  // For kLiteral
};
static_assert(sizeof(SimpleToken) == CompiledPathPattern::kSimpleTokenSize,
              "SimpleToken size must match kSimpleTokenSize");

using StateMask = std::uint64_t;

inline StateMask Bit(std::size_t i) {
  return static_cast<StateMask>(1) << i;
}

/**
 * @brief Compute NFA transitions for a given character
 *
 * Computes the next NFA state set after processing a character, considering
 * all active tokens and their transition rules.
 *
 * @param tokens Array of tokens in the pattern
 * @param token_count Number of tokens
 * @param nfa_states Current NFA state set (bitmask)
 * @param c Character to process
 * @param case_insensitive Whether matching should be case-insensitive
 * @return Next NFA state set after processing the character
 */
inline StateMask ComputeNfaTransitions(const SimpleToken* tokens, size_t token_count,
                                       StateMask nfa_states,
                                       unsigned char c,
                                       bool case_insensitive) {
  StateMask next_nfa = 0;

  // Compute NFA transitions for this character
  for (std::size_t i = 0; i < token_count; ++i) {
    if ((nfa_states & Bit(i)) == 0) {
      continue;
    }

    const SimpleToken& tok = tokens[i];  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic) - hot path; tokens is contiguous array
    bool matches = false;

    switch (tok.kind) {
      case SimpleTokKind::kLiteral:
        if (case_insensitive) {
          matches = CharsEqual(static_cast<char>(c), tok.literal, true);
        } else {
          matches = (static_cast<char>(c) == tok.literal);
        }
        break;

      case SimpleTokKind::kSeparator:
        matches = IsSeparator(static_cast<char>(c));
        break;

      case SimpleTokKind::kAnyNonSep:
        matches = !IsSeparator(static_cast<char>(c));
        break;

      case SimpleTokKind::kStarNonSep:
        if (!IsSeparator(static_cast<char>(c))) {
          next_nfa |= Bit(i);  // Loop back
        }
        break;

      case SimpleTokKind::kStarAny:
        next_nfa |= Bit(i);  // Loop back on any character
        break;
    }

    if (matches) {
      next_nfa |= Bit(i + 1);
    }
  }

  return next_nfa;
}

/**
 * @brief Find or create DFA state for an NFA state set
 *
 * Searches for an existing DFA state that corresponds to the given NFA state set.
 * If not found and the state set is non-empty, creates a new DFA state.
 *
 * @param nfa_state_sets Vector of NFA state sets (modified if new state created)
 * @param dfa_state_map Vector mapping DFA states to NFA state set indices (modified if new state
 * created)
 * @param next_nfa NFA state set to find or create DFA state for
 * @param accept_nfa_mask Mask for accepting NFA states
 * @param dfa_accept_state Current accepting DFA state (modified if new accepting state found)
 * @param pattern_string Pattern string for error messages
 * @return DFA state index, or CompiledPathPattern::kInvalidDfaState if state explosion
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables,readability-identifier-naming) - This is a function, not a global variable (false positive)
inline std::uint16_t FindOrCreateDfaState(std::vector<StateMask>& nfa_state_sets,
                                          std::vector<std::uint16_t>& dfa_state_map,
                                          StateMask next_nfa, StateMask accept_nfa_mask,
                                          std::uint16_t& dfa_accept_state,
                                          [[maybe_unused]] std::string_view pattern_string) {  // Used in LOG_WARNING_BUILD (macro may hide use from clang-tidy)
  // Find existing DFA state for this NFA state set
  std::uint16_t next_dfa_state = CompiledPathPattern::kInvalidDfaState;
  for (std::size_t i = 0; i < nfa_state_sets.size(); ++i) {
    if (nfa_state_sets[i] == next_nfa) {
      next_dfa_state = static_cast<std::uint16_t>(i);
      break;
    }
  }

  // Create new DFA state if needed
  if (next_dfa_state == CompiledPathPattern::kInvalidDfaState && next_nfa != 0) {
    if (nfa_state_sets.size() >= CompiledPathPattern::kMaxDfaStates) {
      // State explosion - fall back to NFA
      LOG_WARNING_BUILD("DFA state explosion: Pattern '"
                        << pattern_string << "' exceeded maximum DFA states ("
                        << CompiledPathPattern::kMaxDfaStates
                        << ") while creating new state. Falling back to NFA simulation.");
      return CompiledPathPattern::kInvalidDfaState;
    }
    next_dfa_state = static_cast<std::uint16_t>(nfa_state_sets.size());
    nfa_state_sets.push_back(next_nfa);
    dfa_state_map.push_back(next_dfa_state);

    // Check if new state is accepting
    if ((next_nfa & accept_nfa_mask) != 0 &&
        dfa_accept_state == CompiledPathPattern::kInvalidDfaState) {
      dfa_accept_state = next_dfa_state;
    }
  }

  return next_dfa_state;
}

// Build simple tokens from a normalized pattern string. Assumes the pattern
// does not use advanced features (no [], {}, \d, \w, anchors).
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables,readability-identifier-naming) - This is a function, not a global variable (false positive)
bool BuildSimpleTokens(std::string_view pattern, std::vector<SimpleToken>& tokens) {
  tokens.clear();
  tokens.reserve(pattern.size());

  for (std::size_t i = 0; i < pattern.size(); ++i) {  // NOSONAR(cpp:S886) - Index-based loop required for character-by-character pattern parsing
    const char c = pattern[i];
    if (c == '*') {
      if (i + 1 < pattern.size() && pattern[i + 1] == '*') {
        // "**" -> StarAny
        SimpleToken tok;
        tok.kind = SimpleTokKind::kStarAny;
        tokens.push_back(tok);
        ++i;  // consume second '*'
      } else {
        // "*" -> StarNonSep
        SimpleToken tok;
        tok.kind = SimpleTokKind::kStarNonSep;
        tokens.push_back(tok);
      }
    } else if (c == '?') {
      SimpleToken tok;
      tok.kind = SimpleTokKind::kAnyNonSep;
      tokens.push_back(tok);
    } else if (c == '/' || c == '\\') {
      SimpleToken tok;
      tok.kind = SimpleTokKind::kSeparator;
      tokens.push_back(tok);
    } else {
      SimpleToken tok;
      tok.kind = SimpleTokKind::kLiteral;
      tok.literal = c;
      tokens.push_back(tok);
    }
  }

  // We support up to 63 tokens so that we can represent positions 0..n in a
  // single 64-bit mask.
  return tokens.size() + 1 <= 64;
}

// Epsilon-mask computation: for each '*' or '**' at position i,
// it indicates that being at position i also allows being at position i+1.
StateMask ComputeEpsilonMask(const SimpleToken* tokens, size_t count) {
  StateMask mask = 0;
  for (size_t i = 0; i < count; ++i) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic) - hot path; tokens is contiguous array
    if (tokens[i].kind == SimpleTokKind::kStarNonSep || tokens[i].kind == SimpleTokKind::kStarAny) {
      mask |= Bit(i);
    }
  }
  return mask;
}

// Optimized bitwise epsilon closure.
inline StateMask ApplyEpsilonClosure(StateMask epsilon_mask, StateMask active,
                                     [[maybe_unused]] size_t n) {
  // If we are at position i, and position i is a star (epsilon_mask & Bit(i)),
  // then we can also be at position i+1.
  // We repeat until no more states can be reached via epsilon transitions.
  // For most patterns, one or two passes are enough.
  StateMask prev = 0;
  do {
    prev = active;
    active |= (active & epsilon_mask) << 1u;
  } while (active != prev);
  return active;
}

StateMask StepSimpleNfaOptimized(const SimpleToken* tokens, size_t n, StateMask epsilon_mask,
                                 StateMask active,
                                 char c,
                                 bool case_insensitive) {
  StateMask next = 0;

  for (std::size_t i = 0; i < n; ++i) {
    if ((active & Bit(i)) == 0) {
      continue;
    }

    const SimpleToken& tok = tokens[i];  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic) - hot path; tokens is contiguous array
    switch (tok.kind) {
      case SimpleTokKind::kLiteral:
        if (CharsEqual(c, tok.literal, case_insensitive)) {
          next |= Bit(i + 1);
        }
        break;

      case SimpleTokKind::kSeparator:
        if (IsSeparator(c)) {
          next |= Bit(i + 1);
        }
        break;

      case SimpleTokKind::kAnyNonSep:  // NOLINT(bugprone-branch-clone) - different semantics: advance (Bit(i+1)) vs loop (Bit(i))
        if (!IsSeparator(c)) {
          next |= Bit(i + 1);
        }
        break;

      case SimpleTokKind::kStarNonSep:
        // Loop on non-separator characters.
        // NOLINTNEXTLINE(bugprone-branch-clone) - Different actions: kAnyNonSep advances (Bit(i+1)), kStarNonSep loops (Bit(i))
        if (!IsSeparator(c)) {
          next |= Bit(i);
        }
        break;

      case SimpleTokKind::kStarAny:
        // Loop on any character.
        next |= Bit(i);
        break;
    }
  }

  return ApplyEpsilonClosure(epsilon_mask, next, n);
}

// Groups BuildDfa output parameters to satisfy cpp:S107 (max 7 params).
struct BuildDfaOutput {
  std::size_t state_count = 0;
  std::uint16_t start_state = 0;
  std::uint16_t accept_state = CompiledPathPattern::kInvalidDfaState;
};

// DFA conversion: Convert NFA to DFA using subset construction.
// Returns unique_ptr to DFA table if successfully built, nullptr otherwise (falls back to NFA).
std::unique_ptr<std::uint16_t[]> BuildDfa(  // NOLINT(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays) - dynamic DFA table, size known at runtime
    const SimpleToken* tokens, size_t token_count,
                                          StateMask epsilon_mask, bool case_insensitive,
                                          std::string_view pattern_string,
                                          BuildDfaOutput& out) {
  out.state_count = 0;
  out.start_state = 0;
  out.accept_state = CompiledPathPattern::kInvalidDfaState;

  if (token_count == 0 || token_count > kMaxPatternTokens) {
    return nullptr;
  }

  // Map from NFA state set (StateMask) to DFA state index
  std::vector<StateMask> nfa_state_sets;
  std::vector<std::uint16_t> dfa_state_map;  // Maps DFA state index to NFA state set index

  // Start state: epsilon closure of NFA state 0
  const StateMask start_nfa = ApplyEpsilonClosure(epsilon_mask, Bit(0), token_count);
  nfa_state_sets.push_back(start_nfa);
  dfa_state_map.push_back(0);
  out.start_state = 0;

  // Accept state is the one containing the final NFA state (token_count)
  const StateMask accept_nfa_mask = Bit(token_count);

  // Build DFA states using subset construction
  std::vector<std::uint16_t> transition_table;
  transition_table.reserve(CompiledPathPattern::kMaxDfaStates *
                           CompiledPathPattern::kDfaCharTableSize);

  for (std::size_t dfa_state_idx = 0; dfa_state_idx < nfa_state_sets.size(); ++dfa_state_idx) {  // NOSONAR(cpp:S886) - Index-based loop required for array access with dfa_state_idx
    if (dfa_state_idx >= CompiledPathPattern::kMaxDfaStates) {
      // State explosion - fall back to NFA
      LOG_WARNING_BUILD("DFA state explosion: Pattern '" << pattern_string
                                                         << "' exceeded maximum DFA states ("
                                                         << CompiledPathPattern::kMaxDfaStates
                                                         << "). Falling back to NFA simulation.");
      return nullptr;
    }

    const StateMask nfa_states = nfa_state_sets[dfa_state_idx];

    // Compute transitions for each possible character
    for (int c_int = 0; c_int < 256; ++c_int) {
      const auto c = static_cast<unsigned char>(c_int);

      // Compute NFA transitions for this character
      StateMask next_nfa =
        ComputeNfaTransitions(tokens, token_count, nfa_states, c, case_insensitive);

      // Apply epsilon closure
      next_nfa = ApplyEpsilonClosure(epsilon_mask, next_nfa, token_count);

      // Find or create DFA state for this NFA state set
      const std::uint16_t next_dfa_state = FindOrCreateDfaState(
          nfa_state_sets, dfa_state_map, next_nfa, accept_nfa_mask, out.accept_state,
          pattern_string);

      if (next_dfa_state == CompiledPathPattern::kInvalidDfaState) {
        // State explosion occurred
        return nullptr;
      }

      transition_table.push_back(next_dfa_state);
    }
  }

  // Allocate and copy DFA table using RAII (unique_ptr)
  out.state_count = nfa_state_sets.size();
  auto dfa_table = std::make_unique<std::uint16_t[]>(  // NOLINT(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays) - dynamic size at runtime
      out.state_count * CompiledPathPattern::kDfaCharTableSize);
  std::memcpy(dfa_table.get(), transition_table.data(),
              out.state_count * CompiledPathPattern::kDfaCharTableSize * sizeof(std::uint16_t));

  // Find accepting state(s) - any DFA state containing the final NFA state
  for (std::size_t i = 0; i < nfa_state_sets.size(); ++i) {
    if ((nfa_state_sets[i] & accept_nfa_mask) != 0) {
      out.accept_state = static_cast<std::uint16_t>(i);
      break;  // Use first accepting state found
    }
  }

  return dfa_table;
}

// Match using DFA (faster than NFA simulation)
bool MatchDfa(const CompiledPathPattern& compiled, std::string_view path) {
  if (!compiled.dfa_table_ || compiled.dfa_state_count_ == 0) {
    return false;
  }

  std::uint16_t current_state = compiled.dfa_start_state_;

  for (const char c : path) {
    const auto uc = static_cast<unsigned char>(c);
    const std::size_t table_index = (current_state * CompiledPathPattern::kDfaCharTableSize) + uc;

    if (table_index >= compiled.dfa_state_count_ * CompiledPathPattern::kDfaCharTableSize) {
      return false;
    }

    const std::uint16_t next_state = compiled.dfa_table_[table_index];

    if (next_state == CompiledPathPattern::kInvalidDfaState) {
      return false;  // Dead state - no match possible
    }

    current_state = next_state;
  }

  return (current_state == compiled.dfa_accept_state_);
}

bool UsesAdvancedFeatures(std::string_view pattern) {
  for (size_t i = 0; i < pattern.size(); ++i) {
    const char c = pattern[i];
    if (c == '[' || c == ']' || c == '{' || c == '}' || c == '^' || c == '$') {
      return true;
    }
    if (c == '\\' && i + 1 < pattern.size()) {
      const char n = pattern[i + 1];
      if (n == 'd' || n == 'w') {
        return true;
      }
    }
  }
  return false;
}

// Check if pattern is pure literal (no wildcards: *, ?, **)
// Literal-only patterns can use direct string comparison instead of DFA/NFA
bool IsLiteralOnly(std::string_view pattern) {
  return std::all_of(pattern.begin(), pattern.end(),
                     [](char c) { return c != '*' && c != '?'; });
}

void AddCharToClass(CharClass& cc, unsigned char c, bool case_insensitive) {
  cc.bitmap.at(c) = true;
  if (case_insensitive) {
    auto lower = static_cast<unsigned char>(ToLowerChar(
      c));  // NOSONAR(cpp:S1905) - ToLowerChar returns char, need unsigned char for bitmap index
    auto upper = static_cast<unsigned char>(std::toupper(c));
    cc.bitmap.at(lower) = true;
    cc.bitmap.at(upper) = true;
  }
}

// Parse a character class starting at pattern[pos] == '['.
// On success, returns true and updates pos to the character after ']'.
bool ParseCharClass(std::string_view pattern, unsigned& pos, bool case_insensitive,
                    CharClass& out_class) {
  if (pos >= pattern.size() || pattern[pos] != '[') {
    return false;
  }
  ++pos;  // skip '['
  if (pos >= pattern.size()) {
    return false;
  }

  if (pattern[pos] == '^') {
    out_class.negate = true;
    ++pos;
  }

  if (pos >= pattern.size()) {
    return false;
  }

  bool in_range = false;
  unsigned char range_start = 0;

  while (pos < pattern.size()) {
    char c = pattern[pos];
    if (c == ']' && !in_range) {
      ++pos;  // consume ']'
      return true;
    }

    if (c == '\\' && pos + 1 < pattern.size()) {
      // Escaped character.
      ++pos;
      c = pattern[pos];
      AddCharToClass(out_class, static_cast<unsigned char>(c), case_insensitive);
      ++pos;
      continue;
    }

    if (!in_range) {
      // Check if this could be start of range "a-z".
      if (pos + 2 < pattern.size() && pattern[pos + 1] == '-' && pattern[pos + 2] != ']') {
        in_range = true;
        range_start = static_cast<unsigned char>(c);
        pos += 2;  // leave pos at range end char
        continue;
      }

      AddCharToClass(out_class, static_cast<unsigned char>(c), case_insensitive);
      ++pos;
    } else {
      // We are at range end.
      auto range_end = static_cast<unsigned char>(c);
      if (range_start > range_end) {
        const unsigned char tmp = range_start;
        range_start = range_end;
        range_end = tmp;
      }
      for (unsigned char ch = range_start; ch <= range_end; ++ch) {
        AddCharToClass(out_class, ch, case_insensitive);
      }
      in_range = false;
      ++pos;
    }
  }

  return false;  // unterminated class
}

// Parse an unsigned integer from pattern starting at pos.
bool ParseUnsigned(std::string_view pattern, unsigned& pos, unsigned& value_out) {
  if (pos >= pattern.size() || !IsDigit(pattern[pos])) {
    return false;
  }
  unsigned value = 0;
  while (pos < pattern.size() && IsDigit(pattern[pos])) {
    constexpr unsigned kDecimalBase = 10u;
    if (auto digit = static_cast<unsigned>(pattern[pos] - '0');
        value > ((std::numeric_limits<unsigned>::max)() - digit) / kDecimalBase) {
      // Overflow, clamp to max.
      value = (std::numeric_limits<unsigned>::max)();
    } else {
      value = (value * kDecimalBase) + digit;  // NOLINT(readability-math-missing-parentheses) - Parentheses added for clarity
    }
    ++pos;
  }
  value_out = value;
  return true;
}

// Parse quantifier following an atom: ?, *, +, {m}, {m,}, {m,n}
void ParseQuantifier(std::string_view pattern, unsigned& pos, unsigned& min_count,
                     unsigned& max_count) {
  if (pos >= pattern.size()) {
    return;
  }

  const char c = pattern[pos];
  if (c == '?') {
    min_count = 0;
    max_count = 1;
    ++pos;
    return;
  }
  if (c == '*') {
    min_count = 0;
    max_count = (std::numeric_limits<unsigned>::max)();
    ++pos;
    return;
  }
  if (c == '+') {
    min_count = 1;
    max_count = (std::numeric_limits<unsigned>::max)();
    ++pos;
    return;
  }
  if (c == '{') {
    ++pos;  // consume '{'
    unsigned m = 0;  // NOLINT(misc-const-correctness) - m is modified by ParseUnsigned (passed by non-const reference), cannot be const
    unsigned n = 0;  // NOLINT(misc-const-correctness) - n is modified by ParseUnsigned (passed by non-const reference), cannot be const; n is conventional for next/max value
    if (const bool has_m = ParseUnsigned(pattern, pos, m); !has_m) {  // NOLINT(cppcoreguidelines-init-variables) - Variable initialized in C++17 init-statement
      return;
    }
    if (pos < pattern.size() && pattern[pos] == '}') {
      ++pos;
      const unsigned min_val = m;  // NOLINTNEXTLINE(misc-const-correctness) - m is modified by ParseUnsigned, but min_val is const
      min_count = min_val;
      max_count = min_val;
      return;
    }
    if (pos < pattern.size() && pattern[pos] == ',') {
      ++pos;
      if (pos < pattern.size() && pattern[pos] == '}') {
        // {m,}  // NOSONAR - Documentation comment explaining regex pattern
        ++pos;
        const unsigned min_val = m;  // NOLINTNEXTLINE(misc-const-correctness) - m is modified by ParseUnsigned, but min_val is const
        min_count = min_val;
        max_count = (std::numeric_limits<unsigned>::max)();
        return;
      }
      if (const bool has_n = ParseUnsigned(pattern, pos, n); !has_n) {  // NOLINT(cppcoreguidelines-init-variables) - Variable initialized in C++17 init-statement
        return;
      }
      if (pos < pattern.size() && pattern[pos] == '}') {
        ++pos;
        const unsigned min_val = m;  // NOLINTNEXTLINE(misc-const-correctness) - m is modified by ParseUnsigned, but min_val is const
        const unsigned max_val = n;  // NOLINTNEXTLINE(misc-const-correctness) - n is modified by ParseUnsigned, but max_val is const
        min_count = min_val;
        max_count = max_val;
        return;
      }
    }
  }
}

// Helper function to parse escape sequences (\d, \w, or literal \)
// Returns true if atom was set, false if parsing failed
bool ParseEscapeSequence(std::string_view pattern, unsigned& pos, Atom& atom) {
  if (pos + 1 >= pattern.size()) {
    atom.kind = AtomKind::kLiteral;
    atom.literal = '\\';
    ++pos;
    return true;
  }
  
  const char esc = pattern[pos + 1];
  if (esc == 'd') {
    atom.kind = AtomKind::kDigit;
    pos += 2;
    return true;
  }
  if (esc == 'w') {
    atom.kind = AtomKind::kWord;
    pos += 2;
    return true;
  }
  
  // Literal backslash
  atom.kind = AtomKind::kLiteral;
  atom.literal = '\\';
  ++pos;
  return true;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) - Anonymous namespace is acceptable for internal linkage; complexity is inherent to pattern compilation algorithm
Pattern CompilePattern(std::string_view pattern, bool case_insensitive) {
  Pattern compiled;
  compiled.case_insensitive = case_insensitive;
  compiled.valid = false;

  unsigned pos = 0;
  if (pos < pattern.size() && pattern[pos] == '^') {
    compiled.anchor_start = true;
    ++pos;
  }
  if (pos < pattern.size() && pattern.back() == '$') {
    compiled.anchor_end = true;
    pattern.remove_suffix(1);
  }

  while (pos < pattern.size() && compiled.atom_count < 64) {
    Atom atom;
    atom.min_count = 1;
    atom.max_count = 1;

    const char c = pattern[pos];

    if (c == '*') {  // NOSONAR(cpp:S6004) - Variable used after if block (line 687 and later)
      // Distinguish between "*" and "**".
      if (pos + 1 < pattern.size() && pattern[pos + 1] == '*') {
        atom.kind = AtomKind::kDoubleStar;
        atom.min_count = 0;
        atom.max_count = (std::numeric_limits<unsigned>::max)();
        pos += 2;
      } else {
        atom.kind = AtomKind::kStar;
        atom.min_count = 0;
        atom.max_count = (std::numeric_limits<unsigned>::max)();
        ++pos;
      }
    } else if (c == '?') {
      atom.kind = AtomKind::kQuestion;
      ++pos;
    } else if (c == '[') {
      atom.kind = AtomKind::kCharClass;
      if (!ParseCharClass(pattern, pos, case_insensitive, atom.char_class)) {
        return compiled;
      }
    } else if (c == '\\') {
      // Only treat \d and \w as shorthands; all other '\' are literals.
      ParseEscapeSequence(pattern, pos, atom);
    } else {
      atom.kind = AtomKind::kLiteral;
      atom.literal = c;
      ++pos;
    }

    // Parse optional quantifier (only for literal, class, digit, word).
    if (atom.kind == AtomKind::kLiteral || atom.kind == AtomKind::kCharClass ||
        atom.kind == AtomKind::kDigit || atom.kind == AtomKind::kWord) {
      ParseQuantifier(pattern, pos, atom.min_count, atom.max_count);
    }

    compiled.atoms.at(compiled.atom_count) = atom;
    ++compiled.atom_count;
  }

  compiled.valid = true;
  return compiled;
}

// Helper function to check required substring prefix match
bool CheckRequiredSubstringPrefix(std::string_view path, std::string_view required_substring, bool case_insensitive) {
  if (path.size() < required_substring.size()) {
    return false;
  }
  if (case_insensitive) {
    // Case-insensitive prefix comparison
    for (size_t i = 0; i < required_substring.size(); ++i) {
      if (ToLowerChar(static_cast<unsigned char>(path[i])) !=  // NOLINT(bugprone-signed-char-misuse) - cast to unsigned for ToLowerChar
          static_cast<unsigned char>(required_substring[i])) {
        return false;
      }
    }
    return true;
  }
  // Case-sensitive prefix comparison
  return path.compare(0, required_substring.size(), required_substring) == 0;
}

// Helper function to check required substring suffix match
bool CheckRequiredSubstringSuffix(std::string_view path, std::string_view required_substring, bool case_insensitive) {
  if (path.size() < required_substring.size()) {
    return false;
  }
  if (case_insensitive) {
    // Case-insensitive suffix comparison
    const size_t path_start = path.size() - required_substring.size();
    for (size_t i = 0; i < required_substring.size(); ++i) {
      if (ToLowerChar(static_cast<unsigned char>(path[path_start + i])) !=  // NOLINT(bugprone-signed-char-misuse) - cast to unsigned for ToLowerChar
          static_cast<unsigned char>(required_substring[i])) {
        return false;
      }
    }
    return true;
  }
  // Case-sensitive suffix comparison
  return path.compare(path.size() - required_substring.size(),
                     required_substring.size(),
                     required_substring) == 0;
}

// Helper function to check required substring contains match
bool CheckRequiredSubstringContains(std::string_view path, std::string_view required_substring, bool case_insensitive) {
  return case_insensitive ? string_search::ContainsSubstringI(path, required_substring)
                          : string_search::ContainsSubstring(path, required_substring);
}

bool MatchAtomOnce(const Atom& atom, std::string_view path, unsigned& index,
                   bool case_insensitive) {
  if (index >= path.size()) {
    return false;
  }
  const char c = path[index];

  bool match = false;
  switch (atom.kind) {
    case AtomKind::kLiteral:
      match = CharsEqual(c, atom.literal, case_insensitive);
      break;
    case AtomKind::kQuestion:
      match = !IsSeparator(c);
      break;
    case AtomKind::kDigit:
      match = (std::isdigit(static_cast<unsigned char>(c)) != 0);
      break;
    case AtomKind::kWord:
      match = IsWordChar(static_cast<unsigned char>(c));
      break;
    case AtomKind::kCharClass:
      match = atom.char_class.Matches(c, case_insensitive);
      break;
    case AtomKind::kStar:
      match = !IsSeparator(c);
      break;
    case AtomKind::kDoubleStar:
      match = true;
      break;
  }

  if (match) {
    ++index;
  }
  return match;
}

// Forward declaration for use by MatchFromUnbounded and MatchFromBounded.
bool MatchFrom(const Pattern& pattern, unsigned atom_index, std::string_view path,
               unsigned path_index);

// Unbounded repetition (x* / x+ / x{m,}): satisfy min_count, find max_consumed, backtrack.
// NOLINTNEXTLINE(misc-no-recursion) - recursive backtracking by design
inline bool MatchFromUnbounded(const Pattern& pattern, unsigned atom_index,
                                     std::string_view path, unsigned path_index) {
  const Atom& atom = pattern.atoms.at(atom_index);
  const unsigned original_path_index = path_index;
  const unsigned min_required = atom.min_count;
  unsigned tmp_index = path_index;
  unsigned count = 0;
  unsigned max_consumed = 0;

  // First, satisfy min_count requirement (if any).
  while (count < min_required && tmp_index < path.size()) {
    const unsigned before = tmp_index;
    if (!MatchAtomOnce(atom, path, tmp_index, pattern.case_insensitive)) {
      return false;
    }
    ++count;
    if (before == tmp_index) {
      return false;
    }
  }

  if (min_required > 0) {
    path_index = tmp_index;
  }

  // Greedily find max_consumed. Use single exit flag to satisfy cpp:S924.
  bool greedy_done = false;
  while (tmp_index < path.size() && !greedy_done) {
    const unsigned before = tmp_index;
    if (!MatchAtomOnce(atom, path, tmp_index, pattern.case_insensitive)) {
      greedy_done = true;
    } else {
      ++count;
      max_consumed = tmp_index - original_path_index;
      greedy_done =
        (count > 1024u && atom.kind == AtomKind::kDoubleStar) || (before == tmp_index);
    }
  }

  const unsigned min_consumed = min_required > 0 ? (path_index - original_path_index) : 0;
  max_consumed = (std::max)(max_consumed, min_consumed);

  for (auto consumed_signed = static_cast<int>(max_consumed);
       consumed_signed >= static_cast<int>(min_consumed); --consumed_signed) {
    const auto consumed = static_cast<unsigned>(consumed_signed);
    const unsigned current_index = original_path_index + consumed;
    if (current_index > path.size()) {
      continue;
    }
    if (MatchFrom(pattern, atom_index + 1, path, current_index)) {
      return true;
    }
  }
  return false;
}

// Bounded repetition: satisfy min_count, then try MatchFrom at each step up to max_count.
// NOLINTNEXTLINE(misc-no-recursion) - recursive backtracking by design
inline bool MatchFromBounded(const Pattern& pattern, unsigned atom_index,
                                   std::string_view path, unsigned path_index) {
  const Atom& atom = pattern.atoms.at(atom_index);
  unsigned current_index = path_index;
  unsigned matched = 0;
  while (matched < atom.min_count) {
    if (!MatchAtomOnce(atom, path, current_index, pattern.case_insensitive)) {
      return false;
    }
    ++matched;
  }

  const unsigned max_possible = atom.max_count;
  while (matched < max_possible) {
    if (MatchFrom(pattern, atom_index + 1, path, current_index)) {
      return true;
    }
    unsigned next_index = current_index;  // NOLINT(misc-const-correctness) - next_index is modified by MatchAtomOnce (passed by non-const reference), cannot be const
    if (!MatchAtomOnce(atom, path, next_index, pattern.case_insensitive)) {
      break;
    }
    current_index = next_index;
    ++matched;
  }
  return MatchFrom(pattern, atom_index + 1, path, current_index);
}

// Recursive backtracking matcher over compiled atoms.
// NOLINTNEXTLINE(misc-no-recursion) - recursive backtracking by design
bool MatchFrom(const Pattern& pattern, unsigned atom_index, std::string_view path,
               unsigned path_index) {
  if (atom_index == pattern.atom_count) {
    return path_index == path.size();
  }

  const Atom& atom = pattern.atoms.at(atom_index);

  if (atom.kind == AtomKind::kStar) {
    // '*' is handled by IsUnbounded() block; MatchAtomOnce for kStar matches non-separators only.
  }

  if (atom.IsUnbounded()) {
    return MatchFromUnbounded(pattern, atom_index, path, path_index);
  }
  return MatchFromBounded(pattern, atom_index, path, path_index);
}

}  // namespace

// Move constructor
// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init,hicpp-member-init) - All members are initialized in member initializer list; warning is false positive
CompiledPathPattern::CompiledPathPattern(CompiledPathPattern&& other) noexcept
    : pattern_string(std::move(other.pattern_string)), case_insensitive(other.case_insensitive),
      valid(other.valid), uses_advanced(other.uses_advanced), is_literal_only(other.is_literal_only),
      dfa_table_(std::move(other.dfa_table_)), dfa_state_count_(other.dfa_state_count_),
      dfa_start_state_(other.dfa_start_state_), dfa_accept_state_(other.dfa_accept_state_),
      simple_token_count(other.simple_token_count), epsilon_mask(other.epsilon_mask),
      use_dfa_(other.use_dfa_), cached_pattern_(std::move(other.cached_pattern_)),
      anchor_start(other.anchor_start), anchor_end(other.anchor_end),
      required_substring(std::move(other.required_substring)),
      has_required_substring(other.has_required_substring),
      required_substring_is_prefix(other.required_substring_is_prefix),
      required_substring_is_suffix(other.required_substring_is_suffix) {
  // Copy simple tokens if needed
  if (!uses_advanced && simple_token_count > 0) {
    std::memcpy(simple_tokens_storage.data(), other.simple_tokens_storage.data(),
                simple_token_count * kSimpleTokenSize);
  }

  // Nullify other's pointers to prevent double free
  // Note: Member initializer list already transferred ownership of all members
  // dfa_table_ and cached_pattern_ are moved via unique_ptr, so they're already nullified
  other.valid = false;
}

// Move assignment operator
CompiledPathPattern& CompiledPathPattern::operator=(CompiledPathPattern&& other) noexcept {
  if (this != &other) {
    // cached_pattern_ and dfa_table_ are managed by unique_ptr - automatic cleanup via RAII

    pattern_string = std::move(other.pattern_string);
    case_insensitive = other.case_insensitive;
    valid = other.valid;
    uses_advanced = other.uses_advanced;
    is_literal_only = other.is_literal_only;
    simple_token_count = other.simple_token_count;
    epsilon_mask = other.epsilon_mask;
    anchor_start = other.anchor_start;
    anchor_end = other.anchor_end;
    required_substring = std::move(other.required_substring);
    has_required_substring = other.has_required_substring;
    required_substring_is_prefix = other.required_substring_is_prefix;
    required_substring_is_suffix = other.required_substring_is_suffix;

    // Copy simple tokens if needed
    if (!uses_advanced && simple_token_count > 0) {
      std::memcpy(simple_tokens_storage.data(), other.simple_tokens_storage.data(),
                  simple_token_count * kSimpleTokenSize);
    }

    // Transfer ownership of DFA table and cached pattern
    dfa_table_ = std::move(other.dfa_table_);
    dfa_state_count_ = other.dfa_state_count_;
    dfa_start_state_ = other.dfa_start_state_;
    dfa_accept_state_ = other.dfa_accept_state_;
    use_dfa_ = other.use_dfa_;
    cached_pattern_ = std::move(other.cached_pattern_);

    // Nullify other's pointers to prevent double free
    // dfa_table_ and cached_pattern_ are moved via unique_ptr, so they're already nullified
    other.valid = false;
  }
  return *this;
}

/**
 * @brief Extract anchor flags from pattern and return pattern without anchors
 *
 * Extracts ^ (start anchor) and $ (end anchor) from pattern string.
 * This allows patterns like "^file_" to be treated as simple patterns.
 *
 * @param pattern Input pattern (may contain ^ and $)
 * @param anchor_start Output flag for start anchor (modified)
 * @param anchor_end Output flag for end anchor (modified)
 * @return Pattern string without anchors
 */
std::string_view ExtractAnchors(std::string_view pattern, bool& anchor_start, bool& anchor_end) {
  anchor_start = false;
  anchor_end = false;

  std::string_view pattern_without_anchors = pattern;
  if (!pattern.empty() && pattern[0] == '^') {
    anchor_start = true;
    pattern_without_anchors = pattern.substr(1);
  }
  if (!pattern_without_anchors.empty() && pattern_without_anchors.back() == '$') {
    anchor_end = true;
    pattern_without_anchors = pattern_without_anchors.substr(0, pattern_without_anchors.size() - 1);
  }

  return pattern_without_anchors;
}

/**
 * @brief Normalize pattern by converting double-star-slash to double-star
 *
 * Normalizes the pattern string by removing trailing slashes after double-star.
 * This simplifies pattern matching logic.
 *
 * @param pattern Pattern to normalize (modified)
 */
void NormalizePattern(std::string& pattern) {
  auto pos = pattern.find("**/");
  while (pos != std::string::npos) {
    pattern.erase(pos + 2, 1);
    pos = pattern.find("**/", pos + 2);
  }
}

/**
 * @brief Compile simple pattern (no advanced features)
 *
 * Compiles a simple pattern (no char classes, quantifiers, etc.) into
 * a CompiledPathPattern with DFA support for fast matching.
 *
 * @param pattern Pattern string (without anchors, already normalized)
 * @param case_insensitive Whether matching should be case-insensitive
 * @param compile_start Start time for timing measurements
 * @return Compiled pattern (valid = true on success)
 */
CompiledPathPattern CompileSimplePattern(
  std::string_view pattern, bool case_insensitive,
  std::chrono::high_resolution_clock::time_point compile_start) {
  CompiledPathPattern compiled;
  compiled.case_insensitive = case_insensitive;
  compiled.uses_advanced = false;
  compiled.valid = false;
  compiled.pattern_string = pattern;

  // OPTIMIZATION: Check if pattern is literal-only (no wildcards)
  // For literal-only patterns, we can use direct string comparison instead of DFA/NFA
  compiled.is_literal_only = IsLiteralOnly(pattern);
  if (compiled.is_literal_only) {
    // Skip DFA/NFA construction for literal-only patterns
    compiled.valid = true;
    auto compile_end = std::chrono::high_resolution_clock::now();
    auto compile_duration =  // NOSONAR(cpp:S1481,cpp:S1854) - Used in LOG_INFO_BUILD below
      std::chrono::duration_cast<std::chrono::microseconds>(compile_end - compile_start);
    LOG_INFO_BUILD("PathPattern compilation [LITERAL]: pattern='"
                   << compiled.pattern_string
                   << "', case_insensitive=" << (compiled.case_insensitive ? "true" : "false")
                   << ", total_time=" << compile_duration.count() << "μs");
    return compiled;
  }

  std::vector<SimpleToken> tokens;
  if (!BuildSimpleTokens(compiled.pattern_string, tokens)) {
    return compiled;
  }

  if (tokens.size() > kMaxPatternTokens) {
    return compiled;
  }

  compiled.simple_token_count = tokens.size();
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) - reinterpret_cast required for type-punning fixed-size storage array to SimpleToken*
  auto* storage = reinterpret_cast<SimpleToken*>(compiled.simple_tokens_storage.data()); // NOSONAR(cpp:S3630) - reinterpret_cast required for type-punning fixed-size storage array to SimpleToken* (performance-critical hot path)
  for (size_t i = 0; i < tokens.size(); ++i) {
    storage[i] = tokens[i];  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic) - hot path; storage is contiguous
  }
  compiled.epsilon_mask = ComputeEpsilonMask(storage, tokens.size());

  // Try to build DFA for faster matching
  auto dfa_start = std::chrono::high_resolution_clock::now();
  BuildDfaOutput dfa_out;  // NOLINT(misc-const-correctness) - modified by BuildDfa (output parameter)

  auto dfa_table =
      BuildDfa(storage, tokens.size(), compiled.epsilon_mask, compiled.case_insensitive,
               compiled.pattern_string, dfa_out);
  auto dfa_end = std::chrono::high_resolution_clock::now();
  auto dfa_duration = std::chrono::duration_cast<std::chrono::microseconds>(dfa_end - dfa_start);  // NOSONAR(cpp:S1481,cpp:S1854) - Used in LOG_INFO_BUILD below

  if (dfa_table != nullptr) {
    // DFA built successfully - use it
    compiled.dfa_table_ = std::move(dfa_table);
    compiled.dfa_state_count_ = dfa_out.state_count;
    compiled.dfa_start_state_ = dfa_out.start_state;
    compiled.dfa_accept_state_ = dfa_out.accept_state;
    compiled.use_dfa_ = true;
  } else {
    // DFA construction failed (state explosion or other issue) - fall back to NFA
    compiled.use_dfa_ = false;
  }

  compiled.valid = true;

  // Log compilation timing for simple patterns
  auto compile_end = std::chrono::high_resolution_clock::now();
  auto compile_duration =  // NOSONAR(cpp:S1481,cpp:S1854) - Used in LOG_INFO_BUILD below
    std::chrono::duration_cast<std::chrono::microseconds>(compile_end - compile_start);

  LOG_INFO_BUILD(
    "PathPattern compilation [SIMPLE]: pattern='"
    << compiled.pattern_string
    << "', case_insensitive=" << (compiled.case_insensitive ? "true" : "false")
    << ", tokens=" << tokens.size() << ", DFA=" << (compiled.use_dfa_ ? "YES" : "NO")
    << (compiled.use_dfa_ ? (", DFA_states=" + std::to_string(compiled.dfa_state_count_)) : "")
    << ", DFA_time=" << dfa_duration.count() << "μs"
    << ", total_time=" << compile_duration.count() << "μs");

  return compiled;
}

/**
 * @brief Compile advanced pattern (with char classes, quantifiers, etc.)
 *
 * Compiles an advanced pattern (with char classes, quantifiers, etc.) into
 * a CompiledPathPattern using the Pattern structure for matching.
 *
 * @param pattern Pattern string (without anchors)
 * @param case_insensitive Whether matching should be case-insensitive
 * @param compile_start Start time for timing measurements
 * @return Compiled pattern (valid = true on success)
 */
CompiledPathPattern CompileAdvancedPattern(
  std::string_view pattern, bool case_insensitive,
  std::chrono::high_resolution_clock::time_point compile_start) {
  CompiledPathPattern compiled;
  compiled.case_insensitive = case_insensitive;
  compiled.uses_advanced = true;
  compiled.valid = false;
  compiled.pattern_string = pattern;

  // Compile and cache the advanced pattern to avoid re-parsing on each match
  auto parse_start = std::chrono::high_resolution_clock::now();
  // Use RAII for Pattern allocation to prevent leaks if exceptions are thrown
  auto p =
    std::make_unique<Pattern>(CompilePattern(compiled.pattern_string, compiled.case_insensitive));
  auto parse_end = std::chrono::high_resolution_clock::now();
  auto parse_duration =  // NOSONAR(cpp:S1481,cpp:S1854) - Used in LOG_INFO_BUILD below
    std::chrono::duration_cast<std::chrono::microseconds>(parse_end - parse_start);

  const unsigned atom_count = p->atom_count;   // NOSONAR(cpp:S1481,cpp:S1854) - Used in LOG_INFO_BUILD below
  const bool pattern_valid = p->valid;         // NOSONAR(cpp:S1481,cpp:S1854) - Used in LOG_INFO_BUILD below

  if (p->valid) {
    // Preserve anchors we extracted earlier (before removing ^ and $)
    // Don't overwrite with p->anchor_start/anchor_end because CompilePattern
    // was called with pattern_without_anchors (no ^ or $)
    // compiled.anchor_start and compiled.anchor_end are already set correctly above
    // Transfer ownership to unique_ptr for RAII management
    compiled.cached_pattern_ = PatternPtr(p.release());
    compiled.valid = true;
  }

  // Log compilation timing for advanced patterns
  auto compile_end = std::chrono::high_resolution_clock::now();
  auto compile_duration =  // NOSONAR(cpp:S1481,cpp:S1854) - Used in LOG_INFO_BUILD below
    std::chrono::duration_cast<std::chrono::microseconds>(compile_end - compile_start);

  LOG_INFO_BUILD("PathPattern compilation [ADVANCED]: pattern='"
                 << compiled.pattern_string
                 << "', case_insensitive=" << (compiled.case_insensitive ? "true" : "false")
                 << ", atoms=" << atom_count << ", parse_time=" << parse_duration.count() << "μs"
                 << ", total_time=" << compile_duration.count() << "μs"
                 << ", valid=" << (pattern_valid ? "YES" : "NO"));

  return compiled;
}

// Result structure for ExtractRequiredSubstring
struct RequiredSubstringInfo {
  std::string substring;
  bool is_prefix = false;
  bool is_suffix = false;
};

// Extract longest literal substring for pre-filtering
// Returns RequiredSubstringInfo with substring, is_prefix, and is_suffix flags.
// For case-insensitive patterns, the substring is converted to lowercase.
RequiredSubstringInfo ExtractRequiredSubstring(std::string_view pattern, bool case_insensitive) {
  RequiredSubstringInfo best;
  RequiredSubstringInfo current;  // NOLINT(misc-const-correctness) - current is modified in loop (substring.push_back, is_prefix/is_suffix assignment), cannot be const

  bool at_start = true;  // NOLINT(misc-const-correctness) - at_start is modified in loop (set to false), cannot be const
  size_t pos = 0;

  for (const char c : pattern) {  // NOSONAR(cpp:S6184) - Complex stateful processing, not a simple "all_of" check
    // Check for special characters that break a literal sequence
    // To be safe, we only consider a strict whitelist of characters as literals.
    // This avoids issues with syntax characters like {, }, [, ], +, etc. being treated as literals.
    // We also exclude separators (/ and \) to avoid issues with ** matching (where / might be
    // absorbed).
    if (const bool is_safe_literal = (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '.' || c == '_' || c == '-'); !is_safe_literal) {
      // End of current literal sequence
      if (current.substring.length() > best.substring.length()) {
        best = current;
      }
      current.substring.clear();
      current.is_prefix = false;
      current.is_suffix = false;
      at_start = false;
      ++pos;
      continue;
    }

    // It's a safe literal char
    if (current.substring.empty() && at_start) {
      current.is_prefix = true;
    }
    // For case-insensitive patterns, convert to lowercase during extraction
    if (case_insensitive) {
      current.substring.push_back(ToLowerChar(static_cast<unsigned char>(c)));
    } else {
      current.substring.push_back(c);
    }
    ++pos;
  }

  // Check last sequence - if it extends to the end of the pattern, it's a suffix
  if (current.substring.length() > best.substring.length()) {
    best = current;
    // Mark as suffix if this sequence extends to the end of the pattern
    // (i.e., the last character we processed was a literal, not a special character)
    best.is_suffix = true;
  } else if (current.substring.length() == best.substring.length() && !current.substring.empty() && !best.is_suffix) {
    // If current sequence has same length as best and extends to end, it's also a suffix
    // But we prefer the one that's already marked (if any)
    best.is_suffix = true;
  }

  // Heuristic: only use substring if it's long enough to be worth the check overhead
  if (best.substring.length() >= 3) {
    return best;
  }

  return {};
}

CompiledPathPattern CompilePathPattern(std::string_view pattern, MatchOptions options) {
  auto compile_start = std::chrono::high_resolution_clock::now();

  const bool case_insensitive =
    (static_cast<unsigned>(options) & static_cast<unsigned>(MatchOptions::kCaseInsensitive)) != 0U;

  // Extract anchors before checking for advanced features
  // This allows patterns like "^file_" to be treated as simple patterns
  bool anchor_start = false;  // NOLINT(misc-const-correctness) - anchor_start is modified by ExtractAnchors (passed by non-const reference), cannot be const
  bool anchor_end = false;  // NOLINT(misc-const-correctness) - anchor_end is modified by ExtractAnchors (passed by non-const reference), cannot be const
  const std::string_view pattern_without_anchors = ExtractAnchors(pattern, anchor_start, anchor_end);

  CompiledPathPattern compiled;
  if (!UsesAdvancedFeatures(pattern_without_anchors)) {
    // Normalize "**/" to "**"
    std::string normalized(pattern_without_anchors);
    NormalizePattern(normalized);

    compiled = CompileSimplePattern(normalized, case_insensitive, compile_start);
  } else {
    compiled = CompileAdvancedPattern(pattern_without_anchors, case_insensitive, compile_start);
  }

  // Set anchor flags (extracted earlier)
  compiled.anchor_start = anchor_start;
  compiled.anchor_end = anchor_end;

  // Optimization: Extract required substring for fast rejection
  // We use pattern_without_anchors because that's what we match against largely
  if (auto req_substr_info = ExtractRequiredSubstring(pattern_without_anchors, case_insensitive); !req_substr_info.substring.empty()) {
    compiled.required_substring = std::move(req_substr_info.substring);
    compiled.has_required_substring = true;
    compiled.required_substring_is_prefix = req_substr_info.is_prefix;
    compiled.required_substring_is_suffix = req_substr_info.is_suffix;
    // If it's a prefix of the pattern WITHOUT anchors, and we have an anchor_start,
    // effectively it must be at the start of the path.
    // If we don't have anchor_start, "prefix of pattern" just means "pattern starts with literal",
    // but standard glob match allows matching substring anywhere?
    // Wait, PathPatternMatches("foo", "a/foo") -> false?
    // Line 121: "By default (no anchors) the pattern is treated as if ^pattern$ was used"
    // So yes, it matches the WHOLE path.
    // So if the pattern starts with a literal, that literal MUST be at the start of the path.

    // Let's verify "Implied Anchors":
    // "By default (no anchors) the pattern is treated as if ^pattern$ was used"
    // So "src" + wildcard + ".cpp" MUST match "src/main.cpp", NOT "foo/src/main.cpp".
    // So if ExtractRequiredSubstring says it's a prefix of the pattern,
    // it MUST be a prefix of the path.

    // However, if the pattern was "**" + wildcard + ".cpp", we extracted ".cpp" (suffix) or nothing.
    // If pattern was "src" + wildcard + ".cpp", we extracted "src/" (prefix).

    // So if ExtractRequiredSubstring returns is_prefix=true, it means the literal
    // is at the very beginning of the pattern string.
    // Since the pattern string is matched against the whole path (implicit ^...$),
    // this literal must be at the start of the path.
    // Similarly, if is_suffix=true, the literal is at the end of the pattern string,
    // so it must be at the end of the path.
  }

  return compiled;
}

// Helpers for PathPatternMatches to keep cognitive complexity under Sonar limit (cpp:S3776).
// Marked inline so they are inlined into PathPatternMatches and do not add a call per match in the hot path.
static inline bool MatchLiteralOnlyPath(const CompiledPathPattern& compiled,
                                        std::string_view path) {
  if (compiled.case_insensitive) {
    if (path.size() != compiled.pattern_string.size()) {
      return false;
    }
    return std::equal(path.begin(), path.end(), compiled.pattern_string.begin(),
                      [](char a, char b) {
                        return ToLowerChar(static_cast<unsigned char>(a)) ==
                               ToLowerChar(static_cast<unsigned char>(b));
                      });
  }
  return path == compiled.pattern_string;
}

static inline bool CheckRequiredSubstringForMatch(
    const CompiledPathPattern& compiled, std::string_view path) {
  if (compiled.required_substring_is_prefix) {
    return CheckRequiredSubstringPrefix(path, compiled.required_substring,
                                        compiled.case_insensitive);
  }
  if (compiled.required_substring_is_suffix) {
    return CheckRequiredSubstringSuffix(path, compiled.required_substring,
                                        compiled.case_insensitive);
  }
  return CheckRequiredSubstringContains(path, compiled.required_substring,
                                        compiled.case_insensitive);
}

static inline bool MatchSimplePathPattern(const CompiledPathPattern& compiled,
                                          std::string_view path) {
  if (compiled.use_dfa_) {
    return MatchDfa(compiled, path);
  }
  const auto* tokens = reinterpret_cast<const SimpleToken*>(  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) NOSONAR(cpp:S3630) - type-pun storage to SimpleToken* (hot path)
      compiled.simple_tokens_storage.data());
  const size_t n = compiled.simple_token_count;
  StateMask active = ApplyEpsilonClosure(compiled.epsilon_mask, Bit(0), n);
  for (const char c : path) {
    active = StepSimpleNfaOptimized(tokens, n, compiled.epsilon_mask, active, c,
                                    compiled.case_insensitive);
    if (active == 0) {
      return false;
    }
  }
  return (active & Bit(n)) != 0;
}

static inline bool MatchAdvancedPathPattern(const CompiledPathPattern& compiled,
                                            std::string_view path) {
  const auto* p = static_cast<const Pattern*>(compiled.cached_pattern_.get());
  if (p == nullptr || !p->valid) {
    return false;
  }
  return MatchFrom(*p, 0, path, 0);
}

bool PathPatternMatches(const CompiledPathPattern& compiled, std::string_view path) {
  if (!compiled.valid) {
    return false;
  }
  if (compiled.is_literal_only) {
    return MatchLiteralOnlyPath(compiled, path);
  }
  if (compiled.has_required_substring &&
      !CheckRequiredSubstringForMatch(compiled, path)) {
    return false;
  }
  if (!compiled.uses_advanced) {
    return MatchSimplePathPattern(compiled, path);
  }
  return MatchAdvancedPathPattern(compiled, path);
}

bool PathPatternMatches(std::string_view pattern, std::string_view path, MatchOptions options) {
  // For small patterns/one-off matches, we could avoid the overhead of
  // CompiledPathPattern, but for consistency and to use the optimized NFA
  // implementation, we'll use it here.
  const CompiledPathPattern compiled = CompilePathPattern(pattern, options);
  return PathPatternMatches(compiled, path);
}

}  // namespace path_pattern
