#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#ifdef FAST_LIBS_BOOST
#include <boost/sort/block_indirect_sort/block_indirect_sort.hpp>
#include <boost/sort/spreadsort/integer_sort.hpp>
#endif  // FAST_LIBS_BOOST

// Sort helpers with FAST_LIBS_BOOST feature flag support.
//   - SortLarge: general comparator sort for large (>10k) ranges.
//     boost::sort::block_indirect_sort (FAST_LIBS_BOOST=ON) is ~20-30% faster
//     than std::sort on large element types (e.g. SearchResult) thanks to
//     block-based moves with better cache locality. Unstable, like std::sort.
//   - SortUint64InPlace: in-place sort of raw uint64_t vectors.
//     boost::sort::spreadsort::integer_sort (FAST_LIBS_BOOST=ON) is O(N)
//     for fixed-width integers (e.g. CollectUniqueMissIds dedup).
// Small sorts (highlight spans, sibling lists, extension sets) intentionally
// stay on std::sort: Boost's setup overhead would dominate at small N.

template <typename RandomIt, typename Compare>
inline void SortLarge(RandomIt first, RandomIt last, Compare comp) {
#ifdef FAST_LIBS_BOOST
  boost::sort::block_indirect_sort(first, last, comp);
#else
  std::sort(first, last, comp);
#endif  // FAST_LIBS_BOOST
}

inline void SortUint64InPlace(std::vector<uint64_t>& values) {
#ifdef FAST_LIBS_BOOST
  boost::sort::spreadsort::integer_sort(values.begin(), values.end());
#else
  std::sort(values.begin(), values.end());
#endif  // FAST_LIBS_BOOST
}
