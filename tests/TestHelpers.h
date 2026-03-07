#pragma once

#include "index/FileIndex.h"
#include "search/SearchContext.h"
#include "core/Settings.h"
#include "utils/FileTimeTypes.h"
#include "utils/FileAttributeConstants.h"
#include "path/PathUtils.h"
#include "path/DirectoryResolver.h"
#include "index/FileIndexStorage.h"
#include "index/LazyAttributeLoader.h"
#include "index/FileIndexMaintenance.h"
#include "search/ParallelSearchEngine.h"
#include "search/SearchThreadPool.h"
#include "search/SearchPatternUtils.h"
#include "MockSearchableIndex.h"
#include "crawler/IndexOperations.h"
#include "path/PathOperations.h"
#include "path/PathStorage.h"
#include "utils/StdRegexUtils.h"
#include "utils/FileSystemUtils.h"
#include "api/GeminiApiUtils.h"
#include "doctest/doctest.h"
#include <string>
#include <string_view>
#include <vector>
#include <future>
#include <stdexcept>
#include <atomic>
#include <shared_mutex>
#include <mutex>
#include <fstream>
#include <set>

#if __cplusplus >= 201703L || (defined(_WIN32) && _MSC_VER >= 1914)
#include <filesystem>
namespace fs = std::filesystem;
#else
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#endif  // __cplusplus >= 201703L || (defined(_WIN32) && _MSC_VER >= 1914)

#ifdef _WIN32
#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem
#else
#include <unistd.h>  // NOSONAR(cpp:S954) - System include, must be after conditional compilation check
#endif  // _WIN32

/**
 * Test helper utilities to reduce code duplication across test files.
 * 
 * This header provides common test setup functions, fixtures, and utilities
 * that are used across multiple test files.
 * 
 * ## Categories of Helpers
 * 
 * ### 1. Fixtures (RAII Test Setup)
 * - `TestSettingsFixture` - Manages test settings with automatic cleanup
 * - `TestFileIndexFixture` - Creates and populates FileIndex for tests
 * 
 * ### 2. Factory Functions
 * - `CreateTestFileIndex()` - Populates FileIndex with test data
 * - `CreateValidatedSearchContext()` - Creates SearchContext with validation
 * - `CreateSearchContextWithExtensions()` - Creates SearchContext with extensions
 * - `CreateSimpleSearchContext()` - Creates basic SearchContext
 * - `CreateExtensionSearchContext()` - Creates extension-only SearchContext
 * - `CreateGeminiJsonResponse()` - Creates Gemini API JSON response wrapper
 * 
 * ### 3. Test Data Generators
 * - `GenerateTestPaths()` - Generates predictable test paths
 * - `GenerateTestExtensions()` - Generates test extensions
 * 
 * ### 4. Assertion Helpers
 * - `ValidateSearchResults()` - Validates search results match criteria
 * - `AllResultsAreFiles()` - Checks all results are files
 * - `AllResultsAreDirectories()` - Checks all results are directories
 * 
 * ### 5. Path Truncation Helpers
 * - `GetPlatformSpecificPath()` - Returns platform-specific paths
 * - `CheckPathWidthWithinLimit()` - Validates path width
 * - `CheckPathPrefix()` - Validates path prefix
 * - `CheckPathContainsEllipsis()` - Validates ellipsis presence
 * - `CheckPathContainsSuffix()` - Validates path suffix
 * 
 * ### 6. Utility Functions
 * - `CreateTempFile()` - Creates temporary files
 * - `CollectFutures()` - Collects results from futures
 * 
 * ## Usage Examples
 * 
 * ### Using TestFileIndexFixture
 * ```cpp
 * TEST_CASE("My test") {
 *   test_helpers::TestSettingsFixture settings("static");
 *   test_helpers::TestFileIndexFixture index_fixture(1000);
 *   
 *   auto results = CollectSearchResults(index_fixture.GetIndex(), "file_", 4);
 *   REQUIRE(results.size() > 0);
 * }
 * ```
 * 
 * ### Using SearchContext Helpers
 * ```cpp
 * TEST_CASE("SearchContext validation") {
 *   auto ctx = test_helpers::CreateValidatedSearchContext(1000, 75);
 *   CHECK(ctx.dynamic_chunk_size == 1000);
 *   CHECK(ctx.hybrid_initial_percent == 75);
 * }
 * ```
 * 
 * ### Using Assertion Helpers
 * ```cpp
 * TEST_CASE("Search results validation") {
 *   auto results = PerformSearch(...);
 *   REQUIRE(test_helpers::ValidateSearchResults(results, "file_", false));
 *   REQUIRE(test_helpers::AllResultsAreFiles(results));
 * }
 * ```
 */
namespace test_helpers {

/**
 * Create a temporary file with a unique name.
 * 
 * @param prefix Prefix for the temporary file name (e.g., "test_", "lazy_loader_")
 * @return Path to the created temporary file
 * @throws std::runtime_error if file creation fails
 */
std::string CreateTempFile(std::string_view prefix);

/**
 * Create a secure temporary directory path.
 * 
 * Security: Uses mkdtemp() on Unix-like systems to create a unique directory name
 * atomically, preventing symlink attacks and TOCTOU vulnerabilities.
 * 
 * @param prefix Prefix for the temporary directory name (e.g., "test_dir_")
 * @return Path to a unique temporary directory (directory is created)
 * @throws std::runtime_error if directory creation fails
 */
std::string CreateTempDirectory(std::string_view prefix);

/**
 * RAII fixture for managing test settings.
 * 
 * Automatically sets in-memory settings in constructor and clears them in destructor.
 * Can optionally set a specific load balancing strategy.
 */
class TestSettingsFixture {  // NOSONAR(cpp:S3624) - Move constructor and move assignment operator are defined as = default (see lines 175-176)
public:
  /**
   * Create fixture with default settings.
   */
  explicit TestSettingsFixture();

  /**
   * Create fixture with specific settings.
   */
  explicit TestSettingsFixture(const AppSettings& settings);

  /**
   * Create fixture with a specific load balancing strategy.
   * 
   * @param strategy Strategy name (e.g., "static", "dynamic", "hybrid", "interleaved")
   */
  explicit TestSettingsFixture(std::string_view strategy);

  /**
   * Destructor: Restores original settings.
   */
  ~TestSettingsFixture();

  // Non-copyable, movable
  TestSettingsFixture(const TestSettingsFixture&) = delete;
  TestSettingsFixture& operator=(const TestSettingsFixture&) = delete;
  TestSettingsFixture(TestSettingsFixture&&) noexcept = default;  // NOSONAR(cpp:S3624) - Default move is correct (simple RAII fixture)
  TestSettingsFixture& operator=(TestSettingsFixture&&) noexcept = default;  // NOSONAR(cpp:S3624) - Default move is correct (simple RAII fixture)

private:
  std::string old_strategy_;
  int old_chunk_size_;
  int old_hybrid_percent_;
  bool settings_were_active_;
};

/**
 * Create a FileIndex populated with test data.
 * 
 * @param index FileIndex to populate (passed by reference, cannot be copied)
 * @param file_count Number of files to create
 * @param base_path Base path for the test hierarchy (default: "C:\\Test")
 */
void CreateTestFileIndex(FileIndex& index, size_t file_count, const std::string& base_path = "C:\\Test");

/**
 * RAII fixture for managing test FileIndex.
 * 
 * Automatically creates and populates a FileIndex with test data.
 * Optionally resets the thread pool after creation.
 * This reduces duplication in tests that need a populated FileIndex.
 */
class TestFileIndexFixture {
public:
  /**
   * Create fixture with specified number of files.
   * 
   * @param file_count Number of files to create in the index
   * @param base_path Base path for the test hierarchy (default: "C:\\Test")
   * @param reset_thread_pool Whether to reset thread pool after creation (default: true)
   */
  explicit TestFileIndexFixture(
      size_t file_count,
      const std::string& base_path = "C:\\Test",
      bool reset_thread_pool = true);

  /**
   * Get reference to the FileIndex.
   * 
   * @return Reference to the populated FileIndex
   */
  FileIndex& GetIndex() { return index_; }
  const FileIndex& GetIndex() const { return index_; }

  // Non-copyable, non-movable (RAII fixture should be created in place)
  TestFileIndexFixture(const TestFileIndexFixture&) = delete;
  TestFileIndexFixture& operator=(const TestFileIndexFixture&) = delete;
  TestFileIndexFixture(TestFileIndexFixture&&) = delete;
  TestFileIndexFixture& operator=(TestFileIndexFixture&&) = delete;

private:
  FileIndex index_;
};

/**
 * RAII fixture for managing test IndexOperations setup.
 * 
 * Automatically creates and configures all dependencies needed for IndexOperations tests:
 * - FileIndexStorage
 * - PathStorage and PathOperations
 * - IndexOperations with atomic counters
 * - Mutex and lock
 * 
 * This eliminates the repetitive 13-line setup pattern in IndexOperations tests.
 */
class TestIndexOperationsFixture {
public:
  /**
   * Create fixture with default configuration.
   */
  explicit TestIndexOperationsFixture();

