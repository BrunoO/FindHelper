#include <doctest/doctest.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "index/FileIndexStorage.h"
#include "index/IndexOperations.h"
#include "path/PathStorage.h"
#include "search/FilenameTrigramExtractor.h"
#include "search/ParallelSearchEngine.h"
#include "utils/FileTimeTypes.h"

struct PathOpsFixture {
  std::shared_mutex mutex_;
  FileIndexStorage storage_{mutex_};
  PathStorage path_storage_;
  std::atomic<size_t> remove_not_in_index_count_{0};
  std::atomic<size_t> remove_inconsistency_count_{0};
  IndexOperations ops{storage_, path_storage_, remove_not_in_index_count_,
                      remove_inconsistency_count_, nullptr};

  void AddEntry(uint64_t id, uint64_t parent_id, std::string_view name, bool is_dir) {
    storage_.InsertLocked(ntfs_file_reference::NtfsFileReference(id),
                          ntfs_file_reference::NtfsFileReference(parent_id), name, is_dir,
                          kFileTimeNotLoaded);
  }
};

namespace {

// Shared by ParsePathOffsets / dotfile SUBCASEs (Sonar duplication reduction).
void AssertComponentsAfterInsert(PathOpsFixture& fixture,
                                 std::string_view filename,
                                 std::string_view full_inserted_path,
                                 bool expect_has_extension,
                                 std::string_view expected_extension,
                                 const char* expected_filename_if_nonnull = nullptr) {
  fixture.AddEntry(1, 0, filename, false);
  fixture.ops.InsertPath(1, full_inserted_path, false);
  const PathStorage::PathComponentsView components =
      fixture.path_storage_.GetPathComponentsByIndex(0);
  CHECK(components.has_extension == expect_has_extension);
  CHECK(components.extension == expected_extension);
  if (expected_filename_if_nonnull != nullptr) {
    CHECK(components.filename == expected_filename_if_nonnull);
  }
}

}  // namespace

TEST_CASE_FIXTURE(PathOpsFixture, "IndexOperations::InsertPath inserts path entry") {
  AddEntry(1, 0, "file.txt", false);
  ops.InsertPath(1, R"(C:\test\file.txt)", false);

  const std::string path = ops.GetPath(1);
  CHECK(path == R"(C:\test\file.txt)");
}

TEST_CASE_FIXTURE(PathOpsFixture, "IndexOperations::InsertPath clears stale failed lazy attrs") {
  AddEntry(1, 0, "file.txt", false);
  {
    const std::unique_lock lock(mutex_);
    FileEntry* entry = storage_.GetEntryMutable(1);
    REQUIRE(entry != nullptr);
    entry->fileSize.MarkFailed();
    entry->lastModificationTime.MarkFailed();
  }

  ops.InsertPath(1, R"(C:\test\file.txt)", false);

  const FileEntry* entry = storage_.GetEntry(1);
  REQUIRE(entry != nullptr);
  CHECK(entry->fileSize.IsNotLoaded());
  CHECK(entry->lastModificationTime.IsNotLoaded());
}

TEST_CASE_FIXTURE(PathOpsFixture, "IndexOperations::InsertPath strips embedded null in path") {
  std::string corrupted = R"(C:\test)";
  corrupted.push_back('\0');
  corrupted.append(R"(\file.txt)");
  AddEntry(1, 0, "file.txt", false);
  ops.InsertPath(1, corrupted, false);
  CHECK(ops.GetPath(1) == R"(C:\test)");
}

TEST_CASE_FIXTURE(PathOpsFixture, "IndexOperations::GetPath returns empty string for non-existent ID") {
  constexpr std::uint64_t kNonExistentId = 999ULL;
  const std::string path = ops.GetPath(kNonExistentId);
  CHECK(path.empty());
}

TEST_CASE_FIXTURE(PathOpsFixture, "IndexOperations::GetPathView returns zero-copy view") {
  AddEntry(1, 0, "file.txt", false);
  ops.InsertPath(1, R"(C:\test\file.txt)", false);

  const std::string_view path_view = ops.GetPathView(1);
  CHECK(path_view == R"(C:\test\file.txt)");
}

TEST_CASE_FIXTURE(PathOpsFixture, "IndexOperations::GetPathView returns empty view for non-existent ID") {
  constexpr std::uint64_t kNonExistentId = 999ULL;
  const std::string_view path_view = ops.GetPathView(kNonExistentId);
  CHECK(path_view.empty());
}

TEST_CASE_FIXTURE(PathOpsFixture, "PathStorage::GetPathComponentsByIndex returns path components") {
  AddEntry(1, 0, "file.txt", false);
  ops.InsertPath(1, R"(C:\test\file.txt)", false);

  const PathStorage::PathComponentsView components =
      path_storage_.GetPathComponentsByIndex(0);
  CHECK(components.full_path == R"(C:\test\file.txt)");
  CHECK(components.filename == "file.txt");
  CHECK(components.extension == "txt");
  CHECK(components.directory_path == "C:\\test");
  CHECK(components.has_extension == true);
}

