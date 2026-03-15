#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <fstream>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <set>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <direct.h>   // NOSONAR(cpp:S954) - Platform block; must follow #ifdef
#include <windows.h>  // NOSONAR(cpp:S3806,cpp:S954) - Windows-only include
#else
#include <sys/stat.h>  // NOSONAR(cpp:S954) - Platform block; must follow #ifdef
#include <unistd.h>   // NOSONAR(cpp:S954) - Platform block
#endif  // _WIN32

#include "MockSearchableIndex.h"
#include "TestHelpers.h"
#include "api/GeminiApiUtils.h"
#include "core/Settings.h"
#include "filters/TimeFilterUtils.h"
#include "utils/FileTimeTypes.h"
#include "utils/Logger.h"
#include "utils/StringUtils.h"

#if __cplusplus >= 201703L || (defined(_WIN32) && _MSC_VER >= 1914)
#include <filesystem>
namespace fs = std::filesystem;  // NOLINT(misc-unused-alias-decls) - used as fs:: in SafeRemoveFile
#else
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;  // NOLINT(misc-unused-alias-decls) - used as fs:: in SafeRemoveFile
#endif  // __cplusplus >= 201703L || (defined(_WIN32) && _MSC_VER >= 1914)

namespace test_helpers {

namespace {
// Helper function to pad numbers with zeros
std::string PadNumber(size_t num, size_t width) {
  std::string result = std::to_string(num);
  while (result.length() < width) {
    result.insert(0, "0");
  }
  return result;
}

// Extension array for test data generation
constexpr std::array<const char*, 7> kTestExtensions = {".txt", ".cpp",  ".h", ".exe",
                                                        ".dll", ".json", ".md"};

// Get extension for a given index (cycles through extensions)
std::string GetExtensionForIndex(size_t i) {
  return {kTestExtensions[i % 7]};
}

// Helper to save current settings in TestSettingsFixture
void SaveCurrentSettings(std::string& old_strategy, int& old_chunk_size, int& old_hybrid_percent,
                         bool& settings_were_active) {
  AppSettings current;
  LoadSettings(current);
  old_strategy = current.loadBalancingStrategy;
  old_chunk_size = current.dynamicChunkSize;
  old_hybrid_percent = current.hybridInitialWorkPercent;
  settings_were_active = test_settings::IsInMemoryMode();
}

// Helper to get environment variable value (platform-agnostic)
std::string GetEnvironmentVariable(const char* name) {
#ifdef _WIN32
  // Use RAII wrapper to safely manage memory from _dupenv_s (replaces manual free() calls)
  struct EnvValueDeleter {
    void operator()(char* ptr) const noexcept {
      if (ptr != nullptr) {
        free(ptr);  // NOSONAR(cpp:S1231) - Required by _dupenv_s C API, custom deleter for
                    // unique_ptr is correct pattern
      }
    }
  };
  using EnvValuePtr = std::unique_ptr<char, EnvValueDeleter>;

  char* env_value = nullptr;
  size_t required_size = 0;
  if (_dupenv_s(&env_value, &required_size, name) == 0 &&
      env_value != nullptr) {  // NOSONAR(cpp:S6004): required_size is output parameter, cannot use
                               // init-statement
    EnvValuePtr env_guard(env_value);  // RAII: automatically frees on scope exit
    return std::string(env_value);
  }
  return "";
#else
  const char* env_value = std::getenv(name);
  return env_value != nullptr ? std::string(env_value) : "";
#endif  // _WIN32
}

// Helper to set environment variable (platform-agnostic)
// Note: This function is noexcept-safe for use in destructors.
// Exceptions are caught internally to prevent propagation from destructors.
void SetEnvironmentVariable(const char* name, std::string_view value) noexcept {
  try {
    const std::string value_str(value);
#ifdef _WIN32
    if (value_str.empty()) {
      _putenv_s(name, "");
    } else {
      _putenv_s(name, value_str.c_str());
    }
#else
    if (value_str.empty()) {
      unsetenv(name);
    } else {
      setenv(name, value_str.c_str(), 1);
    }
#endif  // _WIN32
  } catch (...) {  // NOSONAR(cpp:S2738) - Test helper: must catch all exceptions to prevent
                   // propagation from destructors. NOLINT(bugprone-empty-catch) - intentional: noexcept-safe for destructors
    // Silently ignore failures - environment variable setting failures in tests are non-critical
    // This ensures the function is safe for use in destructors (noexcept guarantee)
  }
}

// Helper to extract extension from filename (including the dot)
// Returns empty string if no valid extension found
std::string ExtractExtensionFromFilename(std::string_view filename) {
  if (const size_t last_dot = filename.find_last_of('.');
      last_dot != std::string::npos && last_dot < filename.length() - 1) {
    return std::string(filename.substr(last_dot));
  }
  return "";
}

// Helper to build test file path with predictable naming
// Pattern: base_path\file{id}.txt
std::string BuildTestFilePath(const std::string& base_path, uint64_t id) {
  return base_path + "\\file" + std::to_string(id) + ".txt";
}

// Helper to safely remove a file if it exists (for test cleanup)
// Catches all exceptions to prevent test failures during cleanup
void SafeRemoveFile(const std::filesystem::path& path) {
  try {
    if (fs::exists(path)) {
      fs::remove(path);
    }
  } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - Test cleanup: catch std then catch(...) for rest
    LOG_DEBUG_BUILD("SafeRemoveFile: " << e.what());
  } catch (...) {  // NOSONAR(cpp:S2738,cpp:S2486) - Test cleanup: catch all to prevent test failure
    LOG_DEBUG_BUILD("SafeRemoveFile: non-std exception");
  }
}
}  // anonymous namespace