  /**
   * Get reference to the IndexOperations.
   * 
   * @return Reference to the configured IndexOperations
   */
  IndexOperations& GetOperations() { return operations_; }
  const IndexOperations& GetOperations() const { return operations_; }

  /**
   * Get reference to the FileIndexStorage.
   * 
   * @return Reference to the storage
   */
  FileIndexStorage& GetStorage() { return storage_; }
  const FileIndexStorage& GetStorage() const { return storage_; }

  /**
   * Get reference to the PathStorage.
   * 
   * @return Reference to the path storage
   */
  PathStorage& GetPathStorage() { return path_storage_; }
  const PathStorage& GetPathStorage() const { return path_storage_; }

  /**
   * Get reference to the PathOperations.
   * 
   * @return Reference to the path operations
   */
  PathOperations& GetPathOperations() { return path_operations_; }
  const PathOperations& GetPathOperations() const { return path_operations_; }

  /**
   * Get reference to the mutex lock.
   * 
   * @return Reference to the unique_lock
   */
  std::unique_lock<std::shared_mutex>& GetLock() { return lock_; }

  /**
   * Get reference to remove_not_in_index_count atomic.
   * 
   * @return Reference to the atomic counter
   */
  std::atomic<size_t>& GetRemoveNotInIndexCount() { return remove_not_in_index_count_; }

  /**
   * Get reference to remove_duplicate_count atomic.
   * 
   * @return Reference to the atomic counter
   */
  std::atomic<size_t>& GetRemoveDuplicateCount() { return remove_duplicate_count_; }

  /**
   * Get reference to remove_inconsistency_count atomic.
   * 
   * @return Reference to the atomic counter
   */
  std::atomic<size_t>& GetRemoveInconsistencyCount() { return remove_inconsistency_count_; }

  // Non-copyable, non-movable (RAII fixture should be created in place)
  TestIndexOperationsFixture(const TestIndexOperationsFixture&) = delete;
  TestIndexOperationsFixture& operator=(const TestIndexOperationsFixture&) = delete;
  TestIndexOperationsFixture(TestIndexOperationsFixture&&) = delete;
  TestIndexOperationsFixture& operator=(TestIndexOperationsFixture&&) = delete;

private:
  std::shared_mutex mutex_{};
  FileIndexStorage storage_;
  PathStorage path_storage_{};
  PathOperations path_operations_;
  std::atomic<size_t> remove_not_in_index_count_{0};
  std::atomic<size_t> remove_duplicate_count_{0};
  std::atomic<size_t> remove_inconsistency_count_{0};
  IndexOperations operations_;
  std::unique_lock<std::shared_mutex> lock_;
};

/**
 * RAII fixture for managing FileIndexMaintenance test setup.
 * 
 * Automatically creates and configures all dependencies needed for FileIndexMaintenance tests:
 * - PathStorage
 * - std::shared_mutex
 * - Atomic counters (remove_not_in_index_count, remove_duplicate_count, remove_inconsistency_count)
 * - FileIndexMaintenance instance
 * 
 * This eliminates the repetitive 13-line setup pattern in FileIndexMaintenance tests.
 */
class TestFileIndexMaintenanceFixture {
public:
  /**
   * Create fixture with default atomic counter values (all 0).
   * 
   * @param get_alive_count Lambda function to get alive count (default: returns 0)
   */
  explicit TestFileIndexMaintenanceFixture(
      const std::function<size_t()>& get_alive_count = []() { return size_t(0); });
  
  /**
   * Create fixture with custom atomic counter values.
   * 
   * @param remove_not_in_index_count Initial value for remove_not_in_index_count
   * @param remove_duplicate_count Initial value for remove_duplicate_count
   * @param remove_inconsistency_count Initial value for remove_inconsistency_count
   * @param get_alive_count Lambda function to get alive count (default: returns 0)
   */
  explicit TestFileIndexMaintenanceFixture(
      size_t remove_not_in_index_count,
      size_t remove_duplicate_count,
      size_t remove_inconsistency_count,
      const std::function<size_t()>& get_alive_count = []() { return size_t(0); });
  
  ~TestFileIndexMaintenanceFixture() = default;
  
  /**
   * Get reference to the FileIndexMaintenance.
   * 
   * @return Reference to the configured FileIndexMaintenance
   */
  FileIndexMaintenance& GetMaintenance() { return maintenance_; }
  const FileIndexMaintenance& GetMaintenance() const { return maintenance_; }
  
  /**
   * Get reference to the PathStorage.
   * 
   * @return Reference to the path storage
   */
  PathStorage& GetPathStorage() { return path_storage_; }
  const PathStorage& GetPathStorage() const { return path_storage_; }
  
  /**
   * Get reference to the mutex.
   * 
   * @return Reference to the shared_mutex
   */
  std::shared_mutex& GetMutex() { return mutex_; }
  
  /**
   * Get reference to remove_not_in_index_count atomic.
   * 
   * @return Reference to the atomic counter
   */
  std::atomic<size_t>& GetRemoveNotInIndexCount() { return remove_not_in_index_count_; }
  
  /**
   * Get reference to remove_duplicate_count atomic.
   * 
   * @return Reference to the atomic counter
   */
  std::atomic<size_t>& GetRemoveDuplicateCount() { return remove_duplicate_count_; }
  
  /**
   * Get reference to remove_inconsistency_count atomic.
   * 
   * @return Reference to the atomic counter
   */
  std::atomic<size_t>& GetRemoveInconsistencyCount() { return remove_inconsistency_count_; }
  
  /**
   * Insert a test path with predictable naming.
   * 
   * @param id File ID
   * @param path Full path (or will use default pattern if empty)
   * @param is_dir Whether this is a directory
   */
  void InsertTestPath(uint64_t id, const std::string& path = "", bool is_dir = false);
  
  /**
   * Insert multiple test paths with predictable names.
   * 
   * @param start_id Starting file ID
   * @param count Number of paths to insert
   * @param base_path Base path for test files (default: "C:\\test")
   */
  void InsertTestPaths(uint64_t start_id, uint64_t count, const std::string& base_path = "C:\\test");
  
  /**
   * Remove a test path.
   * 
   * @param id File ID to remove
   */
  void RemoveTestPath(uint64_t id);
  
  /**
   * Remove multiple test paths.
   * 
   * @param start_id Starting file ID
   * @param count Number of paths to remove
   */
  void RemoveTestPaths(uint64_t start_id, uint64_t count);
  
  /**
   * Insert and immediately remove paths (for creating deleted entries).
   * 
   * @param start_id Starting file ID
   * @param count Number of paths to insert and remove
   * @param base_path Base path for test files (default: "C:\\test")
   */
  void InsertAndRemoveTestPaths(uint64_t start_id, uint64_t count, const std::string& base_path = "C:\\test");
  
  // Non-copyable, non-movable (RAII fixture should be created in place)
  TestFileIndexMaintenanceFixture(const TestFileIndexMaintenanceFixture&) = delete;
  TestFileIndexMaintenanceFixture& operator=(const TestFileIndexMaintenanceFixture&) = delete;
  TestFileIndexMaintenanceFixture(TestFileIndexMaintenanceFixture&&) = delete;
  TestFileIndexMaintenanceFixture& operator=(TestFileIndexMaintenanceFixture&&) = delete;

private:
  PathStorage path_storage_;
  std::shared_mutex mutex_;
  std::atomic<size_t> remove_not_in_index_count_;
  std::atomic<size_t> remove_duplicate_count_;
  std::atomic<size_t> remove_inconsistency_count_;
  FileIndexMaintenance maintenance_;
};

/**
 * RAII fixture for managing test DirectoryResolver setup.
 * 
 * Automatically creates and configures all dependencies needed for DirectoryResolver tests:
 * - FileIndexStorage
 * - PathStorage and PathOperations
 * - IndexOperations with atomic counters
 * - DirectoryResolver
 * - Mutex and lock
 * 
 * This eliminates the repetitive 12-line setup pattern in DirectoryResolver tests.
 */
class TestDirectoryResolverFixture {
public:
  /**
   * Create fixture with default configuration.
   */
  explicit TestDirectoryResolverFixture();

  /**
   * Get reference to the DirectoryResolver.
   * 
   * @return Reference to the configured DirectoryResolver
   */
  DirectoryResolver& GetResolver() { return resolver_; }
  const DirectoryResolver& GetResolver() const { return resolver_; }

  /**
   * Get reference to the FileIndexStorage.
   * 
   * @return Reference to the storage
   */
  FileIndexStorage& GetStorage() { return storage_; }
  const FileIndexStorage& GetStorage() const { return storage_; }

  /**
   * Get reference to the mutex lock.
   * 
   * @return Reference to the unique_lock
   */
  std::unique_lock<std::shared_mutex>& GetLock() { return lock_; }

  /**
   * Get reference to next_file_id atomic.
   * 
   * @return Reference to the next_file_id atomic
   */
  std::atomic<uint64_t>& GetNextFileId() { return next_file_id_; }