TEST_CASE_FIXTURE(PathOpsFixture, "PathStorage::GetPathComponentsByIndex drive-root file keeps C:\\") {
  AddEntry(1, 0, "file.txt", false);
  ops.InsertPath(1, R"(C:\file.txt)", false);

  const PathStorage::PathComponentsView components =
      path_storage_.GetPathComponentsByIndex(0);
  CHECK(components.full_path == R"(C:\file.txt)");
  CHECK(components.filename == "file.txt");
  CHECK(components.directory_path == "C:\\");
  CHECK(components.extension == "txt");
  CHECK(components.has_extension == true);
}

TEST_CASE_FIXTURE(PathOpsFixture, "IndexOperations::Rename updates descendant paths") {
  ops.Insert(ntfs_file_reference::NtfsFileReference(1), ntfs_file_reference::NtfsFileReference(0), "old", true);
  ops.Insert(ntfs_file_reference::NtfsFileReference(2), ntfs_file_reference::NtfsFileReference(1), "dir", true);
  ops.Insert(ntfs_file_reference::NtfsFileReference(3), ntfs_file_reference::NtfsFileReference(2), "file.txt", false);

  REQUIRE(ops.Rename(ntfs_file_reference::NtfsFileReference(1), "new"));

  const std::string dir_path = ops.GetPath(2);
  const std::string file_path = ops.GetPath(3);
  CHECK(dir_path.find("new") != std::string::npos);
  CHECK(file_path.find("new") != std::string::npos);
  CHECK(file_path.find("file.txt") != std::string::npos);
}

TEST_CASE_FIXTURE(PathOpsFixture, "IndexOperations::RemovePath marks entry as deleted") {
  AddEntry(1, 0, "file.txt", false);
  ops.InsertPath(1, R"(C:\test\file.txt)", false);
  CHECK(ops.GetPath(1) == R"(C:\test\file.txt)");

  const bool removed = ops.RemovePath(1);
  CHECK(removed == true);

  // Path should still be retrievable but marked as deleted
  // (PathStorage uses tombstone deletion; value is implementation-defined)
  (void)ops.GetPath(1);  // Must not crash; return value is implementation-defined
}

TEST_CASE_FIXTURE(PathOpsFixture, "IndexOperations::RemovePath returns false for non-existent ID") {
  constexpr std::uint64_t kNonExistentId = 999ULL;
  const bool removed = ops.RemovePath(kNonExistentId);
  CHECK(removed == false);
}

TEST_CASE_FIXTURE(PathOpsFixture, "PathStorage::GetReadOnlyView returns SoA view") {
  AddEntry(1, 0, "file.txt", false);
  ops.InsertPath(1, R"(C:\test\file.txt)", false);

  const PathStorage::SoAView view = path_storage_.GetReadOnlyView();
  CHECK(view.size > 0);
  CHECK(view.path_storage != nullptr);
  CHECK(view.path_offsets != nullptr);
  CHECK(view.path_ids != nullptr);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) - Aggregated doctest case with many subcases
TEST_CASE_FIXTURE(PathOpsFixture, "PathStorage::ParsePathOffsets - dotfile handling") {
  // Verifies that ParsePathOffsets does not treat the leading dot of a dotfile
  // as an extension separator. This is the PathStorage SoA counterpart of the
  // fix in ComputePathOffsets (SearchWorker.cpp).

  SUBCASE("Pure dotfile (.gitignore) has no extension") {
    AssertComponentsAfterInsert(*this, ".gitignore", R"(C:\repo\.gitignore)", false, "", ".gitignore");
  }

  SUBCASE("Pure dotfile (.env) has no extension") {
    AssertComponentsAfterInsert(*this, ".env", R"(C:\project\.env)", false, "", ".env");
  }

  SUBCASE("Dotfile with real extension (.profile.bak) has extension bak") {
    AssertComponentsAfterInsert(*this, ".profile.bak", R"(C:\home\.profile.bak)", true, "bak");
  }

  SUBCASE("Normal file (normal.txt) has extension txt") {
    AssertComponentsAfterInsert(*this, "normal.txt", R"(C:\test\normal.txt)", true, "txt");
  }

  SUBCASE("File with no extension (Makefile) has no extension") {
    AssertComponentsAfterInsert(*this, "Makefile", R"(C:\src\Makefile)", false, "", "Makefile");
  }
}

namespace {

size_t FindSoAIndexById(const PathStorage::SoAView& view, uint64_t id) {
  for (size_t i = 0; i < view.size; ++i) {
    if (view.path_ids[i] == id && view.is_deleted[i] == 0) {
      return i;
    }
  }
  return view.size;
}

}  // namespace

