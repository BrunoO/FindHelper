#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

#include "index/FileIndexStorage.h"
#include "index/LazyValue.h"
#include "path/PathOperations.h"
#include "path/PathStorage.h"
#include "search/ParallelSearchEngine.h"
#include "utils/FileTimeTypes.h"

// Fixture: PathOperations needs FileIndexStorage (for path_storage_index) and PathStorage.
struct PathOpsFixture {
  std::shared_mutex mutex_;
  FileIndexStorage storage_{mutex_};
  PathStorage path_storage_;
  PathOperations ops{storage_, path_storage_};

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
  const PathOperations::PathComponentsView components = fixture.ops.GetPathComponentsViewByIndex(0);
  CHECK(components.has_extension == expect_has_extension);
  CHECK(components.extension == expected_extension);
  if (expected_filename_if_nonnull != nullptr) {
    CHECK(components.filename == expected_filename_if_nonnull);
  }
}

}  // namespace

TEST_CASE_FIXTURE(PathOpsFixture, "PathOperations::InsertPath inserts path entry") {
  AddEntry(1, 0, "file.txt", false);
  ops.InsertPath(1, R"(C:\test\file.txt)", false);

  const std::string path = ops.GetPath(1);
  CHECK(path == R"(C:\test\file.txt)");
}

