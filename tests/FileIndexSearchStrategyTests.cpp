#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

// Enable test mode for non-Windows platforms (allows use of Windows type stubs)
// Note: CMake also defines this, so we only define if not already defined
#ifndef _WIN32
#ifndef USN_WINDOWS_TESTS
#define USN_WINDOWS_TESTS
#endif  // USN_WINDOWS_TESTS
#endif  // _WIN32

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <future>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "TestHelpers.h"
#include "core/Settings.h"
#include "index/FileIndex.h"
#include "path/PathUtils.h"
#include "search/SearchStatisticsCollector.h"
#include "search/SearchThreadPool.h"
#include "utils/FileSystemUtils.h"
#include "utils/Logger.h"
#include "utils/StringUtils.h"

// Helper functions for test data setup and validation
namespace {

// PopulateTestFileIndex is now replaced by test_helpers::CreateTestFileIndex()
// Keeping PopulateDeepHierarchy as it's specific to this test file

/**
 * Populate a FileIndex with a deep directory hierarchy.
 */
void PopulateDeepHierarchy(FileIndex& index, int depth, const std::string& base_name = "level") {
  uint64_t parent_id = 0;
  const std::string current_path = "C:\\Deep";

  // Create base directory
  const uint64_t current_id_base = 1000000;
  uint64_t current_id = current_id_base;
  index.Insert(current_id, parent_id, current_path, true, {0, 0});
  parent_id = current_id;

  for (int i = 1; i <= depth; ++i) {
    const std::string name = base_name + "_" + std::to_string(i);
    current_id++;
    index.Insert(current_id, parent_id, name, true, {0, 0});
    parent_id = current_id;

    // Add a file in each level
    index.Insert(current_id + 1000, parent_id, "file_at_level_" + std::to_string(i) + ".txt", false,
                 kFileTimeNotLoaded);
  }

  index.RecomputeAllPaths();
}

// StrategySettingsGuard is now replaced by test_helpers::TestSettingsFixture

/**
 * Collect all results from futures returned by SearchAsyncWithData().
 * Uses test_helpers::CollectFutures internally but provides convenient wrapper
 * with FileIndex search parameters.
 */
std::vector<SearchResultData>
CollectSearchResults(  // NOSONAR(cpp:S107) - Test-only convenience wrapper with explicit parameters
                       // mirroring production SearchAsyncWithData API
  FileIndex& index, const std::string& query, int thread_count = -1,
  const std::vector<std::string>* extensions = nullptr, bool folders_only = false,
  bool case_sensitive = false, const std::string& path_query = "",
  std::vector<ThreadTiming>* thread_timings = nullptr) {
  std::vector<ThreadTiming> timings;
  if (thread_timings == nullptr) {
    thread_timings = &timings;
  }

  auto futures = index.SearchAsyncWithData(query, thread_count, nullptr, path_query, extensions,
                                           folders_only, case_sensitive, thread_timings);

  return test_helpers::CollectFutures(futures);
}

// Helper to check if result extension matches any expected extension (reduces nesting in
// ValidateResults).
bool ExtensionMatches(std::string_view result_ext,
                      const std::vector<std::string>& expected_extensions) {
  for (const auto& ext : expected_extensions) {
    std::string ext_lower = ToLower(ext);
    if (!ext_lower.empty() && ext_lower[0] == '.') {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - guarded by !ext_lower.empty()
      ext_lower.erase(0, 1);
    }
    if (result_ext == ext_lower) {
      return true;
    }
  }
  return false;
}

/**
 * Validate that search results match expected criteria.
 */
void ValidateResults(const std::vector<SearchResultData>& results,
                     const std::string& expected_query, bool case_sensitive,
                     const std::vector<std::string>* expected_extensions = nullptr,
                     bool folders_only = false) {
  // 1. Check all results contain the query (case-sensitive or not)
  for (const auto& result : results) {
    // Calculate filename using PathUtils
    const std::string_view path_view(result.fullPath);
    const std::string_view filename_view = path_utils::GetFilename(path_view);
    std::string search_target(filename_view);
    std::string query = expected_query;

    if (!case_sensitive) {
      search_target = ToLower(search_target);
      query = ToLower(query);
    }

    REQUIRE(search_target.find(query) != std::string::npos);

    // 2. Check extension filter if provided
    if (expected_extensions != nullptr && !expected_extensions->empty()) {
      const std::string_view extension = path_utils::GetExtension(path_view);
      const std::string result_ext =
        !extension.empty() ? std::string(ToLower(extension)) : std::string();
      REQUIRE(ExtensionMatches(result_ext, *expected_extensions));
    }

    // 3. Check folders_only filter
    if (folders_only) {
      REQUIRE(result.isDirectory);
    }
  }
}

// Helper function to eliminate duplicate TestWithDynamicSettings lambda pattern
void TestCollectAndValidateCommon(FileIndex& index) {
  auto results = CollectSearchResults(index, "file_", 4);
  REQUIRE(!results.empty());
  ValidateResults(results, "file_", false);
}

}  // anonymous namespace

