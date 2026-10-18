#pragma once

#include "search/SearchPatternUtils.h"  // For string_search::ToLowerChar
#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace filename_trigram {

// 24-bit packed trigram key
using TrigramKey = uint32_t;

/**
 * Packs 3 lowercase ASCII characters into a 24-bit TrigramKey.
 */
inline TrigramKey PackTrigram(char c1, char c2, char c3) noexcept {
  return (static_cast<uint32_t>(static_cast<unsigned char>(c1)) << 16) |
         (static_cast<uint32_t>(static_cast<unsigned char>(c2)) << 8) |
         (static_cast<uint32_t>(static_cast<unsigned char>(c3)));
}

// Bitmask to retain the lowest 16 bits (two 8-bit characters) for rolling window shift.
constexpr uint32_t kLow16BitsMask = 0xFFFFU;

/**
 * Extracts unique trigrams from a given string into the provided destination vector.
 * Clears out while preserving its allocated capacity.
 * Converts characters to lowercase before packing.
 */
inline void ExtractTrigrams(std::string_view text, std::vector<TrigramKey>& out) {
  out.clear();
  if (text.size() < 3) {
    return;
  }

  if (out.capacity() < text.size() - 2) {
    out.reserve(text.size() - 2);
  }

  uint32_t key = (static_cast<uint32_t>(static_cast<unsigned char>(
                      string_search::ToLowerChar(text[0]))) << 8U) |
                 static_cast<uint32_t>(static_cast<unsigned char>(
                      string_search::ToLowerChar(text[1])));

  for (size_t i = 2; i < text.size(); ++i) {
    const auto c = static_cast<uint32_t>(static_cast<unsigned char>(
        string_search::ToLowerChar(text[i])));
    key = ((key & kLow16BitsMask) << 8U) | c;
    out.push_back(key);
  }

  // Deduplicate
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
}

/**
 * Extracts unique trigrams from a given string.
 * Converts characters to lowercase before packing.
 */
[[nodiscard]] inline std::vector<TrigramKey> ExtractTrigrams(std::string_view text) {
  std::vector<TrigramKey> trigrams;
  ExtractTrigrams(text, trigrams);
  return trigrams;
}

}  // namespace filename_trigram