  // Non-copyable, non-movable (RAII fixture should be created in place)
  TestDirectoryResolverFixture(const TestDirectoryResolverFixture&) = delete;
  TestDirectoryResolverFixture& operator=(const TestDirectoryResolverFixture&) = delete;
  TestDirectoryResolverFixture(TestDirectoryResolverFixture&&) = delete;
  TestDirectoryResolverFixture& operator=(TestDirectoryResolverFixture&&) = delete;

private:
  std::shared_mutex mutex_{};
  FileIndexStorage storage_;
  PathStorage path_storage_{};
  PathOperations path_operations_;
  std::atomic<size_t> remove_not_in_index_count_{0};
  std::atomic<size_t> remove_duplicate_count_{0};
  std::atomic<size_t> remove_inconsistency_count_{0};
  IndexOperations operations_;
  std::atomic<uint64_t> next_file_id_{1};
  DirectoryResolver resolver_;
  std::unique_lock<std::shared_mutex> lock_;
};

/**
 * RAII fixture for creating temporary test files.
 * 
 * Automatically creates a temporary file with known content and cleans it up.
 * Used by LazyAttributeLoader tests to create test files with predictable content.
 */
class TempFileFixture {
public:
  std::string path;
  uint64_t expectedSize;
  FILETIME expectedTime;

  /**
   * Create a temporary file with known content.
   */
  TempFileFixture();

  /**
   * Destructor: Cleans up temporary file.
   */
  ~TempFileFixture();

  // Delete copy operations to prevent double-deletion of temporary files
  TempFileFixture(const TempFileFixture&) = delete;
  TempFileFixture& operator=(const TempFileFixture&) = delete;

  // Allow move operations (transfer ownership of file path)
  TempFileFixture(TempFileFixture&& other) noexcept;
  TempFileFixture& operator=(TempFileFixture&& other) noexcept;
};

/**
 * RAII fixture for managing LazyAttributeLoader test setup.
 * 
 * Automatically creates and configures all dependencies needed for LazyAttributeLoader tests:
 * - FileIndexStorage
 * - PathStorage
 * - LazyAttributeLoader
 * - TempFileFixture (temporary test file)
 * - File entry inserted into storage
 * 
 * This eliminates the repetitive setup pattern in LazyAttributeLoader tests.
 */
class TestLazyAttributeLoaderFixture {
public:
  /**
   * Create fixture with default configuration.
   * 
   * @param file_id File ID to use (default: 1)
   * @param filename Filename for the file entry (default: "test.txt")
   * @param extension Extension for the file entry (default: ".txt")
   * @param is_directory Whether the entry is a directory (default: false)
   */
  explicit TestLazyAttributeLoaderFixture(
      uint64_t file_id = 1,
      const std::string& filename = "test.txt",
      const std::string& extension = ".txt",
      bool is_directory = false);

  /**
   * Get reference to the LazyAttributeLoader.
   * 
   * @return Reference to the configured LazyAttributeLoader
   */
  LazyAttributeLoader& GetLoader() { return loader_; }
  const LazyAttributeLoader& GetLoader() const { return loader_; }

  /**
   * Get reference to the FileIndexStorage.
   * 
   * @return Reference to the storage
   */
  FileIndexStorage& GetStorage() { return storage_; }
  const FileIndexStorage& GetStorage() const { return storage_; }

  /**
   * Get reference to the PathStorage.
   * 
   * @return Reference to the path storage
   */
  PathStorage& GetPathStorage() { return path_storage_; }
  const PathStorage& GetPathStorage() const { return path_storage_; }

  /**
   * Get reference to the TempFileFixture.
   * 
   * @return Reference to the temporary file fixture
   */
  TempFileFixture& GetTempFile() { return temp_file_; }
  const TempFileFixture& GetTempFile() const { return temp_file_; }

  /**
   * Get the file ID.
   * 
   * @return The file ID used in this fixture
   */
  uint64_t GetFileId() const { return file_id_; }

  /**
   * Insert a file entry with custom parameters.
   * 
   * @param id File ID
   * @param name Filename
   * @param is_dir Whether it's a directory
   * @param path Full path to the file
   */
  void InsertFileEntry(uint64_t id, const std::string& name, bool is_dir, const std::string& path);

  /**
   * Insert a file entry using the default temp file.
   * 
   * @param id File ID
   * @param name Filename
   * @param is_dir Whether it's a directory
   */
  void InsertFileEntryWithTempFile(uint64_t id, const std::string& name, bool is_dir);

  // Non-copyable, non-movable (RAII fixture should be created in place)
  TestLazyAttributeLoaderFixture(const TestLazyAttributeLoaderFixture&) = delete;
  TestLazyAttributeLoaderFixture& operator=(const TestLazyAttributeLoaderFixture&) = delete;
  TestLazyAttributeLoaderFixture(TestLazyAttributeLoaderFixture&&) = delete;
  TestLazyAttributeLoaderFixture& operator=(TestLazyAttributeLoaderFixture&&) = delete;

private:
  // Helper to insert file entry into both storage and path_storage
  // Extracts extension from filename if not provided
  void InsertFileEntryInternal(uint64_t id, const std::string& name, bool is_dir, 
                               const std::string& path, const std::string& extension = "");

  std::shared_mutex mutex_;
  FileIndexStorage storage_;
  PathStorage path_storage_;
  LazyAttributeLoader loader_;
  TempFileFixture temp_file_;
  uint64_t file_id_;
};

// ============================================================================
// LazyAttributeLoader Test Helpers
// ============================================================================

namespace lazy_loader_test_helpers {

/**
 * Verify that file size was loaded successfully.
 * 
 * @param loader LazyAttributeLoader instance
 * @param file_id File ID to check
 * @param expected_size Expected file size
 * @return True if size matches expected value
 */
bool VerifyFileSizeLoaded(const LazyAttributeLoader& loader, uint64_t file_id, uint64_t expected_size);

/**
 * Verify that modification time was loaded successfully.
 * 
 * @param loader LazyAttributeLoader instance
 * @param file_id File ID to check
 * @return True if modification time is valid (not sentinel or failed)
 */
bool VerifyModificationTimeLoaded(const LazyAttributeLoader& loader, uint64_t file_id);

/**
 * Verify that a load operation returned the expected result.
 * 
 * @param loaded Result of load operation
 * @param expected Expected result (default: true)
 */
void VerifyLoadResult(bool loaded, bool expected = true);

/**
 * Verify failure scenario for file size (returns 0, no retry).
 * 
 * Attempts to load file size twice and verifies both return 0.
 * This ensures the failure is marked and no retry occurs.
 * 
 * @param loader LazyAttributeLoader instance
 * @param file_id File ID to check
 */
void VerifyFileSizeFailure(const LazyAttributeLoader& loader, uint64_t file_id);

/**
 * Verify failure scenario for modification time (returns failed/sentinel, no retry).
 *
 * Attempts to load modification time twice and verifies both return invalid time.
 * This ensures the failure is marked and no retry occurs.
 *
 * @param loader LazyAttributeLoader instance
 * @param file_id File ID to check
 */
void VerifyModificationTimeFailure(const LazyAttributeLoader& loader, uint64_t file_id);

/**
 * Assert that storage has file size marked as failed (regression: must not cache 0 on failure).
 *
 * @param storage FileIndexStorage to check
 * @param mutex Mutex shared with storage (must be held when calling storage from other code)
 * @param file_id File ID to check
 */
void AssertStorageFileSizeFailed(const FileIndexStorage& storage, std::shared_mutex& mutex,
                                 uint64_t file_id);

/**
 * Assert that storage has file size loaded with the given value (e.g. 0 for zero-byte file).
 *
 * @param storage FileIndexStorage to check
 * @param mutex Mutex shared with storage
 * @param file_id File ID to check
 * @param expected_size Expected size (e.g. 0 for zero-byte file)
 */
void AssertStorageFileSizeLoaded(const FileIndexStorage& storage, std::shared_mutex& mutex,
                                 uint64_t file_id, uint64_t expected_size);

/**
 * Minimal test setup for LazyAttributeLoader tests.
 * 
 * Holds all objects needed for basic LazyAttributeLoader tests.
 * Used to eliminate duplicate setup code in tests.
 */
struct MinimalLoaderSetup {
  std::shared_mutex mutex;
  FileIndexStorage storage;
  PathStorage path_storage;
  LazyAttributeLoader loader;
  
