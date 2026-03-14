#include "index/LazyAttributeLoader.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <iomanip>
#include <shared_mutex>

#include "index/LazyValue.h"
#include "utils/FileTimeTypes.h"
#include "utils/Logger.h"

// Constructor - takes references to storage components
LazyAttributeLoader::LazyAttributeLoader(FileIndexStorage& storage, PathStorage& path_storage,
                                         std::shared_mutex& mutex)
    : storage_(storage), path_storage_(path_storage), index_mutex_ref_(mutex) {}

// Static I/O helper methods
// These delegate to FileSystemUtils functions (which are inline)
// We wrap them here for organization and to provide a clear interface
// for LazyAttributeLoader

FileAttributes LazyAttributeLoader::LoadAttributes(std::string_view path) {
  // Delegate to FileSystemUtils::GetFileAttributes
  return ::GetFileAttributes(path);
}

uint64_t LazyAttributeLoader::GetFileSize(std::string_view path) {
  // Delegate to FileSystemUtils::GetFileSize
  return ::GetFileSize(path);
}

FILETIME LazyAttributeLoader::GetFileModificationTime(std::string_view path) {
  // Delegate to FileSystemUtils::GetFileModificationTime
  return ::GetFileModificationTime(path);
}

// Performance counters for lazy loading (file-scope, accessible from all functions)
// These are always available regardless of MFT feature status
namespace {
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) - Performance counters must be mutable and shared across functions
std::atomic<size_t> g_lazy_load_count{0};  // NOSONAR(cpp:S5421) - Performance counter must be mutable
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) - Performance counters must be mutable and shared across functions
std::atomic<size_t> g_lazy_load_time_ms{0};  // NOSONAR(cpp:S5421) - Performance counter must be mutable
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) - Performance counters must be mutable and shared across functions
std::atomic<size_t> g_already_loaded_count{0};  // NOSONAR(cpp:S5421) - Performance counter must be mutable
} // anonymous namespace