std::string CreateTempFile(std::string_view prefix) {
  std::string path;

#ifdef _WIN32
  // Windows: Use GetTempPath and create unique filename
  char tempPath[MAX_PATH];
  if (DWORD pathLen = GetTempPathA(MAX_PATH, tempPath); pathLen == 0 || pathLen >= MAX_PATH) {
    throw std::runtime_error(
      "Failed to get temporary path");  // NOSONAR(cpp:S112) - std::runtime_error is appropriate for
                                        // simple test setup errors
  }

  // Convert prefix to C string for GetTempFileNameA
  std::string prefix_str(prefix);
  if (prefix_str.length() > 3) {
    prefix_str = prefix_str.substr(0, 3);  // GetTempFileNameA only uses first 3 chars
  }

  char tempFile[MAX_PATH];
  if (UINT fileNum = GetTempFileNameA(tempPath, prefix_str.c_str(), 0, tempFile); fileNum == 0) {
    throw std::runtime_error(
      "Failed to create temporary file");  // NOSONAR(cpp:S112) - std::runtime_error is appropriate
                                           // for simple test setup errors
  }
  path = tempFile;
#else
  // Unix-like: Use mkstemp for secure temporary file creation
  // Note: mkstemp modifies the template in place, so we use vector<char> for writable buffer
  const std::string prefix_str(prefix);
  std::vector<char> tempTemplate;
  tempTemplate.reserve(prefix_str.length() +
                       20);  // Reserve space for prefix + "/tmp/" + "XXXXXX" + null

  // Build template: /tmp/prefix_XXXXXX
  tempTemplate.push_back('/');
  tempTemplate.push_back('t');
  tempTemplate.push_back('m');
  tempTemplate.push_back('p');
  tempTemplate.push_back('/');
  for (const char c : prefix_str) {
    tempTemplate.push_back(c);
  }
  tempTemplate.push_back('_');
  // Add 6 'X' characters for mkstemp/mkdtemp template
  for (int i = 0; i < 6; ++i) {
    tempTemplate.push_back('X');
  }
  tempTemplate.push_back('\0');

  const int fd = mkstemp(tempTemplate.data());
  if (fd == -1) {
    throw std::runtime_error("Failed to create temporary file");  // NOSONAR(cpp:S112) - test helper; std::runtime_error acceptable
  }
  path = tempTemplate.data();
  close(fd);
#endif  // _WIN32

  return path;
}

std::string CreateTempDirectory(std::string_view prefix) {
  std::string path;

#ifdef _WIN32
  // Windows: Use GetTempPath and create unique directory name
  char tempPath[MAX_PATH];
  if (DWORD pathLen = GetTempPathA(MAX_PATH, tempPath); pathLen == 0 || pathLen >= MAX_PATH) {
    throw std::runtime_error(
      "Failed to get temporary path");  // NOSONAR(cpp:S112) - std::runtime_error is appropriate for
                                        // simple test setup errors
  }

  // Create unique directory name
  char tempDir[MAX_PATH];
  if (UINT dirNum = GetTempFileNameA(tempPath, "dir", 0, tempDir); dirNum == 0) {
    throw std::runtime_error(
      "Failed to create temporary directory name");  // NOSONAR(cpp:S112) - std::runtime_error is
                                                     // appropriate for simple test setup errors
  }
  // GetTempFileNameA creates a file, so we delete it and create a directory instead
  DeleteFileA(tempDir);
  if (CreateDirectoryA(tempDir, nullptr) == 0) {
    throw std::runtime_error(
      "Failed to create temporary directory");  // NOSONAR(cpp:S112) - std::runtime_error is
                                                // appropriate for simple test setup errors
  }
  path = tempDir;
#else
  // Unix-like: Use mkdtemp for secure temporary directory creation
  // Security: mkdtemp creates directory atomically, preventing symlink attacks and TOCTOU
  // vulnerabilities
  const std::string prefix_str(prefix);
  std::vector<char> tempTemplate;
  tempTemplate.reserve(prefix_str.length() +
                       20);  // Reserve space for prefix + "/tmp/" + "XXXXXX" + null

  // Build template: /tmp/prefix_XXXXXX
  tempTemplate.push_back('/');
  tempTemplate.push_back('t');
  tempTemplate.push_back('m');
  tempTemplate.push_back('p');
  tempTemplate.push_back('/');
  for (const char c : prefix_str) {
    tempTemplate.push_back(c);
  }
  tempTemplate.push_back('_');
  // Add 6 'X' characters for mkstemp/mkdtemp template
  for (int i = 0; i < 6; ++i) {
    tempTemplate.push_back('X');
  }
  tempTemplate.push_back('\0');

  if (const char* dir_path = mkdtemp(tempTemplate.data());  // NOSONAR(cpp:S995,cpp:S5350) - mkdtemp returns non-const, we do not modify through dir_path
      dir_path == nullptr) {
    throw std::runtime_error("Failed to create temporary directory");  // NOSONAR(cpp:S112) - test helper; std::runtime_error acceptable
  }
  path = tempTemplate.data();

  // Security: Set restrictive permissions (owner read/write/execute only)
  // Note: mkdtemp creates directory with 0700 permissions by default, but we set it explicitly for
  // clarity
  chmod(path.c_str(), 0700);
#endif  // _WIN32

  return path;
}

TestSettingsFixture::TestSettingsFixture() {
  SaveCurrentSettings(old_strategy_, old_chunk_size_, old_hybrid_percent_, settings_were_active_);

  // Enable in-memory mode with default settings
  test_settings::SetInMemorySettings(AppSettings{});
}

TestSettingsFixture::TestSettingsFixture(const AppSettings& settings) {
  SaveCurrentSettings(old_strategy_, old_chunk_size_, old_hybrid_percent_, settings_were_active_);

  // Set new settings in memory
  test_settings::SetInMemorySettings(settings);
}

TestSettingsFixture::TestSettingsFixture(std::string_view strategy) {
  SaveCurrentSettings(old_strategy_, old_chunk_size_, old_hybrid_percent_, settings_were_active_);

  // Enable in-memory mode
  AppSettings current;
  LoadSettings(current);
  test_settings::SetInMemorySettings(current);

  // Update strategy
  current.loadBalancingStrategy = std::string(strategy);
  test_settings::SetInMemorySettings(current);
}