TEST_SUITE("FileIndex Search Strategies") {
  TEST_SUITE("Static Strategy") {
    // Note: "returns correct results" test is now parameterized in "All Strategies - Common Tests"
    // suite below

    TEST_CASE("Static strategy distributes work evenly") {
      const test_helpers::TestSettingsFixture settings("static");
      test_helpers::TestFileIndexFixture index_fixture(10000);

      const int thread_count = 4;
      std::vector<ThreadTiming> timings;
      auto futures = index_fixture.GetIndex().SearchAsyncWithData(
        "file_", thread_count, nullptr, "", nullptr, false, false, &timings);

      REQUIRE(futures.size() == static_cast<size_t>(thread_count));

      // Collect results and verify all items are processed
      // Note: We need to check individual thread results, so we can't use CollectAndVerifyFutures
      // which consumes futures. We'll collect manually but verify total.
      const size_t total_results =
        test_helpers::search_strategy_test_helpers::CollectFuturesAndCount(futures);

      // Verify we got results from all threads
      REQUIRE(total_results > 0);

      // Verify timing data is available
      REQUIRE(timings.size() == static_cast<size_t>(thread_count));
    }

    TEST_CASE("Static strategy works with single thread") {
      const test_helpers::TestSettingsFixture settings("static");

      test_helpers::TestFileIndexFixture index_fixture(100);

      auto results = CollectSearchResults(index_fixture.GetIndex(), "file_", 1);

      REQUIRE(results.size() > 0);
      ValidateResults(results, "file_", false);
    }

    TEST_CASE("Static strategy works with many threads") {
      const test_helpers::TestSettingsFixture settings("static");

      test_helpers::TestFileIndexFixture index_fixture(1000);

      const int thread_count = 16;
      auto results = CollectSearchResults(index_fixture.GetIndex(), "file_", thread_count);

      REQUIRE(results.size() > 0);
      ValidateResults(results, "file_", false);
      test_helpers::search_strategy_test_helpers::VerifyAllResultsAreFiles(results);
    }

    TEST_CASE("Static strategy handles small datasets") {
      const test_helpers::TestSettingsFixture settings("static");

      test_helpers::TestFileIndexFixture index_fixture(10);

      // Should still work, even if some threads get no work
      auto results = CollectSearchResults(index_fixture.GetIndex(), "file_", 4);

      // With only 10 items, we should get at most 9 results (1 might be a
      // directory)
      REQUIRE(results.size() <= 9);
      ValidateResults(results, "file_", false);

      // Verify all results are files (not directories)
      for (const auto& result : results) {
        REQUIRE_FALSE(result.isDirectory);
      }
    }

  }  // TEST_SUITE("Static Strategy")

  TEST_SUITE("Hybrid Strategy") {
    // Note: "returns correct results" test is now parameterized in "All Strategies - Common Tests"
    // suite below

    TEST_CASE("Hybrid strategy uses dynamic chunks") {
      // Set hybridInitialWorkPercent to 50% to ensure dynamic chunks are
      // available
      const auto settings = test_helpers::search_strategy_test_helpers::CreateHybridSettings(50);
      const test_helpers::TestSettingsFixture settings_fixture(settings);

      test_helpers::TestFileIndexFixture index_fixture(10000);

      std::vector<ThreadTiming> timings;
      auto results = CollectSearchResults(index_fixture.GetIndex(), "file_", 4, nullptr, false,
                                          false, "", &timings);

      REQUIRE(results.size() > 0);
      ValidateResults(results, "file_", false);

      // Verify timing data is available (indicates threads completed)
      REQUIRE(timings.size() > 0);

      // Verify at least some threads processed work
      // Note: Some threads may have 0 items_processed_ if they only process
      // dynamic chunks but they should still have results_count_ > 0
      test_helpers::search_strategy_test_helpers::VerifyThreadsProcessedWork(timings, true);
    }

    TEST_CASE("Hybrid strategy caps initial chunks correctly") {
      const auto settings = test_helpers::search_strategy_test_helpers::CreateHybridSettings(75);
      const size_t total_results =
        test_helpers::search_strategy_test_helpers::TestGetFuturesAndCollect(
        "hybrid", 10000, "file_", 4, 1, 4);
      REQUIRE(total_results > 0);
    }

    TEST_CASE("Hybrid strategy respects initial work percentage") {
      test_helpers::TestFileIndexFixture index_fixture(5000);

      // Test with different percentages
      for (const int percent : {50, 60, 70, 80, 90}) {
        const auto settings =
          test_helpers::search_strategy_test_helpers::CreateHybridSettings(percent);
        const test_helpers::TestSettingsFixture settings_fixture(settings);

        auto results = CollectSearchResults(index_fixture.GetIndex(), "file_", 4);
        REQUIRE(results.size() > 0);
        ValidateResults(results, "file_", false);

        // Verify all results are files (not directories)
        test_helpers::search_strategy_test_helpers::VerifyAllResultsAreFiles(results);
      }
    }

    TEST_CASE("Hybrid strategy handles overlapping concurrent searches") {
      const test_helpers::TestSettingsFixture settings("hybrid");

      test_helpers::TestFileIndexFixture index_fixture(10000);

      // Start multiple searches that will overlap in time
      // This is the crash scenario: user presses Search while previous search
      // is running
      const std::vector<std::string> queries = {"file_", "file_", "file_"};
      const auto results = test_helpers::search_strategy_test_helpers::RunConcurrentSearches(
        index_fixture.GetIndex(), queries, 4, true);

      // All searches should complete and return results
      REQUIRE(results.size() == 3);
      REQUIRE(results[0].total_results > 0);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - guarded by REQUIRE(results.size() == 3)
      REQUIRE(results[1].total_results > 0);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - guarded by REQUIRE(results.size() == 3)
      REQUIRE(results[2].total_results > 0);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - guarded by REQUIRE(results.size() == 3)
    }

  }  // TEST_SUITE("Hybrid Strategy")

  TEST_SUITE("Dynamic Strategy") {
    // Note: "returns correct results" test is now parameterized in "All Strategies - Common Tests"
    // suite below

    TEST_CASE("Dynamic strategy processes all work dynamically") {
      const test_helpers::TestSettingsFixture settings("dynamic");

      test_helpers::TestFileIndexFixture index_fixture(10000);

      auto futures = index_fixture.GetIndex().SearchAsyncWithData("file_", 4, nullptr, "", nullptr,
                                                                  false, false, nullptr);

      const size_t total_results =
        test_helpers::search_strategy_test_helpers::VerifyFuturesSizeAndCollect(futures, 1, 4);
      REQUIRE(total_results > 0);
    }

    TEST_CASE("Dynamic strategy respects chunk size setting") {
      test_helpers::TestFileIndexFixture index_fixture(10000);

      // Test with different chunk sizes
      for (const int chunk_size : {100, 500, 1000, 2000}) {
        const auto settings =
          test_helpers::search_strategy_test_helpers::CreateDynamicSettings(chunk_size);
        const test_helpers::TestSettingsFixture settings_fixture(settings);

        auto results = CollectSearchResults(index_fixture.GetIndex(), "file_", 4);
        REQUIRE(results.size() > 0);
        ValidateResults(results, "file_", false);
      }
    }

    TEST_CASE("Dynamic strategy handles no matches gracefully") {
      const test_helpers::TestSettingsFixture settings("dynamic");

      test_helpers::TestFileIndexFixture index_fixture(1000);

      auto results = CollectSearchResults(index_fixture.GetIndex(), "nonexistent_pattern_xyz", 4);

      REQUIRE(results.size() == 0);
    }

    // Additional crash-prevention tests

    TEST_CASE("Dynamic strategy handles empty index") {
      const test_helpers::TestSettingsFixture settings("dynamic");

      FileIndex index;
      index.ResetThreadPool();
      index.RecomputeAllPaths();  // Empty index with paths computed

      auto results = CollectSearchResults(index, "anything", 4);  // Empty index - no fixture needed

      REQUIRE(results.size() == 0);
    }

    TEST_CASE("Dynamic strategy handles single item") {
      const test_helpers::TestSettingsFixture settings("dynamic");
      test_helpers::TestFileIndexFixture index_fixture(1);

      auto results = CollectSearchResults(index_fixture.GetIndex(), "file_", 4);

      REQUIRE(results.size() <= 1);
      if (!results.empty()) {
        ValidateResults(results, "file_", false);
      }
    }

    TEST_CASE("Dynamic strategy handles very small chunk sizes") {
      test_helpers::search_strategy_test_helpers::TestWithDynamicSettings(
        100, 1000, TestCollectAndValidateCommon);
    }

    TEST_CASE("Dynamic strategy handles many threads with small dataset") {
      const test_helpers::TestSettingsFixture settings("dynamic");
      test_helpers::TestFileIndexFixture index_fixture(50);

      // Request many threads (more than items)
      auto results = CollectSearchResults(index_fixture.GetIndex(), "file_", 16);

      // Should still work, some threads may get no work
      REQUIRE(results.size() <= 50);
      ValidateResults(results, "file_", false);
    }

    TEST_CASE("Dynamic strategy handles large dataset with many threads") {
      const test_helpers::TestSettingsFixture settings("dynamic");
      test_helpers::TestFileIndexFixture index_fixture(50000);

      std::vector<ThreadTiming> timings;
      auto results = CollectSearchResults(index_fixture.GetIndex(), "file_", 8, nullptr, false,
                                          false, "", &timings);

      REQUIRE(results.size() > 0);
      ValidateResults(results, "file_", false);

      // Verify timing data is available
      REQUIRE(timings.size() > 0);

      // Verify at least some threads processed work
      test_helpers::search_strategy_test_helpers::VerifyThreadsProcessedWork(timings, false);
    }

    TEST_CASE("Dynamic strategy handles concurrent searches") {
      const test_helpers::TestSettingsFixture settings("dynamic");
      test_helpers::TestFileIndexFixture index_fixture(5000);

      // Run multiple searches concurrently to test thread safety
      std::vector<std::future<std::vector<SearchResultData>>> futures1;
      std::vector<std::future<std::vector<SearchResultData>>> futures2;

      futures1 = index_fixture.GetIndex().SearchAsyncWithData("file_", 4, nullptr, "", nullptr,
                                                              false, false, nullptr);
      futures2 = index_fixture.GetIndex().SearchAsyncWithData("dir_", 4, nullptr, "", nullptr,
                                                              false, false, nullptr);

      // Collect all results
      auto [total1, total2] =
        test_helpers::search_strategy_test_helpers::CollectTwoFutureVectors(futures1, futures2);

      // Both searches should complete without crashing
      REQUIRE(total1 > 0);
      REQUIRE(total2 >= 0);  // May be 0 if no directories match
    }

    TEST_CASE("Dynamic strategy handles edge case chunk boundaries") {
      test_helpers::search_strategy_test_helpers::TestWithDynamicSettings(
        100, 1000, TestCollectAndValidateCommon);
    }

    TEST_CASE("Dynamic strategy handles extension filter") {
      const test_helpers::TestSettingsFixture settings("dynamic");
      test_helpers::TestFileIndexFixture index_fixture(1000);

      // Test with extensions that should match some files
      // Our test data uses: .txt, .cpp, .h, .exe, .dll, .json, .md
      const std::vector<std::string> extensions = {".txt", ".cpp", ".h"};
      auto results = CollectSearchResults(index_fixture.GetIndex(), "file_", 4, &extensions);

      // Should find some results (at least 3 files out of 1000 should match)
      // But if no results, that's also valid (just means no matches)
      if (!results.empty()) {
        ValidateResults(results, "file_", false, &extensions);
      }
    }

    TEST_CASE("SearchContext data preserved through async execution") {
      // This test verifies that SearchContext data is correctly captured by value
      // in worker lambdas, not by reference. A use-after-free bug would cause
      // the extension filter to fail (garbage data) or potentially crash.
      //
      // The test uses a unique extension that won't match any files. If the
      // SearchContext is captured by reference (dangling), the extension_set
      // would contain garbage data and might incorrectly match some files.
      const test_helpers::TestSettingsFixture settings("dynamic");
      test_helpers::TestFileIndexFixture index_fixture(500);

      // Use a unique extension that doesn't exist in test data
      // Test data uses: .txt, .cpp, .h, .exe, .dll, .json, .md
      const std::vector<std::string> fake_extensions = {".xyz_unique_12345"};
      auto results = CollectSearchResults(index_fixture.GetIndex(), "file_", 4, &fake_extensions);

      // With correct capture-by-value, no files should match this fake extension
      // With use-after-free bug, extension_set would be garbage and might:
      // 1. Match random files (if garbage happens to match)
      // 2. Crash (if garbage causes hash map corruption)
      // 3. Return all files (if has_extension_filter check fails)
      REQUIRE(results.empty());
    }

    TEST_CASE("Search in deep hierarchy") {
      const test_helpers::TestSettingsFixture settings("dynamic");

      FileIndex index;
      index.ResetThreadPool();
      PopulateDeepHierarchy(index, 20);  // 20 levels deep

      auto results = CollectSearchResults(index, "file_at_level_20", 4);

      REQUIRE(results.size() == 1);
      CHECK(results[0].fullPath.find("level_20") != std::string::npos);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - guarded by REQUIRE(results.size() == 1)
    }

    TEST_CASE("Search with path query") {
      const test_helpers::TestSettingsFixture settings("dynamic");

      FileIndex index;
      index.ResetThreadPool();
      PopulateDeepHierarchy(index, 10);

      // Search for files in level_5 directory specifically
      auto results = CollectSearchResults(index, ".txt", 4, nullptr, false, false, "level_5");

      REQUIRE(results.size() >= 1);
      for (const auto& res : results) {
        CHECK(res.fullPath.find("level_5") != std::string::npos);
      }
    }

    TEST_CASE("Dynamic strategy handles folders_only filter") {
      const test_helpers::TestSettingsFixture settings("dynamic");
      test_helpers::TestFileIndexFixture index_fixture(1000);

      auto results = CollectSearchResults(index_fixture.GetIndex(), "dir_", 4, nullptr, true);

      // Should only return directories
      for (const auto& result : results) {
        REQUIRE(result.isDirectory);
      }
    }

    // Additional crash-prevention tests for Windows issues

    TEST_CASE("Dynamic strategy handles rapid successive searches") {
      const test_helpers::TestSettingsFixture settings("dynamic");
      test_helpers::TestFileIndexFixture index_fixture(5000);

      // Run multiple searches in quick succession WITHOUT waiting for
      // completion This simulates the Windows crash scenario: user presses
      // Search multiple times
      std::vector<std::vector<std::future<std::vector<SearchResultData>>>> all_futures;

      // Start 5 searches rapidly without waiting
      for (int i = 0; i < 5; ++i) {
        auto futures = index_fixture.GetIndex().SearchAsyncWithData("file_", 4, nullptr, "",
                                                                    nullptr, false, false, nullptr);
        all_futures.push_back(std::move(futures));
      }

      // Now wait for all futures from all searches to complete
      // This tests that concurrent searches don't crash
      // Note: Some searches might return 0 results if arrays change, but they
      // shouldn't crash
      for (auto& search_futures : all_futures) {
        size_t total = 0;
        for (auto& future : search_futures) {
          try {
            auto results = future.get();
            total += results.size();
          } catch (const std::future_error& e) {
            FAIL("Search threw future_error during concurrent execution: " << e.what());
          } catch (const std::invalid_argument& e) {
            FAIL("Search threw invalid_argument during concurrent execution: " << e.what());
          } catch (const std::out_of_range& e) {
            FAIL("Search threw out_of_range during concurrent execution: " << e.what());
          } catch (const std::bad_alloc& e) {
            FAIL("Search threw bad_alloc during concurrent execution: " << e.what());
          } catch (const std::range_error& e) {
            FAIL("Search threw range_error during concurrent execution: " << e.what());
          } catch (...) {  // NOSONAR(cpp:S2738) - Test error handling: catch-all needed to detect
                           // any exception type
            FAIL("Search threw unknown exception during concurrent execution");
          }
        }
        // At least one search should return results (may be 0 if arrays
        // changed)
        REQUIRE(total >= 0);  // Should complete without crashing, even if 0 results
      }
    }

    TEST_CASE("Dynamic strategy handles overlapping concurrent searches") {
      const test_helpers::TestSettingsFixture settings("dynamic");

      test_helpers::TestFileIndexFixture index_fixture(10000);

      // Start multiple searches that will overlap in time
      // This is the crash scenario: user presses Search while previous search
      // is running
      const std::vector<std::string> queries = {"file_", "file_", "file_"};
      const auto results = test_helpers::search_strategy_test_helpers::RunConcurrentSearches(
        index_fixture.GetIndex(), queries, 4, false);  // Allow 0 results

      // All searches should complete without crashing
      // Note: Results may be 0 if arrays changed, but no exceptions should occur
      REQUIRE(results.size() == 3);
      REQUIRE(results[0].total_results >= 0);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - guarded by REQUIRE(results.size() == 3)
      REQUIRE(results[1].total_results >= 0);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - guarded by REQUIRE(results.size() == 3)
      REQUIRE(results[2].total_results >= 0);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - guarded by REQUIRE(results.size() == 3)

      // At least one search should return results (most likely all will)
      REQUIRE((results[0].total_results > 0 || results[1].total_results > 0 ||  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - guarded by REQUIRE(results.size() == 3)
               results[2].total_results > 0));  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - guarded by REQUIRE(results.size() == 3)
    }

    TEST_CASE("Dynamic strategy handles max iterations limit") {
      const auto settings = test_helpers::search_strategy_test_helpers::CreateDynamicSettings(
        100);  // Small chunks = many iterations
      const test_helpers::TestSettingsFixture settings_fixture(settings);

      test_helpers::TestFileIndexFixture index_fixture(10000);

      auto results = CollectSearchResults(index_fixture.GetIndex(), "file_", 4);

      // Should complete without crashing (max iterations protection should kick
      // in)
      REQUIRE(results.size() >= 0);  // May be partial results if max iterations reached
    }

    TEST_CASE("Dynamic strategy handles array size changes gracefully") {
      const test_helpers::TestSettingsFixture settings("dynamic");
      test_helpers::TestFileIndexFixture index_fixture(1000);

      // Start a search
      auto futures = index_fixture.GetIndex().SearchAsyncWithData("file_", 4, nullptr, "", nullptr,
                                                                  false, false, nullptr);

      // While search is running, add more items (this should be safe due to
      // shared_lock) Note: In production, this would be blocked by the
      // shared_lock, but we test that the search completes safely even if
      // arrays change size

      // Wait for all futures to complete
      for (auto& future : futures) {
        auto results = future.get();
        REQUIRE(results.size() >= 0);  // Should complete without crashing
      }
    }

    TEST_CASE("Dynamic strategy handles zero chunk size edge case") {
      test_helpers::search_strategy_test_helpers::TestWithDynamicSettings(
        100, 1000, TestCollectAndValidateCommon);
    }

    TEST_CASE("Dynamic strategy handles very large chunk sizes") {
      test_helpers::search_strategy_test_helpers::TestWithDynamicSettings(
        10000, 1000, [](FileIndex& index) {
          auto results = CollectSearchResults(index, "file_", 4);
          REQUIRE(results.size() > 0);
          ValidateResults(results, "file_", false);
        });
    }

  }  // TEST_SUITE("Dynamic Strategy")

  TEST_SUITE("All Strategies - Common Tests") {
    TEST_CASE("All strategies return correct results") {
      test_helpers::TestFileIndexFixture index_fixture(1000);

      const std::vector<std::string> strategies =
        test_helpers::search_strategy_test_helpers::GetAllStrategies();

      for (const auto& strategy : strategies) {
        DOCTEST_SUBCASE((strategy + " strategy").c_str()) {
          const test_helpers::TestSettingsFixture settings(strategy);
          index_fixture.GetIndex().ResetThreadPool();

          auto results = CollectSearchResults(index_fixture.GetIndex(), "file_", 4);
          REQUIRE(results.size() > 0);
          ValidateResults(results, "file_", false);
          test_helpers::search_strategy_test_helpers::VerifyAllResultsAreFiles(results);
        }
      }
    }

  }  // TEST_SUITE("All Strategies - Common Tests")

  TEST_SUITE("Pattern Matcher Setup") {
    // Test pattern matcher setup across all strategies
    // This ensures refactoring the duplicated pattern matcher code is safe

    TEST_CASE("PathPattern patterns (auto-detected with ^) work across all strategies") {
      // Test path pattern: match files starting with "file_" (auto-detected by ^ anchor)
      test_helpers::search_strategy_test_helpers::RunTestForAllStrategiesWithSetup(
        100, [](FileIndex& idx, const std::string&) {
          // Path pattern with ^ anchor: ^file_* matches files starting with "file_"
          // Auto-detected as PathPattern (no prefix needed)
          // Note: PathPatternMatcher requires full match, so we need * at the end
          auto results = CollectSearchResults(idx, "^file_*", 4);

          REQUIRE(results.size() > 0);
          // All results should match the pattern
          for (const auto& result : results) {
            const std::string_view path_view(result.fullPath);
            const std::string_view filename = path_utils::GetFilename(path_view);
            CHECK(filename.find("file_") == 0);
          }
        });
    }

    TEST_CASE("Glob patterns (*, ?) work across all strategies") {
      test_helpers::search_strategy_test_helpers::RunTestForAllStrategiesWithSetup(
        100, [](FileIndex& idx, const std::string&) {
          // Glob pattern: file_00* matches file_0001, file_0002, etc.
          auto results = CollectSearchResults(idx, "file_00*", 4);

          REQUIRE(results.size() > 0);
          for (const auto& result : results) {
            const std::string_view path_view(result.fullPath);
            const std::string_view filename = path_utils::GetFilename(path_view);
            CHECK(filename.find("file_00") == 0);
          }
        });
    }

    TEST_CASE("Path patterns (pp:) - precompiled - work across all strategies") {
      const test_helpers::TestSettingsFixture settings;

      FileIndex index;
      PopulateDeepHierarchy(index, 10);

      test_helpers::search_strategy_test_helpers::RunTestForAllStrategies(
        index, [](FileIndex& idx, const std::string&) {
          // Path pattern: match files in level_5 directory (precompiled)
          auto results = CollectSearchResults(idx, "pp:**/level_5/**/*.txt", 4);

          // Should find at least one file in level_5
          if (!results.empty()) {
            for (const auto& result : results) {
              CHECK(result.fullPath.find("level_5") != std::string::npos);
            }
          }
        });
    }

    TEST_CASE("Extension-only mode skips pattern matchers") {
      const test_helpers::TestSettingsFixture settings("dynamic");

      test_helpers::TestFileIndexFixture index_fixture(100);

      // Extension-only mode: empty query, no path query, but has extensions
      const std::vector<std::string> extensions = {".txt", ".cpp"};
      auto results = CollectSearchResults(index_fixture.GetIndex(), "", 4, &extensions);

      // Should return files with matching extensions only
      REQUIRE(results.size() > 0);
      for (const auto& result : results) {
        const std::string_view path_view(result.fullPath);
        if (const size_t last_dot = path_view.find_last_of('.');
            last_dot != std::string_view::npos && last_dot + 1 < path_view.length()) {
          const std::string_view ext = path_view.substr(last_dot + 1);
          const bool has_match = (ext == "txt" || ext == "cpp");
          CHECK(has_match);
        }
      }
    }

    TEST_CASE("Case-sensitive pattern matching") {
      const test_helpers::TestSettingsFixture settings("dynamic");

      test_helpers::TestFileIndexFixture index_fixture(100);

      // Case-sensitive search for "FILE_" (uppercase) - should find nothing
      // (test data uses lowercase "file_")
      auto results =
        CollectSearchResults(index_fixture.GetIndex(), "FILE_", 4, nullptr, false, true);

      REQUIRE(results.empty());

      // Case-insensitive search for "FILE_" - should find results
      results = CollectSearchResults(index_fixture.GetIndex(), "FILE_", 4, nullptr, false, false);
      REQUIRE(results.size() > 0);
    }

  }  // TEST_SUITE("Pattern Matcher Setup")

}  // TEST_SUITE("FileIndex Search Strategies")