// Anonymous namespace for helper functions
namespace {

// Result types for the check helpers below.
// Values are captured under the shared lock so no FileEntry* escapes the lock scope.
// This is required for correctness with flat_hash_map_t (boost::unordered_flat_map),
// which does not guarantee pointer stability on rehash.
struct FileSizeCheckResult {
  bool needs_loading = false;
  bool need_time = false;
  bool has_cached_size = false;  // size already loaded; return cached_size
  uint64_t cached_size = 0;
  std::string path;              // populated only when needs_loading is true
};

struct ModTimeCheckResult {
  bool needs_loading = false;
  bool need_size = false;
  bool has_cached_time = false;  // time already loaded; return cached_time
  bool is_directory = false;     // true if directory (caller returns zero FILETIME)
  FILETIME cached_time{};        // NOLINT(readability-redundant-member-init) - FILETIME is a C struct; {} required to guarantee zero-init
  std::string path;              // populated only when needs_loading is true
};

// Check if file size needs loading.
// All FileEntry fields are read and captured under the shared lock; no pointer is returned.
FileSizeCheckResult CheckFileSizeNeedsLoading(
    const FileIndexStorage& storage, const PathStorage& path_storage,
    std::shared_mutex& mutex, uint64_t id) {
  const std::shared_lock lock(mutex);
  const FileEntry* const entry = storage.GetEntry(id);
  if (entry == nullptr) {
    return {};
  }
  if (!entry->fileSize.IsNotLoaded() || entry->isDirectory) {
    const bool loaded = !entry->fileSize.IsNotLoaded();
    return {false, false, loaded, loaded ? entry->fileSize.GetValue() : 0ULL, {}};
  }
  const bool need_time = entry->lastModificationTime.IsNotLoaded();
  return {true, need_time, false, 0ULL, path_storage.GetPath(id)};
}

// Check if modification time needs loading.
// All FileEntry fields are read and captured under the shared lock; no pointer is returned.
ModTimeCheckResult CheckModificationTimeNeedsLoading(
    const FileIndexStorage& storage, const PathStorage& path_storage,
    std::shared_mutex& mutex, uint64_t id) {
  const std::shared_lock lock(mutex);
  const FileEntry* const entry = storage.GetEntry(id);
  if (entry == nullptr) {
    return {};
  }
  if (!entry->lastModificationTime.IsNotLoaded()) {
    return {false, false, true, false, entry->lastModificationTime.GetValue(), {}};
  }
  if (entry->isDirectory) {
    return {false, false, false, true, {}, {}};
  }
  const bool need_size = entry->fileSize.IsNotLoaded();
  return {true, need_size, false, false, {}, path_storage.GetPath(id)};
}

// Helper function to validate path and mark file size as failed if invalid
[[nodiscard]] bool ValidatePathAndMarkFileSizeFailed(uint64_t id, std::string_view path,
                                                     FileIndexStorage& storage,
                                                     std::shared_mutex& mutex) {
  if (path.empty()) {
    LOG_WARNING_BUILD("LazyAttributeLoader::GetFileSize: Empty path for ID " << id);
    const std::unique_lock lock(mutex);
    if (const FileEntry* entry = storage.GetEntry(id);
        entry != nullptr && entry->fileSize.IsNotLoaded()) {
      FileEntry* const mutable_entry = storage.GetEntryMutable(id);
      if (mutable_entry != nullptr) {
        mutable_entry->fileSize.MarkFailed();
      }
    }
    return false;
  }
  return true;
}

// Helper function to validate path and mark modification time as failed if invalid
[[nodiscard]] bool ValidatePathAndMarkModificationTimeFailed(uint64_t id, std::string_view path,
                                                             FileIndexStorage& storage,
                                                             std::shared_mutex& mutex) {
  if (path.empty()) {
    LOG_WARNING_BUILD("LazyAttributeLoader::GetModificationTime: Empty path for ID " << id);
    const std::unique_lock lock(mutex);
    if (const FileEntry* entry = storage.GetEntry(id);
        entry != nullptr && entry->lastModificationTime.IsNotLoaded()) {
      FileEntry* const mutable_entry = storage.GetEntryMutable(id);
      if (mutable_entry != nullptr) {
        mutable_entry->lastModificationTime.MarkFailed();
      }
    }
    return false;
  }
  return true;
}

// Helper function to perform I/O and load file size (and optionally modification time)
// Semantics: success must come only from the attribute provider (attrs.success). Never derive
// success from path non-empty or similar; on failure the caller must MarkFailed() and must not
// write size (e.g. 0) as a valid cached value.
std::tuple<uint64_t, FILETIME, bool> LoadFileSizeAttributes(std::string_view path, bool need_time) {
  uint64_t size = 0;
  FILETIME mod_time = kFileTimeNotLoaded;
  bool success = false;

  if (need_time) {
    // Load both attributes in one system call
    const FileAttributes attrs = LazyAttributeLoader::LoadAttributes(path);
    size = attrs.fileSize;
    mod_time = attrs.success ? attrs.lastModificationTime : kFileTimeFailed;
    success = attrs.success;
  } else {
    // Only load size (but still use combined attribute loader to get a reliable success flag)
    const FileAttributes attrs = LazyAttributeLoader::LoadAttributes(path);
    size = attrs.fileSize;
    success = attrs.success;
  }

  return {size, mod_time, success};
}

// Helper function to perform I/O and load modification time (and optionally size)
std::tuple<uint64_t, FILETIME, bool> LoadModificationTimeAttributes(std::string_view path,
                                                                    bool need_size) {
  uint64_t size = 0;
  FILETIME mod_time = kFileTimeNotLoaded;
  bool success = false;

  if (need_size) {
    // Load both attributes in one system call
    const FileAttributes attrs = LazyAttributeLoader::LoadAttributes(path);
    size = attrs.fileSize;
    mod_time = attrs.success ? attrs.lastModificationTime : kFileTimeFailed;
    success = attrs.success;
  } else {
    // Only load modification time
    mod_time = LazyAttributeLoader::GetFileModificationTime(path);
    success = !IsFailedTime(mod_time);
    // Ensure we don't return kFileTimeNotLoaded after I/O attempt
    if (!success && IsSentinelTime(mod_time)) {
      mod_time = kFileTimeFailed;
    }
  }

  return {size, mod_time, success};
}

// Helper function to update file size cache with double-check
// On success: write size (0 is valid for zero-byte files). On failure: call MarkFailed() only;
// do not write size as a valid value (regression: must not cache 0 on failure).
uint64_t UpdateFileSizeCache(uint64_t id, uint64_t size, FILETIME mod_time, bool success,
                             bool need_time, FileIndexStorage& storage, std::shared_mutex& mutex) {
#ifndef NDEBUG
  // Regression: when success is true, size must not be a sentinel (we must not cache sentinels)
  (void)id;
  // NOLINTNEXTLINE(readability-simplify-boolean-expr) - Combined assert keeps success/sentinel invariants explicit for debugging
  assert(!success || (size != kFileSizeNotLoaded && size != kFileSizeFailed));
#endif  // NDEBUG
  const std::unique_lock lock(mutex);
  if (const FileEntry* entry = storage.GetEntry(id); entry != nullptr) {
    // Double-check: Did another thread load it while we were doing I/O?
    if (!entry->fileSize.IsNotLoaded()) {
      return entry->fileSize.GetValue();
    }

    // We're the first to load it - update cache
    if (success) {
      storage.UpdateFileSize(id, size);
      if (need_time && entry->lastModificationTime.IsNotLoaded()) {
        storage.UpdateModificationTime(id, mod_time);
      }
    } else {
      // Mark as failed to avoid infinite retry loops
      FileEntry* const mutable_entry = storage.GetEntryMutable(id);
      if (mutable_entry != nullptr) {
        mutable_entry->fileSize.MarkFailed();
      }
    }
    if (const FileEntry* updated_entry = storage.GetEntry(id); updated_entry != nullptr) {
      return updated_entry->fileSize.GetValue();
    }
    return kFileSizeFailed;
  }
  return kFileSizeFailed;
}

// Helper function to update modification time cache with double-check
FILETIME UpdateModificationTimeCache(uint64_t id, uint64_t size, FILETIME mod_time, bool success,
                                     bool need_size, FileIndexStorage& storage,
                                     std::shared_mutex& mutex) {
#ifndef NDEBUG
  // Regression: when success is true, mod_time must not be a sentinel (we must not cache sentinels).
  // Parallel to the identical check in UpdateFileSizeCache.
  (void)id;
  assert(!success || (!IsSentinelTime(mod_time) && !IsFailedTime(mod_time)));
#endif  // NDEBUG
  const std::unique_lock lock(mutex);
  if (const FileEntry* entry = storage.GetEntry(id); entry != nullptr) {
    // Double-check: Did another thread load it while we were doing I/O?
    if (!entry->lastModificationTime.IsNotLoaded()) {
      return entry->lastModificationTime.GetValue();
    }

    // We're the first to load it - update cache
    if (success) {
      storage.UpdateModificationTime(id, mod_time);
      if (need_size && entry->fileSize.IsNotLoaded()) {
        storage.UpdateFileSize(id, size);
      }
    } else {
      // Mark as failed to avoid infinite retry loops
      FileEntry* const mutable_entry = storage.GetEntryMutable(id);
      if (mutable_entry != nullptr) {
        mutable_entry->lastModificationTime.MarkFailed();
      }
    }
    if (const FileEntry* updated_entry = storage.GetEntry(id); updated_entry != nullptr) {
      return updated_entry->lastModificationTime.GetValue();
    }
    return kFileTimeFailed;
  }
  return kFileTimeFailed;
}
}  // anonymous namespace