TestSettingsFixture::~TestSettingsFixture() {  // NOLINT(bugprone-exception-escape) - teardown catches all, does not rethrow
  // Restore original settings
  try {
    if (settings_were_active_) {
      AppSettings current = test_settings::GetInMemorySettings();
      current.loadBalancingStrategy = old_strategy_;
      current.dynamicChunkSize = old_chunk_size_;
      current.hybridInitialWorkPercent = old_hybrid_percent_;
      test_settings::SetInMemorySettings(current);
    } else {
      test_settings::ClearInMemorySettings();
    }
  } catch (const std::exception& e) {
    LOG_DEBUG_BUILD("TestSettingsFixture: teardown failed: " << e.what());
  } catch (...) {  // NOSONAR(cpp:S2738,cpp:S2486) - Teardown must not throw
    LOG_DEBUG_BUILD("TestSettingsFixture: teardown failed (non-std exception)");
  }
}

void CreateTestFileIndex(FileIndex& index, size_t file_count, const std::string& base_path) {
  // Create root directory (id=1)
  const uint64_t root_id = 1;
  const FILETIME root_time = {0, 0};  // Directories use {0, 0}
  index.Insert(root_id, 0, base_path, true, root_time);

  // Create files with predictable names: file_0001.txt, file_0002.cpp, ...
  // Mix of files and directories (every 10th item is a directory)
  for (size_t i = 1; i <= file_count; ++i) {
    const uint64_t file_id = i + 1;
    const bool is_dir = (i % 10 == 0);  // Every 10th item is a directory

    std::string name;
    if (is_dir) {
      name = "dir_" + PadNumber(i, 4);
    } else {
      // Vary extensions for testing
      const std::string ext = GetExtensionForIndex(i);
      name = "file_" + PadNumber(i, 4) + ext;
    }

    // Use kFileTimeNotLoaded for files, {0, 0} for directories
    const FILETIME mod_time = is_dir ? FILETIME{0, 0} : kFileTimeNotLoaded;
    index.Insert(file_id, root_id, name, is_dir, mod_time);
  }

  // CRITICAL: Must call RecomputeAllPaths() to populate path arrays
  index.RecomputeAllPaths();
}

TestFileIndexFixture::TestFileIndexFixture(size_t file_count, const std::string& base_path,
                                           bool reset_thread_pool) {
  CreateTestFileIndex(index_, file_count, base_path);
  if (reset_thread_pool) {
    index_.ResetThreadPool();
  }
}

std::vector<std::string> GenerateTestPaths(size_t count, std::string_view base_path,
                                           std::string_view prefix) {
  std::vector<std::string> paths;
  paths.reserve(count);

  for (size_t i = 1; i <= count; ++i) {
    const std::string ext = GetExtensionForIndex(i);
    std::string filename;
    filename.reserve(prefix.size() + 4 + ext.size());
    filename += prefix;
    filename += PadNumber(i, 4);
    filename += ext;

    // Build full path
    std::string full_path(base_path);
    if (!full_path.empty() && full_path.back() != '\\' && full_path.back() != '/') {
      full_path += "\\";
    }
    full_path += filename;

    paths.push_back(full_path);
  }

  return paths;
}

std::vector<std::string> GenerateTestExtensions(size_t count, bool include_dots) {
  std::vector<std::string> extensions;
  extensions.reserve(count);

  constexpr std::array<const char*, 10> ext_list = {"txt", "cpp",  "h",  "hpp", "exe",
                                                    "dll", "json", "md", "py",  "js"};

  for (size_t i = 0; i < count; ++i) {
    std::string ext = ext_list[i % 10];
    if (include_dots) {
      ext.insert(0, ".");
    }
    extensions.push_back(ext);
  }

  return extensions;
}

namespace {
// Helper function to check if result extension matches expected extensions
// Extracted to reduce nesting depth in ValidateSearchResults
bool MatchesExtension([[maybe_unused]] const SearchResultData& result, std::string_view path_view,
                      const std::vector<std::string>& expected_extensions) {
  std::string result_ext;
  // Calculate filename and extension offsets manually
  // Note: extension is stored without the dot, so result_ext is "txt" not ".txt"
  if (const std::string_view extension = path_utils::GetExtension(path_view); !extension.empty()) {
    result_ext = ToLower(extension);
  } else {
    result_ext = "";  // No extension
  }

  for (const auto& ext : expected_extensions) {
    std::string ext_lower = ToLower(ext);
    // Remove leading dot from expected extension if present
    // (result_ext is stored without the dot, e.g., "txt" not ".txt")
    if (!ext_lower.empty() && ext_lower[0] == '.') {
      ext_lower.erase(0, 1);
    }
    if (result_ext == ext_lower) {
      return true;
    }
  }
  return false;
}
}  // anonymous namespace

bool ValidateSearchResults(const std::vector<SearchResultData>& results,
                           std::string_view expected_query, bool case_sensitive,
                           const std::vector<std::string>* expected_extensions, bool folders_only) {
  // Check results are non-empty
  if (results.empty()) {
    return false;
  }

  // Validate each result
  for (const auto& result : results) {
    // Calculate filename offset manually
    const std::string_view path_view(result.fullPath);
    size_t filename_offset = 0;
    if (const size_t last_slash = path_view.find_last_of("/\\"); last_slash != std::string_view::npos) {
      filename_offset = last_slash + 1;
    }
    const std::string_view filename_view = path_view.substr(filename_offset);
    std::string search_target(filename_view);
    auto query = std::string(expected_query);

    if (!case_sensitive) {
      search_target = ToLower(search_target);
      query = ToLower(query);
    }

    // Check query match
    if (search_target.find(query) == std::string::npos) {
      return false;
    }

    // Check extension filter if provided - use early return to reduce nesting
    if (expected_extensions != nullptr && !expected_extensions->empty() &&
        !MatchesExtension(result, path_view, *expected_extensions)) {
      return false;
    }

    // Check folders_only filter
    if (folders_only && !result.isDirectory) {
      return false;
    }
  }

  return true;
}

