#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include <filesystem>
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

}  // TEST_SUITE("FolderCrawler")
