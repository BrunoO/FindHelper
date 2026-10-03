#include <doctest/doctest.h>

#include <cassert>

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

void AppendTestHit(SearchResultBatch& batch, uint64_t id, std::string_view path, bool is_directory) {
  size_t filename_start = 0;
  size_t extension_start = SIZE_MAX;
  if (const size_t slash = path.find_last_of("/\\"); slash != std::string_view::npos) {
    filename_start = slash + 1;
  }
  if (const size_t dot = path.find_last_of('.'); dot != std::string_view::npos && dot > filename_start) {
    extension_start = dot + 1;
  }
  batch.AppendHit(id, path.data(), path.size(), is_directory, filename_start, extension_start);
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
  // Split across two batches: offsets must stay valid per batch, and the
  // merge must concatenate all batches into one pool.
  SearchResultBatch first_batch;
  AppendTestHit(first_batch, file_id, file_path, false);
  AppendTestHit(first_batch, dir_id, dir_path, true);
  SearchResultBatch second_batch;
  AppendTestHit(second_batch, 888888, "/missing/file.txt", false);
  const std::vector<SearchResultBatch> batches{std::move(first_batch), std::move(second_batch)};

  std::vector<char> pool;
  const std::vector<SearchResult> results =
      MergeAndConvertToSearchResults(pool, batches, index);

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

TEST_CASE("AppendBatchToResultData - batch converts to owned rows identically") {
  // Empty batch converts to nothing (no views formed, no dereference).
  {
    std::vector<SearchResultData> rows;
    AppendBatchToResultData(rows, SearchResultBatch{});
    CHECK(rows.empty());
  }

  // Multiple hits incl. non-ASCII bytes and a directory: offsets must survive
  // arena growth (reallocation) and convert to byte-identical owned strings.
  SearchResultBatch batch;
  const std::string ascii_path = "/root/batch/a.txt";
  const std::string dir_path = "/root/batch/subdir";
  const std::string utf8_path = "/root/donn\xC3\xA9" "es/file.txt";
  batch.AppendHit(7, ascii_path.data(), ascii_path.size(), false, 12, 14);
  batch.AppendHit(8, dir_path.data(), dir_path.size(), true, 12, SIZE_MAX);
  batch.AppendHit(9, utf8_path.data(), utf8_path.size(), false, 15, 20);
  REQUIRE(batch.Size() == 3);

  std::vector<SearchResultData> rows;
  AppendBatchToResultData(rows, batch);
  REQUIRE(rows.size() == 3);
  CHECK(rows[0].id == 7);
  CHECK(rows[0].fullPath == ascii_path);
  CHECK_FALSE(rows[0].isDirectory);
  CHECK(rows[0].filename_start == 12);
  CHECK(rows[0].extension_start == 14);
  CHECK(rows[1].id == 8);
  CHECK(rows[1].fullPath == dir_path);
  CHECK(rows[1].isDirectory);
  CHECK(rows[2].id == 9);
  CHECK(rows[2].fullPath == utf8_path);
  CHECK(rows[2].filename_start == 15);
  CHECK(rows[2].extension_start == 20);

  // Converted rows own their bytes: destroying the batch must not affect them.
  batch.Clear();
  CHECK(rows[0].fullPath == ascii_path);
  CHECK(rows[2].fullPath == utf8_path);
}

TEST_CASE("MergeAndConvertToSearchResults - skips awaiting placeholders") {
  FileIndex index;
  index.Insert(ntfs_file_reference::NtfsFileReference(1), ntfs_file_reference::NtfsFileReference(0), "root", true);

  // A resolved control entry (synthetic crawl path; no recompute — that
  // would ClearAwaiting and disarm the quarantine under test).
  index.InsertPath("/root/merge_test/visible.txt", false);
  const uint64_t visible_id = FindEntryIdByPathSuffix(index, "visible.txt");
  REQUIRE(visible_id != 0);

  // Live-USN shape, inserted last: child CREATE before its parent →
  // bare-name placeholder tracked awaiting (same path IndexOperations::Insert
  // takes on macOS too).
  constexpr uint64_t kMissingParent = 0x00010000000000A0ULL;
  constexpr uint64_t kChild = 0x00010000000000B0ULL;
  index.Insert(ntfs_file_reference::NtfsFileReference(kChild), ntfs_file_reference::NtfsFileReference(kMissingParent), "late.txt", false);
  REQUIRE(index.GetAwaitingStats().count == 1);

  // Workers scanned the bare placeholder bytes before the parent arrived.
  SearchResultBatch batch;
  AppendTestHit(batch, kChild, "late.txt", false);
  AppendTestHit(batch, visible_id, "/root/merge_test/visible.txt", false);
  const std::vector<SearchResultBatch> batches{std::move(batch)};

  std::vector<char> pool;
  const std::vector<SearchResult> results =
      MergeAndConvertToSearchResults(pool, batches, index);

  // Quarantine: the awaiting placeholder never reaches results or the pool.
  REQUIRE(results.size() == 1);
  CHECK(results[0].fileId == visible_id);
  CHECK(results[0].fullPath == "/root/merge_test/visible.txt");
  CHECK(index.CheckBareNameInvariant());
}

TEST_CASE("MergeAndConvertToSearchResults - shows entries once healed") {
  FileIndex index;
  index.Insert(ntfs_file_reference::NtfsFileReference(1), ntfs_file_reference::NtfsFileReference(0), "root", true);

  constexpr uint64_t kMissingParent = 0x00010000000000A0ULL;
  constexpr uint64_t kChild = 0x00010000000000B0ULL;
  index.Insert(ntfs_file_reference::NtfsFileReference(kChild), ntfs_file_reference::NtfsFileReference(kMissingParent), "late.txt", false);
  REQUIRE(index.GetAwaitingStats().count == 1);

  // Parent arrives: heal resolves the placeholder to a full path.
  index.Insert(ntfs_file_reference::NtfsFileReference(kMissingParent), ntfs_file_reference::NtfsFileReference(1), "LateDir", true);
  REQUIRE(index.GetAwaitingStats().count == 0);

  // Post-heal scan carries the resolved path → displayed normally. The path
  // is read from the index (not hardcoded) so the test holds on every
  // platform's volume-root convention.
  const std::string healed_path =
      index.GetPathAccessor().GetPathCopy(kChild);
  REQUIRE(!healed_path.empty());
  REQUIRE(healed_path.find("late.txt") != std::string::npos);
  SearchResultBatch batch;
  AppendTestHit(batch, kChild, healed_path, false);
  const std::vector<SearchResultBatch> batches{std::move(batch)};

  std::vector<char> pool;
  const std::vector<SearchResult> results =
      MergeAndConvertToSearchResults(pool, batches, index);

  REQUIRE(results.size() == 1);
  CHECK(results[0].fileId == kChild);
  CHECK(results[0].fullPath == healed_path);
  CHECK(index.CheckBareNameInvariant());
}

TEST_CASE("MergeAndConvertToSearchResults - reuses scratch buffers across calls") {
  FileIndex index;
  index.InsertPath("/root/scratch_test/file1.txt", false);
  index.InsertPath("/root/scratch_test/file2.txt", false);
  index.InsertPath("/root/scratch_test/file3.txt", false);
  index.RecomputeAllPaths();

  const uint64_t id1 = FindEntryIdByPathSuffix(index, "file1.txt");
  const uint64_t id2 = FindEntryIdByPathSuffix(index, "file2.txt");
  const uint64_t id3 = FindEntryIdByPathSuffix(index, "file3.txt");
  REQUIRE(id1 != 0);
  REQUIRE(id2 != 0);
  REQUIRE(id3 != 0);

  index.UpdateFileSizeById(id1, 100);
  index.UpdateFileSizeById(id2, 200);
  index.UpdateFileSizeById(id3, 300);

  SearchResultBatch batch;
  AppendTestHit(batch, id1, "/root/scratch_test/file1.txt", false);
  AppendTestHit(batch, id2, "/root/scratch_test/file2.txt", false);
  AppendTestHit(batch, id3, "/root/scratch_test/file3.txt", false);
  const std::vector<SearchResultBatch> batches{std::move(batch)};

  SearchResultMergeScratch scratch;
  std::vector<char> pool1;
  const std::vector<SearchResult> results1 =
      MergeAndConvertToSearchResults(pool1, batches, index, &scratch);

  REQUIRE(results1.size() == 3);
  CHECK(results1[0].fileSize == 100);
  CHECK(results1[1].fileSize == 200);
  CHECK(results1[2].fileSize == 300);

  // Scratch should retain capacity from first call
  const size_t cap_ids = scratch.file_ids.capacity();
  const size_t cap_indices = scratch.file_result_indices.capacity();
  const size_t cap_snapshots = scratch.snapshots.capacity();
  CHECK(cap_ids >= 3);
  CHECK(cap_indices >= 3);
  CHECK(cap_snapshots >= 3);

  // Second call with smaller batch should reuse allocated capacity without realloc
  SearchResultBatch smaller_batch;
  AppendTestHit(smaller_batch, id1, "/root/scratch_test/file1.txt", false);
  const std::vector<SearchResultBatch> smaller_batches{std::move(smaller_batch)};

  std::vector<char> pool2;
  const std::vector<SearchResult> results2 =
      MergeAndConvertToSearchResults(pool2, smaller_batches, index, &scratch);

  REQUIRE(results2.size() == 1);
  CHECK(results2[0].fileSize == 100);
  // Reuse contract: the smaller second call (1 row) must not reallocate. This relies
  // on the standard guarantee that reserve(n) with n <= capacity() is a no-op, so the
  // implementation must only ever reserve(), never shrink, the scratch buffers.
  assert(scratch.file_ids.size() == 1);
  assert(scratch.file_result_indices.size() == 1);
  assert(scratch.snapshots.size() == 1);
  CHECK(scratch.file_ids.capacity() == cap_ids);
  CHECK(scratch.file_result_indices.capacity() == cap_indices);
  CHECK(scratch.snapshots.capacity() == cap_snapshots);
}

TEST_CASE("MergeAndConvertToSearchResults - bulk memcpy preserves null terminators and offsets") {
  FileIndex index;
  index.InsertPath("/root/bulk/alpha.txt", false);
  index.InsertPath("/root/bulk/sub/beta.cpp", false);
  index.InsertPath("/root/bulk/sub/gamma", true);
  index.RecomputeAllPaths();

  const uint64_t id_alpha = FindEntryIdByPathSuffix(index, "alpha.txt");
  const uint64_t id_beta = FindEntryIdByPathSuffix(index, "beta.cpp");
  const uint64_t id_gamma = FindEntryIdByPathSuffix(index, "gamma");
  REQUIRE(id_alpha != 0);
  REQUIRE(id_beta != 0);
  REQUIRE(id_gamma != 0);

  SearchResultBatch batch1;
  AppendTestHit(batch1, id_alpha, "/root/bulk/alpha.txt", false);
  AppendTestHit(batch1, id_beta, "/root/bulk/sub/beta.cpp", false);

  SearchResultBatch batch2_empty;

  SearchResultBatch batch3;
  AppendTestHit(batch3, id_gamma, "/root/bulk/sub/gamma", true);

  const size_t expected_pool_bytes =
      batch1.arena.size() + batch2_empty.arena.size() + batch3.arena.size();
  const std::vector<SearchResultBatch> batches{
      std::move(batch1), std::move(batch2_empty), std::move(batch3),};

  std::vector<char> pool;
  const std::vector<SearchResult> results =
      MergeAndConvertToSearchResults(pool, batches, index);

  REQUIRE(results.size() == 3);
  CHECK(pool.size() == expected_pool_bytes);

  // Every result view must point into pool and be followed by '\0'
  for (const SearchResult& r : results) {
    const char* const path_ptr = r.fullPath.data();
    CHECK(path_ptr >= pool.data());
    CHECK(path_ptr + r.fullPath.size() <= pool.data() + pool.size());
    // Direct C-string null termination contract check
    CHECK(path_ptr[r.fullPath.size()] == '\0');
  }

  CHECK(results[0].fullPath == "/root/bulk/alpha.txt");
  CHECK(results[0].GetFilename() == "alpha");
  CHECK(results[0].GetExtension() == "txt");
  CHECK_FALSE(results[0].isDirectory);

  CHECK(results[1].fullPath == "/root/bulk/sub/beta.cpp");
  CHECK(results[1].GetFilename() == "beta");
  CHECK(results[1].GetExtension() == "cpp");
  CHECK_FALSE(results[1].isDirectory);

  CHECK(results[2].fullPath == "/root/bulk/sub/gamma");
  CHECK(results[2].GetFilename() == "gamma");
  CHECK(results[2].GetExtension().empty());
  CHECK(results[2].isDirectory);
}

TEST_CASE("MergeAndConvertToSearchResults - mixed batches with quarantined and bulk") {
  FileIndex index;
  index.Insert(ntfs_file_reference::NtfsFileReference(1), ntfs_file_reference::NtfsFileReference(0), "root", true);
  index.InsertPath("/root/mixed/normal1.txt", false);
  index.InsertPath("/root/mixed/normal2.txt", false);
  const uint64_t id1 = FindEntryIdByPathSuffix(index, "normal1.txt");
  const uint64_t id2 = FindEntryIdByPathSuffix(index, "normal2.txt");
  REQUIRE(id1 != 0);
  REQUIRE(id2 != 0);

  // Quarantine entry: missing parent
  constexpr uint64_t kMissingParent = 0x00010000000000E0ULL;
  constexpr uint64_t kQuarantineChild = 0x00010000000000F0ULL;
  index.Insert(ntfs_file_reference::NtfsFileReference(kQuarantineChild),
               ntfs_file_reference::NtfsFileReference(kMissingParent), "quarantine.txt", false);
  REQUIRE(index.GetAwaitingStats().count == 1);

  // Batch 1: clean (uses bulk path despite has_awaiting == true)
  SearchResultBatch batch1;
  AppendTestHit(batch1, id1, "/root/mixed/normal1.txt", false);

  // Batch 2: contains quarantined hit + normal hit (uses fallback path)
  SearchResultBatch batch2;
  AppendTestHit(batch2, kQuarantineChild, "quarantine.txt", false);
  AppendTestHit(batch2, id2, "/root/mixed/normal2.txt", false);

  const std::vector<SearchResultBatch> batches{std::move(batch1), std::move(batch2)};
  std::vector<char> pool;
  const std::vector<SearchResult> results =
      MergeAndConvertToSearchResults(pool, batches, index);

  // Quarantine child skipped, 2 normal hits retained
  REQUIRE(results.size() == 2);
  const char* const path1 = results[0].fullPath.data();
  CHECK(results[0].fileId == id1);
  CHECK(results[0].fullPath == "/root/mixed/normal1.txt");
  CHECK(path1[results[0].fullPath.size()] == '\0');

  const char* const path2 = results[1].fullPath.data();
  CHECK(results[1].fileId == id2);
  CHECK(results[1].fullPath == "/root/mixed/normal2.txt");
  CHECK(path2[results[1].fullPath.size()] == '\0');

  // Pool size must account only for retained entries
  const size_t expected_pool_size =
      std::string_view("/root/mixed/normal1.txt").size() + 1 +
      std::string_view("/root/mixed/normal2.txt").size() + 1;
  CHECK(pool.size() == expected_pool_size);
}


