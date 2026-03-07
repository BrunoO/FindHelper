#pragma once

#include <cstdint>
#include <shared_mutex>
#include <string>

#include "index/FileIndexStorage.h"
#include "path/PathStorage.h"
#include "utils/FileSystemUtils.h"
#include "utils/FileTimeTypes.h"

/**
 * @file LazyAttributeLoader.h
 * @brief Handles lazy loading of file attributes (size, modification time)
 *
 * This class encapsulates the lazy loading logic for file attributes, including:
 * - Double-check locking pattern for thread-safe lazy loading
 * - Optimization: Loads both size and modification time together when both needed
 * - Failure handling: Marks attributes as failed to prevent retry loops
 *
 * DESIGN PRINCIPLES:
 * - Preserves exact double-check locking pattern from FileIndex
 * - Never holds locks during I/O operations (critical for performance)
 * - Loads both attributes together when both needed (optimization)
 * - Handles failure cases gracefully (marks as failed, prevents retries)
 *
 * THREAD SAFETY:
 * - All methods are thread-safe (use shared_mutex from FileIndex)
 * - I/O operations happen without any locks held
 * - Double-check locking prevents redundant I/O
 */
class LazyAttributeLoader {
 public:
  // Constructor - takes references to storage components
  // Note: index_mutex_ref_ is a reference to FileIndex's shared_mutex (owned by FileIndex)
  explicit LazyAttributeLoader(FileIndexStorage& storage, PathStorage& path_storage,
                               std::shared_mutex& mutex);

  // Static I/O helper methods (delegate to FileSystemUtils)
  // These are static because they don't depend on instance state
  // They're here for organization and future extensibility
  static FileAttributes LoadAttributes(std::string_view path);
  static uint64_t GetFileSize(std::string_view path);
  static FILETIME GetFileModificationTime(std::string_view path);

  // Main lazy loading methods
  // These preserve the exact double-check locking pattern from FileIndex
  // CRITICAL: Never hold locks during I/O operations!
  // OPTIMIZATION: Loads both attributes together when both needed
  [[nodiscard]] uint64_t GetFileSize(uint64_t id) const;
  [[nodiscard]] FILETIME GetModificationTime(uint64_t id) const;

  // Manual loading methods
  // These are called explicitly (e.g., by UI for visible rows)
  // They hold the lock during I/O (acceptable for manual loading)
  // Returns true if attribute was loaded, false if already loaded or failed
  [[nodiscard]] bool LoadFileSize(uint64_t id);
  [[nodiscard]] bool LoadModificationTime(uint64_t id);

  // Log lazy loading statistics (for performance measurement)
  static void LogStatistics();

 private:
  FileIndexStorage& storage_;   // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members,readability-identifier-naming) - ref by design; snake_case_ per AGENTS.md
  PathStorage& path_storage_;   // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members,readability-identifier-naming)
  std::shared_mutex& index_mutex_ref_;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members,readability-identifier-naming) - ref to FileIndex's index_mutex_; name follows project snake_case_ convention

  /**
   * @brief Mark file attributes as failed (helper for error handling)
   *
   * Marks modification time (and optionally size) as failed to avoid infinite retry loops.
   * Called when I/O operations fail (file deleted, permission denied, network error, etc.).
   *
   * @param id File entry ID
   * @param need_size Whether size also needs to be marked as failed
   * @param entry Current file entry (for checking if size needs marking)
   */
  void MarkAttributesAsFailed(uint64_t id, bool need_size, const FileEntry* entry) const;

  // Helper methods (to be implemented in Step 3)
  // template<typename T>
  // T LoadWithDoubleCheck(...) const;
};