  MinimalLoaderSetup()
    : storage(mutex)
    , loader(storage, path_storage, mutex) {
  }
};

/**
 * Create a minimal loader setup with a directory entry.
 * 
 * Note: Returns by reference to avoid copying non-copyable mutex.
 * The setup is created in-place and returned by reference.
 * 
 * @param setup Setup struct to populate (will be modified)
 * @param dir_id Directory ID (default: 1)
 * @param dir_name Directory name (default: "test_dir")
 * @param dir_path Directory path (default: secure temporary directory created via CreateTempDirectory)
 * @return Reference to the setup struct
 * 
 * Security: Default dir_path uses CreateTempDirectory() to generate a secure unique path,
 * preventing symlink attacks and TOCTOU vulnerabilities in publicly writable directories.
 */
MinimalLoaderSetup& CreateLoaderSetupWithDirectory(
    MinimalLoaderSetup& setup,
    uint64_t dir_id = 1,
    const std::string& dir_name = "test_dir",
    const std::string& dir_path = "");

/**
 * Create a minimal loader setup with an empty path entry.
 * 
 * Note: Returns by reference to avoid copying non-copyable mutex.
 * The setup is created in-place and returned by reference.
 * 
 * @param setup Setup struct to populate (will be modified)
 * @param file_id File ID (default: 1)
 * @param file_name File name (default: "empty_path.txt")
 * @param extension File extension (default: ".txt")
 * @return Reference to the setup struct
 */
MinimalLoaderSetup& CreateLoaderSetupWithEmptyPath(
    MinimalLoaderSetup& setup,
    uint64_t file_id = 1,
    const std::string& file_name = "empty_path.txt",
    const std::string& extension = ".txt");

/**
 * Test concurrent access to a lazy-loaded attribute.
 * 
 * Launches multiple threads that all request the same attribute simultaneously,
 * then verifies all threads get consistent results and the attribute was actually loaded.
 * 
 * @tparam ResultType Type of result (uint64_t for size, FILETIME for time)
 * @tparam GetFunc Function type that takes LazyAttributeLoader& and uint64_t, returns ResultType
 * @tparam VerifyFunc Function type that takes ResultType and verifies it
 * @param loader LazyAttributeLoader instance
 * @param file_id File ID to test
 * @param get_func Function to get the attribute value
 * @param verify_func Function to verify the result is valid/consistent
 * @param num_threads Number of threads to launch (default: 10)
 */
template<typename ResultType, typename GetFunc, typename VerifyFunc>
void TestConcurrentAccess(
    LazyAttributeLoader& loader,
    uint64_t file_id,
    GetFunc get_func,
    VerifyFunc verify_func,
    int num_threads = 10) {
  
  std::vector<std::thread> threads;
  std::vector<ResultType> results(num_threads);
  std::atomic completed(0);
  
  for (int i = 0; i < num_threads; ++i) {
    threads.emplace_back([&loader, file_id, &results, i, &completed, get_func]() {
      // All threads request the same attribute simultaneously
      results[i] = get_func(loader, file_id);
      completed.fetch_add(1);
    });
  }
  
  // Wait for all threads
  for (auto& t : threads) {
    t.join();
  }
  
  // Verify all results are valid and consistent
  for (int i = 0; i < num_threads; ++i) {
    verify_func(results[i]);
  }
  
  // Verify attribute was actually loaded (not still unloaded)
  ResultType final_result = get_func(loader, file_id);
  verify_func(final_result);
}

/**
 * Test that requesting one attribute when both are unloaded loads both together.
 * 
 * This verifies the optimization where LazyAttributeLoader loads both size and
 * modification time together when both are unloaded, regardless of which one
 * is requested first.
 * 
 * @param loader LazyAttributeLoader instance
 * @param file_id File ID to test
 * @param expected_size Expected file size
 * @param request_size_first If true, request size first; if false, request time first
 */
void TestOptimizationLoadsBothAttributes(
    const LazyAttributeLoader& loader,
    uint64_t file_id,
    uint64_t expected_size,
    bool request_size_first);

/**
 * Test optimization with fixture (eliminates duplicate fixture creation pattern).
 * 
 * Creates a fixture and tests that requesting one attribute loads both together.
 * 
 * @param request_size_first If true, request size first; if false, request time first
 */
void TestOptimizationWithFixture(bool request_size_first);

} // namespace lazy_loader_test_helpers

// ============================================================================
// FileIndex Search Strategy Test Helpers
// ============================================================================

namespace search_strategy_test_helpers {

/**
 * Verify all search results are files (not directories).
 * 
 * @param results Search results to verify
 */
void VerifyAllResultsAreFiles(const std::vector<SearchResultData>& results);

/**
 * Collect results from futures and verify non-empty (if required).
 * 
 * @param futures Vector of futures to collect from
 * @param require_non_empty If true, require total > 0 (default: true)
 * @return Total number of results collected
 */
size_t CollectAndVerifyFutures(
    std::vector<std::future<std::vector<SearchResultData>>>& futures,
    bool require_non_empty = true);

/**
 * Collect results from futures and return total count (for manual verification).
 * 
 * This is used when you need to check individual thread results or perform
 * additional verification beyond just counting total results.
 * 
 * @param futures Vector of futures to collect from
 * @return Total number of results collected
 */
size_t CollectFuturesAndCount(
    std::vector<std::future<std::vector<SearchResultData>>>& futures);

/**
 * Collect IDs from SearchAsync futures into a set.
 * 
 * @param futures Vector of futures from SearchAsync (returns IDs, not SearchResultData)
 * @return Set of all collected IDs
 */
std::set<uint64_t> CollectIdsFromFutures(
    std::vector<std::future<std::vector<uint64_t>>>& futures);

/**
 * Test cancellation pattern helper.
 * 
 * Sets up cancellation test: creates settings, fixture, cancel flag, starts search,
 * cancels it, and collects results with exception handling.
 * 
 * @param strategy Strategy name (default: "dynamic")
 * @param file_count Number of files in test index (default: 10000)
 * @param cancel_immediately If true, cancel immediately; if false, wait 10ms first (default: true)
 * @param max_expected_results Maximum expected results (default: 9000, assumes cancellation)
 * @return Total results collected (may be partial due to cancellation)
 */
size_t TestCancellation(
    const std::string& strategy = "dynamic",
    size_t file_count = 10000,
    bool cancel_immediately = true,
    size_t max_expected_results = 9000);

/**
 * Verify futures size and collect results.
 * 
 * Common pattern: Get futures, verify size constraints, then collect and count.
 * 
 * @param futures Vector of futures
 * @param min_size Minimum expected size (default: 1)
 * @param max_size Maximum expected size (default: 4)
 * @return Total results collected
 */
size_t VerifyFuturesSizeAndCollect(
    std::vector<std::future<std::vector<SearchResultData>>>& futures,
    size_t min_size = 1,
    size_t max_size = 4);

/**
 * Collect IDs from SearchAsync futures and verify count.
 * 
 * @param futures Vector of futures from SearchAsync
 * @param require_non_empty If true, require total > 0 (default: true)
 * @return Set of collected IDs
 */
std::set<uint64_t> CollectIdsAndVerify(
    std::vector<std::future<std::vector<uint64_t>>>& futures,
    bool require_non_empty = true);

/**
 * Collect results from two separate future vectors.
 * 
 * Used for concurrent search tests with two different queries.
 * 
 * @param futures1 First vector of futures
 * @param futures2 Second vector of futures
 * @return Pair of total counts (first, second)
 */
std::pair<size_t, size_t> CollectTwoFutureVectors(
    std::vector<std::future<std::vector<SearchResultData>>>& futures1,
    std::vector<std::future<std::vector<SearchResultData>>>& futures2);

/**
 * Verify threads processed work based on timing data.
 * 
 * Checks if at least one thread has processed work (items, results, or dynamic chunks).
 * 
 * @param timings Vector of thread timing data
 * @param check_items_processed If true, also check items_processed_ (default: false)
 * @return Number of threads that processed work
 */
size_t VerifyThreadsProcessedWork(
    const std::vector<ThreadTiming>& timings,
    bool check_items_processed = false);

/**
 * Get futures, verify size, and collect results.
 * 
 * Common pattern: Create settings and fixture, get futures, verify size constraints, collect.
 * 
 * @param strategy Strategy name
 * @param file_count Number of files in test index
 * @param query Search query (default: "file_")
 * @param thread_count Number of threads (default: 4)
 * @param min_futures Minimum expected futures (default: 1)
 * @param max_futures Maximum expected futures (default: 4)
 * @return Total results collected
 */
size_t TestGetFuturesAndCollect(
    const std::string& strategy,
    size_t file_count,
    const std::string& query = "file_",
    int thread_count = 4,
    size_t min_futures = 1,
    size_t max_futures = 4);

/**
 * Result of a concurrent search operation.
 */
struct ConcurrentSearchResult {
  size_t total_results = 0;
  bool completed_successfully = true;
  std::string error_message;
};

/**
 * Run multiple concurrent searches and verify all complete successfully.
 * 
 * @param index FileIndex to search
 * @param queries Vector of query strings (one per concurrent search)
 * @param thread_count Number of threads per search (default: 4)
 * @param require_all_non_empty If true, require all searches return > 0 results (default: true)
 * @return Vector of results, one per query
 */
std::vector<ConcurrentSearchResult> RunConcurrentSearches(
    FileIndex& index,
    const std::vector<std::string>& queries,
    int thread_count = 4,
    bool require_all_non_empty = true);

/**
 * Get list of all available search strategies.
 * 
 * @return Vector of strategy names
 */
std::vector<std::string> GetAllStrategies();

/**
 * Run a test function across all strategies.
 * 
 * @tparam TestFunc Function type: void(FileIndex&, const std::string&)
 * @param index FileIndex to use (will be reset for each strategy)
 * @param test_func Test function to run for each strategy
 * @param reset_thread_pool Whether to reset thread pool for each strategy (default: true)
 */
template<typename TestFunc>
inline void RunTestForAllStrategies(
    FileIndex& index,
    TestFunc test_func,
    bool reset_thread_pool = true) {
  std::vector<std::string> strategies = GetAllStrategies();
  
  for (const auto& strategy_name : strategies) {
    test_helpers::TestSettingsFixture strategy_settings(strategy_name);
    if (reset_thread_pool) {
      index.ResetThreadPool();
    }
    test_func(index, strategy_name);
  }
}

/**
 * Create AppSettings for dynamic strategy with chunk size.
 * 
 * @param chunk_size Dynamic chunk size
 * @return Configured AppSettings
 */
AppSettings CreateDynamicSettings(int chunk_size);

/**
 * Create AppSettings for hybrid strategy with initial work percent.
 * 
 * @param initial_work_percent Hybrid initial work percent
 * @return Configured AppSettings
 */
AppSettings CreateHybridSettings(int initial_work_percent);

/**
 * Run test for all strategies with common setup.
 * 
 * Common pattern: Create settings and fixture, then run test for all strategies.
 * 
 * @param file_count Number of files in test index
 * @param test_func Function to execute for each strategy
 */
template<typename TestFunc>
void RunTestForAllStrategiesWithSetup(size_t file_count, TestFunc test_func) {
  test_helpers::TestSettingsFixture settings;
  test_helpers::TestFileIndexFixture index_fixture(file_count);
  RunTestForAllStrategies(index_fixture.GetIndex(), test_func);
}

/**
 * Test with dynamic settings pattern helper.
 * 
 * Common pattern: Create dynamic settings, create fixtures, then execute test logic.
 * 
 * @param chunk_size Dynamic chunk size
 * @param file_count Number of files in test index
 * @param test_func Function to execute with the configured index
 */
template<typename TestFunc>
void TestWithDynamicSettings(int chunk_size, size_t file_count, TestFunc test_func) {
  auto settings = CreateDynamicSettings(chunk_size);
  test_helpers::TestSettingsFixture settings_fixture(settings);
  test_helpers::TestFileIndexFixture index_fixture(file_count);
  test_func(index_fixture.GetIndex());
}

} // namespace search_strategy_test_helpers

/**
 * Generate a vector of test paths.
 * 
 * Creates a predictable set of test paths for use in tests.
 * 
 * @param count Number of paths to generate
 * @param base_path Base path for generated paths (default: "C:\\Test")
 * @param prefix Prefix for filenames (default: "file_")
 * @return Vector of test paths
 */
std::vector<std::string> GenerateTestPaths(
    size_t count,
    const std::string& base_path = "C:\\Test",
    const std::string& prefix = "file_");

/**
 * Generate a vector of test extensions.
 * 
 * Creates a predictable set of extensions for use in tests.
 * 
 * @param count Number of extensions to generate
 * @param include_dots Whether to include leading dots (default: true)
 * @return Vector of test extensions
 */
std::vector<std::string> GenerateTestExtensions(
    size_t count,
    bool include_dots = true);

// ============================================================================
// Assertion Helpers
// ============================================================================

/**
 * Check if search results are non-empty and match expected criteria.
 * 
 * This helper consolidates common validation patterns:
 * - Check results are non-empty
 * - Validate results match query (case-sensitive or not)
 * - Optionally check extensions
 * - Optionally check folders_only flag
 * 
 * Use with REQUIRE: REQUIRE(test_helpers::ValidateSearchResults(...))
 * 
 * @param results Search results to validate
 * @param expected_query Expected query string that should appear in results
 * @param case_sensitive Whether query matching should be case-sensitive
 * @param expected_extensions Optional vector of expected extensions (nullptr to skip)
 * @param folders_only Whether results should be folders only (default: false)
 * @return True if all validations pass
 */
bool ValidateSearchResults(
    const std::vector<SearchResultData>& results,
    std::string_view expected_query,
    bool case_sensitive = false,
    const std::vector<std::string>* expected_extensions = nullptr,
    bool folders_only = false);

/**
 * Check if all search results are files (not directories).
 * 
 * Use with REQUIRE: REQUIRE(test_helpers::AllResultsAreFiles(...))
 * 
 * @param results Search results to check
 * @return True if all results are files
 */
bool AllResultsAreFiles(const std::vector<SearchResultData>& results);

/**
 * Check if all search results are directories.
 * 
 * Use with REQUIRE: REQUIRE(test_helpers::AllResultsAreDirectories(...))
 * 
 * @param results Search results to check
 * @return True if all results are directories
 */
bool AllResultsAreDirectories(const std::vector<SearchResultData>& results);

/**
 * Collect all results from futures into a single vector.
 * 
 * @tparam T Type of result (e.g., uint64_t, SearchResultData)
 * @param futures Vector of futures to collect from
 * @return Combined vector of all results
 */
template<typename T>
std::vector<T> CollectFutures(std::vector<std::future<std::vector<T>>>& futures) {
  std::vector<T> all_results;
  for (auto& future : futures) {
    auto results = future.get();
    all_results.insert(all_results.end(), results.begin(), results.end());
  }
  return all_results;
}

/**
 * Create a SearchContext for simple filename search.
 * 
 * @param query Search query string
 * @param case_sensitive Whether search should be case-sensitive (default: false)
 * @return Configured SearchContext
 */
SearchContext CreateSimpleSearchContext(std::string_view query, bool case_sensitive = false);

/**
 * Create a SearchContext with extension filter only.
 * 
 * @param extensions Vector of extensions to filter (e.g., {".txt", ".cpp"})
 * @param case_sensitive Whether search should be case-sensitive (default: false)
 * @return Configured SearchContext in extension-only mode
 */
SearchContext CreateExtensionSearchContext(const std::vector<std::string>& extensions, bool case_sensitive = false);

// ============================================================================
// SearchContext Test Helpers
// ============================================================================

/**
 * Create SearchContext with specific values and validate/clamp.
 * 
 * Creates a SearchContext, sets the chunk size and hybrid percent,
 * calls ValidateAndClamp(), and returns the validated context.
 * This reduces duplication in tests that need to create and validate
 * a SearchContext with specific values.
 * 
 * @param chunk_size Dynamic chunk size value
 * @param hybrid_percent Hybrid initial percent value
 * @return Configured and validated SearchContext
 */
inline SearchContext CreateValidatedSearchContext(int chunk_size, int hybrid_percent) {
  SearchContext ctx;
  ctx.dynamic_chunk_size = chunk_size;
  ctx.hybrid_initial_percent = hybrid_percent;
  ctx.ValidateAndClamp();
  return ctx;
}

/**
 * Create SearchContext with extensions and prepare extension set.
 * 
 * Creates a SearchContext, sets extensions and case sensitivity,
 * calls PrepareExtensionSet(), and returns the configured context.
 * This reduces duplication in tests that need to create a SearchContext
 * with extensions and prepare the extension set.
 * 
 * @param extensions Vector of extensions (with or without leading dot)
 * @param case_sensitive Whether search should be case-sensitive
 * @return Configured SearchContext with prepared extension set
 */
inline SearchContext CreateSearchContextWithExtensions(
    const std::vector<std::string>& extensions,
    bool case_sensitive = false) {
  SearchContext ctx;
  ctx.extensions = extensions;
  ctx.case_sensitive = case_sensitive;
  ctx.PrepareExtensionSet();
  return ctx;
}

// ============================================================================
// Gemini API JSON Test Helpers
// ============================================================================

/**
 * Create Gemini API JSON response wrapper.
 * 
 * Wraps inner JSON text in the standard Gemini API response structure:
 * {
 *   "candidates": [{
 *     "content": {
 *       "parts": [{
 *         "text": "<inner_text>"
 *       }]
 *     }
 *   }]
 * }
 * 
 * This reduces duplication in tests that need to create Gemini API JSON responses.
 * 
 * @param inner_text Inner JSON text to wrap (e.g., R"({"version": "1.0", "search_config": {...}})")
 * @return Complete Gemini API JSON response string
 */
inline std::string CreateGeminiJsonResponse(std::string_view inner_text) {
  // Build Gemini API JSON response structure
  // Reserve space to avoid reallocations during string concatenation
  constexpr size_t kBaseJsonSize = 100;  // Approximate size of JSON wrapper
  std::string json;
  json.reserve(kBaseJsonSize + inner_text.length() * 2);  // *2 for escaped characters
  
  json = R"({
  "candidates": [{
    "content": {
      "parts": [{
        "text": ")";
  
  // Escape quotes in inner_text and append
  for (char c : inner_text) {
    if (c == '"') {
      json += R"(\")";
    } else if (c == '\\') {
      json += R"(\\)";
    } else if (c == '\n') {
      json += "\\n";
    } else if (c == '\r') {
      json += "\\r";
    } else if (c == '\t') {
      json += "\\t";
    } else {
      json += c;
    }
  }
  
  json += R"("
      }]
    }
  }]
})";
  
  return json;
}