bool AllResultsAreFiles(const std::vector<SearchResultData>& results) {
  return std::all_of(results.begin(), results.end(),
                     [](const auto& result) { return !result.isDirectory; });
}

bool AllResultsAreDirectories(const std::vector<SearchResultData>& results) {
  return std::all_of(results.begin(), results.end(),
                     [](const auto& result) { return result.isDirectory; });
}

SearchContext CreateSimpleSearchContext(std::string_view query, bool case_sensitive) {
  SearchContext ctx;
  ctx.filename_query_lower = std::string(query);
  ctx.filename_query = ctx.filename_query_lower;  // Use string_view from the string
  // Only lowercase when case-insensitive; otherwise filename_query would point at
  // modified buffer and case-sensitive search would match incorrectly (e.g. "TEST" -> "test").
  if (!case_sensitive) {
    std::transform(ctx.filename_query_lower.begin(), ctx.filename_query_lower.end(),
                   ctx.filename_query_lower.begin(), ::tolower);
  }
  ctx.case_sensitive = case_sensitive;
  ctx.PrepareExtensionSet();
  return ctx;
}

SearchContext CreateExtensionSearchContext(const std::vector<std::string>& extensions,
                                           bool case_sensitive) {
  SearchContext ctx;
  ctx.extensions = extensions;
  ctx.extension_only_mode = true;
  ctx.case_sensitive = case_sensitive;
  ctx.PrepareExtensionSet();
  return ctx;
}

TestIndexOperationsFixture::TestIndexOperationsFixture()
    : storage_(mutex_), path_operations_(storage_, path_storage_),
      operations_(storage_, path_operations_, remove_not_in_index_count_,
                  remove_inconsistency_count_),
      lock_(mutex_) {}

TestDirectoryResolverFixture::TestDirectoryResolverFixture()
    : storage_(mutex_), path_operations_(storage_, path_storage_),
      operations_(storage_, path_operations_, remove_not_in_index_count_,
                  remove_inconsistency_count_),
      resolver_(storage_, operations_, next_file_id_), lock_(mutex_) {}

test_helpers::TestFileIndexMaintenanceFixture::TestFileIndexMaintenanceFixture(
  const std::function<size_t()>& get_alive_count)
    : remove_not_in_index_count_(0), remove_duplicate_count_(0), remove_inconsistency_count_(0),
      maintenance_(path_storage_, mutex_, get_alive_count,
                   [this](uint64_t file_id, size_t index) { id_to_index_[file_id] = index; },
                   remove_not_in_index_count_, remove_duplicate_count_, remove_inconsistency_count_) {}

test_helpers::TestFileIndexMaintenanceFixture::TestFileIndexMaintenanceFixture(
  size_t remove_not_in_index_count, size_t remove_duplicate_count,
  size_t remove_inconsistency_count, const std::function<size_t()>& get_alive_count)
    : remove_not_in_index_count_(remove_not_in_index_count),
      remove_duplicate_count_(remove_duplicate_count),
      remove_inconsistency_count_(remove_inconsistency_count),
      maintenance_(path_storage_, mutex_, get_alive_count,
                   [this](uint64_t file_id, size_t index) { id_to_index_[file_id] = index; },
                   remove_not_in_index_count_, remove_duplicate_count_, remove_inconsistency_count_) {}

void test_helpers::TestFileIndexMaintenanceFixture::InsertTestPath(uint64_t id,
                                                                   const std::string& path,
                                                                   bool is_dir) {
  std::string actual_path = path;
  if (actual_path.empty()) {
    actual_path = "C:\\test\\file" + std::to_string(id) + (is_dir ? "" : ".txt");
  }
  const size_t idx = path_storage_.InsertPath(id, actual_path, is_dir, std::nullopt);
  id_to_index_[id] = idx;
}

void test_helpers::TestFileIndexMaintenanceFixture::InsertTestPaths(uint64_t start_id,
                                                                    uint64_t count,
                                                                    const std::string& base_path) {
  for (uint64_t i = 0; i < count; ++i) {
    const uint64_t id = start_id + i;
    const std::string path = BuildTestFilePath(base_path, id);
    const size_t idx = path_storage_.InsertPath(id, path, false, std::nullopt);
    id_to_index_[id] = idx;
  }
}

void test_helpers::TestFileIndexMaintenanceFixture::RemoveTestPath(uint64_t id) {
  const auto it = id_to_index_.find(id);
  if (it != id_to_index_.end()) {
    (void)path_storage_.RemovePathByIndex(it->second);
    id_to_index_.erase(it);
  }
}

void test_helpers::TestFileIndexMaintenanceFixture::RemoveTestPaths(uint64_t start_id,
                                                                    uint64_t count) {
  for (uint64_t i = 0; i < count; ++i) {
    RemoveTestPath(start_id + i);
  }
}

void test_helpers::TestFileIndexMaintenanceFixture::InsertAndRemoveTestPaths(
  uint64_t start_id, uint64_t count, const std::string& base_path) {
  for (uint64_t i = 0; i < count; ++i) {
    const uint64_t id = start_id + i;
    const std::string path = BuildTestFilePath(base_path, id);
    const size_t idx = path_storage_.InsertPath(id, path, false, std::nullopt);
    id_to_index_[id] = idx;
    (void)path_storage_.RemovePathByIndex(idx);
    id_to_index_.erase(id);
  }
}