TEST_CASE_FIXTURE(PathOpsFixture, "IndexOperations::InsertPath shorten keeps SoA length authoritative") {
  // Regression: shorten-in-place left stale tail bytes while GetPathLength()
  // still reported the old length (offset differences), corrupting the SoA
  // scan path (false positives, broken extension filter, NUL in result rows).
  AddEntry(1, 0, "averylongfilename.txt", false);
  AddEntry(2, 0, "other.txt", false);
  ops.InsertPath(1, R"(C:\dir\averylongfilename.txt)", false);
  ops.InsertPath(2, R"(C:\dir\other.txt)", false);

  // Single-file rename to a shorter name (existing_index path).
  ops.InsertPath(1, R"(C:\dir\a.txt)", false);
  CHECK(ops.GetPath(1) == R"(C:\dir\a.txt)");

  const PathStorage::SoAView view = path_storage_.GetReadOnlyView();
  const size_t idx = FindSoAIndexById(view, 1);
  REQUIRE(idx < view.size);
  const size_t path_len =
      parallel_search_detail::GetPathLength(view, idx, path_storage_.GetStorageSize());
  CHECK(path_len == std::string_view(R"(C:\dir\a.txt)").length());
  CHECK(std::string_view(view.path_storage + view.path_offsets[idx], path_len) == R"(C:\dir\a.txt)");
  CHECK(parallel_search_detail::GetExtensionView(view, idx, path_len) == "txt");
}

TEST_CASE_FIXTURE(PathOpsFixture, "IndexOperations::Rename shorten keeps SoA length authoritative") {
  ops.Insert(ntfs_file_reference::NtfsFileReference(1), ntfs_file_reference::NtfsFileReference(0), "verylongdirname", true);
  ops.Insert(ntfs_file_reference::NtfsFileReference(2), ntfs_file_reference::NtfsFileReference(1), "dir", true);
  ops.Insert(ntfs_file_reference::NtfsFileReference(3), ntfs_file_reference::NtfsFileReference(2), "file.txt", false);

  REQUIRE(ops.Rename(ntfs_file_reference::NtfsFileReference(1), "d"));
  const std::string expected_path = ops.GetPath(3);
  CHECK(expected_path.find("file.txt") != std::string::npos);

  const PathStorage::SoAView view = path_storage_.GetReadOnlyView();
  const size_t idx = FindSoAIndexById(view, 3);
  REQUIRE(idx < view.size);
  const size_t path_len =
      parallel_search_detail::GetPathLength(view, idx, path_storage_.GetStorageSize());
  CHECK(path_len == expected_path.length());
  CHECK(std::string_view(view.path_storage + view.path_offsets[idx], path_len) == expected_path);
  CHECK(parallel_search_detail::GetExtensionView(view, idx, path_len) == "txt");
}

TEST_CASE_FIXTURE(PathOpsFixture, "IndexOperations::Rename batch update correctly updates all descendants") {
  ops.Insert(ntfs_file_reference::NtfsFileReference(1), ntfs_file_reference::NtfsFileReference(0), "old_root_folder_name", true);
  ops.Insert(ntfs_file_reference::NtfsFileReference(2), ntfs_file_reference::NtfsFileReference(1), "sub_directory_path", true);
  constexpr size_t kEntryCount = 500;
  for (size_t i = 1; i <= kEntryCount; ++i) {
    ops.Insert(ntfs_file_reference::NtfsFileReference(i + 2), ntfs_file_reference::NtfsFileReference(2),
               "file_" + std::to_string(i) + ".txt", false);
  }

  REQUIRE(ops.Rename(ntfs_file_reference::NtfsFileReference(1), "new_root_folder_name"));

  for (size_t i = 1; i <= kEntryCount; ++i) {
    const std::string updated = ops.GetPath(i + 2);
    CHECK(updated.find("new_root_folder_name") != std::string::npos);
    CHECK(updated.find("file_" + std::to_string(i) + ".txt") != std::string::npos);
  }
}

