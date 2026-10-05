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
#include <optional>
#include <string>
#include <vector>

namespace path_pattern {

// Helper types for Pattern implementation (moved from anonymous namespace to avoid ambiguity)
struct CharClass {

  bool negate = false;

  bool has_any = false;  // when true, class matches any character
  // Simple ASCII bitmap for 256 possible characters.

  std::array<bool, 256> bitmap =
    {};  // Fixed-size bitmap for performance-critical pattern matching (hot path)

  [[nodiscard]] bool Matches(char c, bool /*case_insensitive*/) const {
    const auto uc = static_cast<unsigned char>(c);
    const bool in_class = has_any ? true : bitmap.at(uc);
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

  AtomKind kind = AtomKind::kLiteral;

  char literal = '\0';

  CharClass char_class{};
  // Quantifier bounds for this atom: min <= count <= max.
  // max == max_uint32 means "unbounded".

  unsigned min_count = 1;

  unsigned max_count = 1;

  [[nodiscard]] bool IsUnbounded() const {
    return max_count == std::numeric_limits<unsigned>::max();
  }
};

// Atom capacity of Pattern::atoms (see kMaxPatternAtoms for the rationale).
inline constexpr unsigned kMaxAtoms = static_cast<unsigned>(kMaxPatternAtoms);

// Pattern structure for advanced pattern matching (with char classes, quantifiers, etc.)
struct Pattern {
  bool anchor_start = false;
  bool anchor_end = false;
  bool case_insensitive = false;
  // Sequence of atoms to match in order.
  // The matcher always tries to match the entire path (subject to anchors).
  std::array<Atom, kMaxAtoms>
    atoms;  // Fixed-size array for performance-critical pattern matching (hot path)
  unsigned atom_count = 0;
  CompileStatus status = CompileStatus::kOk;