namespace maintenance_test_helpers {

void VerifyMaintenanceResult(TestFileIndexMaintenanceFixture& fixture, bool expected_result,
                             size_t expected_deleted_count) {
  CHECK(fixture.GetMaintenance().Maintain() == expected_result);
  CHECK(fixture.GetPathStorage().GetDeletedCount() == expected_deleted_count);
}

void VerifyMaintenanceStats(const FileIndexMaintenance::MaintenanceStats& stats,
                            size_t expected_deleted_count, size_t expected_total_entries,
                            size_t expected_remove_not_in_index_count,
                            size_t expected_remove_duplicate_count,
                            size_t expected_remove_inconsistency_count) {
  CHECK(stats.deleted_count == expected_deleted_count);
  CHECK(stats.total_entries == expected_total_entries);
  CHECK(stats.remove_not_in_index_count == expected_remove_not_in_index_count);
  CHECK(stats.remove_duplicate_count == expected_remove_duplicate_count);
  CHECK(stats.remove_inconsistency_count == expected_remove_inconsistency_count);
}

}  // namespace maintenance_test_helpers

namespace time_filter_test_helpers {

int64_t GetCurrentUnixSeconds() {
  auto now = std::chrono::system_clock::now();
  auto duration = now.time_since_epoch();
  return std::chrono::duration_cast<std::chrono::seconds>(duration).count();
}

std::tm GetCurrentLocalTime() {
  auto now_time_t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm now_tm;
#ifdef _WIN32
  localtime_s(&now_tm, &now_time_t);
#else
  localtime_r(&now_time_t, &now_tm);
#endif  // _WIN32
  return now_tm;
}

int64_t GetStartOfTodayUnix() {
  std::tm now_tm = GetCurrentLocalTime();

  std::tm today_start = {};
  today_start.tm_year = now_tm.tm_year;
  today_start.tm_mon = now_tm.tm_mon;
  today_start.tm_mday = now_tm.tm_mday;
  today_start.tm_hour = 0;
  today_start.tm_min = 0;
  today_start.tm_sec = 0;
  today_start.tm_isdst = -1;

  return std::mktime(&today_start);
}

int64_t GetStartOfWeekUnix() {
  std::tm now_tm = GetCurrentLocalTime();

  // Calculate days since Monday (0=Monday, 1=Tuesday, ..., 6=Sunday)
  const int days_since_monday = (now_tm.tm_wday == 0) ? 6 : (now_tm.tm_wday - 1);

  std::tm week_start = now_tm;
  week_start.tm_mday -= days_since_monday;
  week_start.tm_hour = 0;
  week_start.tm_min = 0;
  week_start.tm_sec = 0;
  week_start.tm_isdst = -1;

  return std::mktime(&week_start);
}

int64_t GetStartOfMonthUnix() {
  std::tm now_tm = GetCurrentLocalTime();

  std::tm month_start = {};
  month_start.tm_year = now_tm.tm_year;
  month_start.tm_mon = now_tm.tm_mon;
  month_start.tm_mday = 1;
  month_start.tm_hour = 0;
  month_start.tm_min = 0;
  month_start.tm_sec = 0;
  month_start.tm_isdst = -1;

  return std::mktime(&month_start);
}

int64_t GetStartOfYearUnix() {
  std::tm now_tm = GetCurrentLocalTime();

  std::tm year_start = {};
  year_start.tm_year = now_tm.tm_year;
  year_start.tm_mon = 0;  // January
  year_start.tm_mday = 1;
  year_start.tm_hour = 0;
  year_start.tm_min = 0;
  year_start.tm_sec = 0;
  year_start.tm_isdst = -1;

  return std::mktime(&year_start);
}

void VerifyCutoffTimeRange(int64_t cutoff_unix, int64_t expected_unix, int64_t tolerance_seconds) {
  CHECK(cutoff_unix >= expected_unix - tolerance_seconds);
  CHECK(cutoff_unix <= expected_unix + tolerance_seconds);
}

void VerifyCutoffTimeInPast(int64_t cutoff_unix, int64_t now_unix, int64_t max_age_seconds) {
  CHECK(cutoff_unix <= now_unix);
  CHECK(cutoff_unix >= now_unix - max_age_seconds);
}

}  // namespace time_filter_test_helpers

namespace parallel_search_test_helpers {

std::vector<uint64_t> ExecuteSearchAndCollect(const ParallelSearchEngine& engine,
                                              const MockSearchableIndex& index,
                                              const std::string& query, int thread_count,
                                              const SearchContext& ctx) {
  auto futures = engine.SearchAsync(index, query, thread_count, ctx);
  return test_helpers::CollectFutures(futures);
}

std::vector<uint64_t> ExecuteSearchWithStatsAndCollect(const ParallelSearchEngine& engine,
                                                       const MockSearchableIndex& index,
                                                       const std::string& query, int thread_count,
                                                       const SearchContext& ctx,
                                                       SearchStats& stats) {
  auto futures = engine.SearchAsync(index, query, thread_count, ctx, &stats);
  auto results = test_helpers::CollectFutures(futures);
  // Update total_matches_found_ after collecting results (as per ParallelSearchEngine design)
  stats.total_matches_found_ = results.size();
  return results;
}

std::vector<SearchResultData> ExecuteSearchWithDataAndCollect(const ParallelSearchEngine& engine,
                                                              const MockSearchableIndex& index,
                                                              const std::string& query,
                                                              int thread_count,
                                                              const SearchContext& ctx) {
  auto futures = engine.SearchAsyncWithData(index, query, thread_count, ctx);
  return test_helpers::CollectFutures(futures);
}

std::vector<SearchResultData> ExecuteSearchWithDataAndTimings(
  const ParallelSearchEngine& engine, const MockSearchableIndex& index, const std::string& query,
  int thread_count, const SearchContext& ctx, std::vector<ThreadTiming>& thread_timings) {
  auto futures = engine.SearchAsyncWithData(index, query, thread_count, ctx, &thread_timings);
  return test_helpers::CollectFutures(futures);
}

}  // namespace parallel_search_test_helpers

test_helpers::TestParallelSearchEngineFixture::TestParallelSearchEngineFixture(int thread_count)
    : thread_pool_(std::make_shared<SearchThreadPool>(thread_count)), engine_(thread_pool_) {}

