#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>

#include "index/LazyValue.h"
#include "utils/FileSystemUtils.h"
#include "utils/HashMapAliases.h"
#include "utils/StringUtils.h"

// String pool for interning extensions to reduce memory and speed up
// comparisons.
//
// NOTE:
// We use hash_set_stable_t here instead of hash_set_t because StringPool
// stores and returns raw pointers to the underlying std::string instances:
//
//     const std::string* Intern(const std::string& str);
//
// For correctness, those pointers must remain stable across insertions and
// rehashes. hash_set_stable_t maps to:
//   - boost::unordered_set when FAST_LIBS_BOOST is enabled (node-based, stable)
//   - std::unordered_set otherwise (node-based, stable)
//
// Both are node-based containers that maintain pointer stability across rehashes.
//
// See docs/design/STRING_POOL_DESIGN.md for detailed documentation.
class StringPool {
public:
  // Thread-safe variant: acquires pool_mutex_ before inserting.
  // Use from contexts that do NOT already hold the index exclusive lock.
  const std::string* Intern(std::string_view str) {
    const std::scoped_lock lock(pool_mutex_);
    return InternImpl(str);
  }

  // Lock-free variant: caller must already hold the index exclusive lock,
  // which serializes all writers — pool_mutex_ would be uncontended anyway.
  // Use from InsertLocked / RenameLocked to avoid 500k+ redundant mutex ops.
  const std::string* InternUnderLock(std::string_view str) {
    return InternImpl(str);
  }

private:
  // Shared implementation: fast path skips ToLower alloc when str is already
  // lowercase (the common case for file extensions such as .txt, .exe, .dll).
  const std::string* InternImpl(std::string_view str) {
    if (const bool already_lower = std::all_of(  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
            str.begin(), str.end(),
            [](unsigned char c) { return c < 'A' || c > 'Z'; });
        already_lower) {
      std::string key(str);
      if (auto it = interned_strings_.find(key); it != interned_strings_.end()) {
        return &(*it);  // pool hit — no ToLower alloc needed
      }
      return &(*interned_strings_.insert(std::move(key)).first);
    }
    // Slow path: normalize to lowercase before inserting.
    return &(*interned_strings_.insert(ToLower(str)).first);
  }

  std::mutex pool_mutex_;  // NOLINT(readability-identifier-naming) - snake_case_ per project convention
  hash_set_stable_t<std::string> interned_strings_;  // NOLINT(readability-identifier-naming) - snake_case_ per project convention
};

// Represents a single file or directory in the index.
// NOTE: name is NOT stored here. Use FileIndexStorage::GetNameCache() during
// RecomputeAllPaths, or derive from PathStorage after the build phase.
//
// Layout: members ordered by alignment (largest first) to minimize padding.
// Current size 48 bytes; previous order had 13 bytes padding (56 bytes total).
// Saves ~8 bytes per entry (~8 MB at 1M entries), improving L3 cache hit rate.
struct FileEntry {
  uint64_t parentID = 0;     // NOLINT(readability-identifier-naming) - camelCase kept for backward compatibility
  const std::string* extension = nullptr;  // Interned extension pointer
  mutable LazyFileSize fileSize{};  // NOLINT(readability-identifier-naming,readability-redundant-member-init) - explicit default for clarity
  mutable LazyFileTime lastModificationTime{};  // NOLINT(readability-identifier-naming,readability-redundant-member-init) - explicit default for clarity
  // Index into PathStorage SoA for this entry's path. SIZE_MAX = not set (e.g. before path inserted).
  size_t path_storage_index = static_cast<size_t>(-1);  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  uint16_t name_length = 0;  // Length of filename component; derive name as last name_length chars of PathStorage path
  bool isDirectory = false;   // NOLINT(readability-identifier-naming) - see comment above
};

// Core data storage for FileIndex
// Manages the transactional data model (hash_map) and related structures
//
// Responsibilities:
// - Core CRUD operations (Insert, Remove, Rename, Move)
// - Entry lookup and metadata access
// - String interning for extensions (StringPool)
// - Directory path cache
// - Size tracking
//
// Note: This class does NOT manage path storage (SoA arrays) - that's handled
// by PathStorage. FileIndex coordinates between FileIndexStorage and PathStorage.
class FileIndexStorage {
public:
  // Constructor - takes a reference to the shared mutex (owned by FileIndex)
  explicit FileIndexStorage(std::shared_mutex& mutex) : shared_mutex_ref_(mutex) {}

  // Core CRUD operations
  // Note: These methods assume the caller holds the appropriate lock
  // (InsertLocked, RemoveLocked, etc. are called from FileIndex which manages locking)
  void InsertLocked(uint64_t id, uint64_t parent_id, std::string_view name,
                    bool is_directory, FILETIME modification_time,
                    std::string_view extension);
  void RemoveLocked(uint64_t id);
  [[nodiscard]] bool RenameLocked(uint64_t id, std::string_view new_name, [[maybe_unused]] std::string& old_full_path, [[maybe_unused]] std::string& new_full_path);
  [[nodiscard]] bool MoveLocked(uint64_t id, uint64_t new_parent_id, [[maybe_unused]] std::string& old_full_path, [[maybe_unused]] std::string& new_full_path);

