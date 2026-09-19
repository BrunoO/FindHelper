#pragma once

#include "index/FileIndexStorage.h"
#include "path/PathUtils.h"
#include <array>
#include <string>
#include <string_view>

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
  //
  // Parameters:
  //   id:         File/directory ID (for logging)
  //   parent_id:  Parent directory ID
  //   name:       Name of the leaf file/directory (from name_cache.Find(id))
  //   storage:    Reference to FileIndexStorage for parent chain traversal
  //   name_cache: Temporary name arena (populated by FileIndexStorage::InsertLocked)
  static std::string BuildFullPathWithLogging(uint64_t id, uint64_t parent_id,
                                               std::string_view name,
                                               const FileIndexStorage& storage,
                                               const NameArena& name_cache);

private:
  static constexpr int kMaxPathDepth = 64; // Maximum path depth (prevents infinite loops)

  // Internal helper to collect path components from the parent chain.
  // Reads each parent's name from name_cache. Returns number of components.
  static int CollectPathComponents(uint64_t parent_id, std::string_view name,
                                    const FileIndexStorage& storage,
                                    const NameArena& name_cache,
                                    std::array<std::string_view, kMaxPathDepth>& components);

  // Internal helper to build path string from components
  static std::string BuildPathFromComponents(const std::array<std::string_view, kMaxPathDepth>& components,
                                              int component_count);
};