// Get file size by ID - automatically loads if not yet loaded
// Preserves exact double-check locking pattern from FileIndex
// CRITICAL: Never hold locks during I/O operations!
// OPTIMIZATION: If modification time also needs loading, loads both together
uint64_t LazyAttributeLoader::GetFileSize(
  uint64_t id) const {  // NOSONAR(cpp:S3776) - Cognitive complexity: helper functions extracted to
                        // reduce complexity while maintaining performance
  // 1. Check with shared lock first (fast, allows concurrent readers).
  //    Path is retrieved under the same lock to avoid a second lock acquisition.
  const auto [needs_loading, need_time, has_cached_size, cached_size, path] =
      CheckFileSizeNeedsLoading(storage_, path_storage_, index_mutex_ref_, id);
  if (!needs_loading) {
    if (has_cached_size) {
      g_already_loaded_count++;
      return cached_size;
    }
    return 0;
  }

  // 2. Validate path before I/O
  if (!ValidatePathAndMarkFileSizeFailed(id, path, storage_, index_mutex_ref_)) {
    return kFileSizeFailed;
  }

  // 4. Do I/O WITHOUT holding any lock (this is the slow part!)
  auto start_time = std::chrono::high_resolution_clock::now();
  auto [size, mod_time, success] = LoadFileSizeAttributes(path, need_time);
  auto end_time = std::chrono::high_resolution_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

  // Update performance counters
  g_lazy_load_count++;
  g_lazy_load_time_ms += elapsed;

  // 5. Update with unique lock (brief - just writing cached value)
  return UpdateFileSizeCache(id, size, mod_time, success, need_time, storage_, index_mutex_ref_);
}

