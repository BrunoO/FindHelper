#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "index/LazyValue.h"
#include "index/NtfsFileReference.h"
#include "utils/FileTimeTypes.h"
#include "utils/HashMapAliases.h"
#include "utils/StringUtils.h"

// Bump-pointer arena for the temporary name cache used during index build.
//
// All name strings are stored in a single contiguous buffer (arena_). The
// index_ maps each file ID to a {offset, length} slot within that buffer.
//
// STABILITY CONTRACT: string_view values returned by Find() point directly into
// arena_. They are valid only as long as arena_ is not grown (i.e. no call to
// InsertOrAssign() or UpdateIfPresent() that appends to the buffer). On
// reallocation the buffer moves, leaving any live string_view dangling. In
// practice: InsertOrAssign() is called only during the build loop; Find() is
// called only from PathRecomputer::RecomputeAllPaths(), which runs after the
// build loop completes. No views are held across an insert, so stability is
// guaranteed. Do NOT hold a Find() result across any mutating call.
//
// This replaces hash_map_t<uint64_t, std::string> to eliminate:
//   - One heap allocation per file name (~500K malloc calls at build time)
//   - ~500K free() calls in ReleaseNameCache()
//   - Scattered heap nodes that hurt cache locality in PathBuilder parent-chain walks
class NameArena {
public:
  // NOTE: Slot::offset is uint32_t by design to minimize per-entry memory.
  // This implies a hard 4 GiB arena limit. We intentionally accept this
  // trade-off because current project datasets are far below that size and
  // the memory win is meaningful at scale. If future workloads approach this
  // bound, widen offset to uint64_t/size_t and add explicit overflow guards.
  // Optional: pre-reserve to prevent all reallocations during the build loop.
  void Reserve(size_t entry_count, size_t total_name_bytes) {
    arena_.reserve(total_name_bytes);
    index_.reserve(entry_count);
  }

  // Insert or replace the name for id.
  // New id: appends name to arena.
  // Existing id, name fits in slot: updates in-place (no arena growth).
  // Existing id, name longer: appends new copy; old bytes become dead space
  //   (acceptable — renames during build are rare; the whole arena is freed by Release()).
  void InsertOrAssign(uint64_t id, std::string_view name) {
    name = TruncateAtEmbeddedNull(name);
    assert(name.size() <= std::numeric_limits<uint16_t>::max() &&
           "File name exceeds uint16_t slot capacity");
    // Single hash operation: try_emplace returns the iterator and whether a new
    // entry was created. For the new-entry path (hot path during build loop),
    // this avoids the double lookup that find() + insert_or_assign() would incur.
    const auto [it, inserted] = index_.try_emplace(
        id, Slot{static_cast<uint32_t>(arena_.size()),
                 static_cast<uint16_t>(name.size())});
    if (inserted) {
      // Offset was pre-computed above as arena_.size() before this append.
      arena_.insert(arena_.end(), name.begin(), name.end());
      return;
    }
    // Existing entry: update in-place if the name fits, otherwise append a new copy.
    auto& slot = it->second;
    if (name.size() <= slot.length) {
      std::memcpy(arena_.data() + slot.offset, name.data(), name.size());
      slot.length = static_cast<uint16_t>(name.size());
    } else {
      const auto new_offset = static_cast<uint32_t>(arena_.size());
      arena_.insert(arena_.end(), name.begin(), name.end());
      slot = {new_offset, static_cast<uint16_t>(name.size())};
    }
  }

  // Update an existing entry in-place (or append if longer). No-op if id is
  // absent — e.g. after Release(), so USN rename events arriving post-release
  // are silently ignored, matching the old find()-guarded behaviour.
  void UpdateIfPresent(uint64_t id, std::string_view new_name) {
    new_name = TruncateAtEmbeddedNull(new_name);
    assert(new_name.size() <= std::numeric_limits<uint16_t>::max() &&
           "File name exceeds uint16_t slot capacity");
    const auto it = index_.find(id);
    if (it == index_.end()) {
      return;
    }
    auto& slot = it->second;
    if (new_name.size() <= slot.length) {
      std::memcpy(arena_.data() + slot.offset, new_name.data(), new_name.size());
      slot.length = static_cast<uint16_t>(new_name.size());
    } else {
      const auto new_offset = static_cast<uint32_t>(arena_.size());
      arena_.insert(arena_.end(), new_name.begin(), new_name.end());
      slot = {new_offset, static_cast<uint16_t>(new_name.size())};
    }
  }

  // Return a string_view into the arena for id, or {} if not found.
  // File names are never legitimately empty, so callers may treat {} as "not found".
  [[nodiscard]] std::string_view Find(uint64_t id) const {
    if (const auto it = index_.find(id); it != index_.end()) {
      const auto& slot = it->second;
      return {arena_.data() + slot.offset, slot.length};
    }
    return {};
  }

  [[nodiscard]] bool Empty() const { return index_.empty(); }

  // Free all memory. Called by ReleaseNameCache() and ClearLocked().
  void Release() {
    std::vector<char>{}.swap(arena_);
    flat_hash_map_t<uint64_t, Slot>{}.swap(index_);
  }

private:
  struct Slot {
    uint32_t offset = 0;  // NOLINT(readability-identifier-naming) - plain data slot fields
    uint16_t length = 0;  // NOLINT(readability-identifier-naming) - plain data slot fields
  };

