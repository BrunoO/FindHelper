#include "search/CombinedIndexPostings.h"
#include "search/FilenameTrigramPostings.h"
#include "utils/HashMapAliases.h"
#include "utils/Logger.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace {

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
    // Shared packing (tech-debt T3): byte-identical to the filename-trigram
    // index's ExtractPostings.
    filename_trigram::AppendFilenameTrigramPostings(
        row_base + filename_offset, path_len - filename_offset, row_id,
        ctx.out->trigram_postings);
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

  // Exact upper bound of trigram postings, shared with the filename-trigram
  // index (tech-debt T3): sum over live rows of max(0, L - 2).
  const size_t max_postings =
      filename_trigram::ComputeMaxTrigramPostings(soa_view, storage_size);

  if (max_postings > filename_trigram::kMaxPostingReservation) {
    LOG_IMPORTANT_BUILD("CombinedIndexPostings::Extract: clamping trigram reserve from "
                        << max_postings << " to " << filename_trigram::kMaxPostingReservation
                        << " postings");
  }

  const size_t reserved_postings =
      (std::min)(max_postings, filename_trigram::kMaxPostingReservation);
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
