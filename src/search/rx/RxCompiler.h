#pragma once

/**
 * @file search/rx/RxCompiler.h
 * @brief `rx:` structured pattern dialect: S-expression source -> ECMAScript regex.
 *
 * Spec: specs/2026-10-29_RX_MATCHER_SPEC.md.
 *
 * `rx:` is a readable front end, not a new matching engine. CompileRx parses the
 * S-expression once (on the thread that builds the matcher, never per row) and
 * lowers it to an ECMAScript regex that the existing `re:` machinery
 * (CreateMatcherImplStdRegex, its literal/suffix pre-filters and the
 * thread-local regex cache) executes unchanged. Alongside the regex it reports
 * the longest literal every match must contain, harvested from the AST - exact
 * where re-deriving it from the emitted regex text would be lossy.
 *
 * Layering: standard-library-only, no dependency on the matcher layer, so the
 * GUI diagnostics can use it without pulling in matcher machinery.
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace rx {

// Source-length budget, same value as search_pattern_utils::kMaxPatternLength
// (kept separate so this header stays dependency-free; pinned by a test).
inline constexpr std::size_t kMaxRxSourceLength = 1000;
// The emitted regex must also stay within the `re:` length budget, otherwise
// CreateMatcherImplStdRegex would silently reject it.
inline constexpr std::size_t kMaxRxEmittedLength = 1000;
// Maximum parenthesis nesting depth (bounds parser recursion).
inline constexpr std::size_t kMaxRxNestingDepth = 32;
// Upper bound for (repeat ...) counts.
inline constexpr std::size_t kMaxRxRepeatCount = 10000;

// Why an rx source did not compile. kNone means it compiled.
enum class RxError : std::uint8_t {  // NOLINT(performance-enum-size) - explicit uint8_t; checker may still flag in some contexts
  kNone = 0U,                     // NOLINT(readability-identifier-naming) - project k prefix for enum constants
  kPatternTooLong,                // NOLINT(readability-identifier-naming) - source longer than kMaxRxSourceLength
  kEmittedRegexTooLong,           // NOLINT(readability-identifier-naming) - lowered regex longer than kMaxRxEmittedLength
  kNestingTooDeep,                // NOLINT(readability-identifier-naming) - more than kMaxRxNestingDepth open '('
  kUnclosedParen,                 // NOLINT(readability-identifier-naming) - input ended with '(' still open (typically mid-typing)
  kUnexpectedCloseParen,          // NOLINT(readability-identifier-naming) - ')' with no matching '('
  kUnterminatedString,            // NOLINT(readability-identifier-naming) - '"' with no closing '"'
  kEmptyList,                     // NOLINT(readability-identifier-naming) - "()"
  kMissingOperator,               // NOLINT(readability-identifier-naming) - list does not start with a bare operator name
  kUnknownOperator,               // NOLINT(readability-identifier-naming) - operator name not in the dialect
  kWrongArity,                    // NOLINT(readability-identifier-naming) - wrong number of arguments
  kInvalidArgument,               // NOLINT(readability-identifier-naming) - argument of the wrong kind for the operator
  kInvalidRange,                  // NOLINT(readability-identifier-naming) - bad char range or repeat count
  kNestedQuantifier,              // NOLINT(readability-identifier-naming) - quantifier inside a quantifier (ReDoS guard)
  kGeneralNegationNotSupported,   // NOLINT(readability-identifier-naming) - (not X) where X is not a character class
  kPathOperatorMisplaced,         // NOLINT(readability-identifier-naming) - dir/name/ext/exclude-dir/path outside top level or (path ...)
  kDuplicatePathClause,           // NOLINT(readability-identifier-naming) - more than one (name ...) or (ext ...) in a path
};

// Result of CompileRx. On success `regex` holds the ECMAScript pattern (empty
// when the source is empty: that is an all-pass query, like a bare `pp:`).
struct RxCompileResult {
  RxError error = RxError::kNone;
  // Human-readable reason (empty on success), already including any
  // "did you mean" suggestion. Suitable for direct display.
  std::string message;
  // Byte offset into the rx source (after the `rx:` prefix) the error refers to.
  std::size_t error_offset = 0;
  // Emitted ECMAScript regex (empty on error or for an empty source).
  std::string regex;
  // Longest literal every match must contain (>= 3 chars), or empty. Case is
  // preserved; callers lowercase it for case-insensitive matching.
  std::string required_literal;
  // Number of '(' still open when the input ended. Non-zero only with
  // kUnclosedParen; lets the UI distinguish "still typing" from "wrong".
  std::size_t open_paren_count = 0;

  [[nodiscard]] bool Ok() const noexcept { return error == RxError::kNone; }
  [[nodiscard]] bool IsEmptyQuery() const noexcept { return Ok() && regex.empty(); }
  // True when the only problem is unclosed parentheses (the user is mid-typing).
  [[nodiscard]] bool IsIncomplete() const noexcept { return error == RxError::kUnclosedParen; }
};

// Compile an rx source (the text after the `rx:` prefix). Never throws for
// malformed input; failures are reported through RxCompileResult::error.
[[nodiscard]] RxCompileResult CompileRx(std::string_view source);

// Visual/syntactic category of an rx token for presentation and syntax highlighting.
enum class RxTokenCategory : std::uint8_t {  // NOLINT(performance-enum-size) - retained suppression
  kPrefix = 0U,                              // NOLINT(readability-identifier-naming) - retained suppression
  kParenthesis,                              // NOLINT(readability-identifier-naming) - retained suppression
  kOperator,                                 // NOLINT(readability-identifier-naming) - retained suppression
  kPathClause,                               // NOLINT(readability-identifier-naming) - retained suppression
  kNamedClass,                               // NOLINT(readability-identifier-naming) - retained suppression
  kQuotedString,                             // NOLINT(readability-identifier-naming) - retained suppression
  kBareLiteral,                              // NOLINT(readability-identifier-naming) - retained suppression
  kWhitespace,                               // NOLINT(readability-identifier-naming) - retained suppression
};

// Classifies a bare atom (word/symbol) in an rx expression for syntax highlighting.
// When is_clause_head is true, the atom immediately follows an opening '('
// (e.g., in "(seq ...)" or "(dir ...)").
[[nodiscard]] RxTokenCategory ClassifyRxAtom(std::string_view atom, bool is_clause_head) noexcept;

// Short, stable description of an error kind (never null).
[[nodiscard]] const char* DescribeRxError(RxError error) noexcept;

}  // namespace rx