TEST_CASE_FIXTURE(PathOpsFixture, "IndexOperations::Rename moves trigram postings on row relocation") {
  // Regression: a length-changing directory rename relocated rows (tombstone +
  // append) without moving trigram postings, so trigram-accelerated search
  // pointed at tombstoned rows and missed the live files.
  ops.Insert(ntfs_file_reference::NtfsFileReference(1), ntfs_file_reference::NtfsFileReference(0), "old", true);
  ops.Insert(ntfs_file_reference::NtfsFileReference(2), ntfs_file_reference::NtfsFileReference(1), "dir", true);
  ops.Insert(ntfs_file_reference::NtfsFileReference(3), ntfs_file_reference::NtfsFileReference(2), "mykeyfile.txt", false);

  const std::vector<filename_trigram::TrigramKey> trigrams =
      filename_trigram::ExtractTrigrams("key");
  REQUIRE(!trigrams.empty());
  {
    const auto before = storage_.GetTrigramIndex().QueryCandidates(trigrams);
    REQUIRE(before.has_value());
    CHECK(before->cardinality() == 1);
  }

  // Longer replacement cannot stay in place: rows relocate to new indices.
  REQUIRE(ops.Rename(ntfs_file_reference::NtfsFileReference(1), "much_longer_directory_name"));
  CHECK(ops.GetPath(3).find("much_longer_directory_name") != std::string::npos);

  const FileEntry* entry = storage_.GetEntry(3);
  REQUIRE(entry != nullptr);
  const auto after = storage_.GetTrigramIndex().QueryCandidates(trigrams);
  REQUIRE(after.has_value());
  CHECK(after->cardinality() == 1);
  CHECK(after->contains(static_cast<uint32_t>(entry->path_storage_index)));
}

TEST_CASE_FIXTURE(PathOpsFixture, "IndexOperations::InsertPath keeps extension postings in sync") {
  AddEntry(1, 0, "report.pdf", false);
  AddEntry(2, 1, "notes.txt", false);
  ops.InsertPath(1, R"(C:\docs\report.pdf)", false);
  ops.InsertPath(2, R"(C:\docs\notes.txt)", false);

  const auto pdf_res = storage_.GetExtensionIndex().QueryCandidates({"pdf"});
  REQUIRE(pdf_res.has_value());
  CHECK(pdf_res->cardinality() == 1);
  const auto txt_res = storage_.GetExtensionIndex().QueryCandidates({"txt"});
  REQUIRE(txt_res.has_value());
  CHECK(txt_res->cardinality() == 1);

  // Re-inserting the same id with a new path moves the posting, not duplicates it
  ops.InsertPath(1, R"(C:\docs\report.docx)", false);
  CHECK(storage_.GetExtensionIndex().QueryCandidates({"pdf"})->isEmpty());
  const auto docx_res = storage_.GetExtensionIndex().QueryCandidates({"docx"});
  REQUIRE(docx_res.has_value());
  CHECK(docx_res->cardinality() == 1);
}

TEST_CASE_FIXTURE(PathOpsFixture, "IndexOperations::RemovePath drops extension postings") {
  AddEntry(1, 0, "report.pdf", false);
  ops.InsertPath(1, R"(C:\docs\report.pdf)", false);
  REQUIRE(storage_.GetExtensionIndex().QueryCandidates({"pdf"})->cardinality() == 1);

  CHECK(ops.RemovePath(1));
  CHECK(storage_.GetExtensionIndex().QueryCandidates({"pdf"})->isEmpty());
}

TEST_CASE_FIXTURE(PathOpsFixture, "IndexOperations::Rename moves extension postings on row relocation") {
  ops.Insert(ntfs_file_reference::NtfsFileReference(1), ntfs_file_reference::NtfsFileReference(0), "old", true);
  ops.Insert(ntfs_file_reference::NtfsFileReference(2), ntfs_file_reference::NtfsFileReference(1), "dir", true);
  ops.Insert(ntfs_file_reference::NtfsFileReference(3), ntfs_file_reference::NtfsFileReference(2), "mykeyfile.txt", false);

  {
    const auto before = storage_.GetExtensionIndex().QueryCandidates({"txt"});
    REQUIRE(before.has_value());
    CHECK(before->cardinality() == 1);
  }

  // Longer replacement cannot stay in place: rows relocate to new indices.
  REQUIRE(ops.Rename(ntfs_file_reference::NtfsFileReference(1), "much_longer_directory_name"));
  CHECK(ops.GetPath(3).find("much_longer_directory_name") != std::string::npos);

  const FileEntry* entry = storage_.GetEntry(3);
  REQUIRE(entry != nullptr);
  const auto after = storage_.GetExtensionIndex().QueryCandidates({"txt"});
  REQUIRE(after.has_value());
  CHECK(after->cardinality() == 1);
  CHECK(after->contains(static_cast<uint32_t>(entry->path_storage_index)));
}

TEST_CASE_FIXTURE(PathOpsFixture, "IndexOperations::InsertPath indexes dotted directory names") {
  // Pins the spec §6 contract: dotted directory names produce an extension
  // posting (superset semantics); the verifier's item-type filter governs
  // visibility, not the index.
  AddEntry(1, 0, "foo.d", true);
  ops.InsertPath(1, R"(C:\data\foo.d)", true);

  const auto res = storage_.GetExtensionIndex().QueryCandidates({"d"});
  REQUIRE(res.has_value());
  CHECK(res->cardinality() == 1);
}



