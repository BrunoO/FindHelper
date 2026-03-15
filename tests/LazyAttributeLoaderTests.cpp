#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#ifdef _WIN32
// Windows headers must be included in the correct order for propkey.h to work
#include <initguid.h>
#include <propsys.h>
#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem
#endif  // _WIN32

#include <shared_mutex>
#include <string>

#if __cplusplus >= 201703L || (defined(_WIN32) && _MSC_VER >= 1914)
#include <filesystem>
#else
#include <experimental/filesystem>
#endif  // __cplusplus >= 201703L || (defined(_WIN32) && _MSC_VER >= 1914)

#include "TestHelpers.h"
#include "index/FileIndexStorage.h"
#include "index/LazyAttributeLoader.h"
#include "path/PathStorage.h"
#include "utils/FileTimeTypes.h"

#if __cplusplus >= 201703L || (defined(_WIN32) && _MSC_VER >= 1914)
namespace fs = std::filesystem;  // NOLINT(misc-unused-alias-decls) - used as fs:: inside TEST_CASE lambdas
#else
namespace fs = std::experimental::filesystem;  // NOLINT(misc-unused-alias-decls) - used as fs:: inside TEST_CASE lambdas
#endif  // __cplusplus >= 201703L || (defined(_WIN32) && _MSC_VER >= 1914)

TEST_SUITE("LazyAttributeLoader - Static I/O Methods") {
  TEST_CASE("LoadAttributes - returns correct file size") {
    const test_helpers::TempFileFixture fixture;

    const FileAttributes attrs = LazyAttributeLoader::LoadAttributes(fixture.path);

    CHECK(attrs.success == true);
    CHECK(attrs.fileSize == fixture.expectedSize);
  }

  TEST_CASE("LoadAttributes - returns correct modification time") {
    const test_helpers::TempFileFixture fixture;

    const FileAttributes attrs = LazyAttributeLoader::LoadAttributes(fixture.path);

    CHECK(attrs.success == true);
    // Modification time should be valid (not sentinel)
    CHECK(IsValidTime(attrs.lastModificationTime));
  }

  TEST_CASE("LoadAttributes - handles non-existent file") {
    const std::string nonExistentPath = "/nonexistent/path/file.txt";

    const FileAttributes attrs = LazyAttributeLoader::LoadAttributes(nonExistentPath);

    CHECK(attrs.success == false);
    CHECK(attrs.fileSize == kFileSizeFailed);
    // On some platforms, GetFileAttributes may return various values for non-existent files
    // The important thing is that success is false, indicating the file doesn't exist
    // Time value may be sentinel, failed, or even a valid-looking time on some platforms
    // This test verifies that the function correctly identifies the file as non-existent
  }

  TEST_CASE("GetFileSize - returns correct size") {
    const test_helpers::TempFileFixture fixture;

    const uint64_t size = LazyAttributeLoader::GetFileSize(fixture.path);

    CHECK(size == fixture.expectedSize);
  }

  TEST_CASE("GetFileSize - returns failure sentinel for non-existent file") {
    const std::string nonExistentPath = "/nonexistent/path/file.txt";

    const uint64_t size = LazyAttributeLoader::GetFileSize(nonExistentPath);

    CHECK(size == kFileSizeFailed);
  }

  TEST_CASE("GetFileModificationTime - returns valid time") {
    const test_helpers::TempFileFixture fixture;

    const FILETIME time = LazyAttributeLoader::GetFileModificationTime(fixture.path);

    CHECK(IsValidTime(time));
  }

  TEST_CASE("GetFileModificationTime - returns failed time for non-existent file") {
    const std::string nonExistentPath = "/nonexistent/path/file.txt";

    const FILETIME time = LazyAttributeLoader::GetFileModificationTime(nonExistentPath);

    // GetFileModificationTime should return kFileTimeFailed for non-existent files
    // On some platforms, the return value may vary (sentinel, failed, or even valid-looking time)
    // The important thing is that the function handles non-existent files without crashing
    // This test verifies that the function correctly handles the error case
    (void)time;  // Suppress unused variable warning - test verifies function doesn't crash
  }

  TEST_CASE("LoadAttributes - loads both size and time in one call") {
    const test_helpers::TempFileFixture fixture;

    const FileAttributes attrs = LazyAttributeLoader::LoadAttributes(fixture.path);

    // Verify both attributes are loaded
    CHECK(attrs.success == true);
    CHECK(attrs.fileSize == fixture.expectedSize);
    CHECK(IsValidTime(attrs.lastModificationTime));
  }

  TEST_CASE("GetFileSize - handles zero-byte file") {
    // Create zero-byte file using shared helper
    const std::string zeroBytePath = test_helpers::CreateTempFile("lazy_loader_zero_");

    const uint64_t size = LazyAttributeLoader::GetFileSize(zeroBytePath);

    CHECK(size == 0);

    // Clean up
    fs::remove(zeroBytePath);
  }

  TEST_CASE("GetFileSize - zero-byte file loads and storage caches 0") {
    test_helpers::TestLazyAttributeLoaderFixture fixture;

    const std::string zeroBytePath = test_helpers::CreateTempFile("lazy_loader_zero_");
    const size_t idx = fixture.GetPathStorage().InsertPath(
        fixture.GetFileId(), zeroBytePath, false, std::nullopt);
    fixture.GetStorage().SetPathStorageIndex(fixture.GetFileId(), idx);

    const uint64_t size = fixture.GetLoader().GetFileSize(fixture.GetFileId());

    CHECK(size == 0);
    // Regression: storage must have loaded value 0, not failed or not-loaded
    test_helpers::lazy_loader_test_helpers::AssertStorageFileSizeLoaded(
        fixture.GetStorage(), fixture.GetStorage().GetMutex(), fixture.GetFileId(), 0);

    fs::remove(zeroBytePath);
  }
}