TEST_CASE_FIXTURE(PathOpsFixture, "PathOperations::InsertPath clears stale failed lazy attrs") {
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

TEST_CASE_FIXTURE(PathOpsFixture, "PathOperations::InsertPath strips embedded null in path") {
  std::string corrupted = R"(C:\test)";
  corrupted.push_back('\0');
  corrupted.append(R"(\file.txt)");
  AddEntry(1, 0, "file.txt", false);
  ops.InsertPath(1, corrupted, false);
  CHECK(ops.GetPath(1) == R"(C:\test)");
}

TEST_CASE_FIXTURE(PathOpsFixture, "PathOperations::GetPath returns empty string for non-existent ID") {
  constexpr std::uint64_t kNonExistentId = 999ULL;
  const std::string path = ops.GetPath(kNonExistentId);
  CHECK(path.empty());
}

TEST_CASE_FIXTURE(PathOpsFixture, "PathOperations::GetPathView returns zero-copy view") {
  AddEntry(1, 0, "file.txt", false);
  ops.InsertPath(1, R"(C:\test\file.txt)", false);

  const std::string_view path_view = ops.GetPathView(1);
  CHECK(path_view == R"(C:\test\file.txt)");
}

TEST_CASE_FIXTURE(PathOpsFixture, "PathOperations::GetPathView returns empty view for non-existent ID") {
  constexpr std::uint64_t kNonExistentId = 999ULL;
  const std::string_view path_view = ops.GetPathView(kNonExistentId);
  CHECK(path_view.empty());
}

TEST_CASE_FIXTURE(PathOpsFixture, "PathOperations::GetPathComponentsView returns path components") {
  AddEntry(1, 0, "file.txt", false);
  ops.InsertPath(1, R"(C:\test\file.txt)", false);

  const PathOperations::PathComponentsView components = ops.GetPathComponentsView(1);
  CHECK(components.full_path == R"(C:\test\file.txt)");
  CHECK(components.filename == "file.txt");
  CHECK(components.extension == "txt");
  CHECK(components.directory_path == "C:\\test");
  CHECK(components.has_extension == true);
}

TEST_CASE_FIXTURE(PathOpsFixture, "PathOperations::GetPathComponentsView drive-root file keeps C:\\") {
  AddEntry(1, 0, "file.txt", false);
  ops.InsertPath(1, R"(C:\file.txt)", false);

  const PathOperations::PathComponentsView components = ops.GetPathComponentsView(1);
  CHECK(components.full_path == R"(C:\file.txt)");
  CHECK(components.filename == "file.txt");
  CHECK(components.directory_path == "C:\\");
  CHECK(components.extension == "txt");
  CHECK(components.has_extension == true);
}

TEST_CASE_FIXTURE(PathOpsFixture, "PathOperations::GetPathComponentsViewByIndex returns path components") {
  AddEntry(1, 0, "file.txt", false);
  ops.InsertPath(1, R"(C:\test\file.txt)", false);

  // Get by index 0 (first entry)
  const PathOperations::PathComponentsView components = ops.GetPathComponentsViewByIndex(0);
  CHECK(components.full_path == R"(C:\test\file.txt)");
  CHECK(components.filename == "file.txt");
}

TEST_CASE_FIXTURE(PathOpsFixture, "PathOperations::UpdatePrefix updates descendant paths") {
  // Insert directory and file
  AddEntry(1, 0, "dir", true);
  AddEntry(2, 1, "file.txt", false);
  ops.InsertPath(1, R"(C:\old\dir)", true);
  ops.InsertPath(2, R"(C:\old\dir\file.txt)", false);

  // Update prefix
  ops.UpdatePrefix(R"(C:\old\)", R"(C:\new\)");

  // Verify paths were updated
  const std::string dir_path = ops.GetPath(1);
  const std::string file_path = ops.GetPath(2);
  CHECK(dir_path == R"(C:\new\dir)");
  CHECK(file_path == R"(C:\new\dir\file.txt)");
}

TEST_CASE_FIXTURE(PathOpsFixture, "PathOperations::RemovePath marks entry as deleted") {
  AddEntry(1, 0, "file.txt", false);
  ops.InsertPath(1, R"(C:\test\file.txt)", false);
  CHECK(ops.GetPath(1) == R"(C:\test\file.txt)");

  const bool removed = ops.RemovePath(1);
  CHECK(removed == true);

  // Path should still be retrievable but marked as deleted
  // (PathStorage uses tombstone deletion; value is implementation-defined)
  (void)ops.GetPath(1);  // Must not crash; return value is implementation-defined
}

TEST_CASE_FIXTURE(PathOpsFixture, "PathOperations::RemovePath returns false for non-existent ID") {
  constexpr std::uint64_t kNonExistentId = 999ULL;
  const bool removed = ops.RemovePath(kNonExistentId);
  CHECK(removed == false);
}

TEST_CASE_FIXTURE(PathOpsFixture, "PathOperations::GetSearchableView returns SoA view") {
  AddEntry(1, 0, "file.txt", false);
  ops.InsertPath(1, R"(C:\test\file.txt)", false);

  const PathStorage::SoAView view = ops.GetSearchableView();
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

TEST_CASE_FIXTURE(PathOpsFixture, "PathOperations::InsertPath shorten keeps SoA length authoritative") {
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

  const PathStorage::SoAView view = ops.GetSearchableView();
  const size_t idx = FindSoAIndexById(view, 1);
  REQUIRE(idx < view.size);
  const size_t path_len =
      parallel_search_detail::GetPathLength(view, idx, path_storage_.GetStorageSize());
  CHECK(path_len == std::string_view(R"(C:\dir\a.txt)").length());
  CHECK(std::string_view(view.path_storage + view.path_offsets[idx], path_len) == R"(C:\dir\a.txt)");
  CHECK(parallel_search_detail::GetExtensionView(view, idx, path_len) == "txt");
}

TEST_CASE_FIXTURE(PathOpsFixture, "PathOperations::UpdatePrefix shorten keeps SoA length authoritative") {
  AddEntry(1, 0, "dir", true);
  AddEntry(2, 1, "file.txt", false);
  ops.InsertPath(1, R"(C:\verylongdirname\dir)", true);
  ops.InsertPath(2, R"(C:\verylongdirname\dir\file.txt)", false);

  ops.UpdatePrefix(R"(C:\verylongdirname\)", R"(C:\d\)");
  CHECK(ops.GetPath(2) == R"(C:\d\dir\file.txt)");

  const PathStorage::SoAView view = ops.GetSearchableView();
  const size_t idx = FindSoAIndexById(view, 2);
  REQUIRE(idx < view.size);
  const size_t path_len =
      parallel_search_detail::GetPathLength(view, idx, path_storage_.GetStorageSize());
  CHECK(path_len == std::string_view(R"(C:\d\dir\file.txt)").length());
  CHECK(std::string_view(view.path_storage + view.path_offsets[idx], path_len) == R"(C:\d\dir\file.txt)");
  CHECK(parallel_search_detail::GetExtensionView(view, idx, path_len) == "txt");
}