  [[nodiscard]] bool IsValid() const noexcept {
    return status == CompileStatus::kOk;
  }
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

using string_search::ToLowerChar;

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

// Build simple tokens from a normalized pattern string. Assumes the pattern
// does not use advanced features (no [], {}, \d, \w, anchors).
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables,readability-identifier-naming) - This is a function, not a global variable (false positive)
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - state index bounded by DFA construction; char cast to uint8_t gives 0-255
bool BuildSimpleTokens(std::string_view pattern, std::vector<SimpleToken>& tokens) {
  tokens.clear();
  tokens.reserve(pattern.size());

  for (std::size_t i = 0; i < pattern.size(); ++i) {  // NOSONAR(cpp:S886) - Index-based loop required for character-by-character pattern parsing
    if (const char c = pattern[i]; c == '*') {
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
  // single 64-bit mask (see kMaxSimpleTokens). Callers treat a false return as
  // "retry on the backtracking matcher", not as a compile failure.
  return tokens.size() + 1 <= kMaxSimpleTokens;
}
// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

// Epsilon-mask computation: for each '*' or '**' at position i,
// it indicates that being at position i also allows being at position i+1.
StateMask ComputeEpsilonMask(const SimpleToken* tokens, size_t count) {
  StateMask mask = 0;
  for (size_t i = 0; i < count; ++i) {

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

    switch (const SimpleToken& tok = tokens[i]; tok.kind) {
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



bool UsesAdvancedFeatures(std::string_view pattern) {
  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  // i < pattern.size() established by loop condition; i+1 guarded by explicit bounds check above
  for (size_t i = 0; i < pattern.size(); ++i) {
    const char c = pattern[i];
    if (c == '[' || c == ']' || c == '{' || c == '}' || c == '^' || c == '$') {
      return true;
    }
    if (c == '\\' && i + 1 < pattern.size()) {
      if (const char n = pattern[i + 1]; n == 'd' || n == 'w') {
        return true;
      }
    }
  }
  return false;
  // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
}

// True when the pattern needs whole-segment double-star semantics. A
// char-level NFA/DFA epsilon for skipping double-star-slash is
// position-independent, so it would also match mid-segment (a "report*"
// folder pattern matching "my_report"). Such patterns take the backtracking
// matcher instead, where the zero-directories skip is tried positionally
// in MatchFrom and the segment boundary holds.
bool NeedsSegmentSemantics(std::string_view pattern) {
  if (pattern.find("**/") != std::string_view::npos ||
      pattern.find("**\\") != std::string_view::npos) {
    return true;
  }
  if (pattern.size() >= 3) {
    if (const std::string_view tail = pattern.substr(pattern.size() - 3);
        tail == "/**" || tail == "\\**") {
      return true;
    }
  }
  return false;
}

// Check if pattern is pure literal (no wildcards: *, ?, **)
// Literal-only patterns can use direct string comparison instead of DFA/NFA
bool IsLiteralOnly(std::string_view pattern) {
  return std::all_of(pattern.begin(), pattern.end(),  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
                     [](char c) { return c != '*' && c != '?'; });
}

// Same whitelist as ExtractRequiredSubstring / AppendLiteralRuns (highlight + prefilter).
[[nodiscard]] inline bool IsSafeLiteralChar(char c) {
  return (std::isalnum(static_cast<unsigned char>(c)) != 0) || c == '.' || c == '_' || c == '-';
}

void AddCharToClass(CharClass& cc, unsigned char c, bool case_insensitive) {
  cc.bitmap.at(c) = true;
  if (case_insensitive) {
    const auto lower = static_cast<unsigned char>(ToLowerChar(
      c));  // NOSONAR(cpp:S1905) - ToLowerChar returns char, need unsigned char for bitmap index
    const auto upper = static_cast<unsigned char>(std::toupper(c));
    cc.bitmap.at(lower) = true;
    cc.bitmap.at(upper) = true;
  }
}

// Parse a character class starting at pattern[pos] == '['.
// On success, returns true and updates pos to the character after ']'.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - state index bounded by DFA construction; char cast to uint8_t gives 0-255
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
// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

// Parse an unsigned integer from pattern starting at pos.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - state index bounded by DFA construction; char cast to uint8_t gives 0-255
bool ParseUnsigned(std::string_view pattern, unsigned& pos, unsigned& value_out) {
  if (pos >= pattern.size() || !IsDigit(pattern[pos])) {
    return false;
  }
  unsigned value = 0;
  while (pos < pattern.size() && IsDigit(pattern[pos])) {
    constexpr unsigned kDecimalBase = 10u;
    if (const auto digit = static_cast<unsigned>(pattern[pos] - '0');
        value > (std::numeric_limits<unsigned>::max() - digit) / kDecimalBase) {
      // Overflow, clamp to max.
      value = std::numeric_limits<unsigned>::max();
    } else {
      value = (value * kDecimalBase) + digit;  // NOLINT(readability-math-missing-parentheses) - Parentheses added for clarity
    }
    ++pos;
  }
  value_out = value;
  return true;
}
// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

// Parse a repetition suffix following an atom: +, {m}, {m,}, {m,n}.
//
// '*' and '?' are deliberately absent: in PathPattern they are always
// wildcards, in every position, never repetition operators. "Zero or more" is
// spelled {0,} and "optional" is spelled {0,1}. A '*' or '?' left unconsumed
// here is picked up by the next CompilePattern iteration as a wildcard atom
// (kStar / kDoubleStar / kQuestion).
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - state index bounded by DFA construction; char cast to uint8_t gives 0-255
void ParseRepetitionSuffix(std::string_view pattern, unsigned& pos,
                           unsigned& min_count, unsigned& max_count) {
  if (pos >= pattern.size()) {
    return;
  }

  const char c = pattern[pos];
  if (c == '+') {
    min_count = 1;
    max_count = std::numeric_limits<unsigned>::max();
    ++pos;
    return;
  }
  if (c == '{') {
    // A brace group is a repetition suffix only when it parses as one
    // ({m}, {m,}, {m,n}). Anything else leaves the pattern untouched so the
    // '{' is compiled as a literal by the next iteration - matching the simple
    // compile path and CompileSimplePatternAsBacktracking, which both treat
    // braces literally. Without the restore, "a{x}b" would silently compile to
    // a, x, } and match "ax}b" instead of "a{x}b".
    const unsigned brace_pos = pos;
    ++pos;  // consume '{'
    unsigned m = 0;  // NOLINT(misc-const-correctness) - m is modified by ParseUnsigned (passed by non-const reference), cannot be const
    unsigned n = 0;  // NOLINT(misc-const-correctness) - n is modified by ParseUnsigned (passed by non-const reference), cannot be const; n is conventional for next/max value
    if (const bool has_m = ParseUnsigned(pattern, pos, m); !has_m) {
      pos = brace_pos;
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
        max_count = std::numeric_limits<unsigned>::max();
        return;
      }
      if (const bool has_n = ParseUnsigned(pattern, pos, n); !has_n) {
        pos = brace_pos;
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
      // Trailing garbage before '}' (e.g. "{2,x}"): not a repetition suffix.
      pos = brace_pos;
    }
  }
}
// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

// Helper function to parse escape sequences (\d, \w, or literal \)
// Returns true if atom was set, false if parsing failed
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - state index bounded by DFA construction; char cast to uint8_t gives 0-255
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
// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - state index bounded by DFA construction; char cast to uint8_t gives 0-255
// NOLINTNEXTLINE(readability-function-cognitive-complexity) - complexity is inherent to the pattern compilation algorithm (atom dispatch plus the repetition suffix)
Pattern CompilePattern(std::string_view pattern, bool case_insensitive) {
  Pattern compiled;
  compiled.case_insensitive = case_insensitive;

  unsigned pos = 0;
  if (pos < pattern.size() && pattern[pos] == '^') {
    compiled.anchor_start = true;
    ++pos;
  }
  if (pos < pattern.size() && pattern.back() == '$') {
    compiled.anchor_end = true;
    pattern.remove_suffix(1);
  }

  while (pos < pattern.size() && compiled.atom_count < kMaxAtoms) {
    Atom atom;
    atom.min_count = 1;
    atom.max_count = 1;

    const char c = pattern[pos];

    if (c == '*') {  // NOSONAR(cpp:S6004) - Variable used after if block (line 687 and later)
      // Distinguish between "*" and "**".
      if (pos + 1 < pattern.size() && pattern[pos + 1] == '*') {
        atom.kind = AtomKind::kDoubleStar;
        atom.min_count = 0;
        atom.max_count = std::numeric_limits<unsigned>::max();
        pos += 2;
      } else {
        atom.kind = AtomKind::kStar;
        atom.min_count = 0;
        atom.max_count = std::numeric_limits<unsigned>::max();
        ++pos;
      }
    } else if (c == '?') {
      atom.kind = AtomKind::kQuestion;
      ++pos;
    } else if (c == '[') {
      atom.kind = AtomKind::kCharClass;
      if (!ParseCharClass(pattern, pos, case_insensitive, atom.char_class)) {
        compiled.status = CompileStatus::kUnterminatedClass;
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

    // Optional repetition suffix: only literal, class, digit and word atoms
    // can take one. '*' and '?' are never suffixes - they compile to wildcard
    // atoms (kStar / kDoubleStar / kQuestion) in every position, so only '+'
    // and '{...}' can follow an atom here.
    if (atom.kind == AtomKind::kLiteral || atom.kind == AtomKind::kCharClass ||
        atom.kind == AtomKind::kDigit || atom.kind == AtomKind::kWord) {
      ParseRepetitionSuffix(pattern, pos, atom.min_count, atom.max_count);
    }

    compiled.atoms.at(compiled.atom_count) = atom;
    ++compiled.atom_count;
  }

  if (pos < pattern.size()) {
    // Truncated: more input than atom storage holds. Report the reason rather
    // than matching only a prefix of the pattern (same rule as the check in
    // CompileSimplePatternAsBacktracking).
    LOG_WARNING_BUILD("PathPattern compilation [ADVANCED]: pattern exceeds atom limit, invalid");
    compiled.status = CompileStatus::kTooManyAtoms;
    return compiled;
  }

  return compiled;
}
// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

// Helper function to check required substring prefix match
// required_substring is pre-lowercased at extraction for case-insensitive
// patterns (ExtractRequiredSubstring), so double-sided lowering in CheckPrefix
// is equivalent to the previous single-sided loop.
bool CheckRequiredSubstringPrefix(std::string_view path, std::string_view required_substring, bool case_insensitive) {
  if (case_insensitive) {
    return string_search::string_search_detail::CheckPrefix<
        string_search::string_search_detail::CaseInsensitive>(path, required_substring);
  }
  return string_search::string_search_detail::CheckPrefix<
      string_search::string_search_detail::CaseSensitive>(path, required_substring);
}

// Helper function to check required substring suffix match
bool CheckRequiredSubstringSuffix(std::string_view path, std::string_view required_substring, bool case_insensitive) {
  if (path.size() < required_substring.size()) {
    return false;
  }
  const size_t start_pos = path.size() - required_substring.size();
  if (case_insensitive) {
    return string_search::string_search_detail::ReverseCompare<
        string_search::string_search_detail::CaseInsensitive>(path, required_substring, start_pos);
  }
  return string_search::string_search_detail::ReverseCompare<
      string_search::string_search_detail::CaseSensitive>(path, required_substring, start_pos);
}

// Helper function to check required substring contains match
bool CheckRequiredSubstringContains(std::string_view path, std::string_view required_substring,
                                     const string_search::SimdSubstringHints& hints,
                                     bool case_insensitive) {
  return case_insensitive ? string_search::ContainsSubstringI(path, required_substring, hints)
                          : string_search::ContainsSubstring(path, required_substring, hints);
}

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - state index bounded by DFA construction; char cast to uint8_t gives 0-255
bool MatchAtomOnce(const Atom& atom, std::string_view path, unsigned& index,
                   bool case_insensitive) {
  if (index >= path.size()) {
    return false;
  }
  const char c = path[index];

  bool match = false;
  switch (atom.kind) {
    case AtomKind::kLiteral:
      if (atom.literal == '/' || atom.literal == '\\') {
        match = IsSeparator(c);
      } else {
        match = CharsEqual(c, atom.literal, case_insensitive);
      }
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
// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

// Forward declaration for use by MatchFromUnbounded and MatchFromBounded.
bool MatchFrom(const Pattern& pattern, unsigned atom_index, std::string_view path,
               unsigned path_index);

// Unbounded repetition (x* / x+ / x{m,}): satisfy min_count, find max_consumed, backtrack.
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

// True when atoms[i] starts a double-star-slash unit: an unbounded "**"
// directly followed by a single separator. Such a unit may match zero
// directories (skipped positionally by MatchFrom below).
bool IsDoubleStarSlashSkip(const Pattern& pattern, unsigned atom_index) {
  if (atom_index + 1 >= pattern.atom_count) {
    return false;
  }
  const Atom& atom = pattern.atoms.at(atom_index);
  const Atom& next = pattern.atoms.at(atom_index + 1);
  return atom.kind == AtomKind::kDoubleStar && atom.IsUnbounded() &&
         next.kind == AtomKind::kLiteral &&
         (next.literal == '/' || next.literal == '\\') && next.min_count == 1 &&
         next.max_count == 1;
}

// True when atoms[i] starts a trailing slash + recursive-wildcard unit: a
// single separator followed by a final unbounded "**", which may match zero
// directories (so the pattern also matches the directory itself).
bool IsTrailingSlashDoubleStarSkip(const Pattern& pattern, unsigned atom_index) {
  if (atom_index + 2 != pattern.atom_count) {
    return false;
  }
  const Atom& atom = pattern.atoms.at(atom_index);
  const Atom& next = pattern.atoms.at(atom_index + 1);
  return atom.kind == AtomKind::kLiteral &&
         (atom.literal == '/' || atom.literal == '\\') && atom.min_count == 1 &&
         atom.max_count == 1 && next.kind == AtomKind::kDoubleStar &&
         next.IsUnbounded();
}

// Recursive backtracking matcher over compiled atoms.
bool MatchFrom(const Pattern& pattern, unsigned atom_index, std::string_view path,
               unsigned path_index) {
  if (atom_index == pattern.atom_count) {
    return path_index == path.size();
  }

  const Atom& atom = pattern.atoms.at(atom_index);

  // Double-star-slash matches zero directories: try skipping the DoubleStar
  // and its separator first, so a "report*" folder matches "report_q1/..."
  // but the separator still enforces the segment boundary (not "my_report").
  // Backtracking tries this skip at each candidate position, so unlike a
  // position-independent NFA epsilon it cannot match mid-segment.
  if (IsDoubleStarSlashSkip(pattern, atom_index) &&
      MatchFrom(pattern, atom_index + 2, path, path_index)) {
    return true;
  }

  // Trailing slash-double-star matches zero directories.
  if (IsTrailingSlashDoubleStarSkip(pattern, atom_index) &&
      MatchFrom(pattern, atom_index + 2, path, path_index)) {
    return true;
  }

  // Unbounded repetition (x+ / x{m,} / '*' / '**'): satisfy min_count, find
  // max_consumed, backtrack. kStar reaches this too: MatchAtomOnce matches a
  // single non-separator for it, and the loop below repeats it.
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
      status(other.status), uses_advanced(other.uses_advanced), is_literal_only(other.is_literal_only),
      simple_token_count(other.simple_token_count), epsilon_mask(other.epsilon_mask),
      cached_pattern_(std::move(other.cached_pattern_)),
      anchor_start(other.anchor_start), anchor_end(other.anchor_end),
      required_substring(std::move(other.required_substring)),
      required_substring_hints(other.required_substring_hints),
      has_required_substring(other.has_required_substring),
      required_substring_is_prefix(other.required_substring_is_prefix),
      required_substring_is_suffix(other.required_substring_is_suffix),
      extra_required_substrings(std::move(other.extra_required_substrings)),
      extra_required_hints(other.extra_required_hints),
      required_suffix(std::move(other.required_suffix)),
      required_suffix_hints(other.required_suffix_hints),
      has_required_suffix(other.has_required_suffix) {
  // Copy simple tokens if needed
  if (!uses_advanced && simple_token_count > 0) {
    std::memcpy(simple_tokens_storage.data(), other.simple_tokens_storage.data(),
                simple_token_count * kSimpleTokenSize);
  }

  // Nullify other's pointers to prevent double free
  // Note: Member initializer list already transferred ownership of all members
  // cached_pattern_ is moved via unique_ptr, so it\'s already nullified
  other.status = CompileStatus::kMovedFrom;
}

// Move assignment operator
CompiledPathPattern& CompiledPathPattern::operator=(CompiledPathPattern&& other) noexcept {
  if (this != &other) {
    // cached_pattern_ is managed by unique_ptr - automatic cleanup via RAII

    pattern_string = std::move(other.pattern_string);
    case_insensitive = other.case_insensitive;
    status = other.status;
    uses_advanced = other.uses_advanced;
    is_literal_only = other.is_literal_only;
    simple_token_count = other.simple_token_count;
    epsilon_mask = other.epsilon_mask;
    anchor_start = other.anchor_start;
    anchor_end = other.anchor_end;
    required_substring = std::move(other.required_substring);
    required_substring_hints = other.required_substring_hints;
    has_required_substring = other.has_required_substring;
    required_substring_is_prefix = other.required_substring_is_prefix;
    required_substring_is_suffix = other.required_substring_is_suffix;
    extra_required_substrings = std::move(other.extra_required_substrings);
    extra_required_hints = other.extra_required_hints;
    required_suffix = std::move(other.required_suffix);
    required_suffix_hints = other.required_suffix_hints;
    has_required_suffix = other.has_required_suffix;

    // Copy simple tokens if needed
    if (!uses_advanced && simple_token_count > 0) {
      std::memcpy(simple_tokens_storage.data(), other.simple_tokens_storage.data(),
                  simple_token_count * kSimpleTokenSize);
    }

    // Transfer ownership of DFA table and cached pattern
    cached_pattern_ = std::move(other.cached_pattern_);

    // Nullify other's pointers to prevent double free
    // cached_pattern_ is moved via unique_ptr, so it\'s already nullified
    other.status = CompileStatus::kMovedFrom;
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
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - state index bounded by DFA construction; char cast to uint8_t gives 0-255
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
// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

// Returns elapsed time in microseconds since start; used for compile-path timing logs.
auto ElapsedMicroseconds(std::chrono::high_resolution_clock::time_point start) {
  return std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::high_resolution_clock::now() - start);
}

/**
 * @brief Compile simple pattern (no advanced features) into the bitmask NFA form
 *
 * A pattern with more tokens than the 64-bit NFA state mask can hold (see
 * kMaxSimpleTokens) is NOT a compile failure: this returns nullopt and the
 * caller retries it on CompileSimplePatternAsBacktracking, which has no
 * position-mask limit. Token meanings are identical on both paths.
 *
 * @param pattern Pattern string (without anchors)
 * @param case_insensitive Whether matching should be case-insensitive
 * @param compile_start Start time for timing measurements
 * @return Compiled pattern, or nullopt when the pattern exceeds kMaxSimpleTokens
 */
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - state index bounded by DFA construction; char cast to uint8_t gives 0-255
std::optional<CompiledPathPattern> CompileSimplePattern(
  std::string_view pattern, bool case_insensitive,
  std::chrono::high_resolution_clock::time_point compile_start) {
  CompiledPathPattern compiled;
  compiled.case_insensitive = case_insensitive;
  compiled.uses_advanced = false;
  compiled.pattern_string = pattern;

  // OPTIMIZATION: Check if pattern is literal-only (no wildcards)
  // For literal-only patterns, we can use direct string comparison instead of DFA/NFA
  compiled.is_literal_only = IsLiteralOnly(pattern);
  if (compiled.is_literal_only) {
    // Skip DFA/NFA construction for literal-only patterns
    [[maybe_unused]] const auto compile_duration = ElapsedMicroseconds(compile_start);  // NOSONAR(cpp:S1481,cpp:S1854) - Read only by LOG_INFO_BUILD (compiled out under NDEBUG)
    LOG_INFO_BUILD("PathPattern compilation [LITERAL]: pattern='"
                   << compiled.pattern_string
                   << "', case_insensitive=" << (compiled.case_insensitive ? "true" : "false")
                   << ", total_time=" << compile_duration.count() << "μs");
    return compiled;
  }

  std::vector<SimpleToken> tokens;
  if (!BuildSimpleTokens(compiled.pattern_string, tokens)) {
    LOG_INFO_BUILD("PathPattern compilation [SIMPLE]: pattern has "
                   << tokens.size() << " tokens, over the " << kMaxSimpleTokens
                   << "-token NFA limit; retrying on the backtracking matcher");
    return std::nullopt;
  }

  compiled.simple_token_count = tokens.size();
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) - reinterpret_cast required for type-punning fixed-size storage array to SimpleToken*
  auto* storage = reinterpret_cast<SimpleToken*>(compiled.simple_tokens_storage.data()); // NOSONAR(cpp:S3630) - reinterpret_cast required for type-punning fixed-size storage array to SimpleToken* (performance-critical hot path)
  for (size_t i = 0; i < tokens.size(); ++i) {
    storage[i] = tokens[i];
  }
  compiled.epsilon_mask = ComputeEpsilonMask(storage, tokens.size());

  [[maybe_unused]] const auto compile_duration = ElapsedMicroseconds(compile_start);  // NOSONAR(cpp:S1481,cpp:S1854) - Read only by LOG_INFO_BUILD (compiled out under NDEBUG)
  LOG_INFO_BUILD(
    "PathPattern compilation [SIMPLE]: pattern='"
    << compiled.pattern_string
    << "', case_insensitive=" << (compiled.case_insensitive ? "true" : "false")
    << ", tokens=" << tokens.size()
    << ", total_time=" << compile_duration.count() << "μs");

  return compiled;
}
// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

/**
 * @brief Compile advanced pattern (with char classes, quantifiers, etc.)
 *
 * Compiles an advanced pattern (with char classes, quantifiers, etc.) into
 * a CompiledPathPattern using the Pattern structure for matching.
 *
 * @param pattern Pattern string (without anchors)
 * @param case_insensitive Whether matching should be case-insensitive
 * @param compile_start Start time for timing measurements
 * @return Compiled pattern (status == kOk on success)
 */
CompiledPathPattern CompileAdvancedPattern(
  std::string_view pattern, bool case_insensitive,
  std::chrono::high_resolution_clock::time_point compile_start) {
  CompiledPathPattern compiled;
  compiled.case_insensitive = case_insensitive;
  compiled.uses_advanced = true;
  compiled.pattern_string = pattern;

  // Compile and cache the advanced pattern to avoid re-parsing on each match
  const auto parse_start = std::chrono::high_resolution_clock::now();
  // Use RAII for Pattern allocation to prevent leaks if exceptions are thrown
  auto p =
    std::make_unique<Pattern>(CompilePattern(compiled.pattern_string, compiled.case_insensitive));
  const auto parse_end = std::chrono::high_resolution_clock::now();
  [[maybe_unused]] const auto parse_duration =  // NOSONAR(cpp:S1481,cpp:S1854) - Read only by LOG_INFO_BUILD (compiled out under NDEBUG)
    std::chrono::duration_cast<std::chrono::microseconds>(parse_end - parse_start);

  [[maybe_unused]] const unsigned atom_count = p->atom_count;   // NOSONAR(cpp:S1481,cpp:S1854) - Read only by LOG_INFO_BUILD (compiled out under NDEBUG)
  [[maybe_unused]] const auto pattern_status = p->status;    // NOSONAR(cpp:S1481,cpp:S1854) - Read only by LOG_INFO_BUILD (compiled out under NDEBUG)

  compiled.status = p->status;
  if (p->IsValid()) {
    // Preserve anchors we extracted earlier (before removing ^ and $)
    // Don't overwrite with p->anchor_start/anchor_end because CompilePattern
    // was called with pattern_without_anchors (no ^ or $)
    // compiled.anchor_start and compiled.anchor_end are already set correctly above
    // Transfer ownership to unique_ptr for RAII management
    compiled.cached_pattern_ = PatternPtr(p.release());
  }

  [[maybe_unused]] const auto compile_duration = ElapsedMicroseconds(compile_start);  // NOSONAR(cpp:S1481,cpp:S1854) - Read only by LOG_INFO_BUILD (compiled out under NDEBUG)
  LOG_INFO_BUILD("PathPattern compilation [ADVANCED]: pattern='"
                 << compiled.pattern_string
                 << "', case_insensitive=" << (compiled.case_insensitive ? "true" : "false")
                 << ", atoms=" << atom_count << ", parse_time=" << parse_duration.count() << "μs"
                 << ", total_time=" << compile_duration.count() << "μs"
                 << ", status=" << DescribeCompileStatus(pattern_status));

  return compiled;
}

/**
 * @brief Compile a simple-shaped pattern for the backtracking matcher.
 *
 * Used when a pattern has no advanced features but needs whole-segment
 * double-star semantics (see NeedsSegmentSemantics). Token meanings are
 * exactly the simple ones: '*' = zero or more non-separators, '?' = one
 * non-separator, double-star = any chars, separators match either '/' or
 * backslash. Unlike CompilePattern, no repetition suffix is parsed, so '+',
 * '{' and '}' stay plain literals (e.g. a "C++" folder still matches) - but
 * '*' and '?' mean the same thing here as there, always wildcards.
 *
 * @param pattern Pattern string (without anchors)
 * @param case_insensitive Whether matching should be case-insensitive
 * @param compile_start Start time for timing measurements
 * @return Compiled pattern (status == kOk on success)
 */
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - i < pattern.size() by loop condition; i+1 guarded; count < kMaxAtoms by loop condition
CompiledPathPattern CompileSimplePatternAsBacktracking(
  std::string_view pattern, bool case_insensitive,
  std::chrono::high_resolution_clock::time_point compile_start) {
  CompiledPathPattern compiled;
  compiled.case_insensitive = case_insensitive;
  compiled.uses_advanced = true;
  compiled.pattern_string = std::string(pattern);

  auto parsed = std::make_unique<Pattern>();
  parsed->case_insensitive = case_insensitive;
  unsigned count = 0;
  size_t i = 0;
  while (i < pattern.size() && count < kMaxAtoms) {
    Atom atom;
    atom.min_count = 1;
    atom.max_count = 1;
    if (const char c = pattern[i]; c == '*') {
      if (i + 1 < pattern.size() && pattern[i + 1] == '*') {
        atom.kind = AtomKind::kDoubleStar;
        atom.min_count = 0;
        atom.max_count = std::numeric_limits<unsigned>::max();
        i += 2;
      } else {
        atom.kind = AtomKind::kStar;
        atom.min_count = 0;
        atom.max_count = std::numeric_limits<unsigned>::max();
        ++i;
      }
    } else if (c == '?') {
      atom.kind = AtomKind::kQuestion;
      ++i;
    } else {
      atom.kind = AtomKind::kLiteral;
      atom.literal = c;
      ++i;
    }
    parsed->atoms.at(count) = atom;
    ++count;
  }
  if (i < pattern.size()) {
    // Truncated: more input than atom storage holds. Report the reason rather
    // than matching only a prefix of the pattern (same rule as CompilePattern).
    LOG_WARNING_BUILD("PathPattern compilation [BACKTRACK]: pattern exceeds atom limit, invalid");
    compiled.status = CompileStatus::kTooManyAtoms;
    return compiled;
  }
  parsed->atom_count = count;
  compiled.cached_pattern_ = PatternPtr(parsed.release());

  [[maybe_unused]] const auto compile_duration = ElapsedMicroseconds(compile_start);  // NOSONAR(cpp:S1481,cpp:S1854) - Read only by LOG_INFO_BUILD (compiled out under NDEBUG)
  LOG_INFO_BUILD("PathPattern compilation [BACKTRACK]: pattern='"
                 << compiled.pattern_string
                 << "', case_insensitive=" << (compiled.case_insensitive ? "true" : "false")
                 << ", atoms=" << count
                 << ", total_time=" << compile_duration.count() << "μs");

  return compiled;
}
// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

// Result structure for ExtractRequiredSubstring
struct RequiredSubstringInfo {
  std::string substring;
  bool is_prefix = false;
  bool is_suffix = false;
};

// Lowercase a literal run for case-insensitive prefilter storage (mirrors the
// lowering in ExtractRequiredSubstring so comparisons stay single-sided).
std::string LowerRunForPrefilter(std::string_view run, bool case_insensitive) {
  std::string lowered;
  lowered.reserve(run.size());
  for (const char c : run) {
    lowered.push_back(case_insensitive ? ToLowerChar(static_cast<unsigned char>(c)) : c);
  }
  return lowered;
}

// Append safe-literal runs for prefiltering, skipping character-class
// interiors ([...]) always and quantifier-brace interiors ({...}) for advanced
// patterns, where the enclosed chars are syntax (class members / counts), not
// match literals. Skipping is always sound (weaker filter); including such
// runs would wrongly reject matches (e.g. "file[0-9].txt" must match
// "file3.txt", which contains no literal "0-9").
// In advanced patterns a brace group is a quantifier when it parses as one;
// when it does not, its content is literal. The interior is skipped either
// way: weaker filter, never unsound.
size_t SkipBraceGroupInterior(std::string_view pattern, size_t i, size_t n) {
  ++i;  // consume '{'
  while (i < n && pattern[i] != '}') {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - i < n guarded
    ++i;
  }
  if (i < n) {
    ++i;  // consume '}'
  }
  return i;
}

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - i bounded by loop; i+1 guarded
void AppendPrefilterRuns(std::string_view pattern, bool advanced_parsing,
                         std::vector<std::string_view>& out_runs) {
  out_runs.clear();
  size_t i = 0;
  const size_t n = pattern.size();
  bool in_class = false;
  while (i < n) {
    const char c = pattern[i];
    if (in_class) {
      // Mirrors ParseCharClass: an escape cannot close the class.
      if (c == '\\' && i + 1 < n) {
        i += 2;
        continue;
      }
      if (c == ']') {
        in_class = false;
      }
      ++i;
      continue;
    }
    if (c == '[') {
      in_class = true;
      ++i;
      continue;
    }
    // In advanced patterns a brace group is a quantifier when it parses as
    // one; when it does not, its content is literal. Skip the interior either
    // way: weaker filter, never unsound. Simple patterns match braces
    // literally, so their content stays a valid run.
    if (advanced_parsing && c == '{') {
      i = SkipBraceGroupInterior(pattern, i, n);
      continue;
    }
    if (!IsSafeLiteralChar(c)) {
      ++i;
      continue;
    }
    const size_t run_start = i;
    while (i < n && IsSafeLiteralChar(pattern[i])) {
      ++i;
    }
    out_runs.push_back(pattern.substr(run_start, i - run_start));
  }
}
// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

// Extract longest literal substring for pre-filtering
// Returns RequiredSubstringInfo with substring, is_prefix, and is_suffix flags.
// For case-insensitive patterns, the substring is converted to lowercase.
RequiredSubstringInfo ExtractRequiredSubstring(std::string_view pattern, bool case_insensitive) {
  if (pattern.length() < kMinRequiredSubstringLength) {
    return {};
  }

  const bool advanced_parsing = UsesAdvancedFeatures(pattern);
  std::vector<std::string_view> runs;
  AppendPrefilterRuns(pattern, advanced_parsing, runs);
  const std::string_view* best = nullptr;
  for (const std::string_view& run : runs) {
    if (best == nullptr || run.size() > best->size()) {
      best = &run;
    }
  }
  if (best == nullptr || best->size() < kMinRequiredSubstringLength) {
    return {};
  }

  RequiredSubstringInfo info;
  info.substring = LowerRunForPrefilter(*best, case_insensitive);
  const auto best_offset = static_cast<size_t>(best->data() - pattern.data());
  info.is_prefix = (best_offset == 0);
  info.is_suffix = (best_offset + best->size() == pattern.size());
  if (!info.is_suffix && !pattern.empty() && IsSafeLiteralChar(pattern.back())) {
    // The pattern ends with a literal run equal to the primary text (e.g. the
    // second "foo" in "*foo*foo"): any match genuinely ends with it.
    if (const std::string_view trailing = runs.back();
        trailing.size() == best->size() &&
        LowerRunForPrefilter(trailing, case_insensitive) == info.substring) {
      info.is_suffix = true;
    }
  }
  return info;
}

// True when the run ending at run_end (exclusive) is followed by a repetition
// suffix that can match zero copies ({0...}), making the run's last char
// optional. Such runs are not guaranteed in a match, so they must not become
// prefilter constraints (e.g. "[a]xyz{0,2}1234" matches "axy1234").
// '*' and '?' are wildcards, not repetition: a run followed by one stays exact,
// because every atom of the run is mandatory and the wildcard can only consume
// characters *after* the run ("[a]xyz*1234" needs "xyz", hence "axy1234" is
// no longer a match).
// Simple patterns have no repetition suffixes, so the guard is skipped.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - run_end guarded against pattern.size() above; run_end + 1 guarded inline
bool IsRunLastCharOptional(std::string_view pattern, size_t run_end, bool advanced_parsing) {
  if (!advanced_parsing || run_end >= pattern.size()) {
    return false;
  }
  // {m}, {m,}, {m,n}: zero-able only when the minimum is 0.
  return pattern[run_end] == '{' && run_end + 1 < pattern.size() &&
         pattern[run_end + 1] == '0';
}
// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

// Collect AND-ed prefilter constraints beyond the primary required substring:
// every other safe-literal run of sufficient length, plus a trailing-run
// suffix constraint when the primary does not already cover the pattern end.
// A path ending with the trailing run trivially contains it, so the endswith
// check subsumes a contains check for that run.
void CollectExtraRequiredSubstrings(CompiledPathPattern& compiled, std::string_view pattern,
                                    bool advanced_parsing) {
  std::vector<std::string_view> runs;
  AppendPrefilterRuns(pattern, advanced_parsing, runs);
  if (runs.empty()) {
    return;
  }
  const bool has_trailing_run = !pattern.empty() && IsSafeLiteralChar(pattern.back());
  const bool primary_covers_suffix = compiled.has_required_substring &&
                                     compiled.required_substring_is_suffix;
  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - back() guarded by !runs.empty()
  for (const std::string_view run : runs) {
    if (run.size() < kMinRequiredSubstringLength) {
      continue;
    }
    const size_t run_end = static_cast<size_t>(run.data() - pattern.data()) + run.size();
    const bool is_trailing = has_trailing_run && run_end == pattern.size();
    std::string lowered = LowerRunForPrefilter(run, compiled.case_insensitive);
    if (is_trailing) {
      if (!(primary_covers_suffix && lowered == compiled.required_substring)) {
        compiled.required_suffix = std::move(lowered);
        compiled.required_suffix_hints =
            string_search::MakeSimdSubstringHints(compiled.required_suffix);
        compiled.has_required_suffix = true;
      }
      continue;
    }
    if (compiled.has_required_substring && lowered == compiled.required_substring) {
      continue;  // already checked as the primary substring
    }
    if (IsRunLastCharOptional(pattern, run_end, advanced_parsing)) {
      continue;
    }
    compiled.extra_required_substrings.push_back(std::move(lowered));
  }
  // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  compiled.extra_required_hints.reserve(compiled.extra_required_substrings.size());
  for (const std::string& extra : compiled.extra_required_substrings) {
    compiled.extra_required_hints.push_back(string_search::MakeSimdSubstringHints(extra));
  }
}

CompiledPathPattern CompilePathPattern(std::string_view pattern, MatchOptions options) {
  const auto compile_start = std::chrono::high_resolution_clock::now();

  const bool case_insensitive =
    (static_cast<unsigned>(options) & static_cast<unsigned>(MatchOptions::kCaseInsensitive)) != 0U;

  // Extract anchors before checking for advanced features
  // This allows patterns like "^file_" to be treated as simple patterns
  bool anchor_start = false;  // NOLINT(misc-const-correctness) - anchor_start is modified by ExtractAnchors (passed by non-const reference), cannot be const
  bool anchor_end = false;  // NOLINT(misc-const-correctness) - anchor_end is modified by ExtractAnchors (passed by non-const reference), cannot be const
  const std::string_view pattern_without_anchors = ExtractAnchors(pattern, anchor_start, anchor_end);

  CompiledPathPattern compiled;
  // NOTE: patterns needing whole-segment double-star semantics take the
  // backtracking matcher: double-star-slash means zero or more whole
  // segments, so a "report*" folder matches "report_q1" but not "my_report".
  // A char-level NFA/DFA epsilon cannot express this positionally
  // (see NeedsSegmentSemantics). Simple-shaped ones keep exact simple token
  // meanings (no quantifier parsing) via CompileSimplePatternAsBacktracking.
  // advanced_parsing also gates the prefilter quantifier guard below: only
  // CompileAdvancedPattern parses trailing ?, *, {0..} as quantifiers.
  const bool advanced_parsing = UsesAdvancedFeatures(pattern_without_anchors);
  if (!advanced_parsing) {
    // Prefer the bitmask NFA, but take the backtracking matcher when the pattern
    // needs whole-segment double-star semantics, or when it has more tokens
    // than the NFA's 64-bit state mask can index (BuildSimpleTokens then returns
    // nullopt). Both compile paths read the same token meanings, so this is a
    // speed trade, not a semantic one.
    std::optional<CompiledPathPattern> simple;
    if (!NeedsSegmentSemantics(pattern_without_anchors)) {
      simple = CompileSimplePattern(pattern_without_anchors, case_insensitive, compile_start);
    }
    if (simple) {
      compiled = std::move(*simple);
    } else {
      compiled = CompileSimplePatternAsBacktracking(pattern_without_anchors, case_insensitive,
                                                    compile_start);
    }
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
    compiled.required_substring_hints = string_search::MakeSimdSubstringHints(compiled.required_substring);
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

  // Multi-literal AND-prefilter plus trailing-run suffix constraint. Runs even
  // when no primary was found (then it trivially finds nothing either).
  CollectExtraRequiredSubstrings(compiled, pattern_without_anchors, advanced_parsing);

  return compiled;
}

const char* DescribeCompileStatus(CompileStatus status) {
  switch (status) {
    case CompileStatus::kOk:
      return "ok";
    case CompileStatus::kTooManyAtoms:
      return "too many atoms (pattern too long)";
    case CompileStatus::kUnterminatedClass:
      return "unterminated character class (missing ']')";
    case CompileStatus::kMovedFrom:
      return "moved-from pattern";
  }
  return "unknown";
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

static inline bool HasPrefilterConstraints(const CompiledPathPattern& compiled) {
  return compiled.has_required_substring || compiled.has_required_suffix ||
         !compiled.extra_required_substrings.empty();
}

static inline bool CheckRequiredSubstringForMatch(
    const CompiledPathPattern& compiled, std::string_view path) {
  if (compiled.has_required_substring) {
    bool primary_ok = false;
    if (compiled.required_substring_is_prefix) {
      primary_ok = CheckRequiredSubstringPrefix(path, compiled.required_substring,
                                                compiled.case_insensitive);
    } else if (compiled.required_substring_is_suffix) {
      primary_ok = CheckRequiredSubstringSuffix(path, compiled.required_substring,
                                                compiled.case_insensitive);
    } else {
      primary_ok = CheckRequiredSubstringContains(path, compiled.required_substring,
                                                  compiled.required_substring_hints,
                                                  compiled.case_insensitive);
    }
    if (!primary_ok) {
      return false;
    }
  }
  if (compiled.has_required_suffix &&
      !CheckRequiredSubstringSuffix(path, compiled.required_suffix,
                                    compiled.case_insensitive)) {
    return false;
  }
  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - i bounded by loop; hints indexed in lockstep (same size by construction)
  for (size_t i = 0; i < compiled.extra_required_substrings.size(); ++i) {
    if (!CheckRequiredSubstringContains(path, compiled.extra_required_substrings[i],
                                        compiled.extra_required_hints[i],
                                        compiled.case_insensitive)) {
      return false;
    }
  }
  // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  return true;
}

static inline bool MatchSimplePathPattern(const CompiledPathPattern& compiled,
                                          std::string_view path) {
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
  if (p == nullptr || !p->IsValid()) {
    return false;
  }
  return MatchFrom(*p, 0, path, 0);
}

bool PathPatternMatches(const CompiledPathPattern& compiled, std::string_view path) {
  if (!compiled.IsValid()) {
    return false;
  }
  if (compiled.is_literal_only) {
    return MatchLiteralOnlyPath(compiled, path);
  }
  if (HasPrefilterConstraints(compiled) && !CheckRequiredSubstringForMatch(compiled, path)) {
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

void AppendLiteralRuns(std::string_view pattern, std::vector<std::string_view>& out_runs) {
  out_runs.clear();
  out_runs.reserve((pattern.size() + 1U) / 2U);
  size_t i = 0;
  while (i < pattern.size()) {
    while (i < pattern.size() && !IsSafeLiteralChar(pattern[i])) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
      ++i;
    }
    const size_t run_start = i;
    while (i < pattern.size() && IsSafeLiteralChar(pattern[i])) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
      ++i;
    }
    if (i > run_start) {
      out_runs.push_back(pattern.substr(run_start, i - run_start));
    }
  }
}

bool FindPathPatternSpans(std::string_view pattern, std::string_view text, bool case_sensitive,
                          std::vector<string_search::MatchSpan>& out_spans) {
  out_spans.clear();
  if (pattern.empty() || text.empty()) {
    return false;
  }

  std::vector<std::string_view> runs;
  AppendLiteralRuns(pattern, runs);
  if (runs.empty()) {
    return false;
  }

  out_spans.reserve(runs.size());

  size_t text_pos = 0;
  std::vector<string_search::MatchSpan> one;
  one.reserve(1);
  for (const std::string_view lit : runs) {
    if (text_pos > text.size()) {
      break;
    }
    if (const std::string_view remaining = text.substr(text_pos);
        !string_search::FindSubstringSpans(remaining, lit, case_sensitive, one) || one.empty()) {
      continue;
    }
    const string_search::MatchSpan& hit = one.front();
    out_spans.push_back(
        string_search::MatchSpan{text_pos + hit.start_, text_pos + hit.end_});
    text_pos = text_pos + hit.end_;
  }

  // Merge adjacent runs so "bar" + ".txt" paint as one continuous highlight.
  string_search::string_search_detail::MergeAdjacentSpans(out_spans);

  return !out_spans.empty();
}

}  // namespace path_pattern
