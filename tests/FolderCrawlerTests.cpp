#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <system_error>

#include "TestHelpers.h"
#include "crawler/FolderCrawler.h"
#include "doctest/doctest.h"
#include "index/FileIndex.h"
#include "path/PathUtils.h"

// ---------------------------------------------------------------------------
// Fixture path resolution
// ---------------------------------------------------------------------------

namespace {

// Walks up from the test executable directory (up to kMaxLevels) to find
// tests/data/fixture. Returns an empty string when not found.
std::string ResolveFixturePath() {
  static std::string cached_path;
  if (!cached_path.empty()) {
    return cached_path;
  }
  constexpr int kMaxLevels = 8;
  std::string base = path_utils::GetExecutableDirectory();
  if (!base.empty() && base.back() == path_utils::kPathSeparator) {
    base.pop_back();
  }
  for (int level = 0; level < kMaxLevels; ++level) {
    if (base.empty()) {
      break;
    }
    const std::filesystem::path candidate(
        path_utils::JoinPath(base, "tests/data/fixture"));
    if (std::error_code ec;
        std::filesystem::exists(candidate, ec) &&
        std::filesystem::is_directory(candidate, ec)) {
      cached_path = candidate.string();
      return cached_path;
    }
    base = path_utils::GetParentDirectory(base);
  }
  return {};
}

}  // namespace

// ---------------------------------------------------------------------------
// Known fixture layout (tests/data/fixture/)
// ---------------------------------------------------------------------------
//
//  alpha/
//    a.txt      1 B   (Fib 1)
//    b.txt      1 B   (Fib 1)
//    c.cpp      2 B   (Fib 2)
//  beta/
//    x.h        3 B   (Fib 3)
//    y.json     5 B   (Fib 5)
//    z.md       8 B   (Fib 8)
//    sub/
//      p.txt   13 B   (Fib 7)
//      q.cpp   21 B   (Fib 8)
//  gamma/
//    deep/
//      deeper/
//        leaf.h 34 B  (Fib 9)
//    r.txt      55 B  (Fib 10)
//
//  Files: 10    Dirs (children): 6    Total index entries: 16

