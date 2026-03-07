#pragma once

#include "index/FileIndexStorage.h"
#include "path/PathUtils.h"
#include "utils/HashMapAliases.h"
#include <array>
#include <string>

// Path computation helper class
// Stateless helper for building full paths from parent chains
//
// Responsibilities:
// - Build full paths from parent ID chains (during RecomputeAllPaths only)
// - Handle volume root logic
//
// Design:
// - Stateless (all methods are static or const)
// - No dependencies on FileIndex (only FileIndexStorage + name_cache)
// - Thread-safe (no shared state)
class PathBuilder {
public:
  // Build full path with depth limit checking and logging.
  // Used exclusively by PathRecomputer::RecomputeAllPaths.
  // name_cache maps entry ID -> filename string; pointers into it are stable
  // (node-based container) so they may be stored in the components array.
  //
  // Parameters:
  //   id:         File/directory ID (for logging)
  //   parent_id:  Parent directory ID
  //   name:       Name of the leaf file/directory (from name_cache[id])
  //   storage:    Reference to FileIndexStorage for parent chain traversal
  //   name_cache: Temporary name cache (populated by FileIndexStorage::InsertLocked)
  static std::string BuildFullPathWithLogging(uint64_t id, uint64_t parent_id,
                                               std::string_view name,
                                               const FileIndexStorage& storage,
                                               const hash_map_t<uint64_t, std::string>& name_cache);

private:
  static constexpr int kMaxPathDepth = 64; // Maximum path depth (prevents infinite loops)

  // Internal helper to collect path components from the parent chain.
  // Reads each parent's name from name_cache. Returns number of components.
  static int CollectPathComponents(uint64_t parent_id, std::string_view name,
                                    const FileIndexStorage& storage,
                                    const hash_map_t<uint64_t, std::string>& name_cache,
                                    std::array<const std::string*, kMaxPathDepth>& components);

  // Internal helper to build path string from components
  static std::string BuildPathFromComponents(const std::array<const std::string*, kMaxPathDepth>& components,
                                              int component_count);
};

