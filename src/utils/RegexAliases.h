#pragma once

// Regex aliases with FAST_LIBS_BOOST feature flag support
// Supports two options:
//   1. std::regex (default, no flags) - Standard library, works everywhere
//   2. boost::regex (FAST_LIBS_BOOST=ON) - Better performance, requires Boost 1.80+
//
// Setup:
//   - Boost: Requires Boost 1.80+ installed system-wide or via CMake find_package
//   - CMake will handle platform-specific availability checks
//   See docs/BOOST_VS_ANKERL_ANALYSIS.md for detailed comparison

#ifdef FAST_LIBS_BOOST
// Use boost::regex for better performance (Boost 1.80+)
// Requires Boost to be available via find_package or system installation
#include <boost/regex.hpp>

// Bring boost::regex types into our namespace for compatibility
// Type aliases use _t suffix for consistency with existing utility aliases
// (hash_map_t, hash_set_t) - this is intentional for utility type aliases
using regex_t = boost::regex;  // NOSONAR(cpp:S117) NOLINT(readability-identifier-naming) - API-matching alias
using regex_error_t = boost::regex_error;  // NOSONAR(cpp:S117) NOLINT(readability-identifier-naming) - API-matching alias

// Use boost::regex_constants directly (same API as std::regex_constants)
namespace regex_constants = boost::regex_constants;  // NOLINT(misc-unused-alias-decls) - used by Popups.cpp, StdRegexUtils.h

// Convenience functions that delegate to boost
template<typename... Args>
bool regex_match(Args&&... args) {  // NOLINT(readability-identifier-naming,cppcoreguidelines-missing-std-forward) - API-matching; forward via pack expansion
    return boost::regex_match(std::forward<Args>(args)...);
}

template<typename... Args>
bool regex_search(Args&&... args) {  // NOLINT(readability-identifier-naming,cppcoreguidelines-missing-std-forward) - API-matching; forward via pack expansion
    return boost::regex_search(std::forward<Args>(args)...);
}
#else
// Use standard library (default)
#include <regex>

// Use std::regex types directly
// Type aliases use _t suffix for consistency with existing utility aliases
// (hash_map_t, hash_set_t) - this is intentional for utility type aliases
using regex_t = std::regex;  // NOSONAR(cpp:S117) NOLINT(readability-identifier-naming) - API-matching alias
using regex_error_t = std::regex_error;  // NOSONAR(cpp:S117) NOLINT(readability-identifier-naming) - API-matching alias

// Use std::regex_constants directly
namespace regex_constants = std::regex_constants;  // NOLINT(misc-unused-alias-decls) - used by Popups.cpp, StdRegexUtils.h

// Convenience functions that delegate to std
template<typename... Args>
bool regex_match(Args&&... args) {  // NOLINT(readability-identifier-naming,cppcoreguidelines-missing-std-forward) - API-matching; forward via pack expansion
    return std::regex_match(std::forward<Args>(args)...);
}

template<typename... Args>
bool regex_search(Args&&... args) {  // NOLINT(readability-identifier-naming,cppcoreguidelines-missing-std-forward) - API-matching; forward via pack expansion
    return std::regex_search(std::forward<Args>(args)...);
}
#endif  // FAST_LIBS_BOOST