// Get file modification time by ID - automatically loads if not yet loaded
// Preserves exact double-check locking pattern from FileIndex
// CRITICAL: Never hold locks during I/O operations!
// OPTIMIZATION: If size also needs loading, loads both together
FILETIME LazyAttributeLoader::GetModificationTime(
  uint64_t id) const {  // NOSONAR(cpp:S3776) - Cognitive complexity: helper functions extracted to
                        // reduce complexity while maintaining performance
  // 1. Check with shared lock first (fast, allows concurrent readers).
  //    Path is retrieved under the same lock to avoid a second lock acquisition.
  const auto [needs_loading, need_size, has_cached_time, is_directory, cached_time, path] =
      CheckModificationTimeNeedsLoading(storage_, path_storage_, index_mutex_ref_, id);
  if (!needs_loading) {
    if (has_cached_time) {
      g_already_loaded_count++;
      return cached_time;
    }
    if (is_directory) {
      return {0, 0};
    }
    return kFileTimeNotLoaded;
  }

  // 2. Validate path before I/O
  if (!ValidatePathAndMarkModificationTimeFailed(id, path, storage_, index_mutex_ref_)) {
    return kFileTimeFailed;
  }

  // 4. Do I/O WITHOUT holding any lock (this is the slow part!)
  auto start_time = std::chrono::high_resolution_clock::now();
  auto [size, mod_time, success] = LoadModificationTimeAttributes(path, need_size);
  auto end_time = std::chrono::high_resolution_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

  // Update performance counters
  g_lazy_load_count++;
  g_lazy_load_time_ms += elapsed;

  // 5. Update with unique lock (brief - just writing cached value)
  return UpdateModificationTimeCache(id, size, mod_time, success, need_size, storage_, index_mutex_ref_);
}

// Manual loading methods
// Called explicitly (e.g., by UI for visible rows).
// Use same 3-phase pattern as GetFileSize/GetModificationTime: shared lock for path read,
// I/O without lock, unique lock only for write. Reduces index_mutex_ contention during load.
// Returns true if attribute was loaded, false if already loaded or failed.

bool LazyAttributeLoader::LoadFileSize(uint64_t id) {
  // Phase 1: Read path under shared lock (no I/O).
  std::string path;
  {
    const std::shared_lock lock(index_mutex_ref_);
    if (const FileEntry* const entry = storage_.GetEntry(id);
        entry == nullptr || entry->isDirectory || !entry->fileSize.IsNotLoaded()) {
      return false;
    }
    path = path_storage_.GetPath(id);
  }
  if (!ValidatePathAndMarkFileSizeFailed(id, path, storage_, index_mutex_ref_)) {
    return false;
  }

  // Phase 2: I/O without holding any lock.
  const FileAttributes attrs = LazyAttributeLoader::LoadAttributes(path);

  // Phase 3: Write result under unique lock with double-check.
  const std::unique_lock lock(index_mutex_ref_);
  if (const FileEntry* const entry = storage_.GetEntry(id);
      entry == nullptr || entry->isDirectory || !entry->fileSize.IsNotLoaded()) {
    return false;  // Another thread loaded it when !IsNotLoaded().
  }
  if (attrs.success) {
    storage_.UpdateFileSize(id, attrs.fileSize);
    return true;
  }
  if (FileEntry* const mutable_entry = storage_.GetEntryMutable(id); mutable_entry != nullptr) {
    mutable_entry->fileSize.MarkFailed();
  }
  return false;
}

