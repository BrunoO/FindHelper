#pragma once

#include <cstddef>
#include <memory>

class SearchThreadPool;

/**
 * @file SearchThreadPoolManager.h
 * @brief Manages search thread pool lifecycle (extracted from FileIndex Option C)
 *
 * Responsibilities:
 * - Lazy creation of thread pool from settings (CreateDefaultThreadPool)
 * - GetThreadPool() / GetPoolSharedPtr() for use by FileIndex and ParallelSearchEngine
 * - SetThreadPool, ResetThreadPool, GetThreadPoolCount
 *
 * FileIndex holds SearchThreadPoolManager and delegates these methods;
 * FileIndex still owns search_engine_ and passes the pool to it.
 *
 * @see docs/2026-01-31_FILEINDEX_REFACTORING_PLAN.md Option C
 */
class SearchThreadPoolManager {
public:
  /** Construct with optional initial pool (nullptr = create lazily on first use). */
  explicit SearchThreadPoolManager(
      std::shared_ptr<SearchThreadPool> initial_pool = nullptr);

  SearchThreadPoolManager(const SearchThreadPoolManager&) = delete;
  SearchThreadPoolManager& operator=(const SearchThreadPoolManager&) = delete;
  SearchThreadPoolManager(SearchThreadPoolManager&&) = delete;
  SearchThreadPoolManager& operator=(SearchThreadPoolManager&&) = delete;

  /** Get thread pool reference; creates pool from settings if null. */
  SearchThreadPool& GetThreadPool();

  /** Get shared_ptr to pool (for ParallelSearchEngine); creates if null. */
  [[nodiscard]] std::shared_ptr<SearchThreadPool> GetPoolSharedPtr();

  /** Get current pool without creating (for constructor); may return nullptr. */
  [[nodiscard]] std::shared_ptr<SearchThreadPool> GetPoolSharedPtrWithoutCreating() const;

  void SetThreadPool(std::shared_ptr<SearchThreadPool> pool);

  void ResetThreadPool();

  /** Returns 0 if pool not yet initialized or on error. */
  [[nodiscard]] size_t GetThreadPoolCount();

private:
  static std::shared_ptr<SearchThreadPool> CreateDefaultThreadPool();

  std::shared_ptr<SearchThreadPool> pool_;  // NOLINT(readability-identifier-naming) - project convention snake_case_ for members
};
