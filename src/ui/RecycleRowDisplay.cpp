/**
 * @file ui/RecycleRowDisplay.cpp
 * @brief Implementation of the Recycle Bin Name-column display decision.
 */

#include "ui/RecycleRowDisplay.h"

#include <cstdint>
#include <string>

#include "index/recycle/RecycleBinMetadataStore.h"
#include "path/PathUtils.h"
#include "search/SearchTypes.h"
#include "utils/FileSystemUtils.h"
#include "utils/FileTimeTypes.h"

namespace ui {

namespace {

/// The $I deletion stamp is a raw Win32 FILETIME (100-ns ticks since 1601); 0 means "not recorded".
constexpr std::uint64_t kUnknownDeletionTicks = 0;
/// Low 32-bit word mask for splitting the stored FILETIME into its portable halves.
constexpr std::uint32_t kLowWordMask = 0xFFFFFFFFU;

constexpr std::string_view kTooltipOriginalPrefix = "Original: ";
constexpr std::string_view kTooltipDeletedPrefix = "\nDeleted: ";
constexpr std::string_view kUnknownDeletionText = "(unknown)";

/**
 * Splits a stored raw FILETIME (100-ns ticks since 1601) into the low/high words that are the only
 * portable surface of the type on macOS. Same split as FileSystemUtils.cpp's TimespecToFileTime
 * and RawMftRecordParser; the byte order of the two words is what FormatFileTime consumes.
 */
[[nodiscard]] FILETIME MakeFileTimeFromTicks(std::uint64_t ticks) {
  FILETIME file_time = {};
  file_time.dwLowDateTime = static_cast<std::uint32_t>(ticks & kLowWordMask);
  file_time.dwHighDateTime = static_cast<std::uint32_t>(ticks >> 32U);
  return file_time;
}

/// Today's behaviour: the physical $R filename, no badge, no tooltip (and no tooltip allocation).
[[nodiscard]] RecycleRowDisplay PhysicalDisplay(const SearchResult& result) {
  return {result.GetFilename(), false, {}};
}

/// Deletion stamp line value: formatted FILETIME, or "(unknown)" when the record carried none.
[[nodiscard]] std::string BuildDeletionText(const recycle_bin::RecycleFileMetadata& metadata) {
  // Named local instead of a ternary: one branch returns a temporary std::string, so a
  // string_view would dangle (never create a view over a branch temporary).
  if (metadata.deletion_time_filetime == kUnknownDeletionTicks) {
    return std::string(kUnknownDeletionText);
  }
  return FormatFileTime(MakeFileTimeFromTicks(metadata.deletion_time_filetime));
}

}  // namespace

RecycleRowDisplay BuildRecycleRowDisplay(const SearchResult& result,
                                         const recycle_bin::RecycleMetadataSnapshot* snapshot,
                                         std::string_view physical_path) {
  // No snapshot (the UI wires the real one in a later task) or no metadata for this fileId: the
  // row renders exactly as it does today.
  if (snapshot == nullptr) {
    return PhysicalDisplay(result);
  }
  const auto metadata_it = snapshot->by_file_id.find(result.fileId);
  if (metadata_it == snapshot->by_file_id.end()) {
    return PhysicalDisplay(result);
  }

  // AC-3 guard: a fileId hit alone is not enough. Eviction/enrichment races can leave a stale
  // entry, and a recycled fileId reused by a live file would badge the live row. Only a path still
  // under "$Recycle.Bin" (the same helper the delete path uses) is confidently recycled.
  if (!path_utils::IsUnderRecycleBin(physical_path)) {
    return PhysicalDisplay(result);
  }

  const recycle_bin::RecycleFileMetadata& metadata = metadata_it->second;

  // The displayed name is the filename of original_path, not metadata.original_name: the
  // snapshot's filename/extension offsets are recomputed from original_path at publish, so taking
  // the name from the same string keeps the displayed extension consistent with them. If the two
  // ever disagree (a producer would have to set original_name independently), original_path's
  // filename wins.
  RecycleRowDisplay display{};
  display.display_name = path_utils::GetFilename(metadata.original_path);
  display.show_badge = true;

  const std::string deletion_text = BuildDeletionText(metadata);

  // Built only on the badge path; one allocation, sized up front.
  display.tooltip.reserve(kTooltipOriginalPrefix.size() + metadata.original_path.size() +
                          kTooltipDeletedPrefix.size() + deletion_text.size());
  display.tooltip.append(kTooltipOriginalPrefix);
  display.tooltip.append(metadata.original_path);
  display.tooltip.append(kTooltipDeletedPrefix);
  display.tooltip.append(deletion_text);
  return display;
}

}  // namespace ui