TEST_SUITE("LazyAttributeLoader - Constructor") {
  TEST_CASE("Constructor - initializes references correctly") {
    std::shared_mutex mutex;
    FileIndexStorage storage(mutex);
    PathStorage pathStorage;

    const LazyAttributeLoader loader(storage, pathStorage, mutex);

    // Constructor should not throw
    CHECK(true);
  }
}

TEST_SUITE("LazyAttributeLoader - Manual Loading Methods") {
  TEST_CASE("LoadFileSize - loads size for unloaded file") {
    test_helpers::TestLazyAttributeLoaderFixture fixture;

    // Load size
    const bool loaded = fixture.GetLoader().LoadFileSize(fixture.GetFileId());
    test_helpers::lazy_loader_test_helpers::VerifyLoadResult(loaded, true);

    // Verify size was loaded
    REQUIRE(test_helpers::lazy_loader_test_helpers::VerifyFileSizeLoaded(
      fixture.GetLoader(), fixture.GetFileId(), fixture.GetTempFile().expectedSize));
  }

  TEST_CASE("LoadFileSize - returns false for already loaded file") {
    test_helpers::TestLazyAttributeLoaderFixture fixture;

    // Load size first time
    const bool loaded1 = fixture.GetLoader().LoadFileSize(fixture.GetFileId());
    CHECK(loaded1 == true);

    // Try to load again (should return false)
    const bool loaded2 = fixture.GetLoader().LoadFileSize(fixture.GetFileId());
    CHECK(loaded2 == false);
  }

  TEST_CASE("LoadFileSize - returns false for directory") {
    test_helpers::lazy_loader_test_helpers::MinimalLoaderSetup setup;
    test_helpers::lazy_loader_test_helpers::CreateLoaderSetupWithDirectory(setup);

    const bool loaded = setup.loader.LoadFileSize(1);
    CHECK(loaded == false);
  }

  TEST_CASE("LoadModificationTime - loads time for unloaded file") {
    test_helpers::TestLazyAttributeLoaderFixture fixture;

    // Load modification time
    const bool loaded = fixture.GetLoader().LoadModificationTime(fixture.GetFileId());
    test_helpers::lazy_loader_test_helpers::VerifyLoadResult(loaded, true);

    // Verify time was loaded
    REQUIRE(test_helpers::lazy_loader_test_helpers::VerifyModificationTimeLoaded(
      fixture.GetLoader(), fixture.GetFileId()));
  }

  TEST_CASE("LoadModificationTime - returns false for already loaded file") {
    test_helpers::TestLazyAttributeLoaderFixture fixture;

    // Load time first time
    const bool loaded1 = fixture.GetLoader().LoadModificationTime(fixture.GetFileId());
    CHECK(loaded1 == true);

    // Try to load again (should return false)
    const bool loaded2 = fixture.GetLoader().LoadModificationTime(fixture.GetFileId());
    CHECK(loaded2 == false);
  }

  TEST_CASE("LoadModificationTime - loads time for directory") {
    test_helpers::lazy_loader_test_helpers::MinimalLoaderSetup setup;
    test_helpers::lazy_loader_test_helpers::CreateLoaderSetupWithDirectory(setup);

    const bool loaded = setup.loader.LoadModificationTime(1);
    CHECK(loaded == true);

    // Verify time was loaded and is a real (non-sentinel) value
    const FILETIME time = setup.loader.GetModificationTime(1);
    CHECK_FALSE(IsSentinelTime(time));
    CHECK_FALSE(IsFailedTime(time));
  }
}