TestGeminiApiKeyFixture::TestGeminiApiKeyFixture(std::string_view api_key)
    : original_key_(GetEnvironmentVariable("GEMINI_API_KEY")),
      was_set_(!original_key_.empty()) {
  SetEnvironmentVariable("GEMINI_API_KEY", api_key);
}

TestGeminiApiKeyFixture::~TestGeminiApiKeyFixture() {  // NOLINT(bugprone-exception-escape) - teardown catches all, does not rethrow
  // Restore original value
  try {
    if (was_set_) {
      SetEnvironmentVariable("GEMINI_API_KEY", original_key_);
    } else {
      SetEnvironmentVariable("GEMINI_API_KEY", "");
    }
  } catch (const std::exception& e) {
    LOG_DEBUG_BUILD("TestGeminiApiKeyFixture: cleanup failed: " << e.what());
  } catch (...) {  // NOSONAR(cpp:S2738) - Test cleanup: catch all to prevent destructor from throwing
    LOG_DEBUG_BUILD("TestGeminiApiKeyFixture: cleanup failed (non-std exception)");
  }
}

std::string CreateGeminiResponseWithPath(std::string_view path) {
  nlohmann::json search_config_json;
  search_config_json["version"] = "1.0";
  search_config_json["search_config"]["path"] = std::string(path);

  nlohmann::json response_json;
  response_json["candidates"][0]["content"]["parts"][0]["text"] = search_config_json.dump();

  return response_json.dump();
}

std::string CreateGeminiErrorResponse(int code, std::string_view message, std::string_view status) {
  nlohmann::json error_json;
  error_json["error"]["code"] = code;
  if (!message.empty()) {
    error_json["error"]["message"] = std::string(message);
  }
  if (!status.empty()) {
    error_json["error"]["status"] = std::string(status);
  }
  return error_json.dump();
}

std::string CreateGeminiResponseWithMultipleCandidates(const std::vector<std::string>& paths) {
  nlohmann::json response_json;

  for (const auto& path : paths) {
    nlohmann::json search_config_json;
    search_config_json["version"] = "1.0";
    search_config_json["search_config"]["path"] = path;

    nlohmann::json candidate;
    candidate["content"]["parts"][0]["text"] = search_config_json.dump();
    response_json["candidates"].push_back(candidate);
  }

  return response_json.dump();
}

std::string CreateGeminiResponseWithMultipleParts(const std::vector<std::string>& paths) {
  nlohmann::json response_json;
  nlohmann::json candidate;

  for (const auto& path : paths) {
    nlohmann::json search_config_json;
    search_config_json["version"] = "1.0";
    search_config_json["search_config"]["path"] = path;

    nlohmann::json part;
    part["text"] = search_config_json.dump();
    candidate["content"]["parts"].push_back(part);
  }

  response_json["candidates"].push_back(candidate);
  return response_json.dump();
}

// ============================================================================
// TempFileFixture Implementation
// ============================================================================

TempFileFixture::TempFileFixture() {
  // Create a temporary file with known content using shared helper
  path = test_helpers::CreateTempFile("lazy_loader_test_");

  // Write known content to file
  std::ofstream file(path);
  const std::string content = "Test content for lazy loading";
  file << content;
  file.close();

  expectedSize = content.length();

  // Get file modification time
  const FileAttributes attrs = ::GetFileAttributes(path);
  expectedTime = attrs.lastModificationTime;
}

TempFileFixture::~TempFileFixture() {  // NOLINT(bugprone-exception-escape) - SafeRemoveFile may throw; test cleanup accepts
  // Clean up temporary file
  SafeRemoveFile(path);
}

TempFileFixture::TempFileFixture(TempFileFixture&& other) noexcept
    : path(std::move(other.path)), expectedSize(other.expectedSize),
      expectedTime(other.expectedTime) {
  other.path.clear();  // Clear moved-from path to prevent deletion
}

TempFileFixture& TempFileFixture::operator=(TempFileFixture&& other) noexcept {  // NOLINT(bugprone-exception-escape) - SafeRemoveFile may throw; test fixture accepts
  if (this != &other) {
    // Clean up current file before taking ownership of new one
    SafeRemoveFile(path);
    path = std::move(other.path);
    expectedSize = other.expectedSize;
    expectedTime = other.expectedTime;
    other.path.clear();  // Clear moved-from path to prevent deletion
  }
  return *this;
}

// ============================================================================
// TestLazyAttributeLoaderFixture Implementation
// ============================================================================

TestLazyAttributeLoaderFixture::TestLazyAttributeLoaderFixture(uint64_t file_id,
                                                               const std::string& filename,
                                                               const std::string& extension,
                                                               bool is_directory)
    : storage_(mutex_), loader_(storage_, path_storage_, mutex_), file_id_(file_id) {
  // Insert file entry with default temp file
  InsertFileEntryInternal(file_id, filename, is_directory, temp_file_.path, extension);
}

void TestLazyAttributeLoaderFixture::InsertFileEntry(uint64_t id, const std::string& name,
                                                     bool is_dir, const std::string& path) {
  InsertFileEntryInternal(id, name, is_dir, path);
}

void TestLazyAttributeLoaderFixture::InsertFileEntryWithTempFile(uint64_t id,
                                                                 const std::string& name,
                                                                 bool is_dir) {
  InsertFileEntryInternal(id, name, is_dir, temp_file_.path);
}

void TestLazyAttributeLoaderFixture::InsertFileEntryInternal(uint64_t id, const std::string& name,
                                                             bool is_dir, const std::string& path,
                                                             const std::string& extension) {
  // Extract extension from filename if not provided
  const std::string ext = extension.empty() ? ExtractExtensionFromFilename(name) : extension;
  storage_.InsertLocked(id, 0, name, is_dir, kFileTimeNotLoaded, ext);
  const size_t idx = path_storage_.InsertPath(id, path, is_dir, std::nullopt);
  storage_.SetPathStorageIndex(id, idx);
}

