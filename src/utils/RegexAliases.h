#pragma once

// Regex aliases with FAST_LIBS_BOOST support.
//   - std::regex (default) — standard library.
//   - boost::regex (FAST_LIBS_BOOST=ON) — better performance; compiled library
//     (linked via Boost::regex on GCC/Clang; auto-linked on MSVC).
//
// See docs/BOOST_VS_ANKERL_ANALYSIS.md for hash map comparison.

#ifdef FAST_LIBS_BOOST
#include <boost/regex.hpp>

using regex_t = boost::regex;  // NOSONAR(cpp:S117) NOLINT(readability-identifier-naming) - API-matching alias
using regex_error_t = boost::regex_error;  // NOSONAR(cpp:S117) NOLINT(readability-identifier-naming) - API-matching alias
template <typename BidirectionalIterator>
using match_results_t = boost::match_results<BidirectionalIterator>;  // NOSONAR(cpp:S117) NOLINT(readability-identifier-naming) - API-matching alias
namespace regex_constants = boost::regex_constants;  // NOLINT(misc-unused-alias-decls) - used by Popups.cpp, StdRegexUtils.h

template<typename... Args>
bool regex_match(Args&&... args) {  // NOLINT(readability-identifier-naming,cppcoreguidelines-missing-std-forward) - API-matching; forward via pack expansion
    return boost::regex_match(std::forward<Args>(args)...);
}

template<typename... Args>
bool regex_search(Args&&... args) {  // NOLINT(readability-identifier-naming,cppcoreguidelines-missing-std-forward) - API-matching; forward via pack expansion
    return boost::regex_search(std::forward<Args>(args)...);
}
#else
#include <regex>

using regex_t = std::regex;  // NOSONAR(cpp:S117) NOLINT(readability-identifier-naming) - API-matching alias
using regex_error_t = std::regex_error;  // NOSONAR(cpp:S117) NOLINT(readability-identifier-naming) - API-matching alias
template <typename BidirectionalIterator>
using match_results_t = std::match_results<BidirectionalIterator>;  // NOSONAR(cpp:S117) NOLINT(readability-identifier-naming) - API-matching alias
namespace regex_constants = std::regex_constants;  // NOLINT(misc-unused-alias-decls) - used by Popups.cpp, StdRegexUtils.h

template<typename... Args>
bool regex_match(Args&&... args) {  // NOLINT(readability-identifier-naming,cppcoreguidelines-missing-std-forward) - API-matching; forward via pack expansion
    return std::regex_match(std::forward<Args>(args)...);
}

template<typename... Args>
bool regex_search(Args&&... args) {  // NOLINT(readability-identifier-naming,cppcoreguidelines-missing-std-forward) - API-matching; forward via pack expansion
    return std::regex_search(std::forward<Args>(args)...);
}
#endif  // FAST_LIBS_BOOST

// Engine capabilities that differ between the two aliases above and cannot be
// probed from the alias itself.
namespace regex_traits {

// How '$' behaves with no explicit flags. The two engines disagree, and the
// difference is observable (pinned against the real engine in
// tests/RegexSearchPrefilterTests.cpp):
//   - boost::regex (Perl syntax): line-oriented. It matches at the end of the
//     input *and* immediately before any line terminator, so '\.cpp$' matches
//     both "x.cpp\n" and "x.cpp\nX".
//   - std::regex (ECMAScript): matches only at the end of the input. This
//     project never passes regex_constants::multiline, so "x.cpp\n" is not a
//     match.
// Code that decides a match without running the regex (the exact extension-set
// fast path in SearchPatternUtils.h) must mirror this, or the same query returns
// different rows depending on which engine the build linked.
inline constexpr bool kDollarIsLineOriented =
#ifdef FAST_LIBS_BOOST
    true;
#else
    false;
#endif  // FAST_LIBS_BOOST

}  // namespace regex_traits
