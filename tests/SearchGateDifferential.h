#pragma once

#include "index/FileIndex.h"
#include "search/SearchPatternUtils.h"
#include "search/SearchTypes.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace gate_differential {

class ScanForcer {
public:
  explicit ScanForcer(FileIndex& file_index)
      : trigram_idx_(file_index.GetTrigramIndexForTest()),
        extension_idx_(file_index.GetExtensionIndexForTest()),
        trigram_gen_(trigram_idx_ != nullptr ? trigram_idx_->MarkBuilding() : 0),
        extension_gen_(extension_idx_ != nullptr ? extension_idx_->MarkBuilding() : 0) {}

  ~ScanForcer() {
    if (trigram_idx_ != nullptr) {
      trigram_idx_->AbortBuild(trigram_gen_);
    }
    if (extension_idx_ != nullptr) {
      extension_idx_->AbortBuild(extension_gen_);
    }
  }

  ScanForcer(const ScanForcer&) = delete;
  ScanForcer& operator=(const ScanForcer&) = delete;
  ScanForcer(ScanForcer&&) = delete;
  ScanForcer& operator=(ScanForcer&&) = delete;

private:
  filename_trigram::FilenameTrigramIndex* trigram_idx_ = nullptr;
  extension_index::ExtensionIndex* extension_idx_ = nullptr;
  uint64_t trigram_gen_ = 0;
  uint64_t extension_gen_ = 0;
};

struct BatteryQuery {
  std::string filename;
  std::string path;
  std::vector<std::string> extensions;
  bool folders_only = false;
  bool case_sensitive = false;
};

struct BatteryEntry {
  int id = 0;
  BatteryQuery query;
  std::string hazard_description;
  std::string must_include;
  bool expect_filter_path = true;
};

inline void PrintVerifyDiff(const FileIndex& file_index,
                            const std::vector<uint64_t>& normal_ids,
                            const std::vector<uint64_t>& forced_scan_ids,
                            std::ostream& os = std::cout) {
  std::vector<uint64_t> missing_ids;
  std::set_difference(forced_scan_ids.begin(), forced_scan_ids.end(),
                      normal_ids.begin(), normal_ids.end(),
                      std::back_inserter(missing_ids));

  std::vector<uint64_t> extra_ids;
  std::set_difference(normal_ids.begin(), normal_ids.end(),
                      forced_scan_ids.begin(), forced_scan_ids.end(),
                      std::back_inserter(extra_ids));

  const auto format_id_path = [&](uint64_t id) -> std::string {
    return file_index.GetPathAccessor().GetPathCopy(id);
  };

  constexpr size_t kMaxSamples = 5;
  if (!missing_ids.empty()) {
    os << "  Missing in normal path (" << missing_ids.size()
       << " false negatives):\n";
    const size_t print_count = (std::min)(missing_ids.size(), kMaxSamples);
    for (size_t i = 0; i < print_count; ++i) {
      os << "    ID " << missing_ids[i] << ": \""
         << format_id_path(missing_ids[i]) << "\"\n";
    }
    if (missing_ids.size() > print_count) {
      os << "    ... and " << (missing_ids.size() - print_count) << " more\n";
    }
  }
  if (!extra_ids.empty()) {
    os << "  Extra in normal path (" << extra_ids.size()
       << " false positives):\n";
    const size_t print_count = (std::min)(extra_ids.size(), kMaxSamples);
    for (size_t i = 0; i < print_count; ++i) {
      os << "    ID " << extra_ids[i] << ": \""
         << format_id_path(extra_ids[i]) << "\"\n";
    }
    if (extra_ids.size() > print_count) {
      os << "    ... and " << (extra_ids.size() - print_count) << " more\n";
    }
  }
}

