#pragma once

#include "path/PathStorage.h"
#include "search/ExtensionIndex.h"
#include <cstddef>
#include <cstdint>
#include <vector>

/**
 * Single-pass SoA extraction producing both trigram and extension postings.
 *
 * This is the ONLY SoA traversal per rebuild: wired call sites
 * (maintenance compaction, full recompute) call this once under the index
 * lock and hand each half to its index's MarkBuilding +
 * BeginRebuild(pre-extracted) + SubmitBuild sequence. The trigram half is
 * byte-identical by construction to FilenameTrigramIndex::ExtractPostings
 * (same slice, same packing, same push order) — which is left untouched;
 * filename_trigram_index_tests plus the differential test below guard
 * against drift.
 */
struct CombinedIndexPostings {
  // packed (key << 32 | row_id), trigram format
  std::vector<uint64_t> trigram_postings{};  // NOLINT(readability-redundant-member-init) - Explicit initialization for member init check
  // grouped, one string per distinct ext
  std::vector<extension_index::ExtensionGroup> extension_groups{};  // NOLINT(readability-redundant-member-init) - Explicit initialization for member init check
};

[[nodiscard]] CombinedIndexPostings ExtractCombinedPostings(
    const PathStorage::SoAView& soa_view, size_t storage_size);
