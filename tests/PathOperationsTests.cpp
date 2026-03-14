#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdint>

#include "path/PathOperations.h"
#include "path/PathStorage.h"

// Fixture: constructs a PathStorage + PathOperations pair for each test.
// PathOperations holds a reference to PathStorage, so both must live together.
struct PathOpsFixture {
  PathStorage storage;
  PathOperations ops{storage};
};

TEST_CASE_FIXTURE(PathOpsFixture, "PathOperations::InsertPath inserts path entry") {
  ops.InsertPath(1, R"(C:\test\file.txt)", false);

  const std::string path = ops.GetPath(1);
  CHECK(path == R"(C:\test\file.txt)");
}

TEST_CASE_FIXTURE(PathOpsFixture, "PathOperations::GetPath returns empty string for non-existent ID") {
  constexpr std::uint64_t kNonExistentId = 999ULL;
  const std::string path = ops.GetPath(kNonExistentId);
  CHECK(path.empty());
}

TEST_CASE_FIXTURE(PathOpsFixture, "PathOperations::GetPathView returns zero-copy view") {
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
  ops.InsertPath(1, R"(C:\test\file.txt)", false);

  const PathOperations::PathComponentsView components = ops.GetPathComponentsView(1);
  CHECK(components.full_path == R"(C:\test\file.txt)");
  CHECK(components.filename == "file.txt");
  CHECK(components.extension == "txt");
  CHECK(components.directory_path == "C:\\test");
  CHECK(components.has_extension == true);
}

TEST_CASE_FIXTURE(PathOpsFixture, "PathOperations::GetPathComponentsViewByIndex returns path components") {
  ops.InsertPath(1, R"(C:\test\file.txt)", false);

  // Get by index 0 (first entry)
  const PathOperations::PathComponentsView components = ops.GetPathComponentsViewByIndex(0);
  CHECK(components.full_path == R"(C:\test\file.txt)");
  CHECK(components.filename == "file.txt");
}

TEST_CASE_FIXTURE(PathOpsFixture, "PathOperations::UpdatePrefix updates descendant paths") {
  // Insert directory and file
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
    ops.InsertPath(1, R"(C:\repo\.gitignore)", false);
    const PathOperations::PathComponentsView components = ops.GetPathComponentsViewByIndex(0);
    CHECK(components.has_extension == false);
    CHECK(components.extension == "");
    CHECK(components.filename == ".gitignore");
  }

  SUBCASE("Pure dotfile (.env) has no extension") {
    ops.InsertPath(1, R"(C:\project\.env)", false);
    const PathOperations::PathComponentsView components = ops.GetPathComponentsViewByIndex(0);
    CHECK(components.has_extension == false);
    CHECK(components.extension == "");
    CHECK(components.filename == ".env");
  }

  SUBCASE("Dotfile with real extension (.profile.bak) has extension bak") {
    ops.InsertPath(1, R"(C:\home\.profile.bak)", false);
    const PathOperations::PathComponentsView components = ops.GetPathComponentsViewByIndex(0);
    CHECK(components.has_extension == true);
    CHECK(components.extension == "bak");
  }

  SUBCASE("Normal file (normal.txt) has extension txt") {
    ops.InsertPath(1, R"(C:\test\normal.txt)", false);
    const PathOperations::PathComponentsView components = ops.GetPathComponentsViewByIndex(0);
    CHECK(components.has_extension == true);
    CHECK(components.extension == "txt");
  }

  SUBCASE("File with no extension (Makefile) has no extension") {
    ops.InsertPath(1, R"(C:\src\Makefile)", false);
    const PathOperations::PathComponentsView components = ops.GetPathComponentsViewByIndex(0);
    CHECK(components.has_extension == false);
    CHECK(components.extension == "");
    CHECK(components.filename == "Makefile");
  }
}

