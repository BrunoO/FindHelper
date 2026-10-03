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

  for (size_t i = 0; i <= text.size() - 3; ++i) {
    const char c1 = string_search::ToLowerChar(static_cast<unsigned char>(text[i]));
    const char c2 = string_search::ToLowerChar(static_cast<unsigned char>(text[i + 1]));
    const char c3 = string_search::ToLowerChar(static_cast<unsigned char>(text[i + 2]));
    out.push_back(PackTrigram(c1, c2, c3));
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