bool LazyAttributeLoader::LoadModificationTime(uint64_t id) {
  // Phase 1: Read path under shared lock (no I/O).
  std::string path;
  {
    const std::shared_lock lock(index_mutex_ref_);
    if (const FileEntry* const entry = storage_.GetEntry(id);
        entry == nullptr || entry->isDirectory || !entry->lastModificationTime.IsNotLoaded()) {
      return false;
    }
    path = path_storage_.GetPath(id);
  }
  if (!ValidatePathAndMarkModificationTimeFailed(id, path, storage_, index_mutex_ref_)) {
    return false;
  }

  // Phase 2: I/O without holding any lock.
  const FileAttributes attrs = LazyAttributeLoader::LoadAttributes(path);
  const bool success = attrs.success;
  const FILETIME mod_time = success ? attrs.lastModificationTime : kFileTimeFailed;

  // Phase 3: Write result under unique lock with double-check.
  const std::unique_lock lock(index_mutex_ref_);
  if (const FileEntry* const entry = storage_.GetEntry(id);
      entry == nullptr || entry->isDirectory || !entry->lastModificationTime.IsNotLoaded()) {
    return (entry != nullptr && !entry->isDirectory) ? !entry->lastModificationTime.IsFailed() : false;
    // Another thread loaded (or failed): true if loaded, false if failed; null/directory -> false.
  }
  if (success) {
    storage_.UpdateModificationTime(id, mod_time);
    return true;
  }
  if (FileEntry* const mutable_entry = storage_.GetEntryMutable(id); mutable_entry != nullptr) {
    mutable_entry->lastModificationTime.MarkFailed();
  }
  return false;
}

namespace {
// Helper for LogStatistics: attempt to log error; swallow exceptions so static destruction never aborts.
void LogStatisticsLogError(const char* prefix, const char* detail) {
  try {
    LOG_ERROR_BUILD(prefix << detail);
  } catch (...) {  // NOLINT(bugprone-empty-catch) - Logger may be destroyed during static destruction; cannot log or rethrow. NOSONAR(cpp:S2738,cpp:S2486) - catch-all required to prevent abort; cannot handle (no Logger) or rethrow in destructor path
  }
}
}  // namespace

// Log lazy loading statistics (for performance measurement)
void LazyAttributeLoader::LogStatistics() {
  // Guard against calling during static destruction (Logger might be destroyed)
  // Use try-catch to prevent abort() if Logger is unavailable
  try {
    const size_t load_count = g_lazy_load_count.load();   // NOSONAR(cpp:S1481,cpp:S1854) - Used in LOG_INFO_BUILD below (macro not expanded by Sonar)
    const size_t total_time = g_lazy_load_time_ms.load();  // NOSONAR(cpp:S1481,cpp:S1854) - Used in LOG_INFO_BUILD below (macro not expanded by Sonar)
    LOG_INFO_BUILD("Lazy Loading Statistics: " << load_count << " loads, "
                  << g_already_loaded_count.load() << " already loaded, "
                  << "total " << total_time << " ms, "
                  << "avg " << std::fixed << std::setprecision(2)
                  << (load_count > 0 ? static_cast<double>(total_time) / static_cast<double>(load_count) : 0.0)
                  << " ms per load");
  } catch (const std::bad_alloc& e) {
    LogStatisticsLogError("LogStatistics() bad_alloc: ", e.what());
  } catch (const std::ios_base::failure& e) {
    LogStatisticsLogError("LogStatistics() ios_base::failure: ", e.what());
  } catch (const std::runtime_error& e) {  // NOSONAR(cpp:S1181) - Category catch; finer-grained types handled by std::exception fallback
    LogStatisticsLogError("LogStatistics() runtime_error: ", e.what());
  } catch (const std::logic_error& e) {  // NOSONAR(cpp:S1181) - Category catch; finer-grained types handled by std::exception fallback
    LogStatisticsLogError("LogStatistics() logic_error: ", e.what());
  } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - Catch-all for other std::exception-derived; required for shutdown safety
    LogStatisticsLogError("LogStatistics() exception: ", e.what());
  } catch (...) {  // NOSONAR(cpp:S1181,cpp:S2486,cpp:S2738) - Prevent abort during static destruction (Logger may be destroyed before static counters)
    LogStatisticsLogError("LogStatistics() unknown exception", "");
  }
}

void LazyAttributeLoader::MarkAttributesAsFailed(uint64_t id, bool need_size,
                                                 const FileEntry* entry) const {
  // CRITICAL: Mark as failed to avoid infinite retry loops
  // File might be deleted, permission denied, network error, etc.
  FileEntry* const mutable_entry = storage_.GetEntryMutable(id);
  if (mutable_entry != nullptr) {
    mutable_entry->lastModificationTime.MarkFailed();
    if (need_size && entry->fileSize.IsNotLoaded()) {
      mutable_entry->fileSize.MarkFailed();
    }
  }
}
