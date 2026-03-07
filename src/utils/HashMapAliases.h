#pragma once

#include <string>
#include <string_view>

// Hash map/set aliases with FAST_LIBS_BOOST feature flag support
// Supports two options:
//   1. std::unordered_map (default, no flags) - Standard library, works everywhere
//   2. boost::unordered_map (FAST_LIBS_BOOST=ON) - Faster lookups, stable references
//      Requires Boost 1.80+ (platform availability handled by CMake)
//
// Setup:
//   - Boost: Requires Boost 1.80+ installed system-wide or via CMake find_package
//   - CMake will handle platform-specific availability checks
//   See docs/BOOST_VS_ANKERL_ANALYSIS.md for detailed comparison

// Transparent hasher/equality for heterogeneous string lookup.
// Enables find(string_view) on a map<string, V> without constructing a string.
// Only effective with boost::unordered_map (FAST_LIBS_BOOST=ON, Boost 1.80+).
// std::unordered_map (C++17) does not support heterogeneous lookup.
struct TransparentStringHash {
  using is_transparent = void;  // NOLINT(readability-identifier-naming) - required ADL name for transparent hashing
  std::size_t operator()(std::string_view sv) const noexcept {
    return std::hash<std::string_view>{}(sv);
  }
};

struct TransparentStringEqual {
  using is_transparent = void;  // NOLINT(readability-identifier-naming) - required ADL name for transparent hashing
  bool operator()(std::string_view lhs, std::string_view rhs) const noexcept {
    return lhs == rhs;
  }
};

#ifdef FAST_LIBS_BOOST
// Use boost::unordered_map for faster lookups (Boost 1.81+)
// Requires Boost to be available via find_package or system installation
#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

namespace fast_hash {
    template<typename K, typename V>
    using hash_map = boost::unordered_map<K, V>;  // NOLINT(readability-identifier-naming) - short alias used project-wide

    template<typename V>
    using hash_set = boost::unordered_set<V>;  // NOLINT(readability-identifier-naming) - short alias used project-wide
}
#else
// Use standard library (default)
#include <unordered_map>
#include <unordered_set>

namespace fast_hash {
    template<typename K, typename V>
    using hash_map = std::unordered_map<K, V>;  // NOLINT(readability-identifier-naming) - short alias used project-wide

    template<typename V>
    using hash_set = std::unordered_set<V>;  // NOLINT(readability-identifier-naming) - short alias used project-wide
}
#endif  // FAST_LIBS_BOOST

// Convenience aliases for common use cases
template<typename K, typename V>
using hash_map_t = fast_hash::hash_map<K, V>;  // NOLINT(readability-identifier-naming) - short alias used project-wide

template<typename V>
using hash_set_t = fast_hash::hash_set<V>;  // NOLINT(readability-identifier-naming) - short alias used project-wide

// Alias for sets that require pointer stability (e.g., StringPool)
// Uses boost::unordered_set when FAST_LIBS_BOOST is enabled (also stable),
// otherwise falls back to std::unordered_set
#ifdef FAST_LIBS_BOOST
template<typename V>
using hash_set_stable_t = boost::unordered_set<V>;  // NOLINT(readability-identifier-naming) - short alias used project-wide
#else
template<typename V>
using hash_set_stable_t = std::unordered_set<V>;  // NOLINT(readability-identifier-naming) - short alias used project-wide; <unordered_set> already included above (line 31)
#endif  // FAST_LIBS_BOOST

// Alias for maps with std::string keys supporting heterogeneous lookup via string_view.
// With FAST_LIBS_BOOST: boost::unordered_map (Boost 1.81+) + TransparentStringHash/Equal
//   enables find(string_view) without allocating a temporary std::string key.
// Without FAST_LIBS_BOOST: std::unordered_map (C++17 does not support heterogeneous lookup).
#ifdef FAST_LIBS_BOOST
template<typename V>
using string_key_map_t = boost::unordered_map<std::string, V, TransparentStringHash, TransparentStringEqual>;  // NOLINT(readability-identifier-naming) - short alias used project-wide
#else
template<typename V>
using string_key_map_t = std::unordered_map<std::string, V>;  // NOLINT(readability-identifier-naming) - short alias used project-wide
#endif  // FAST_LIBS_BOOST

// Flat (open-addressing) hash map alias for cache-friendly iteration and lookup.
// With FAST_LIBS_BOOST: boost::unordered_flat_map (Boost 1.81+) stores values inline in a
//   contiguous array — no per-node heap allocation, better cache locality for large maps.
// Without FAST_LIBS_BOOST: falls back to std::unordered_map (node-based, same as hash_map_t).
#ifdef FAST_LIBS_BOOST
template<typename K, typename V>
using flat_hash_map_t = boost::unordered_flat_map<K, V>;  // NOLINT(readability-identifier-naming) - short alias used project-wide
#else
template<typename K, typename V>
using flat_hash_map_t = std::unordered_map<K, V>;  // NOLINT(readability-identifier-naming) - short alias used project-wide
#endif  // FAST_LIBS_BOOST
