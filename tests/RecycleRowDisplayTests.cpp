#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

/**
 * @file RecycleRowDisplayTests.cpp
 * @brief Unit tests for ui::BuildRecycleRowDisplay (spec section 5 Phase 4 item 3, AC-3 guard).
 *
 * The helper is pure (no imgui, no GuiState), so these cases pin every branch on macOS: today's
 * no-snapshot rendering, the fail-safe branches, the recycled row's original name/badge/tooltip,
 * and the documented original_path-vs-original_name precedence.
 */

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "doctest/doctest.h"
#include "index/recycle/RecycleBinMetadataStore.h"
#include "path/PathUtils.h"
#include "search/SearchTypes.h"
#include "ui/RecycleRowDisplay.h"

namespace {

using recycle_bin::RecycleFileMetadata;
using recycle_bin::RecycleMetadataSnapshot;

// INV-4: this feature adds no field to SearchResult; the header's layout bound must keep holding.
static_assert(sizeof(SearchResult) <= 208, "INV-4: SearchResult layout bound");

/// Physical $R payload path, the row shape the merge step produces.
constexpr std::string_view kPhysicalBinPath = "C:\\$Recycle.Bin\\S-1-5-21-11\\$R1A2B3C.txt";
/// Live path used by the AC-3 guard cases (no "$Recycle.Bin" anywhere).
constexpr std::string_view kLivePath = "C:\\Users\\bob\\notes.txt";
/// A fileId that no case publishes, so the lookup misses.
constexpr std::uint64_t kUnpublishedFileId = 999;

/// Raw $I deletion stamp (100-ns ticks since 1601) that lands in mid-2023 in every timezone.
constexpr std::uint64_t kDeletionTicks2023 = 133333333333333333ULL;

/// One results row: physical path plus the offsets a main-scan hit carries.
[[nodiscard]] SearchResult MakeResult(std::string_view physical_path, std::uint64_t file_id,
                                      bool is_directory = false) {
  SearchResult result{};
  result.fullPath = physical_path;
  const std::string_view filename = path_utils::GetFilename(physical_path);
  result.filename_offset = static_cast<size_t>(filename.data() - physical_path.data());
  const std::string_view extension = path_utils::GetExtension(physical_path);
  result.extension_offset = extension.empty()
                                ? std::string_view::npos
                                : static_cast<size_t>(extension.data() - physical_path.data());
  result.fileId = file_id;
  result.isDirectory = is_directory;
  return result;
}

/// One published snapshot holding a single fileId -> metadata entry.
[[nodiscard]] std::shared_ptr<const RecycleMetadataSnapshot> MakeSnapshot(
    std::uint64_t file_id, std::string original_path, std::uint64_t deletion_ticks,
    std::string original_name = {}) {
  auto snapshot = std::make_shared<RecycleMetadataSnapshot>();
  RecycleFileMetadata metadata;
  metadata.original_path = std::move(original_path);
  metadata.original_name = original_name.empty()
                               ? std::string(path_utils::GetFilename(metadata.original_path))
                               : std::move(original_name);
  metadata.deletion_time_filetime = deletion_ticks;
  snapshot->generation = 1;
  snapshot->by_file_id.emplace(file_id, std::move(metadata));
  return snapshot;
}

}  // namespace

TEST_CASE("BuildRecycleRowDisplay - null snapshot keeps today's physical name") {
  const std::string physical(kPhysicalBinPath);
  const SearchResult result = MakeResult(physical, 100);

  const ui::RecycleRowDisplay display =
      ui::BuildRecycleRowDisplay(result, nullptr, result.fullPath);

  // SearchResult::GetFilename() is the stem; the Name column recomposes "<stem>.<ext>" so the
  // drawn physical label is byte-for-byte what it was before this feature.
  CHECK(display.display_name == "$R1A2B3C");
  CHECK(display.display_name == result.GetFilename());
  CHECK_FALSE(display.show_badge);
  CHECK(display.tooltip.empty());
}

TEST_CASE("BuildRecycleRowDisplay - fileId absent from the snapshot keeps the physical name") {
  const std::string physical(kPhysicalBinPath);
  const SearchResult result = MakeResult(physical, 100);
  const auto snapshot = MakeSnapshot(kUnpublishedFileId, "C:\\Users\\bob\\elsewhere.txt", 0);

  const ui::RecycleRowDisplay display =
      ui::BuildRecycleRowDisplay(result, snapshot.get(), result.fullPath);

  CHECK(display.display_name == "$R1A2B3C");
  CHECK_FALSE(display.show_badge);
  CHECK(display.tooltip.empty());
}