// ============================================================================
// Path Truncation Test Helpers
// ============================================================================

namespace test_helpers_detail {
  // Tolerance for floating-point width comparisons in path truncation tests
  // Used to account for floating-point precision issues
  constexpr float kPathWidthTolerance = 0.1f;
}

/**
 * Create a monospace text width calculator for testing.
 * 
 * Uses character count as width (simulates a monospace font where each character has width 1.0).
 * This is useful for testing path truncation functions with predictable width calculations.
 * 
 * @return TextWidthCalculator that returns character count as width
 */
inline path_utils::TextWidthCalculator CreateMonospaceWidthCalculator() {
  return [](std::string_view text) {
    return static_cast<float>(text.length());
  };
}

/**
 * Create a proportional text width calculator for testing.
 * 
 * Simulates a variable-width font where different characters have different widths:
 * - lowercase letters: 0.5
 * - uppercase letters: 0.7
 * - digits: 0.6
 * - other characters: 0.8
 * 
 * This is useful for testing path truncation functions with variable-width font calculations.
 * 
 * @return TextWidthCalculator that returns variable width based on character type
 */
inline path_utils::TextWidthCalculator CreateProportionalWidthCalculator() {
  return [](std::string_view text) {
    float width = 0.0f;
    for (char c : text) {
      if (c >= 'a' && c <= 'z') {
        width += 0.5f;
      } else if (c >= 'A' && c <= 'Z') {
        width += 0.7f;
      } else if (c >= '0' && c <= '9') {
        width += 0.6f;
      } else {
        width += 0.8f;
      }
    }
    return width;
  };
}

