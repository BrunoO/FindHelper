/**
 * @file MockSearchableIndex.h
 * @brief Mock implementation of ISearchableIndex for unit testing
 *
 * This mock implementation allows testing ParallelSearchEngine and other
 * components that depend on ISearchableIndex without requiring a full
 * FileIndex instance.
 *
 * Features:
 * - Easy test data setup
 * - Thread-safe (uses shared_mutex)
 * - Supports all ISearchableIndex methods
 * - Configurable test scenarios
 */

#pragma once

#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "index/FileIndexStorage.h"
#include "index/ISearchableIndex.h"
#include "index/LazyValue.h"
#include "path/PathStorage.h"

/**
 * Mock implementation of ISearchableIndex for testing
 *
 * This class provides a simple, controllable implementation of ISearchableIndex
 * that can be easily configured with test data. It uses PathStorage internally
 * to manage SoA arrays, matching the real FileIndex implementation.
 */
class MockSearchableIndex final : public ISearchableIndex {
public:
  /**
   * Constructor
   *
   * Creates an empty mock index. Use AddEntry() to populate with test data.
   */
  MockSearchableIndex() : storage_(mutex_) {}

  /**
   * Destructor
   */
  ~MockSearchableIndex() override = default;

  // ISearchableIndex interface implementation
  PathStorage::SoAView GetSearchableView() const override {
    return path_storage_.GetReadOnlyView();
  }

  std::shared_mutex& GetMutex() const override {
    return mutex_;
  }

  const FileEntry* GetEntry(uint64_t id) const override {
    const std::shared_lock lock(mutex_);
    if (auto it = entries_.find(id); it != entries_.end()) {
      return &it->second;
    }
    return nullptr;
  }

  size_t GetTotalItems() const override {
    const std::shared_lock lock(mutex_);
    return path_storage_.GetSize();
  }

  size_t GetStorageSize() const override {
    const std::shared_lock lock(mutex_);
    return path_storage_.GetStorageSize();
  }

  /**
   * Add a test entry to the mock index
   *
   * This method adds a file or directory entry to the mock index, updating
   * both the entry map and the path storage arrays.
   *
   * @param id File/directory ID
   * @param parent_id Parent directory ID (0 for root)
   * @param name File or directory name
   * @param is_directory True if directory, false if file
   * @param full_path Full path for the entry (e.g., "C:\\Test\\file.txt")
   */
  void AddEntry(uint64_t id, uint64_t parent_id, std::string_view name,
                bool is_directory, std::string_view full_path) {
    const std::unique_lock lock(mutex_);

    // Create FileEntry
    FileEntry entry;
    entry.parentID = parent_id;
    entry.name_length = static_cast<uint16_t>(name.size());
    entry.isDirectory = is_directory;
    entry.fileSize = kFileSizeNotLoaded;
    entry.lastModificationTime = kFileTimeNotLoaded;
    entry.extension = nullptr; // Default to no extension

    // Extract extension if it's a file
    if (!is_directory) {
      const size_t dot_pos = name.find_last_of('.');
      if (dot_pos != std::string_view::npos && dot_pos < name.length() - 1) {
        const std::string ext(name.substr(dot_pos + 1));
        entry.extension = storage_.InternExtension(ext);
      }
    }

    // Store entry
    entries_[id] = entry;

    // Add to path storage (mock does not use path_storage_index in storage_)
    (void)path_storage_.InsertPath(id, std::string(full_path), is_directory, std::nullopt);
  }

  /**
   * Clear all test data
   */
  void Clear() {
    const std::unique_lock lock(mutex_);
    entries_.clear();
    // PathStorage and FileIndexStorage don't have Clear() methods,
    // so we'll need to recreate them. For now, just clear entries.
    // In practice, tests will create a new MockSearchableIndex for each test.
  }

  /**
   * Get number of entries (for testing)
   */
  size_t GetEntryCount() const {
    const std::shared_lock lock(mutex_);
    return entries_.size();
  }

private:
  // Mutable mutex (required by ISearchableIndex interface)
  mutable std::shared_mutex mutex_;

  // Path storage for SoA arrays
  PathStorage path_storage_;

  // FileIndexStorage for entry management and extension interning
  FileIndexStorage storage_;

  // Entry map (ID -> FileEntry)
  std::unordered_map<uint64_t, FileEntry> entries_;
};

