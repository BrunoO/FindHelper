#include "search/SearchThreadPoolManager.h"

#include <exception>
#include <memory>
#include <utility>

#include "core/Settings.h"
#include "utils/Logger.h"
#include "utils/ThreadPool.h"

SearchThreadPoolManager::SearchThreadPoolManager(
    std::shared_ptr<ThreadPool> initial_pool)
    : pool_(std::move(initial_pool)) {}

ThreadPool& SearchThreadPoolManager::GetThreadPool() {
  return *GetPoolSharedPtr();
}

std::shared_ptr<ThreadPool> SearchThreadPoolManager::GetPoolSharedPtr() {
  if (pool_ != nullptr) {
    return pool_;
  }
  pool_ = CreateDefaultThreadPool();
  return pool_;
}

std::shared_ptr<ThreadPool>
SearchThreadPoolManager::GetPoolSharedPtrWithoutCreating() const {
  return pool_;
}

void SearchThreadPoolManager::SetThreadPool(
    std::shared_ptr<ThreadPool> pool) {
  pool_ = std::move(pool);
}

void SearchThreadPoolManager::ResetThreadPool() {
  pool_.reset();
  LOG_INFO_BUILD("SearchThreadPool reset - will be recreated on next use");
}

size_t SearchThreadPoolManager::GetThreadPoolCount() {
  try {
    const ThreadPool& thread_pool = GetThreadPool();
    return thread_pool.GetThreadCount();
  } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - Catch-all safety net
    LOG_ERROR_BUILD("Exception during thread pool initialization: "
                    << e.what());
    return 0;
  }
}

std::shared_ptr<ThreadPool>
SearchThreadPoolManager::CreateDefaultThreadPool() {
  size_t num_threads = 0;
  try {
    AppSettings settings;
    LoadSettings(settings);
    if (settings.searchThreadPoolSize > 0) {
      num_threads = static_cast<size_t>(settings.searchThreadPoolSize);
    }
  } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - Fallback to auto-detect on settings failure
    LOG_ERROR_BUILD("Error loading thread pool size from settings: "
                    << e.what() << ". Using auto-detect.");
  }

  auto pool = std::make_shared<ThreadPool>(num_threads, "SearchPool");
  LOG_INFO_BUILD("SearchThreadPool created with "
                 << pool->GetThreadCount() << " threads"
                 << (num_threads == 0 ? " (auto-detected)"
                                      : " (from settings)"));
  return pool;
}