/**
 * Get platform-specific test path.
 * 
 * Returns the Windows path on Windows, Unix path on Unix-like systems.
 * This reduces duplication in tests that need platform-specific paths.
 * 
 * @param windows_path Path to use on Windows (e.g., "C:\\Users\\Test\\file.txt")
 * @param unix_path Path to use on Unix-like systems (e.g., "Users/Test/file.txt")
 * @return Platform-appropriate path
 */
inline std::string GetPlatformSpecificPath(
    [[maybe_unused]] std::string_view windows_path,
    [[maybe_unused]] std::string_view unix_path) {
#ifdef _WIN32
  return std::string(windows_path);
#else
  return std::string(unix_path);
#endif  // _WIN32
}

/**
 * Check if truncated path result width is within limit.
 * 
 * @param result The truncated path result
 * @param max_width Maximum allowed width
 * @param calc Text width calculator function
 * @return True if result width <= max_width + tolerance
 */
template<typename TextWidthCalculator>
inline bool CheckPathWidthWithinLimit(
    const std::string& result,
    float max_width,
    const TextWidthCalculator& calc) {
  float result_width = calc(result);
  return result_width <= max_width + test_helpers_detail::kPathWidthTolerance;
}

/**
 * Check if truncated path starts with expected prefix.
 * 
 * @param result The truncated path result
 * @param expected_prefix Expected prefix (e.g., "C:\\" or "...")
 * @return True if result starts with expected_prefix
 */
inline bool CheckPathPrefix(std::string_view result, std::string_view expected_prefix) {
  if (expected_prefix.empty()) {
    return true;
  }
  if (result.length() < expected_prefix.length()) {
    return false;
  }
  return result.substr(0, expected_prefix.length()) == expected_prefix;
}

/**
 * Check if truncated path contains ellipsis.
 * 
 * @param result The truncated path result
 * @return True if result contains "..."
 */
inline bool CheckPathContainsEllipsis(std::string_view result) {
  return result.find("...") != std::string::npos;
}

/**
 * Check if truncated path contains expected suffix.
 * 
 * @param result The truncated path result
 * @param expected_suffix Expected suffix substring
 * @return True if result contains expected_suffix
 */
inline bool CheckPathContainsSuffix(std::string_view result, std::string_view expected_suffix) {
  if (expected_suffix.empty()) {
    return true;
  }
  return result.find(expected_suffix) != std::string::npos;
}

// ============================================================================
// StringUtils Parameterized Test Helpers
// ============================================================================

/**
 * Test case structure for string utility functions that return std::string.
 */
struct StringUtilsTestCase {
  std::string input;
  std::string expected;
};

/**
 * Test case structure for string utility functions that return std::vector<std::string>.
 */
struct StringUtilsVectorTestCase {
  std::string input;
  std::vector<std::string> expected;
};

/**
 * Run parameterized tests for string utility functions that return std::string.
 * 
 * This helper eliminates the repetitive pattern of:
 * - Defining TestCase struct
 * - Creating vector of test cases
 * - Looping with DOCTEST_SUBCASE
 * - Checking both std::string and std::string_view versions
 * 
 * @tparam Func Function type that takes string/string_view and returns string
 * @param func Function to test (can be overloaded for string and string_view)
 * @param test_cases Vector of test cases with input and expected output
 * 
 * @example
 * ```cpp
 * TEST_CASE("ToLower") {
 *   std::vector<test_helpers::StringUtilsTestCase> test_cases = {
 *     {"HELLO", "hello"},
 *     {"WORLD", "world"}
 *   };
 *   test_helpers::RunParameterizedStringTests(ToLower, test_cases);
 * }
 * ```
 */
template<typename Func>
void RunParameterizedStringTests(Func func, const std::vector<StringUtilsTestCase>& test_cases) {
  for (const auto& tc : test_cases) {
    DOCTEST_SUBCASE(tc.input.c_str()) {
      CHECK(func(tc.input) == tc.expected);
      CHECK(func(std::string_view(tc.input)) == tc.expected);
    }
  }
}

/**
 * Run parameterized tests for string utility functions that return std::vector<std::string>.
 * 
 * This helper eliminates the repetitive pattern for functions like ParseExtensions.
 * 
 * @tparam Func Function type that takes string/string_view and returns vector<string>
 * @param func Function to test (can be overloaded for string and string_view)
 * @param test_cases Vector of test cases with input and expected output
 * 
 * @example
 * ```cpp
 * TEST_CASE("ParseExtensions") {
 *   std::vector<test_helpers::StringUtilsVectorTestCase> test_cases = {
 *     {"txt;doc;pdf", {"txt", "doc", "pdf"}},
 *     {"", {}}
 *   };
 *   test_helpers::RunParameterizedVectorTests(ParseExtensions, test_cases);
 * }
 * ```
 */
template<typename Func>
void RunParameterizedVectorTests(Func func, const std::vector<StringUtilsVectorTestCase>& test_cases) {
  for (const auto& tc : test_cases) {
    DOCTEST_SUBCASE(tc.input.c_str()) {
      std::vector<std::string> result = func(tc.input);
      CHECK(result == tc.expected);
      
      std::vector<std::string> sv_result = func(std::string_view(tc.input));
      CHECK(sv_result == tc.expected);
    }
  }
}

// ============================================================================
// StdRegexUtils Parameterized Test Helpers
// ============================================================================

/**
 * Test case structure for regex match tests.
 */
struct RegexMatchTestCase {
  std::string pattern;
  std::string text;
  bool expected;
  bool case_sensitive = false;  // Default to case-insensitive
};

/**
 * Run parameterized tests for regex match functions.
 * 
 * This helper eliminates the repetitive pattern of testing multiple
 * pattern/text combinations with the same expected result.
 * 
 * @param test_cases Vector of test cases with pattern, text, expected result, and case sensitivity
 * 
 * @example
 * ```cpp
 * TEST_CASE("Invalid regex patterns return false") {
 *   std::vector<test_helpers::RegexMatchTestCase> test_cases = {
 *     {"[", "test", false},      // Unclosed bracket
 *     {"(", "test", false},      // Unclosed paren
 *     {"*", "test", false}       // Star without preceding
 *   };
 *   test_helpers::RunParameterizedRegexMatchTests(test_cases);
 * }
 * ```
 */
inline void RunParameterizedRegexMatchTests(const std::vector<RegexMatchTestCase>& test_cases) {
  for (const auto& tc : test_cases) {
    DOCTEST_SUBCASE((tc.pattern + " vs " + tc.text).c_str()) {
      CHECK(std_regex_utils::RegexMatch(std::string_view(tc.pattern), std::string_view(tc.text), tc.case_sensitive) == tc.expected);
    }
  }
}

/**
 * Test case structure for PatternAnalysis tests.
 */
struct PatternAnalysisTestCase {
  std::string pattern;
  bool expected_is_literal;
  bool expected_is_simple;
  bool expected_requires_full_match;
};

/**
 * Run parameterized tests for PatternAnalysis.
 * 
 * This helper eliminates the repetitive pattern of testing multiple
 * patterns with PatternAnalysis checks.
 * 
 * @param test_cases Vector of test cases with pattern and expected analysis results
 */
inline void RunParameterizedPatternAnalysisTests(const std::vector<PatternAnalysisTestCase>& test_cases) {
  for (const auto& tc : test_cases) {
    DOCTEST_SUBCASE(tc.pattern.c_str()) {
      std_regex_utils::PatternAnalysis analysis(tc.pattern);
      CHECK(analysis.is_literal == tc.expected_is_literal);
      CHECK(analysis.is_simple == tc.expected_is_simple);
      CHECK(analysis.requires_full_match == tc.expected_requires_full_match);
    }
  }
}