  std::vector<char> arena_;                    // NOLINT(readability-identifier-naming) - snake_case_ per project convention
  flat_hash_map_t<uint64_t, Slot> index_;      // NOLINT(readability-identifier-naming) - snake_case_ per project convention
};


// Represents a single file or directory in the index.
// NOTE: name is NOT stored here. Use FileIndexStorage::GetNameCache() during
// RecomputeAllPaths, or derive from PathStorage after the build phase.
//
// Layout: members ordered by alignment (largest first) to minimize padding.
// sizeof(FileEntry) is typically 40 bytes on 64-bit (extension pointer removed; name_length removed).
struct FileEntry {  // NOSONAR(cpp:S8379) - Lazy cache fields mutated only under FileIndex index_mutex_
  uint64_t parentID = 0;     // NOLINT(readability-identifier-naming) - camelCase kept for backward compatibility
  mutable LazyFileSize fileSize;  // NOLINT(readability-identifier-naming) - camelCase kept for FileEntry API  // NOSONAR(cpp:S8379) - lazy cache; mutated only under index_mutex_
  mutable LazyFileTime lastModificationTime;  // NOLINT(readability-identifier-naming) - camelCase kept for FileEntry API  // NOSONAR(cpp:S8379) - lazy cache; mutated only under index_mutex_
  // Index into PathStorage SoA for this entry's path. SIZE_MAX = not set (e.g. before path inserted).
  size_t path_storage_index = static_cast<size_t>(-1);  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  bool isDirectory = false;   // NOLINT(readability-identifier-naming) - see comment above
};

// Core data storage for FileIndex
// Manages the transactional data model (hash_map) and related structures
//
// Responsibilities:
// - Core CRUD operations (Insert, Remove, Rename, Move)
// - Entry lookup and metadata access
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
  //
  // register_mft_record: when true (USN/FRN ids), id is indexed in record_number_to_id_
  // for ResolveEntryReference. When false (synthetic InsertPath / DirectoryResolver ids),
  // the id must not shadow a real MFT record number.
  void InsertLocked(uint64_t id, uint64_t parent_id, std::string_view name,
                    bool is_directory, FILETIME modification_time,
                    bool register_mft_record = true);
  void RemoveLocked(uint64_t id);
  [[nodiscard]] bool RenameLocked(uint64_t id, std::string_view new_name, [[maybe_unused]] std::string& old_full_path, [[maybe_unused]] std::string& new_full_path);
  [[nodiscard]] bool MoveLocked(uint64_t id, uint64_t new_parent_id, [[maybe_unused]] std::string& old_full_path, [[maybe_unused]] std::string& new_full_path);

  // Lookup operations
  [[nodiscard]] const FileEntry* GetEntry(uint64_t id) const;
  FileEntry* GetEntryMutable(uint64_t id); // For modifications (assumes lock is held, fields are mutable)

  // Resolve a file reference to an indexed entry. Looks up the 48-bit MFT record number
  // first (canonical indexed id), then falls back to an exact 64-bit FRN match.
  // ParentFileReferenceNumber often carries a stale sequence in the upper 16 bits.
  [[nodiscard]] std::pair<const FileEntry*, uint64_t> ResolveEntryReference(
      uint64_t file_reference) const;
  [[nodiscard]] size_t Size() const { return entry_count_.load(); }

  // Update operations (assume lock is already held, methods are const because FileEntry fields are mutable)
  void UpdateFileSize(uint64_t id, uint64_t size);
  void UpdateModificationTime(uint64_t id, const FILETIME& time);

  // Set PathStorage SoA index for an entry (used after InsertPath / RebuildPathBuffer).
  // Caller must hold unique_lock on mutex.
  void SetPathStorageIndex(uint64_t id, size_t index);

  // Temporary name cache — populated by InsertLocked/RenameLocked during index build,
  // consumed by PathRecomputer::RecomputeAllPaths to walk parent chains, then released.
  // After ReleaseNameCache(), derive names from PathStorage (e.g. final path segment).
  [[nodiscard]] const NameArena& GetNameCache() const { return name_cache_; }
  void ReleaseNameCache() { name_cache_.Release(); }

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

  // MFT record number (lower 48 bits) -> canonical indexed id. Updated on insert/remove.
  flat_hash_map_t<uint64_t, uint64_t> record_number_to_id_;  // NOLINT(readability-identifier-naming) - snake_case_ per project convention

  // Directory path cache (path -> ID mapping for fast directory lookups)
  // string_key_map_t: with FAST_LIBS_BOOST uses transparent hash/equal to allow find(string_view);
  // otherwise falls back to std::unordered_map<std::string, V>.
  string_key_map_t<uint64_t> directory_path_to_id_;  // NOLINT(readability-identifier-naming) - snake_case_ per project convention

  // Temporary name cache: ID -> filename string.
  // Populated by InsertLocked/RenameLocked during index build.
  // Used by PathRecomputer::RecomputeAllPaths to walk parent chains.
  // Freed by ReleaseNameCache() after RecomputeAllPaths completes.
  // Arena-backed: one contiguous buffer + flat index; ~500K fewer malloc calls vs node-based map.
  NameArena name_cache_;  // NOLINT(readability-identifier-naming) - snake_case_ per project convention

  // Lock-free size counter for performance
  std::atomic<size_t> entry_count_{0};  // NOLINT(readability-identifier-naming) - snake_case_ per project convention
};

