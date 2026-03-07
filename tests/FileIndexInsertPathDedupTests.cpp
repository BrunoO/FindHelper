#include <doctest/doctest.h>

#include "index/FileIndex.h"
#include "path/PathUtils.h"

namespace {

// Expected directory path after RecomputeAllPaths. PathBuilder uses platform
// separator and (on Windows) default volume root, so we must match that.
std::string ExpectedTemporaryDirPath() {
  return std::string(path_utils::GetDefaultVolumeRootPath())
      .append("root").append(path_utils::kPathSeparatorStr)
      .append("Testing").append(path_utils::kPathSeparatorStr)
      .append("Temporary");
}

}  // namespace

TEST_CASE("InsertPath is idempotent for existing directory created via DirectoryResolver") {
  FileIndex index;

  // First insert a file path; this will create parent directories via DirectoryResolver.
  index.InsertPath("/root/Testing/Temporary/file1.txt", false);

  // Now insert the directory path itself; this should NOT create a second directory entry.
  index.InsertPath("/root/Testing/Temporary", true);

  // Recompute paths to ensure path storage is populated for search.
  index.RecomputeAllPaths();

  const std::string expected_dir = ExpectedTemporaryDirPath();

  // Collect all entries with full path expected_dir and verify we have exactly one.
  size_t count = 0;
  const auto accessor = index.GetPathAccessor();
  (void)accessor;
  index.ForEachEntryWithPath(
    [&count, &expected_dir](uint64_t /*id*/, const FileEntry& entry, std::string_view path) {
      if (entry.isDirectory && path == expected_dir) {
        ++count;
      }
      return true;
    });

  CHECK(count == 1);  // NOLINT(cert-err33-c) - doctest CHECK macro handles assertion; return value is not meant to be consumed
}

TEST_CASE("InsertPath is idempotent when directory is inserted before files") {
  FileIndex index;

  // First insert the directory path; this creates the FileEntry and caches ancestors.
  index.InsertPath("/root/Testing/Temporary", true);

  // Now insert a file under that directory; this must reuse the existing directory ID
  // instead of creating a second directory entry for the same full path.
  index.InsertPath("/root/Testing/Temporary/file1.txt", false);

  // Recompute paths to ensure path storage is populated for search.
  index.RecomputeAllPaths();

  const std::string expected_dir = ExpectedTemporaryDirPath();

  // Collect all entries with full path expected_dir and verify we have exactly one.
  size_t count = 0;
  const auto accessor = index.GetPathAccessor();
  (void)accessor;
  index.ForEachEntryWithPath(
    [&count, &expected_dir](uint64_t /*id*/, const FileEntry& entry, std::string_view path) {
      if (entry.isDirectory && path == expected_dir) {
        ++count;
      }
      return true;
    });

  CHECK(count == 1);  // NOLINT(cert-err33-c) - doctest CHECK macro handles assertion; return value is not meant to be consumed
}

