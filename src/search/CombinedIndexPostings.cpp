#include "search/CombinedIndexPostings.h"
#include "search/FilenameTrigramExtractor.h"
#include "utils/HashMapAliases.h"
#include "utils/Logger.h"
#include "utils/StringSearch.h"
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace {

// Must match kMaxPostingReservation in FilenameTrigramIndex.cpp:
// allocation-hint-only cap for the trigram postings reservation (32M
// postings ≈ 256 MB transient). Kept in sync by inspection; the
// differential test pins behavior, not the constant.
constexpr size_t kCombinedMaxPostingReservation = 32'000'000;

// Per-row extraction context: outputs plus the extension interning map and
// its scratch buffer. Keeps ExtractCombinedPostings under the cognitive
// complexity threshold by moving the row body out of the loop.
struct RowExtractionContext {
  CombinedIndexPostings* out = nullptr;  // NOLINT(readability-identifier-naming) - plain data fields
  flat_hash_map_t<std::string, size_t>* index_by_extension = nullptr;  // NOLINT(readability-identifier-naming) - plain data fields
  std::string* scratch = nullptr;  // NOLINT(readability-identifier-naming) - plain data fields
};

// Appends one live row's postings to both halves. Trigram half is identical
// to FilenameTrigramIndex::ExtractPostings; extension half interns the
// normalized extension (one owning string per distinct extension).
void AppendRowPostings(const PathStorage::SoAView& soa_view, size_t storage_size, size_t i,
                       const RowExtractionContext& ctx) {
  // Computed once, shared by both halves.
  const size_t path_len = soa_view.GetPathLength(i, storage_size);
  const char* const row_base = soa_view.path_storage + soa_view.path_offsets[i];
  const auto row_id = static_cast<uint32_t>(i);

  if (const size_t filename_offset = soa_view.filename_start[i];
      filename_offset + 3 <= path_len) {
    const size_t fn_len = path_len - filename_offset;
    const char* const fn = row_base + filename_offset;
    uint32_t key = (static_cast<uint32_t>(static_cast<unsigned char>(
                       string_search::ToLowerChar(fn[0]))) << 8U) |
                   static_cast<uint32_t>(static_cast<unsigned char>(
                       string_search::ToLowerChar(fn[1])));
    for (size_t j = 2; j < fn_len; ++j) {
      const auto c = static_cast<uint32_t>(static_cast<unsigned char>(
          string_search::ToLowerChar(fn[j])));
      key = ((key & filename_trigram::kLow16BitsMask) << 8U) | c;
      ctx.out->trigram_postings.push_back((static_cast<uint64_t>(key) << 32U) | row_id);
    }
  }

  const size_t ext_offset = soa_view.extension_start[i];
  if (ext_offset == SIZE_MAX || ext_offset >= path_len) {
    return;
  }
  extension_index::ExtensionIndex::NormalizeInto(
      {row_base + ext_offset, path_len - ext_offset}, *ctx.scratch);
  if (ctx.scratch->empty()) {
    return;
  }
  if (const auto it = ctx.index_by_extension->find(*ctx.scratch);
      it != ctx.index_by_extension->end()) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - it->second is a group index minted by this loop (size at push time), always < size
    ctx.out->extension_groups[it->second].row_ids.push_back(row_id);
    return;
  }
  const size_t group_index = ctx.out->extension_groups.size();
  ctx.out->extension_groups.push_back({*ctx.scratch, {row_id}});
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - group_index == size() before the push_back above
  ctx.index_by_extension->try_emplace(ctx.out->extension_groups[group_index].extension,
                                      group_index);
}

}  // namespace

CombinedIndexPostings ExtractCombinedPostings(
    const PathStorage::SoAView& soa_view, size_t storage_size) {
  CombinedIndexPostings out;
  if (soa_view.size == 0) {
    return out;
  }

  const auto extract_start = std::chrono::steady_clock::now();

  // Pre-pass (trigram half, copied from FilenameTrigramIndex::ExtractPostings):
  // exact upper bound of unique trigrams = sum over rows of max(0, L - 2).
  size_t max_postings = 0;
  for (size_t i = 0; i < soa_view.size; ++i) {
    if (soa_view.is_deleted[i] != 0) {
      continue;
    }
    const size_t fn_start = soa_view.filename_start[i];
    if (const size_t path_len = soa_view.GetPathLength(i, storage_size);
        fn_start + 3 <= path_len) {
      max_postings += (path_len - fn_start - 2);
    }
  }

  if (max_postings > kCombinedMaxPostingReservation) {
    LOG_IMPORTANT_BUILD("CombinedIndexPostings::Extract: clamping trigram reserve from "
                        << max_postings << " to " << kCombinedMaxPostingReservation << " postings");
  }

  const size_t reserved_postings = (std::min)(max_postings, kCombinedMaxPostingReservation);
  out.trigram_postings.reserve(reserved_postings);

  // Extension interning map: extension -> group index. One owning string
  // per distinct extension; per-row records are plain row-id appends.
  // Reserved upfront: distinct extensions number in the low thousands, so
  // this avoids every rehash during the pass (rehashing mid-extraction
  // showed up directly in extract timings).
  flat_hash_map_t<std::string, size_t> index_by_extension;
  index_by_extension.reserve(4096);
  std::string scratch;
  const RowExtractionContext row_ctx{&out, &index_by_extension, &scratch};

  for (size_t i = 0; i < soa_view.size; ++i) {
    if (soa_view.is_deleted[i] != 0) {
      continue;
    }
    AppendRowPostings(soa_view, storage_size, i, row_ctx);
  }

  const auto extract_microseconds = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - extract_start)
          .count());

  size_t extension_rows = 0;
  for (const auto& group : out.extension_groups) {
    extension_rows += group.row_ids.size();
  }

  LogBenchmarkDuration("CombinedIndexPostings extraction", extract_microseconds);
  LOG_IMPORTANT_BUILD("CombinedIndexPostings::Extract: trigram_postings="
                      << out.trigram_postings.size() << " ("
                      << (out.trigram_postings.size() * sizeof(uint64_t) / logger_constants::kBytesPerMB)
                      << " MB transient), extension_groups=" << out.extension_groups.size()
                      << ", extension_rows=" << extension_rows);

  return out;
}
