#pragma once

#include "index/FileIndexStorage.h"
#include "path/PathOperations.h"
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
 * - Single method RecomputeAllPaths() - called by FileIndex::RecomputeAllPaths() under lock
 *
 * @see docs/2026-01-31_FILEINDEX_REFACTORING_PLAN.md Option A
 */
class PathRecomputer {
public:
  /**
   * Constructs the recomputer with references to storage and path components.
   * @param storage File index storage (paths and metadata).
   * @param path_storage Path storage to be repopulated.
   * @param path_operations Path building and directory resolution.
   */
  PathRecomputer(FileIndexStorage& storage,
                 PathStorage& path_storage,
                 PathOperations& path_operations);

  PathRecomputer(const PathRecomputer&) = delete;
  PathRecomputer& operator=(const PathRecomputer&) = delete;
  PathRecomputer(PathRecomputer&&) = delete;
  PathRecomputer& operator=(PathRecomputer&&) = delete;

  /**
   * Rebuild all paths from storage and reset OneDrive sentinels.
   * Caller must hold unique_lock on FileIndex's mutex.
   */
  void RecomputeAllPaths();

private:
  // NOLINTNEXTLINE(readability-identifier-naming) - project convention: snake_case_ for members
  FileIndexStorage& storage_;
  // NOLINTNEXTLINE(readability-identifier-naming)
  PathStorage& path_storage_;
  // NOLINTNEXTLINE(readability-identifier-naming)
  PathOperations& path_operations_;
};
