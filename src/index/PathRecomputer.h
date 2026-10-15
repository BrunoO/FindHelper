#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "index/FileIndexStorage.h"
#include "path/PathStorage.h"

/**
 * @file PathRecomputer.h
 * @brief Recomputes all paths from index storage (extracted from FileIndex)
 *
 * Responsibilities:
 * - Clear path storage and directory cache
 * - Iterate over FileIndexStorage, build full path via PathBuilder, insert into PathStorage
 * - Reset OneDrive files to sentinel values for lazy loading
 *
 * Design:
 * - Caller (FileIndex) holds unique_lock; PathRecomputer does not take the lock
 * - Single method RecomputeAllPaths() — called by FileIndex::RecomputeAllPaths() under lock
 *
 * @see docs/2026-01-31_FILEINDEX_REFACTORING_PLAN.md Option A
 */
class PathRecomputer {
public:
  struct Stats {
    size_t entries_collected = 0;
    size_t roots = 0;
    size_t adjacency_edges = 0;
    size_t adjacency_buckets = 0;
    size_t unresolved_parent_entries = 0;
    size_t fallback_entries = 0;
    size_t visited_entries = 0;
    size_t max_dfs_depth = 0;
    size_t path_entries = 0;
    size_t empty_paths = 0;
    size_t offset_fast_path_entries = 0;
    uint64_t path_bytes = 0;
    uint64_t collect_time_microseconds = 0;
    uint64_t dfs_time_microseconds = 0;
    PathStorage::Stats path_storage;
  };

  /**
   * Constructs the recomputer with references to storage and path components.
   * @param storage File index storage (paths and metadata).
   * @param path_storage Path storage to be repopulated.
   */
  PathRecomputer(FileIndexStorage& storage,
                 PathStorage& path_storage);

  PathRecomputer(const PathRecomputer&) = delete;
  PathRecomputer& operator=(const PathRecomputer&) = delete;
  PathRecomputer(PathRecomputer&&) = delete;
  PathRecomputer& operator=(PathRecomputer&&) = delete;

  /**
   * Rebuild all paths from storage and reset OneDrive sentinels.
   * Single DFS pass with heuristic bulk reserve (no dry-run).
   * Caller must hold unique_lock on FileIndex's mutex. Nothing may insert
   * into FileIndexStorage during the call (see Step 1 pointer stability).
   */
  Stats RecomputeAllPaths();

private:
  // NOLINTNEXTLINE(readability-identifier-naming) - project convention: snake_case_ for members
  FileIndexStorage& storage_;
  // NOLINTNEXTLINE(readability-identifier-naming)
  PathStorage& path_storage_;
};