// ============================================================================
// GeminiApiUtils Parameterized Test Helpers
// ============================================================================

/**
 * Test case structure for ValidatePathPatternFormat tests.
 */
struct ValidatePathPatternTestCase {
  std::string pattern;
  bool expected;
};

/**
 * Run parameterized tests for ValidatePathPatternFormat.
 * 
 * This helper eliminates the repetitive pattern of testing multiple
 * patterns with CHECK statements.
 * 
 * @param test_cases Vector of test cases with pattern and expected result
 */
inline void RunParameterizedValidatePathPatternTests(const std::vector<ValidatePathPatternTestCase>& test_cases) {
  for (const auto& tc : test_cases) {
    DOCTEST_SUBCASE(tc.pattern.c_str()) {
      CHECK(gemini_api_utils::ValidatePathPatternFormat(tc.pattern) == tc.expected);
    }
  }
}

/**
 * Create a Gemini API response JSON with a search config path.
 * 
 * This helper eliminates the repetitive pattern of:
 * - Creating search_config_json
 * - Setting version and path
 * - Creating response_json
 * - Wrapping in candidates structure
 * 
 * @param path The path pattern to include in search_config
 * @return Complete Gemini API JSON response string
 */
std::string CreateGeminiResponseWithPath(std::string_view path);

/**
 * RAII fixture for managing GEMINI_API_KEY environment variable in tests.
 * 
 * Automatically sets the environment variable in constructor and
 * clears it in destructor. Handles platform-specific differences.
 * 
 * This eliminates the repetitive pattern of:
 * - Platform-specific setenv/_putenv_s calls
 * - Manual cleanup with unsetenv/_putenv_s
 */
class TestGeminiApiKeyFixture {  // NOSONAR(cpp:S3624) - Move constructor and move assignment operator are defined as = default (see lines 1685-1686)
public:
  /**
   * Create fixture and set GEMINI_API_KEY environment variable.
   * 
   * @param api_key The API key value to set (empty string to unset)
   */
  explicit TestGeminiApiKeyFixture(std::string_view api_key);

  /**
   * Destructor: Clears the environment variable.
   */
  ~TestGeminiApiKeyFixture();

  // Non-copyable, movable
  TestGeminiApiKeyFixture(const TestGeminiApiKeyFixture&) = delete;
  TestGeminiApiKeyFixture& operator=(const TestGeminiApiKeyFixture&) = delete;
  TestGeminiApiKeyFixture(TestGeminiApiKeyFixture&&) noexcept = default;  // NOSONAR(cpp:S3624) - Default move is correct (simple RAII fixture)
  TestGeminiApiKeyFixture& operator=(TestGeminiApiKeyFixture&&) noexcept = default;  // NOSONAR(cpp:S3624) - Default move is correct (simple RAII fixture)

private:
  std::string original_key_{};
  bool was_set_{false};
};

/**
 * Test case structure for ParseSearchConfigJson parameterized tests.
 */
struct ParseSearchConfigJsonTestCase {
  std::string json_input;
  bool expected_success;
  std::string expected_path;  // Empty if not expected
  std::string expected_error_substring;  // Empty if no error expected
  std::vector<std::string> expected_extensions;  // Empty if none expected
  std::string test_name;  // Name for DOCTEST_SUBCASE
};

/**
 * Run assertions for a single ParseSearchConfigJson test case (max nesting 3).
 * Used by RunParameterizedParseSearchConfigJsonTests to satisfy cpp:S134.
 */
inline void RunOneParseSearchConfigJsonTest(
    const ParseSearchConfigJsonTestCase& tc,
    const gemini_api_utils::GeminiApiResult& result) {
  CHECK(result.success == tc.expected_success);
  if (!tc.expected_success) {
    if (!tc.expected_error_substring.empty()) {
      CHECK(result.error_message.find(tc.expected_error_substring) != std::string::npos);
    }
    return;
  }
  if (!tc.expected_path.empty()) {
    CHECK(result.search_config.path == tc.expected_path);
  }
  if (!tc.expected_extensions.empty()) {
    CHECK(result.search_config.extensions.size() == tc.expected_extensions.size());
    for (size_t i = 0; i < tc.expected_extensions.size() && i < result.search_config.extensions.size(); ++i) {
      CHECK(result.search_config.extensions[i] == tc.expected_extensions[i]);
    }
  }
}

/**
 * Run parameterized tests for ParseSearchConfigJson.
 * 
 * This helper eliminates the repetitive pattern of:
 * - Creating JSON
 * - Calling ParseSearchConfigJson
 * - Checking result.success
 * - Checking result.search_config.path
 * - Checking result.error_message
 * - Checking result.search_config.extensions
 * 
 * @param test_cases Vector of test cases with JSON input and expected results
 * 
 * Note: This is an inline function in the header to avoid linking issues
 * when tests don't include GeminiApiUtils.cpp
 */
inline void RunParameterizedParseSearchConfigJsonTests(
    const std::vector<ParseSearchConfigJsonTestCase>& test_cases) {
  using namespace gemini_api_utils;
  
  for (const auto& tc : test_cases) {
    DOCTEST_SUBCASE(tc.test_name.empty() ? tc.json_input.c_str() : tc.test_name.c_str()) {
      auto result = ParseSearchConfigJson(tc.json_input);
      RunOneParseSearchConfigJsonTest(tc, result);
    }
  }
}

/**
 * Create a Gemini API error response JSON.
 * 
 * This helper eliminates the repetitive pattern of manually constructing
 * error response JSON structures.
 * 
 * @param code Error code (e.g., 400, 429, 500)
 * @param message Error message
 * @param status Error status (e.g., "INVALID_ARGUMENT", "RESOURCE_EXHAUSTED")
 * @return Complete Gemini API error response JSON string
 */
std::string CreateGeminiErrorResponse(
    int code,
    std::string_view message,
    std::string_view status = "");

/**
 * Create a Gemini API response JSON with multiple candidates.
 * 
 * @param paths Vector of paths to include in each candidate
 * @return Complete Gemini API JSON response string with multiple candidates
 */
std::string CreateGeminiResponseWithMultipleCandidates(
    const std::vector<std::string>& paths);

/**
 * Create a Gemini API response JSON with multiple parts in first candidate.
 * 
 * @param paths Vector of paths to include in each part
 * @return Complete Gemini API JSON response string with multiple parts
 */
std::string CreateGeminiResponseWithMultipleParts(
    const std::vector<std::string>& paths);

/**
 * Test case structure for BuildSearchConfigPrompt tests.
 */
struct BuildSearchConfigPromptTestCase {
  std::string description;
  std::string expected_substring;  // Substring that should be found in prompt
  bool check_description_in_prompt = true;  // Whether to check if description is in prompt
};

/**
 * Run parameterized tests for BuildSearchConfigPrompt.
 * 
 * This helper eliminates the repetitive pattern of:
 * - Creating description
 * - Calling BuildSearchConfigPrompt
 * - Checking prompt.find(description) != std::string::npos
 * 
 * @param test_cases Vector of test cases with description and expected substring
 */
inline void RunParameterizedBuildSearchConfigPromptTests(
    const std::vector<BuildSearchConfigPromptTestCase>& test_cases) {
  using namespace gemini_api_utils;
  
  for (const auto& tc : test_cases) {
    DOCTEST_SUBCASE(tc.description.empty() ? "empty description" : tc.description.c_str()) {
      std::string prompt = BuildSearchConfigPrompt(tc.description);
      
      CHECK(!prompt.empty());
      if (tc.check_description_in_prompt && !tc.description.empty()) {
        CHECK(prompt.find(tc.description) != std::string::npos);
      }
      if (!tc.expected_substring.empty()) {
        CHECK(prompt.find(tc.expected_substring) != std::string::npos);
      }
    }
  }
}

/**
 * Test case structure for GetGeminiApiKeyFromEnv tests.
 */
struct GetGeminiApiKeyTestCase {
  std::string api_key;
  std::string expected_key;
  bool expect_empty = false;  // If true, expect empty key regardless of api_key value
};

/**
 * Run parameterized tests for GetGeminiApiKeyFromEnv.
 * 
 * This helper eliminates the repetitive pattern of:
 * - Creating TestGeminiApiKeyFixture
 * - Calling GetGeminiApiKeyFromEnv
 * - Checking key == expected_key
 * 
 * @param test_cases Vector of test cases with API key and expected result
 */
inline void RunParameterizedGetGeminiApiKeyTests(
    const std::vector<GetGeminiApiKeyTestCase>& test_cases) {
  using namespace gemini_api_utils;
  
  for (const auto& tc : test_cases) {
    DOCTEST_SUBCASE(tc.api_key.empty() ? "empty key" : tc.api_key.c_str()) {
      test_helpers::TestGeminiApiKeyFixture fixture(tc.api_key);
      
      std::string key = GetGeminiApiKeyFromEnv();
      if (tc.expect_empty) {
        CHECK(key.empty());
      } else {
        CHECK(key == tc.expected_key);
      }
    }
  }
}

/**
 * Helper functions for FileIndexMaintenance tests.
 */