inline std::vector<uint64_t> RunSearch(FileIndex& file_index,
                                       const BatteryQuery& query,
                                       int thread_count,
                                       std::vector<ThreadTiming>* timings_out = nullptr) {
  SearchStats stats;
  std::vector<ThreadTiming> local_timings;
  std::vector<ThreadTiming>* const timings_target =
      timings_out != nullptr ? timings_out : &local_timings;

  const std::vector<std::string>* const extensions_ptr =
      query.extensions.empty() ? nullptr : &query.extensions;

  auto futures = file_index.SearchAsyncWithData(
      query.filename, thread_count, &stats, query.path, extensions_ptr,
      query.folders_only ? ItemTypeFilter::FoldersOnly : ItemTypeFilter::All,
      query.case_sensitive, timings_target, nullptr, nullptr);

  std::vector<uint64_t> ids;
  for (auto& fut : futures) {
    SearchResultBatch batch = fut.get();
    for (const SearchResultHit& hit : batch.hits) {
      ids.push_back(hit.id);
    }
  }

  std::sort(ids.begin(), ids.end());
  ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
  return ids;
}

[[nodiscard]] inline bool WaitForIndexesReady(FileIndex& file_index, int timeout_ms = 30000) {
  constexpr int kPollIntervalMs = 50;
  for (int waited_ms = 0; waited_ms < timeout_ms; waited_ms += kPollIntervalMs) {
    {
      const std::shared_lock lock(file_index.GetMutex());
      const auto* const trigram_idx = file_index.GetTrigramIndex();
      const auto* const extension_idx = file_index.GetExtensionIndex();
      if (((trigram_idx == nullptr) || trigram_idx->IsReady()) &&
          ((extension_idx == nullptr) || extension_idx->IsReady())) {
        return true;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
  }
  return false;
}

[[nodiscard]] inline bool DifferentialCheck(FileIndex& file_index,
                                            const BatteryEntry& entry,
                                            std::ostream& os = std::cerr) {
  // Path A: Normal accelerated path
  std::vector<ThreadTiming> timings;
  const std::vector<uint64_t> normal_ids =
      RunSearch(file_index, entry.query, 2, &timings);

  if (const bool normal_took_filter = (timings.size() == 1);
      entry.expect_filter_path && !normal_took_filter) {
    os << "Battery entry #" << entry.id << " (" << entry.hazard_description
       << "): query took scan path; gate was expected to be exercised!\n";
    return false;
  }

  // Path B: Forced scan path with matcher prefilter bypassed
  std::vector<uint64_t> forced_scan_ids;
  {
    search_pattern_utils::matcher_test_hooks::ScopedSkipLiteralPrefilter bypass;
    ScanForcer forcer(file_index);
    forced_scan_ids = RunSearch(file_index, entry.query, 2, nullptr);
  }

  // Witness verification (if declared)
  if (!entry.must_include.empty()) {
    const PathStorage::SoAView soa = file_index.GetSearchableView();
    const size_t storage_size = file_index.GetStorageSize();
    uint64_t witness_id = UINT64_MAX;

    for (size_t id = 0; id < soa.size; ++id) {
      if (soa.path_offsets != nullptr && soa.path_storage != nullptr) {
        const size_t len = soa.GetPathLength(id, storage_size);
        const std::string_view path(soa.path_storage + soa.path_offsets[id], len);
        if (path.find(entry.must_include) != std::string_view::npos) {
          witness_id = soa.path_ids[id];
          break;
        }
      }
    }

    if (witness_id != UINT64_MAX) {
      const bool witness_in_scan =
          std::binary_search(forced_scan_ids.begin(), forced_scan_ids.end(), witness_id);
      if (!witness_in_scan) {
        os << "Battery entry #" << entry.id << " (" << entry.hazard_description
           << "): witness row \"" << entry.must_include
           << "\" was not matched by forced scan oracle!\n";
        return false;
      }
      const bool witness_in_normal =
          std::binary_search(normal_ids.begin(), normal_ids.end(), witness_id);
      if (!witness_in_normal) {
        os << "Battery entry #" << entry.id << " (" << entry.hazard_description
           << "): witness row \"" << entry.must_include
           << "\" was dropped by accelerated normal path!\n";
        return false;
      }
    }
  }

  if (normal_ids != forced_scan_ids) {
    os << "Battery entry #" << entry.id << " (" << entry.hazard_description
       << ") MISMATCH: normal " << normal_ids.size()
       << " vs forced scan " << forced_scan_ids.size() << "\n";
    PrintVerifyDiff(file_index, normal_ids, forced_scan_ids, os);
    return false;
  }

  return true;
}

}  // namespace gate_differential