TEST_CASE("BuildRecycleRowDisplay - AC-3 guard: a live path is never badged") {
  const std::string physical(kLivePath);
  const SearchResult result = MakeResult(physical, 100);
  // The store still knows the fileId (stale entry from before a restore/evict, or a fileId reused
  // by a live file): the guard, not the lookup, must decide.
  const auto snapshot = MakeSnapshot(100, std::string(kLivePath), kDeletionTicks2023);

  const ui::RecycleRowDisplay display =
      ui::BuildRecycleRowDisplay(result, snapshot.get(), result.fullPath);

  CHECK(display.display_name == "notes");
  CHECK_FALSE(display.show_badge);
  CHECK(display.tooltip.empty());
}

TEST_CASE("BuildRecycleRowDisplay - id present and path under the bin shows name, badge, tooltip") {
  const std::string physical(kPhysicalBinPath);
  const SearchResult result = MakeResult(physical, 100);
  const auto snapshot = MakeSnapshot(100, std::string(kLivePath), kDeletionTicks2023);

  const ui::RecycleRowDisplay display =
      ui::BuildRecycleRowDisplay(result, snapshot.get(), result.fullPath);

  // Original filename including its extension, taken from original_path.
  CHECK(display.display_name == "notes.txt");
  CHECK(display.show_badge);

  // Two lines: original location, then the deletion stamp.
  const std::string expected_prefix = "Original: C:\\Users\\bob\\notes.txt\nDeleted: ";
  CHECK(display.tooltip.rfind(expected_prefix, 0) == 0);
  const std::string stamp = display.tooltip.substr(expected_prefix.size());
  CHECK(stamp.size() == 16);  // "YYYY-MM-DD HH:MM"
  CHECK(stamp.substr(0, 4) == "2023");
  CHECK(stamp != "(unknown)");
}

TEST_CASE("BuildRecycleRowDisplay - bin match is case-insensitive") {
  const std::string physical("C:\\$RECYCLE.BIN\\S-1-5-21-11\\$R1A2B3C.txt");
  const SearchResult result = MakeResult(physical, 100);
  const auto snapshot = MakeSnapshot(100, std::string(kLivePath), kDeletionTicks2023);

  const ui::RecycleRowDisplay display =
      ui::BuildRecycleRowDisplay(result, snapshot.get(), result.fullPath);

  CHECK(display.show_badge);
  CHECK(display.display_name == "notes.txt");
}

TEST_CASE("BuildRecycleRowDisplay - no deletion stamp reports (unknown)") {
  const std::string physical(kPhysicalBinPath);
  const SearchResult result = MakeResult(physical, 100);
  const auto snapshot = MakeSnapshot(100, std::string(kLivePath), /*deletion_ticks=*/0);

  const ui::RecycleRowDisplay display =
      ui::BuildRecycleRowDisplay(result, snapshot.get(), result.fullPath);

  CHECK(display.show_badge);
  CHECK(display.display_name == "notes.txt");
  CHECK(display.tooltip == std::string("Original: C:\\Users\\bob\\notes.txt\nDeleted: (unknown)"));
}

TEST_CASE("BuildRecycleRowDisplay - a recycled directory row behaves the same") {
  const std::string physical("C:\\$Recycle.Bin\\S-1-5-21-11\\$R1A2B3C");
  const SearchResult result = MakeResult(physical, 100, /*is_directory=*/true);
  const auto snapshot = MakeSnapshot(100, "C:\\Users\\bob\\old_project", kDeletionTicks2023);

  const ui::RecycleRowDisplay display =
      ui::BuildRecycleRowDisplay(result, snapshot.get(), result.fullPath);

  CHECK(display.display_name == "old_project");
  CHECK(display.show_badge);
  CHECK(display.tooltip.rfind("Original: C:\\Users\\bob\\old_project\nDeleted: ", 0) == 0);
}

TEST_CASE("BuildRecycleRowDisplay - original_path supplies the displayed filename") {
  const std::string physical(kPhysicalBinPath);
  const SearchResult result = MakeResult(physical, 100);
  // Producers may leave original_name stale/independent; original_path is the documented source
  // because the snapshot's name/extension offsets are recomputed from it.
  const auto snapshot =
      MakeSnapshot(100, "C:\\Users\\bob\\fresh_name.txt", kDeletionTicks2023, "stale_name.dat");

  const ui::RecycleRowDisplay display =
      ui::BuildRecycleRowDisplay(result, snapshot.get(), result.fullPath);

  CHECK(display.display_name == "fresh_name.txt");
  CHECK(display.show_badge);
  CHECK(display.tooltip.rfind("Original: C:\\Users\\bob\\fresh_name.txt\nDeleted: ", 0) == 0);
}
