#include <doctest/doctest.h>

#include "index/FileIndex.h"
#include "search/SearchResultUtils.h"
#include "utils/FileAttributeConstants.h"
#include "utils/FileTimeTypes.h"

namespace {

[[nodiscard]] uint64_t FindEntryIdByPathSuffix(const FileIndex& index, std::string_view path_suffix) {
  uint64_t found_id = 0;
  index.ForEachEntryWithPath([&found_id, path_suffix](uint64_t id, const FileEntry& /*entry*/,
                                                      std::string_view path) {
    if (path.size() >= path_suffix.size() &&
        path.substr(path.size() - path_suffix.size()) == path_suffix) {
      found_id = id;
      return false;
    }
    return true;
  });
  return found_id;
}

SearchResultData MakeResultData(uint64_t id, std::string_view path, bool is_directory) {
  SearchResultData datum;
  datum.id = id;
  datum.fullPath.assign(path);
  datum.isDirectory = is_directory;
  if (const size_t slash = path.find_last_of("/\\"); slash != std::string_view::npos) {
    datum.filename_start = slash + 1;
  }
  if (const size_t dot = path.find_last_of('.'); dot != std::string_view::npos && dot > datum.filename_start) {
    datum.extension_start = dot + 1;
  }
  return datum;
}

}  // namespace

TEST_CASE("FileIndex::FillCachedAttributeSnapshots - batch copies cached attrs") {
  FileIndex index;
  index.InsertPath("/root/batch_test/readme.txt", false);
  index.RecomputeAllPaths();

  const uint64_t file_id = FindEntryIdByPathSuffix(index, "readme.txt");
  REQUIRE(file_id != 0);

  constexpr uint64_t k_expected_size = 8192;
  FILETIME expected_time = kFileTimeNotLoaded;
  expected_time.dwLowDateTime = 4242;
  expected_time.dwHighDateTime = 9999;
  index.UpdateFileSizeById(file_id, k_expected_size);
  index.UpdateModificationTime(file_id, expected_time);

  const std::vector<uint64_t> ids{file_id, 999999};
  std::vector<std::optional<FileIndex::EntryAttributeSnapshot>> snapshots(2);
  index.FillCachedAttributeSnapshots(ids, snapshots);

  REQUIRE(snapshots[0].has_value());
  CHECK(snapshots[0]->file_size == k_expected_size);
  CHECK(snapshots[0]->last_modification_time.dwLowDateTime == expected_time.dwLowDateTime);
  CHECK(snapshots[0]->last_modification_time.dwHighDateTime == expected_time.dwHighDateTime);
  CHECK_FALSE(snapshots[1].has_value());
}

TEST_CASE("MergeAndConvertToSearchResults - applies batched cached attributes") {
  FileIndex index;
  index.InsertPath("/root/merge_test/data.bin", false);
  index.InsertPath("/root/merge_test/subdir", true);
  index.RecomputeAllPaths();

  const uint64_t file_id = FindEntryIdByPathSuffix(index, "data.bin");
  const uint64_t dir_id = FindEntryIdByPathSuffix(index, "subdir");
  REQUIRE(file_id != 0);
  REQUIRE(dir_id != 0);

  constexpr uint64_t k_expected_size = 2048;
  index.UpdateFileSizeById(file_id, k_expected_size);

  const std::string file_path = "/root/merge_test/data.bin";
  const std::string dir_path = "/root/merge_test/subdir";
  const std::vector<SearchResultData> data{
      MakeResultData(file_id, file_path, false),
      MakeResultData(dir_id, dir_path, true),
      MakeResultData(888888, "/missing/file.txt", false),
  };

  std::vector<char> pool;
  const std::vector<SearchResult> results =
      MergeAndConvertToSearchResults(pool, data, index);

  REQUIRE(results.size() == 3);
  CHECK(results[0].fileSize == k_expected_size);
  CHECK(results[1].fileSize == kFileSizeNotLoaded);
  CHECK(results[1].folderFileCount == kFolderFileCountNotLoaded);
  CHECK(results[2].fileSize == kFileSizeNotLoaded);
  CHECK(results[0].fullPath == file_path);
}

TEST_CASE("FileIndex::InvalidateSize - resets cached size to not-loaded sentinel") {
  FileIndex index;
  index.InsertPath("/root/invalidate_size/file.dat", false);
  index.InsertPath("/root/invalidate_size/folder", true);
  index.RecomputeAllPaths();

  const uint64_t file_id = FindEntryIdByPathSuffix(index, "file.dat");
  const uint64_t dir_id = FindEntryIdByPathSuffix(index, "folder");
  REQUIRE(file_id != 0);
  REQUIRE(dir_id != 0);

  constexpr uint64_t k_cached_size = 4096;
  index.UpdateFileSizeById(file_id, k_cached_size);
  {
    const auto attrs = index.TryGetCachedAttributes(file_id);
    REQUIRE(attrs.has_value());
    CHECK(attrs->file_size == k_cached_size);
  }

  // DATA_* path: invalidate only — must not require filesystem presence.
  CHECK(index.InvalidateSize(file_id));
  {
    const auto attrs = index.TryGetCachedAttributes(file_id);
    REQUIRE(attrs.has_value());
    CHECK(attrs->file_size == kFileSizeNotLoaded);
  }

  // Directories and missing IDs cannot be invalidated.
  CHECK_FALSE(index.InvalidateSize(dir_id));
  CHECK_FALSE(index.InvalidateSize(999999));
}
