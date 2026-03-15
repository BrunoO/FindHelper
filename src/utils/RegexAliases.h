#pragma once

// Regex aliases with FAST_LIBS_BOOST support.
//   - std::regex (default) — standard library.
//   - boost::regex (FAST_LIBS_BOOST=ON) — better performance; header-only in C++11+ (no build).
//
// See docs/BOOST_VS_ANKERL_ANALYSIS.md for hash map comparison.

#ifdef FAST_LIBS_BOOST
#include <boost/regex.hpp>

using regex_t = boost::regex;  // NOSONAR(cpp:S117) NOLINT(readability-identifier-naming) - API-matching alias
using regex_error_t = boost::regex_error;  // NOSONAR(cpp:S117) NOLINT(readability-identifier-naming) - API-matching alias
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
