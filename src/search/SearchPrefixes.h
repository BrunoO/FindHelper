#pragma once

#include <array>
#include <cstddef>
#include <string_view>

// Search-input prefix tokens (`pp:`, `re:`, `fz:`) and the helpers that detect
// and strip them.
//
// Why this header exists: a prefix token is a user-facing contract that was
// repeated verbatim in the matcher factory (SearchPatternUtils.h), the
// search-controller debounce guard (SearchController.cpp), the AI search prompt
// (GeminiApiUtils.cpp) and the regex-generator popup (Popups.cpp). Hard-coding
// the literal at every dispatch site meant that renaming one token (`rs:` -->
// `re:`) had to be applied by hand in 35 files, and adding a fourth prefix
// carried the same drift risk. Declare each token once here and let every
// dispatch site go through HasExplicitPrefix / StripPrefix / IsBarePrefix.
//
// Scope: tokens and detection only. User-facing prose (Search Help, README, the
// Gemini prompt) and tests keep literal prefixes on purpose: a rename must stay
// visible in the contract the user reads, and a test that shares a constant
// with the implementation cannot catch a rename.
//
// Layering: header-only, standard-library-only. Deliberately not part of
// SearchPatternUtils.h so the API and UI layers can use the tokens without
// pulling in the matcher machinery.

namespace search_pattern_utils {
namespace prefix {

inline constexpr std::string_view kPathPattern = "pp:";  // PathPattern dialect
inline constexpr std::string_view kRegex = "re:";        // ECMAScript std::regex
inline constexpr std::string_view kFuzzy = "fz:";        // subsequence match

// Every token recognized as an explicit prefix, in the detection priority order
// used by DetectPatternType. Kept enumerable so tests can pin that a newly added
// token is really detected and stripped: a token listed here without a matching
// arm in IsExplicitPrefixToken fails the prefix-table tests.
inline constexpr std::array<std::string_view, 3> kAll{kPathPattern, kRegex, kFuzzy};

// Removed by the `rs:` --> `re:` rename. This is NOT a recognized prefix any
// more; it is kept only so the UI can explain the rename instead of leaving the
// user with an empty results table (see PatternDiagnostics).
inline constexpr std::string_view kLegacyRegex = "rs:";

// Token width. DetectPatternType compares a fixed-width leading token, so every
// prefix must be exactly this long; a longer/shorter token must fail to compile
// rather than silently land in PathPattern/Glob/Substring detection.
inline constexpr std::size_t kTokenLength = 3;

static_assert(kPathPattern.size() == kTokenLength && kRegex.size() == kTokenLength &&
                  kFuzzy.size() == kTokenLength,
              "Every search prefix must be kTokenLength bytes: detection compares a fixed-width token");

}  // namespace prefix

// True when @p input starts with the specific @p token.
[[nodiscard]] constexpr bool HasPrefix(std::string_view input, std::string_view token) {
  return !token.empty() && input.size() >= token.size() && input.compare(0, token.size(), token) == 0;
}

// True when @p token is one of the declared prefixes. A leading token is
// kTokenLength wide, so this is a fixed-width comparison and every arm must
// compare a whole token (see the static_assert above).
[[nodiscard]] constexpr bool IsExplicitPrefixToken(std::string_view token) {
  return token == prefix::kPathPattern || token == prefix::kRegex || token == prefix::kFuzzy;
}

// True when @p input starts with one of the explicit prefixes.
[[nodiscard]] constexpr bool HasExplicitPrefix(std::string_view input) {
  return input.size() >= prefix::kTokenLength &&
         IsExplicitPrefixToken(input.substr(0, prefix::kTokenLength));
}

// True when @p input is exactly a prefix token and nothing else, i.e. the user is
// mid-typing ("pp:", "re:") and there is no pattern content yet.
[[nodiscard]] constexpr bool IsBarePrefix(std::string_view input) {
  return input.size() == prefix::kTokenLength && IsExplicitPrefixToken(input);
}

// The pattern with its explicit prefix removed; @p input unchanged when it has
// none. The returned view refers into @p input (no allocation). An input that is
// exactly a prefix yields an empty view.
[[nodiscard]] constexpr std::string_view StripPrefix(std::string_view input) {
  return HasExplicitPrefix(input) ? input.substr(prefix::kTokenLength) : input;
}

// True when @p input starts with the retired `rs:` token, which is no longer a
// prefix. Callers use this to explain the rename rather than letting the query
// fall through to PathPattern/Glob/Substring and silently return wrong rows.
[[nodiscard]] constexpr bool HasLegacyRegexPrefix(std::string_view input) {
  return HasPrefix(input, prefix::kLegacyRegex);
}

}  // namespace search_pattern_utils
