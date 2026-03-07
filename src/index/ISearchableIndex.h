#pragma once

/**
 * @file ISearchableIndex.h
 * @brief Interface for searchable index implementations
 *
 * This interface provides read-only access to FileIndex data for search
 * operations, eliminating the need for friend class declarations.
 *
 * FileIndex implements this interface to provide access to its internal
 * data structures for parallel search operations.
 */

#include "index/FileIndexStorage.h"
#include "path/PathStorage.h"
#include <shared_mutex>

// Forward declaration (matches struct definition in FileIndexStorage.h)
struct FileEntry;

/**
 * Interface for searchable index implementations
 * 
 * This interface provides read-only access to FileIndex data for search
 * operations, eliminating the need for friend class declarations.
 * 
 * FileIndex implements this interface to provide access to its internal
 * data structures for parallel search operations.
 */
class ISearchableIndex {
public:
  virtual ~ISearchableIndex() = default;
  
protected:
  // Protected default constructor (interface class)
  ISearchableIndex() = default;
  
public:
  // Non-copyable, non-movable (interface class)
  ISearchableIndex(const ISearchableIndex&) = delete;
  ISearchableIndex& operator=(const ISearchableIndex&) = delete;
  ISearchableIndex(ISearchableIndex&&) = delete;
  ISearchableIndex& operator=(ISearchableIndex&&) = delete;

  /**
   * Get read-only view of SoA arrays for parallel search
   * 
   * Returns a zero-copy view of the Structure of Arrays (SoA) layout
   * used for efficient parallel searching. The view is only valid while
   * no modifications are made to the underlying data.
   * 
   * @return SoAView containing pointers to all arrays
   */
  [[nodiscard]] virtual PathStorage::SoAView GetSearchableView() const = 0;

  /**
   * Get mutex for synchronization
   * 
   * Returns the shared_mutex used for thread-safe access to the index.
   * Worker threads should acquire a shared_lock before accessing data.
   * 
   * @return Reference to the shared_mutex
   */
  [[nodiscard]] virtual std::shared_mutex& GetMutex() const = 0;

  /**
   * Get FileEntry by ID
   * 
   * Returns a pointer to the FileEntry for the given ID, or nullptr
   * if not found. The pointer is valid only while holding a lock.
   * 
   * @param id File ID
   * @return Pointer to FileEntry, or nullptr if not found
   */
  [[nodiscard]] virtual const FileEntry* GetEntry(uint64_t id) const = 0;

  /**
   * Get total number of items in the index
   * 
   * @return Total number of items (including deleted entries)
   */
  [[nodiscard]] virtual size_t GetTotalItems() const = 0;

  /**
   * Get storage size in bytes
   * 
   * Returns the total size of the path storage buffer in bytes.
   * Used for determining optimal thread count based on data size.
   * 
   * @return Total bytes in path storage
   */
  [[nodiscard]] virtual size_t GetStorageSize() const = 0;
};

