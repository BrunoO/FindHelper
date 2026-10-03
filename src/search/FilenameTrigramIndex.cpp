#include "search/FilenameTrigramIndex.h"
#include <algorithm>
#include <cassert>

namespace filename_trigram {

void FilenameTrigramIndex::Build(const PathStorage::SoAView& soa_view, size_t storage_size) {
  posting_lists_.clear();
  // Typical filename trigram vocabulary is around 10k-30k unique trigrams.
  // Pre-reserving avoids multiple rehashes of the posting list map.
  posting_lists_.reserve(16384);

  std::vector<TrigramKey> scratch_trigrams;
  scratch_trigrams.reserve(64);

  for (size_t i = 0; i < soa_view.size; ++i) {
    if (soa_view.is_deleted[i] != 0) {
      continue;
    }

    const char* path = soa_view.path_storage + soa_view.path_offsets[i];
    const size_t filename_offset = soa_view.filename_start[i];
    const size_t path_len = soa_view.GetPathLength(i, storage_size);

    if (filename_offset <= path_len) {
      const std::string_view filename(path + filename_offset, path_len - filename_offset);
      ExtractTrigrams(filename, scratch_trigrams);
      const auto row_id = static_cast<uint32_t>(i);
      for (const TrigramKey trigram : scratch_trigrams) {
        posting_lists_[trigram].add(row_id);
      }
    }
  }

  // Optimize roaring bitmaps after bulk load
  for (auto& [key, bitmap] : posting_lists_) {
    bitmap.runOptimize();
  }
}

void FilenameTrigramIndex::Clear() noexcept {
  posting_lists_.clear();
}

void FilenameTrigramIndex::AddEntry(uint32_t row_id, std::string_view filename) {
  std::vector<TrigramKey> trigrams;
  ExtractTrigrams(filename, trigrams);
  for (const TrigramKey trigram : trigrams) {
    posting_lists_[trigram].add(row_id);
  }
}

void FilenameTrigramIndex::RemoveEntry(uint32_t row_id, std::string_view filename) {
  std::vector<TrigramKey> trigrams;
  ExtractTrigrams(filename, trigrams);
  for (const TrigramKey trigram : trigrams) {
    const auto it = posting_lists_.find(trigram);
    if (it != posting_lists_.end()) {
      it->second.remove(row_id);
    }
  }
}

std::optional<roaring::Roaring> FilenameTrigramIndex::QueryCandidates(
    const std::vector<TrigramKey>& required_trigrams) const {
  if (required_trigrams.empty()) {
    return std::nullopt;
  }

  std::vector<const roaring::Roaring*> bitmaps;
  bitmaps.reserve(required_trigrams.size());

  for (const TrigramKey trigram : required_trigrams) {
    const auto it = posting_lists_.find(trigram);
    if (it == posting_lists_.end() || it->second.isEmpty()) {
      return std::nullopt; // Early exit: intersection will be empty
    }
    bitmaps.push_back(&it->second);
  }

  // Sort bitmaps by cardinality for efficient intersection
  std::sort(bitmaps.begin(), bitmaps.end(),
            [](const roaring::Roaring* a, const roaring::Roaring* b) {
              return a->cardinality() < b->cardinality();
            });

  // Start with the smallest bitmap
  roaring::Roaring result = *bitmaps[0];

  for (size_t i = 1; i < bitmaps.size(); ++i) {
    result &= *bitmaps[i];
    if (result.isEmpty()) {
      break;
    }
  }

  return result;
}

size_t FilenameTrigramIndex::GetMemoryUsageBytes() const {
  size_t total = 0;
  for (const auto& [key, bitmap] : posting_lists_) {
    total += sizeof(TrigramKey) + sizeof(roaring::Roaring);
    total += bitmap.getSizeInBytes();
  }
  return total;
}

} // namespace filename_trigram