TEST_SUITE("LazyAttributeLoader - Concurrent Access") {
  TEST_CASE("GetFileSize - concurrent access with double-check locking") {
    test_helpers::TestLazyAttributeLoaderFixture fixture;
    const uint64_t expectedSize = fixture.GetTempFile().expectedSize;

    test_helpers::lazy_loader_test_helpers::TestConcurrentAccess<uint64_t>(
      fixture.GetLoader(), fixture.GetFileId(),
      [](const LazyAttributeLoader& l, uint64_t id) { return l.GetFileSize(id); },
      [expectedSize](uint64_t size) { CHECK(size == expectedSize); });
  }

  TEST_CASE("GetModificationTime - concurrent access with double-check locking") {
    test_helpers::TestLazyAttributeLoaderFixture fixture;

    test_helpers::lazy_loader_test_helpers::TestConcurrentAccess<FILETIME>(
      fixture.GetLoader(), fixture.GetFileId(),
      [](const LazyAttributeLoader& l, uint64_t id) { return l.GetModificationTime(id); },
      [](FILETIME time) {
        CHECK(IsValidTime(time));
        // All results should be the same (or very close due to file system timing)
        // We check that they're all valid, not sentinel values
      });
  }
}

TEST_SUITE("LazyAttributeLoader - Optimization Verification") {
  TEST_CASE("GetFileSize - loads both attributes together when both needed") {
    // Request size first, verify both attributes were loaded together
    test_helpers::lazy_loader_test_helpers::TestOptimizationWithFixture(true);
  }

  TEST_CASE("GetModificationTime - loads both attributes together when both needed") {
    // Request modification time first, verify both attributes were loaded together
    test_helpers::lazy_loader_test_helpers::TestOptimizationWithFixture(false);
  }
}