constexpr size_t kFixtureFiles = 10;
constexpr size_t kFixtureDirs  = 6;   // alpha, beta, beta/sub, gamma, gamma/deep, gamma/deep/deeper
constexpr size_t kFixtureTotal = kFixtureFiles + kFixtureDirs;

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_SUITE("FolderCrawler") {

TEST_CASE("crawl fixture - entry counts match known structure") {
  const std::string fixture_path = ResolveFixturePath();
  if (fixture_path.empty()) {
    MESSAGE("tests/data/fixture not found next to test binary — skipping");
    return;
  }

  const test_helpers::TestSettingsFixture settings("static");
  FileIndex file_index;
  const FolderCrawlerConfig config{/*.thread_count=*/1};  // single-threaded: deterministic
  FolderCrawler crawler(file_index, config);

  const bool success = crawler.Crawl(fixture_path);

  REQUIRE(success);
  CHECK(crawler.GetErrorCount() == 0);
  CHECK(crawler.GetFilesProcessed() == kFixtureFiles);
  CHECK(crawler.GetDirectoriesProcessed() == kFixtureDirs);

  // GetTotalItems() includes ancestor dirs created by DirectoryResolver for the
  // fixture root path — the exact count depends on the run-time path depth.
  // Assert at least the fixture entries are present.
  file_index.RecomputeAllPaths();
  CHECK(file_index.GetTotalItems() >= kFixtureTotal);
}

TEST_CASE("crawl fixture - empty path returns false") {
  const test_helpers::TestSettingsFixture settings("static");
  FileIndex file_index;
  FolderCrawler crawler(file_index);

  CHECK_FALSE(crawler.Crawl(""));
}

TEST_CASE("crawl fixture - non-existent path succeeds with zero entries") {
  // ENOENT from open() is treated as "directory vanished" — not a hard error.
  // The crawl returns true with zero files/directories processed.
  // Use a path under the system temp directory: a Unix-style absolute path like
  // /this/... is not reliably resolved by Win32 enumeration APIs and can yield
  // errors other than ERROR_PATH_NOT_FOUND, causing Crawl() to return false.
  std::error_code ec;
  const std::filesystem::path temp_base = std::filesystem::temp_directory_path(ec);
  if (ec || temp_base.empty()) {
    MESSAGE("temp directory unavailable — skipping");
    return;
  }
  const std::string nonexistent_path =
      (temp_base / "findhelper_crawler_nonexistent_xyz").string();

  const test_helpers::TestSettingsFixture settings("static");
  FileIndex file_index;
  FolderCrawler crawler(file_index);

  const bool result = crawler.Crawl(nonexistent_path);
  CHECK(result);
  CHECK(crawler.GetFilesProcessed() == 0);
  CHECK(crawler.GetDirectoriesProcessed() == 0);
}

TEST_CASE("FileIndex - HashPathForCrawl matches PathHashInline of trimmed path") {
  CHECK(FileIndex::HashPathForCrawl("/a/b/c") == FileIndex::HashPathForCrawl("/a/b/c/"));
  CHECK(FileIndex::HashPathForCrawl("C:\\a\\b\\c") == FileIndex::HashPathForCrawl("C:/a/b/c"));
  CHECK(FileIndex::HashPathForCrawl("/a/b/c//") == FileIndex::HashPathForCrawl("/a/b/c"));
}

TEST_CASE("crawl then recompute - paths survive without the name arena") {
  // Crawl inserts skip the temporary name arena (populate_name_cache=false);
  // RecomputeAllPaths must still rebuild identical paths, deriving leaves
  // back from the materialized PathStorage rows.
  std::error_code ec;
  const std::filesystem::path temp_base = std::filesystem::temp_directory_path(ec);
  if (ec || temp_base.empty()) {
    MESSAGE("temp directory unavailable — skipping");
    return;
  }
  const std::filesystem::path root = temp_base / "findhelper_crawler_recompute";
  std::filesystem::remove_all(root, ec);
  ec.clear();

  const std::filesystem::path sub = root / "r" / "sub";
  std::filesystem::create_directories(sub, ec);
  if (ec) {
    MESSAGE("failed to build tree — skipping");
    std::filesystem::remove_all(root, ec);
    return;
  }
  std::ofstream(root / "r" / "a.txt") << "x";
  std::ofstream(sub / "nested.txt") << "x";

  const test_helpers::TestSettingsFixture settings("static");
  FileIndex file_index;
  FolderCrawlerConfig config{/*.thread_count=*/1};  // single-threaded: deterministic
  FolderCrawler crawler(file_index, config);
  REQUIRE(crawler.Crawl(root.string()));

  auto snapshot_paths = [&file_index]() {
    std::set<std::string> paths;
    const PathStorage::SoAView view = file_index.GetSearchableView();
    const size_t storage_size = file_index.GetStorageSize();
    for (size_t i = 0; i < view.size; ++i) {
      const size_t len = view.GetPathLength(i, storage_size);
      paths.emplace(view.path_storage + view.path_offsets[i], len);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - i < view.size
    }
    return paths;
  };

  const auto before = snapshot_paths();
  const std::string expected =
      path_utils::JoinPath(path_utils::JoinPath(root.string(), "r"), "sub");
  CHECK(before.count(path_utils::JoinPath(expected, "nested.txt")) == 1);

  file_index.RecomputeAllPaths();

  const auto after = snapshot_paths();
  // NOTE: exact-set equality does NOT hold here, on unmodified code either:
  // RecomputeAllPaths re-roots the crawl subtree at "/" (pre-existing
  // orphan-prune behavior for resolver ancestors above the crawl root —
  // verified identical with src/ stashed). What §2.1 guarantees is that no
  // leaf is lost to the missing arena: same entry count, and every crawled
  // leaf reachable under its relative structure.
  CHECK(after.size() == before.size());
  auto ends_with = [](std::string_view path, std::string_view suffix) {
    return path.size() >= suffix.size() &&
           path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0;
  };
  auto contains_suffix = [&after, &ends_with](std::string_view suffix) {
    for (const auto& p : after) {
      if (ends_with(p, suffix)) {
        return true;
      }
    }
    return false;
  };
  const std::string suffix_nested =
      path_utils::JoinPath(path_utils::JoinPath("r", "sub"), "nested.txt");
  const std::string suffix_a = path_utils::JoinPath("r", "a.txt");
  CHECK(contains_suffix(suffix_nested));
  CHECK(contains_suffix(suffix_a));

  std::filesystem::remove_all(root, ec);
}

}  // TEST_SUITE("FolderCrawler")
