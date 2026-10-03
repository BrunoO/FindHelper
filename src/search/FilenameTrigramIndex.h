#pragma once

#include "path/PathStorage.h"
#include "search/FilenameTrigramExtractor.h"
#include <optional>
#include <roaring/roaring.hh>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace filename_trigram {

class FilenameTrigramIndex {
public:
  FilenameTrigramIndex() = default;

  /**
   * Build the index from a PathStorage SoAView.
   */
  void Build(const PathStorage::SoAView& soa_view, size_t storage_size);

  /**
   * Add a single entry to the index.
   */
  void AddEntry(uint32_t row_id, std::string_view filename);

  /**
   * Remove a single entry from the index.
   */
  void RemoveEntry(uint32_t row_id, std::string_view filename);

  /**
   * Clear all entries from the index.
   */
  void Clear() noexcept;

  /**
   * Query the index for candidates containing ALL specified trigrams.
   * Returns an empty optional if ANY trigram has 0 postings (early exit).
   * Returns an empty roaring bitmap if no intersection exists.
   */
  [[nodiscard]] std::optional<roaring::Roaring> QueryCandidates(
      const std::vector<TrigramKey>& required_trigrams) const;

  [[nodiscard]] size_t GetMemoryUsageBytes() const;

private:
  std::unordered_map<TrigramKey, roaring::Roaring> posting_lists_;
};

} // namespace filename_trigram