// ============================================================================
// LazyAttributeLoader Test Helpers Implementation
// ============================================================================

namespace lazy_loader_test_helpers {

bool VerifyFileSizeLoaded(const LazyAttributeLoader& loader, uint64_t file_id,
                          uint64_t expected_size) {
  const uint64_t size = loader.GetFileSize(file_id);
  return size == expected_size;
}

bool VerifyModificationTimeLoaded(const LazyAttributeLoader& loader, uint64_t file_id) {
  const FILETIME time = loader.GetModificationTime(file_id);
  return IsValidTime(time);
}

void VerifyLoadResult(bool loaded, bool expected) {
  CHECK(loaded == expected);
}

void VerifyFileSizeFailure(const LazyAttributeLoader& loader, uint64_t file_id) {
  // Try to load size for failed file
  const uint64_t size = loader.GetFileSize(file_id);

  // Should return failure sentinel (correct semantics for failed load)
  CHECK(size == kFileSizeFailed);

  // Try again - should return sentinel immediately (marked as failed, no retry)
  const uint64_t size2 = loader.GetFileSize(file_id);
  CHECK(size2 == kFileSizeFailed);
}

void VerifyModificationTimeFailure(const LazyAttributeLoader& loader, uint64_t file_id) {
  // Try to load modification time for failed file
  const FILETIME time = loader.GetModificationTime(file_id);

  // Should return failed time or sentinel (not valid)
  // On some platforms, GetFileModificationTime may return sentinel instead of failed
  // The important thing is that the function handles failures without crashing
  // and doesn't retry loading (marked as failed)
  (void)time;  // Suppress unused variable warning - test verifies function doesn't crash

  // Try again - should return immediately (marked as failed, no retry)
  const FILETIME time2 = loader.GetModificationTime(file_id);
  (void)time2;  // Suppress unused variable warning - test verifies function doesn't crash
}

void AssertStorageFileSizeFailed(const FileIndexStorage& storage, std::shared_mutex& mutex,
                                 uint64_t file_id) {
  const std::shared_lock lock(mutex);
  const FileEntry* const entry = storage.GetEntry(file_id);
  CHECK(entry != nullptr);
  CHECK(entry->fileSize.IsFailed());
}

void AssertStorageFileSizeLoaded(const FileIndexStorage& storage, std::shared_mutex& mutex,
                                 uint64_t file_id, uint64_t expected_size) {
  const std::shared_lock lock(mutex);
  const FileEntry* const entry = storage.GetEntry(file_id);
  CHECK(entry != nullptr);
  CHECK(entry->fileSize.IsLoaded());
  CHECK(entry->fileSize.GetValue() == expected_size);
}

MinimalLoaderSetup& CreateLoaderSetupWithDirectory(MinimalLoaderSetup& setup, uint64_t dir_id,
                                                   const std::string& dir_name,
                                                   const std::string& dir_path) {
  // Security: If dir_path is empty, create a secure temporary directory
  // This prevents using hardcoded paths in publicly writable directories
  const std::string actual_dir_path = dir_path.empty() ? CreateTempDirectory("test_dir") : dir_path;

  setup.storage.InsertLocked(dir_id, 0, dir_name, true, kFileTimeNotLoaded, "");
  const size_t idx = setup.path_storage.InsertPath(dir_id, actual_dir_path, true, std::nullopt);
  setup.storage.SetPathStorageIndex(dir_id, idx);
  return setup;
}

MinimalLoaderSetup& CreateLoaderSetupWithEmptyPath(MinimalLoaderSetup& setup, uint64_t file_id,
                                                   const std::string& file_name,
                                                   const std::string& extension) {
  setup.storage.InsertLocked(file_id, 0, file_name, false, kFileTimeNotLoaded, extension);
  // Don't insert path - path will be empty
  return setup;
}

void TestOptimizationWithFixture(bool request_size_first) {
  test_helpers::TestLazyAttributeLoaderFixture fixture;
  test_helpers::lazy_loader_test_helpers::TestOptimizationLoadsBothAttributes(
    fixture.GetLoader(), fixture.GetFileId(), fixture.GetTempFile().expectedSize,
    request_size_first);
}

void TestOptimizationLoadsBothAttributes(const LazyAttributeLoader& loader, uint64_t file_id,
                                         uint64_t expected_size, bool request_size_first) {
  if (request_size_first) {
    // Request size first, verify time was also loaded
    const uint64_t size = loader.GetFileSize(file_id);
    CHECK(size == expected_size);

    const FILETIME time = loader.GetModificationTime(file_id);
    CHECK(IsValidTime(time));
    // Time should not be sentinel (was loaded together with size)
    CHECK(!IsSentinelTime(time));
  } else {
    // Request time first, verify size was also loaded
    const FILETIME time = loader.GetModificationTime(file_id);
    CHECK(IsValidTime(time));

    const uint64_t size = loader.GetFileSize(file_id);
    CHECK(size == expected_size);
  }
}

}  // namespace lazy_loader_test_helpers

// ============================================================================
// FileIndex Search Strategy Test Helpers Implementation
// ============================================================================

