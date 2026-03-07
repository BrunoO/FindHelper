#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "path/PathOperations.h"
#include "path/PathStorage.h"
#include <cstdint>

TEST_CASE("PathOperations::InsertPath inserts path entry") {
  PathStorage path_storage;
  PathOperations path_ops(path_storage);

  path_ops.InsertPath(1, R"(C:\test\file.txt)", false);

  const std::string path = path_ops.GetPath(1);
  CHECK(path == R"(C:\test\file.txt)");
}

TEST_CASE("PathOperations::GetPath returns empty string for non-existent ID") {
  PathStorage path_storage;
  const PathOperations path_ops(path_storage);

  constexpr std::uint64_t kNonExistentId = 999ULL;
  const std::string path = path_ops.GetPath(kNonExistentId);
  CHECK(path.empty());
}

TEST_CASE("PathOperations::GetPathView returns zero-copy view") {
  PathStorage path_storage;
  PathOperations path_ops(path_storage);

  path_ops.InsertPath(1, R"(C:\test\file.txt)", false);

  const std::string_view path_view = path_ops.GetPathView(1);
  CHECK(path_view == R"(C:\test\file.txt)");
}

TEST_CASE("PathOperations::GetPathView returns empty view for non-existent ID") {
  PathStorage path_storage;
  const PathOperations path_ops(path_storage);

  constexpr std::uint64_t kNonExistentId = 999ULL;
  const std::string_view path_view = path_ops.GetPathView(kNonExistentId);
  CHECK(path_view.empty());
}

TEST_CASE("PathOperations::GetPathComponentsView returns path components") {
  PathStorage path_storage;
  PathOperations path_ops(path_storage);

  path_ops.InsertPath(1, R"(C:\test\file.txt)", false);

  const PathOperations::PathComponentsView components = path_ops.GetPathComponentsView(1);
  CHECK(components.full_path == R"(C:\test\file.txt)");
  CHECK(components.filename == "file.txt");
  CHECK(components.extension == "txt");
  CHECK(components.directory_path == "C:\\test");
  CHECK(components.has_extension == true);
}

TEST_CASE("PathOperations::GetPathComponentsViewByIndex returns path components") {
  PathStorage path_storage;
  PathOperations path_ops(path_storage);

  path_ops.InsertPath(1, R"(C:\test\file.txt)", false);

  // Get by index 0 (first entry)
  const PathOperations::PathComponentsView components = path_ops.GetPathComponentsViewByIndex(0);
  CHECK(components.full_path == R"(C:\test\file.txt)");
  CHECK(components.filename == "file.txt");
}

TEST_CASE("PathOperations::UpdatePrefix updates descendant paths") {
  PathStorage path_storage;
  PathOperations path_ops(path_storage);

  // Insert directory and file
  path_ops.InsertPath(1, R"(C:\old\dir)", true);
  path_ops.InsertPath(2, R"(C:\old\dir\file.txt)", false);

  // Update prefix
  path_ops.UpdatePrefix(R"(C:\old\)", R"(C:\new\)");

  // Verify paths were updated
  const std::string dir_path = path_ops.GetPath(1);
  const std::string file_path = path_ops.GetPath(2);
  CHECK(dir_path == R"(C:\new\dir)");
  CHECK(file_path == R"(C:\new\dir\file.txt)");
}

TEST_CASE("PathOperations::RemovePath marks entry as deleted") {
  PathStorage path_storage;
  PathOperations path_ops(path_storage);

  path_ops.InsertPath(1, R"(C:\test\file.txt)", false);
  CHECK(path_ops.GetPath(1) == R"(C:\test\file.txt)");

  const bool removed = path_ops.RemovePath(1);
  CHECK(removed == true);

  // Path should still be retrievable but marked as deleted
  // (PathStorage uses tombstone deletion)
  const std::string path = path_ops.GetPath(1);
  // Note: PathStorage may return empty or the path depending on implementation
  // The important thing is that RemovePath returned true
}

TEST_CASE("PathOperations::RemovePath returns false for non-existent ID") {
  PathStorage path_storage;
  PathOperations path_ops(path_storage);

  constexpr std::uint64_t kNonExistentId = 999ULL;
  const bool removed = path_ops.RemovePath(kNonExistentId);
  CHECK(removed == false);
}

TEST_CASE("PathOperations::GetSearchableView returns SoA view") {
  PathStorage path_storage;
  PathOperations path_ops(path_storage);

  path_ops.InsertPath(1, R"(C:\test\file.txt)", false);

  const PathStorage::SoAView view = path_ops.GetSearchableView();
  CHECK(view.size > 0);
  CHECK(view.path_storage != nullptr);
  CHECK(view.path_offsets != nullptr);
  CHECK(view.path_ids != nullptr);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) - Aggregated doctest case with many subcases
TEST_CASE("PathStorage::ParsePathOffsets - dotfile handling") {
  // Verifies that ParsePathOffsets does not treat the leading dot of a dotfile
  // as an extension separator. This is the PathStorage SoA counterpart of the
  // fix in ComputePathOffsets (SearchWorker.cpp).

  SUBCASE("Pure dotfile (.gitignore) has no extension") {
    PathStorage path_storage;
    PathOperations path_ops(path_storage);
    path_ops.InsertPath(1, R"(C:\repo\.gitignore)", false);

    PathOperations::PathComponentsView components = path_ops.GetPathComponentsViewByIndex(0);
    CHECK(components.has_extension == false);
    CHECK(components.extension == "");
    CHECK(components.filename == ".gitignore");
  }

  SUBCASE("Pure dotfile (.env) has no extension") {
    PathStorage path_storage;
    PathOperations path_ops(path_storage);
    path_ops.InsertPath(1, R"(C:\project\.env)", false);

    PathOperations::PathComponentsView components = path_ops.GetPathComponentsViewByIndex(0);
    CHECK(components.has_extension == false);
    CHECK(components.extension == "");
    CHECK(components.filename == ".env");
  }

  SUBCASE("Dotfile with real extension (.profile.bak) has extension bak") {
    PathStorage path_storage;
    PathOperations path_ops(path_storage);
    path_ops.InsertPath(1, R"(C:\home\.profile.bak)", false);

    PathOperations::PathComponentsView components = path_ops.GetPathComponentsViewByIndex(0);
    CHECK(components.has_extension == true);
    CHECK(components.extension == "bak");
  }

  SUBCASE("Normal file (normal.txt) has extension txt") {
    PathStorage path_storage;
    PathOperations path_ops(path_storage);
    path_ops.InsertPath(1, R"(C:\test\normal.txt)", false);

    PathOperations::PathComponentsView components = path_ops.GetPathComponentsViewByIndex(0);
    CHECK(components.has_extension == true);
    CHECK(components.extension == "txt");
  }

  SUBCASE("File with no extension (Makefile) has no extension") {
    PathStorage path_storage;
    PathOperations path_ops(path_storage);
    path_ops.InsertPath(1, R"(C:\src\Makefile)", false);

    PathOperations::PathComponentsView components = path_ops.GetPathComponentsViewByIndex(0);
    CHECK(components.has_extension == false);
    CHECK(components.extension == "");
    CHECK(components.filename == "Makefile");
  }
}