namespace maintenance_test_helpers {
  
  /**
   * Verify maintenance result and deleted count after maintenance.
   * 
   * @param fixture Test fixture
   * @param expected_result Expected return value from Maintain()
   * @param expected_deleted_count Expected deleted count after maintenance (default: 0)
   */
  void VerifyMaintenanceResult(TestFileIndexMaintenanceFixture& fixture, 
                                bool expected_result, 
                                size_t expected_deleted_count = 0);
  
  /**
   * Verify maintenance statistics.
   * 
   * @param stats Maintenance statistics to verify
   * @param expected_deleted_count Expected deleted count
   * @param expected_total_entries Expected total entries
   * @param expected_remove_not_in_index_count Expected remove_not_in_index_count (default: 0)
   * @param expected_remove_duplicate_count Expected remove_duplicate_count (default: 0)
   * @param expected_remove_inconsistency_count Expected remove_inconsistency_count (default: 0)
   */
  void VerifyMaintenanceStats(const FileIndexMaintenance::MaintenanceStats& stats,
                              size_t expected_deleted_count,
                              size_t expected_total_entries,
                              size_t expected_remove_not_in_index_count = 0,
                              size_t expected_remove_duplicate_count = 0,
                              size_t expected_remove_inconsistency_count = 0);
  
} // namespace maintenance_test_helpers

/**
 * Helper functions for TimeFilterUtils tests.
 */
namespace time_filter_test_helpers {
  
  /**
   * Get current Unix timestamp (seconds since 1970).
   * 
   * @return Current Unix timestamp in seconds
   */
  int64_t GetCurrentUnixSeconds();
  
  /**
   * Get current local time as tm struct.
   * Handles platform differences (Windows vs Unix).
   * 
   * @return Current local time as tm struct
   */
  std::tm GetCurrentLocalTime();
  
  /**
   * Get start of today (midnight) as Unix timestamp.
   * 
   * @return Unix timestamp for start of today
   */
  int64_t GetStartOfTodayUnix();
  
  /**
   * Get start of current week (Monday at midnight) as Unix timestamp.
   * 
   * @return Unix timestamp for start of current week
   */
  int64_t GetStartOfWeekUnix();
  
  /**
   * Get start of current month (1st day at midnight) as Unix timestamp.
   * 
   * @return Unix timestamp for start of current month
   */
  int64_t GetStartOfMonthUnix();
  
  /**
   * Get start of current year (January 1st at midnight) as Unix timestamp.
   * 
   * @return Unix timestamp for start of current year
   */
  int64_t GetStartOfYearUnix();
  
  /**
   * Verify cutoff time is within expected range.
   * 
   * @param cutoff_unix Cutoff time as Unix timestamp
   * @param expected_unix Expected time as Unix timestamp
   * @param tolerance_seconds Tolerance in seconds (default: 2 hours for timezone/DST)
   */
  void VerifyCutoffTimeRange(int64_t cutoff_unix, int64_t expected_unix, 
                             int64_t tolerance_seconds = 2 * 60 * 60);
  
  /**
   * Verify cutoff time is in the past and within max_age.
   * 
   * @param cutoff_unix Cutoff time as Unix timestamp
   * @param now_unix Current time as Unix timestamp
   * @param max_age_seconds Maximum age in seconds
   */
  void VerifyCutoffTimeInPast(int64_t cutoff_unix, int64_t now_unix, 
                              int64_t max_age_seconds);
  
} // namespace time_filter_test_helpers

/**
 * Helper functions for ParallelSearchEngine tests.
 */
namespace parallel_search_test_helpers {
  
  /**
   * Execute search and collect results (for SearchAsync).
   * 
   * @param engine ParallelSearchEngine instance
   * @param index MockSearchableIndex to search
   * @param query Search query string
   * @param thread_count Number of threads (-1 for auto)
   * @param ctx SearchContext
   * @return Vector of file IDs matching the query
   */
  std::vector<uint64_t> ExecuteSearchAndCollect(
      const ParallelSearchEngine& engine,
      const MockSearchableIndex& index,
      const std::string& query,
      int thread_count,
      const SearchContext& ctx);
  
  /**
   * Execute search with stats and collect results (for SearchAsync with stats).
   * 
   * @param engine ParallelSearchEngine instance
   * @param index MockSearchableIndex to search
   * @param query Search query string
   * @param thread_count Number of threads (-1 for auto)
   * @param ctx SearchContext
   * @param stats Output parameter for search statistics
   * @return Vector of file IDs matching the query
   */
  std::vector<uint64_t> ExecuteSearchWithStatsAndCollect(
      const ParallelSearchEngine& engine,
      const MockSearchableIndex& index,
      const std::string& query,
      int thread_count,
      const SearchContext& ctx,
      SearchStats& stats);
  
  /**
   * Execute search with data and collect results (for SearchAsyncWithData).
   * 
   * @param engine ParallelSearchEngine instance
   * @param index MockSearchableIndex to search
   * @param query Search query string
   * @param thread_count Number of threads (-1 for auto)
   * @param ctx SearchContext
   * @return Vector of SearchResultData matching the query
   */
  std::vector<SearchResultData> ExecuteSearchWithDataAndCollect(
      const ParallelSearchEngine& engine,
      const MockSearchableIndex& index,
      const std::string& query,
      int thread_count,
      const SearchContext& ctx);
  
  /**
   * Execute search with data and thread timings (for SearchAsyncWithData with timings).
   * 
   * @param engine ParallelSearchEngine instance
   * @param index MockSearchableIndex to search
   * @param query Search query string
   * @param thread_count Number of threads (-1 for auto)
   * @param ctx SearchContext
   * @param thread_timings Output parameter for thread timing information
   * @return Vector of SearchResultData matching the query
   */
  std::vector<SearchResultData> ExecuteSearchWithDataAndTimings(
      const ParallelSearchEngine& engine,
      const MockSearchableIndex& index,
      const std::string& query,
      int thread_count,
      const SearchContext& ctx,
      std::vector<ThreadTiming>& thread_timings);
  
} // namespace parallel_search_test_helpers

// ============================================================================
// SearchPatternUtils Parameterized Test Helpers
// ============================================================================

/**
 * Test case structure for DetectPatternType tests.
 */
struct DetectPatternTypeTestCase {
  std::string pattern;
  search_pattern_utils::PatternType expected_type;
};

/**
 * Run parameterized tests for DetectPatternType.
 * 
 * This helper eliminates the repetitive pattern of testing multiple
 * patterns with CHECK statements.
 * 
 * @param test_cases Vector of test cases with pattern and expected type
 * 
 * @example
 * ```cpp
 * TEST_CASE("Explicit prefixes") {
 *   std::vector<test_helpers::DetectPatternTypeTestCase> test_cases = {
 *     {"rs:test", search_pattern_utils::PatternType::StdRegex},
 *     {"pp:test", search_pattern_utils::PatternType::PathPattern}
 *   };
 *   test_helpers::RunParameterizedDetectPatternTypeTests(test_cases);
 * }
 * ```
 */
inline void RunParameterizedDetectPatternTypeTests(const std::vector<DetectPatternTypeTestCase>& test_cases) {
  for (const auto& tc : test_cases) {
    DOCTEST_SUBCASE(tc.pattern.c_str()) {
      CHECK(search_pattern_utils::DetectPatternType(tc.pattern) == tc.expected_type);
    }
  }
}

/**
 * RAII fixture for managing ParallelSearchEngine test setup.
 * 
 * Automatically creates and configures all dependencies needed for ParallelSearchEngine tests:
 * - SearchThreadPool (default: 4 threads)
 * - ParallelSearchEngine instance
 * 
 * This eliminates the repetitive 2-line setup pattern in ParallelSearchEngine tests.
 */
class TestParallelSearchEngineFixture {
public:
  /**
   * Create fixture with default thread pool size (4 threads).
   */
  explicit TestParallelSearchEngineFixture(int thread_count = 4);
  
  ~TestParallelSearchEngineFixture() = default;
  
  /**
   * Get reference to the ParallelSearchEngine.
   * 
   * @return Reference to the configured ParallelSearchEngine
   */
  ParallelSearchEngine& GetEngine() { return engine_; }
  const ParallelSearchEngine& GetEngine() const { return engine_; }
  
  /**
   * Get reference to the SearchThreadPool.
   * 
   * @return Shared pointer to the thread pool
   */
  std::shared_ptr<SearchThreadPool> GetThreadPool() { return thread_pool_; }
  
  // Non-copyable, non-movable (RAII fixture should be created in place)
  TestParallelSearchEngineFixture(const TestParallelSearchEngineFixture&) = delete;
  TestParallelSearchEngineFixture& operator=(const TestParallelSearchEngineFixture&) = delete;
  TestParallelSearchEngineFixture(TestParallelSearchEngineFixture&&) = delete;
  TestParallelSearchEngineFixture& operator=(TestParallelSearchEngineFixture&&) = delete;

private:
  std::shared_ptr<SearchThreadPool> thread_pool_;
  ParallelSearchEngine engine_;
};

} // namespace test_helpers