  // Lookup operations
  [[nodiscard]] const FileEntry* GetEntry(uint64_t id) const;
  FileEntry* GetEntryMutable(uint64_t id); // For modifications (assumes lock is held, fields are mutable)
  [[nodiscard]] size_t Size() const { return entry_count_.load(std::memory_order_relaxed); }

  // Update operations (assume lock is already held, methods are const because FileEntry fields are mutable)
  void UpdateFileSize(uint64_t id, uint64_t size);
  void UpdateModificationTime(uint64_t id, const FILETIME& time);

  // Set PathStorage SoA index for an entry (used after InsertPath / RebuildPathBuffer).
  // Caller must hold unique_lock on mutex.
  void SetPathStorageIndex(uint64_t id, size_t index);

  // String pool for extensions
  const std::string* InternExtension(std::string_view ext);

  // Temporary name cache — populated by InsertLocked/RenameLocked during index build,
  // consumed by PathRecomputer::RecomputeAllPaths to walk parent chains, then released.
  // After ReleaseNameCache(), derive names from PathStorage (last name_length chars of path).
  [[nodiscard]] const hash_map_t<uint64_t, std::string>& GetNameCache() const { return name_cache_; }
  void ReleaseNameCache() { hash_map_t<uint64_t, std::string>{}.swap(name_cache_); }

  // Directory cache operations
  [[nodiscard]] inline uint64_t GetDirectoryId(std::string_view path) const {
#ifdef FAST_LIBS_BOOST
    // Transparent lookup: boost::unordered_map + TransparentStringHash avoids string allocation
    if (auto it = directory_path_to_id_.find(path); it != directory_path_to_id_.end()) {
      return it->second;
    }
#else
    // std::unordered_map (C++17) does not support heterogeneous lookup; construct key once
    const std::string path_str(path);
    if (auto it = directory_path_to_id_.find(path_str); it != directory_path_to_id_.end()) {
      return it->second;
    }
#endif  // FAST_LIBS_BOOST
    return 0;
  }
  void CacheDirectory(std::string_view path, uint64_t id);
  void RemoveDirectoryFromCache(std::string_view path);
  void ClearDirectoryCache();

  /**
   * @brief Clear all entries from the index
   *
   * Clears the index, directory cache, and resets the size counter.
   * This is used when rebuilding the index from scratch (e.g., when starting
   * a new folder crawl).
   *
   * @pre Caller must hold unique_lock on mutex
   */
  void ClearLocked();

  // Synchronization (returns reference to shared mutex)
  [[nodiscard]] std::shared_mutex& GetMutex() const { return shared_mutex_ref_; }

  // Iterator support (for iteration over id_to_entry_)
  using Iterator = flat_hash_map_t<uint64_t, FileEntry>::const_iterator;
  [[nodiscard]] Iterator begin() const { return id_to_entry_.cbegin(); }  // NOLINT(readability-identifier-naming) - STL-style begin/end names by design
  [[nodiscard]] Iterator end() const { return id_to_entry_.cend(); }  // NOLINT(readability-identifier-naming) - STL-style begin/end names by design

private:
  // Reference to shared mutex (owned by FileIndex, shared with PathStorage)
  std::shared_mutex& shared_mutex_ref_;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members,readability-identifier-naming) - ref owned by FileIndex; snake_case_

  // Core index (ID -> metadata).
  // flat_hash_map_t: with FAST_LIBS_BOOST uses boost::unordered_flat_map (open-addressing,
  // contiguous value storage) for cache-friendly iteration and lookup over 1M+ entries.
  // Falls back to std::unordered_map (node-based) otherwise.
  // NOTE: boost::unordered_flat_map does NOT guarantee pointer stability on rehash.
  // No FileEntry* may be held across an insert. See LazyAttributeLoader check helpers.
  flat_hash_map_t<uint64_t, FileEntry> id_to_entry_;  // NOLINT(readability-identifier-naming) - snake_case_ per project convention

  // String pool for extension interning
  // See docs/design/STRING_POOL_DESIGN.md for detailed documentation
  StringPool extension_string_pool_;  // NOLINT(readability-identifier-naming) - snake_case_ per project convention

  // Directory path cache (path -> ID mapping for fast directory lookups)
  // string_key_map_t: with FAST_LIBS_BOOST uses transparent hash/equal to allow find(string_view);
  // otherwise falls back to std::unordered_map<std::string, V>.
  string_key_map_t<uint64_t> directory_path_to_id_;  // NOLINT(readability-identifier-naming) - snake_case_ per project convention

  // Temporary name cache: ID -> filename string.
  // Populated by InsertLocked/RenameLocked during index build.
  // Used by PathRecomputer::RecomputeAllPaths to walk parent chains.
  // Freed by ReleaseNameCache() after RecomputeAllPaths completes.
  // Node-based (hash_map_t); pointers to values are stable across inserts.
  hash_map_t<uint64_t, std::string> name_cache_;  // NOLINT(readability-identifier-naming) - snake_case_ per project convention

  // Lock-free size counter for performance
  std::atomic<size_t> entry_count_{0};  // NOLINT(readability-identifier-naming) - snake_case_ per project convention
};