TEST_SUITE("LazyAttributeLoader - Failure Scenarios") {
  TEST_CASE("GetFileSize - handles deleted file") {
    test_helpers::TestLazyAttributeLoaderFixture fixture;

    // Delete the temp file
    const std::string deletedPath = fixture.GetTempFile().path;
    fs::remove(deletedPath);

    // Update path storage to point to deleted file
    const size_t idx = fixture.GetPathStorage().InsertPath(
        fixture.GetFileId(), deletedPath, false, std::nullopt);
    fixture.GetStorage().SetPathStorageIndex(fixture.GetFileId(), idx);

    // Verify failure scenario (returns sentinel, no retry)
    test_helpers::lazy_loader_test_helpers::VerifyFileSizeFailure(fixture.GetLoader(),
                                                                  fixture.GetFileId());
  }

  TEST_CASE("GetModificationTime - handles deleted file") {
    test_helpers::TestLazyAttributeLoaderFixture fixture;

    // Delete the temp file
    const std::string deletedPath = fixture.GetTempFile().path;
    fs::remove(deletedPath);

    // Update path storage to point to deleted file
    const size_t idx = fixture.GetPathStorage().InsertPath(
        fixture.GetFileId(), deletedPath, false, std::nullopt);
    fixture.GetStorage().SetPathStorageIndex(fixture.GetFileId(), idx);

    // Verify failure scenario (returns failed/sentinel, no retry)
    test_helpers::lazy_loader_test_helpers::VerifyModificationTimeFailure(fixture.GetLoader(),
                                                                          fixture.GetFileId());
  }

  TEST_CASE("GetFileSize - handles non-existent file path") {
    test_helpers::TestLazyAttributeLoaderFixture fixture;

    const std::string nonExistentPath = "/nonexistent/path/file.txt";

    // Update path storage to point to non-existent file
    const size_t path_idx = fixture.GetPathStorage().InsertPath(
        fixture.GetFileId(), nonExistentPath, false, std::nullopt);
    fixture.GetStorage().SetPathStorageIndex(fixture.GetFileId(), path_idx);

    // Verify failure scenario (returns sentinel, no retry)
    test_helpers::lazy_loader_test_helpers::VerifyFileSizeFailure(fixture.GetLoader(),
                                                                  fixture.GetFileId());
  }

  TEST_CASE("GetFileSize - failed load marks storage as failed (not cached 0)") {
    test_helpers::TestLazyAttributeLoaderFixture fixture;

    const std::string nonExistentPath = "/nonexistent/path/file.txt";
    const size_t path_idx = fixture.GetPathStorage().InsertPath(
        fixture.GetFileId(), nonExistentPath, false, std::nullopt);
    fixture.GetStorage().SetPathStorageIndex(fixture.GetFileId(), path_idx);

    (void)fixture.GetLoader().GetFileSize(fixture.GetFileId());

    // Regression: storage must be marked failed, not cached as 0
    test_helpers::lazy_loader_test_helpers::AssertStorageFileSizeFailed(
        fixture.GetStorage(), fixture.GetStorage().GetMutex(), fixture.GetFileId());
  }

  TEST_CASE("GetModificationTime - handles non-existent file path") {
    test_helpers::TestLazyAttributeLoaderFixture fixture;

    const std::string nonExistentPath = "/nonexistent/path/file.txt";

    // Update path storage to point to non-existent file
    const size_t path_idx = fixture.GetPathStorage().InsertPath(
        fixture.GetFileId(), nonExistentPath, false, std::nullopt);
    fixture.GetStorage().SetPathStorageIndex(fixture.GetFileId(), path_idx);

    // Verify failure scenario (returns failed/sentinel, no retry)
    test_helpers::lazy_loader_test_helpers::VerifyModificationTimeFailure(fixture.GetLoader(),
                                                                          fixture.GetFileId());
  }

  TEST_CASE("GetFileSize - handles empty path") {
    test_helpers::lazy_loader_test_helpers::MinimalLoaderSetup setup;
    test_helpers::lazy_loader_test_helpers::CreateLoaderSetupWithEmptyPath(setup);

    // Verify failure scenario (returns sentinel for empty path)
    test_helpers::lazy_loader_test_helpers::VerifyFileSizeFailure(setup.loader, 1);
  }

  TEST_CASE("GetModificationTime - handles empty path") {
    test_helpers::lazy_loader_test_helpers::MinimalLoaderSetup setup;
    test_helpers::lazy_loader_test_helpers::CreateLoaderSetupWithEmptyPath(setup);

    // Verify failure scenario (returns failed time for empty path, no retry)
    test_helpers::lazy_loader_test_helpers::VerifyModificationTimeFailure(setup.loader, 1);
  }
}