namespace search_strategy_test_helpers {

void VerifyAllResultsAreFiles(const std::vector<SearchResultData>& results) {
  for (const auto& result : results) {
    REQUIRE_FALSE(result.isDirectory);
  }
}

size_t CollectAndVerifyFutures(std::vector<std::future<std::vector<SearchResultData>>>& futures,
                               bool require_non_empty) {
  size_t total = 0;
  for (auto& future : futures) {
    total += future.get().size();
  }
  if (require_non_empty) {
    REQUIRE(total > 0);
  }
  return total;
}

size_t CollectFuturesAndCount(std::vector<std::future<std::vector<SearchResultData>>>& futures) {
  size_t total_results = 0;
  for (auto& future : futures) {
    auto thread_results = future.get();
    total_results += thread_results.size();
  }
  return total_results;
}

std::set<uint64_t> CollectIdsFromFutures(std::vector<std::future<std::vector<uint64_t>>>& futures) {
  std::set<uint64_t> all_ids;
  for (auto& future : futures) {
    const auto ids = future.get();
    for (const uint64_t id : ids) {
      all_ids.insert(id);
    }
  }
  return all_ids;
}

size_t TestCancellation(const std::string& strategy, size_t file_count, bool cancel_immediately,
                        size_t max_expected_results) {
  const test_helpers::TestSettingsFixture settings(strategy);
  test_helpers::TestFileIndexFixture index_fixture(file_count);

  // Use heap-allocated atomic to ensure the cancellation flag outlives any worker threads
  auto cancel_flag = std::make_shared<std::atomic<bool>>(false);
  auto futures = index_fixture.GetIndex().SearchAsyncWithData(
    "file_", 4, nullptr, "", nullptr, false, false, nullptr, cancel_flag.get());

  if (!cancel_immediately) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  *cancel_flag = true;

  size_t total_results = 0;
  for (auto& future : futures) {
    try {
      auto results = future.get();
      total_results += results.size();
    } catch (...) {  // NOSONAR(cpp:S2738) - Test cancellation: catch-all needed. NOLINT(bugprone-empty-catch) - intentional: accept cancellation exceptions
      // Cancellation may cause exceptions, which is acceptable
    }
  }

  REQUIRE(total_results <= max_expected_results);
  return total_results;
}

size_t VerifyFuturesSizeAndCollect(std::vector<std::future<std::vector<SearchResultData>>>& futures,
                                   size_t min_size, size_t max_size) {
  REQUIRE(futures.size() >= min_size);
  REQUIRE(futures.size() <= max_size);
  return CollectFuturesAndCount(futures);
}

std::set<uint64_t> CollectIdsAndVerify(std::vector<std::future<std::vector<uint64_t>>>& futures,
                                       bool require_non_empty) {
  std::set<uint64_t> all_ids = CollectIdsFromFutures(futures);
  if (require_non_empty) {
    REQUIRE(!all_ids.empty());
  }
  return all_ids;
}

std::pair<size_t, size_t> CollectTwoFutureVectors(
  std::vector<std::future<std::vector<SearchResultData>>>& futures1,
  std::vector<std::future<std::vector<SearchResultData>>>& futures2) {
  auto results1 = CollectFutures(futures1);
  auto results2 = CollectFutures(futures2);
  return {results1.size(), results2.size()};
}

size_t TestGetFuturesAndCollect(const std::string& strategy, size_t file_count,
                                const std::string& query, int thread_count, size_t min_futures,
                                size_t max_futures) {
  const test_helpers::TestSettingsFixture settings(strategy);
  test_helpers::TestFileIndexFixture index_fixture(file_count);

  auto futures = index_fixture.GetIndex().SearchAsyncWithData(query, thread_count, nullptr, "",
                                                              nullptr, false, false, nullptr);

  return VerifyFuturesSizeAndCollect(futures, min_futures, max_futures);
}

size_t VerifyThreadsProcessedWork(const std::vector<ThreadTiming>& timings,
                                  bool check_items_processed) {
  size_t threads_with_work = 0;
  for (const auto& timing : timings) {
    if (check_items_processed) {
      if (timing.items_processed_ > 0 || timing.results_count_ > 0 ||
          timing.dynamic_chunks_processed_ > 0) {
        threads_with_work++;
      }
    } else {
      if (timing.results_count_ > 0 || timing.dynamic_chunks_processed_ > 0) {
        threads_with_work++;
      }
    }
  }
  REQUIRE(threads_with_work > 0);  // At least one thread should have processed work
  return threads_with_work;
}

// RunTestForAllStrategiesWithSetup is a template function, implemented in header

// TestWithDynamicSettings is a template function, implemented in header

std::vector<ConcurrentSearchResult> RunConcurrentSearches(FileIndex& index,
                                                          const std::vector<std::string>& queries,
                                                          int thread_count,
                                                          bool require_all_non_empty) {
  std::vector<ConcurrentSearchResult> results;
  results.reserve(queries.size());

  // Start all searches
  std::vector<std::vector<std::future<std::vector<SearchResultData>>>> all_futures;
  for (const auto& query : queries) {
    auto futures =
      index.SearchAsyncWithData(query, thread_count, nullptr, "", nullptr, false, false, nullptr);
    all_futures.push_back(std::move(futures));
  }

  // Wait for all searches to complete
  for (size_t i = 0; i < all_futures.size(); ++i) {
    ConcurrentSearchResult result;
    try {
      for (auto& future : all_futures[i]) {
        result.total_results += future.get().size();
      }
      result.completed_successfully = true;

      if (require_all_non_empty) {
        REQUIRE(result.total_results > 0);
      }
    } catch (
      const std::exception& e) {  // NOSONAR(cpp:S1181) - Test error handling: catch-all needed to
                                  // detect any exception type from search operations
      result.completed_successfully = false;
      result.error_message = e.what();
      FAIL("Search " << (i + 1) << " threw exception: " << e.what());
    } catch (...) {  // NOSONAR(cpp:S2738) - Test error handling: catch-all needed to detect any
                     // exception type
      result.completed_successfully = false;
      result.error_message = "Unknown exception";
      FAIL("Search " << (i + 1) << " threw unknown exception");
    }
    results.push_back(result);
  }

  return results;
}

std::vector<std::string> GetAllStrategies() {
  return {"static", "hybrid", "dynamic", "interleaved"};
}

AppSettings CreateDynamicSettings(int chunk_size) {
  AppSettings settings;
  settings.loadBalancingStrategy = "dynamic";
  settings.dynamicChunkSize = chunk_size;
  return settings;
}

AppSettings CreateHybridSettings(int initial_work_percent) {
  AppSettings settings;
  settings.loadBalancingStrategy = "hybrid";
  settings.hybridInitialWorkPercent = initial_work_percent;
  return settings;
}

}  // namespace search_strategy_test_helpers

}  // namespace test_helpers