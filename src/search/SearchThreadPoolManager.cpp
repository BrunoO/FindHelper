#include "search/SearchThreadPoolManager.h"

#include <exception>
#include <thread>

#include <nlohmann/json.hpp>

#include "core/Settings.h"
#include "search/SearchThreadPool.h"
#include "utils/Logger.h"

// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init,hicpp-member-init) - pool_ initialized in initializer list below
SearchThreadPoolManager::SearchThreadPoolManager(
    std::shared_ptr<SearchThreadPool> initial_pool)
    : pool_(std::move(initial_pool)) {}

SearchThreadPool& SearchThreadPoolManager::GetThreadPool() {
  return *GetPoolSharedPtr();
}

std::shared_ptr<SearchThreadPool> SearchThreadPoolManager::GetPoolSharedPtr() {
  if (pool_ != nullptr) {
    return pool_;
  }
  pool_ = CreateDefaultThreadPool();
  if (pool_ == nullptr) {
    size_t num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) {
      num_threads = 2;
    }
    pool_ = std::make_shared<SearchThreadPool>(num_threads);
    LOG_ERROR_BUILD("SearchThreadPool is null after initialization - created "
                    "fallback pool with "
                    << num_threads << " threads");
  }
  return pool_;
}

std::shared_ptr<SearchThreadPool>
SearchThreadPoolManager::GetPoolSharedPtrWithoutCreating() const {
  return pool_;
}

// NOLINTNEXTLINE(readability-identifier-naming) - public API method name SetThreadPool
void SearchThreadPoolManager::SetThreadPool(
    std::shared_ptr<SearchThreadPool> pool) {
  pool_ = std::move(pool);
}

void SearchThreadPoolManager::ResetThreadPool() {
  pool_.reset();
  LOG_INFO_BUILD("SearchThreadPool reset - will be recreated on next use");
}

size_t SearchThreadPoolManager::GetThreadPoolCount() {
  try {
    const SearchThreadPool& thread_pool = GetThreadPool();
    return thread_pool.GetThreadCount();
  } catch (const std::bad_alloc& e) {
    LOG_ERROR_BUILD("Memory allocation failure during thread pool initialization: "
                    << e.what());
    return 0;
  } catch (const std::runtime_error& e) {  // NOSONAR(cpp:S1181) - Runtime error category
    LOG_ERROR_BUILD("Thread pool initialization failed: " << e.what());
    return 0;
  } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - Catch-all safety net
    LOG_ERROR_BUILD("Unexpected exception during thread pool initialization: "
                    << e.what());
    return 0;
  }
}

std::shared_ptr<SearchThreadPool>
SearchThreadPoolManager::CreateDefaultThreadPool() {  // NOLINT(readability-function-cognitive-complexity)
  try {
    AppSettings settings;
    LoadSettings(settings);

    size_t num_threads = 0;
    if (settings.searchThreadPoolSize == 0) {
      num_threads = std::thread::hardware_concurrency();
      if (num_threads == 0) {
        num_threads = 2;
      }
    } else {
      num_threads = static_cast<size_t>(settings.searchThreadPoolSize);
    }

    auto pool = std::make_shared<SearchThreadPool>(num_threads);
    LOG_INFO_BUILD("SearchThreadPool created with "
                   << num_threads << " threads"
                   << (settings.searchThreadPoolSize == 0 ? " (auto-detected)"
                                                          : " (from settings)"));
    return pool;
  } catch (const nlohmann::json::exception& e) {
    (void)e;
    LOG_ERROR_BUILD("Failed to load thread pool size from settings (JSON error): "
                    << e.what() << ". Using auto-detect.");
    size_t num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) {
      num_threads = 2;
    }
    auto pool = std::make_shared<SearchThreadPool>(num_threads);
    LOG_INFO_BUILD("SearchThreadPool created with "
                   << num_threads << " threads (auto-detected, fallback)");
    return pool;
  } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - Final safety net
    LOG_ERROR_BUILD("Error loading thread pool size from settings: "
                    << e.what() << ". Using auto-detect.");
    size_t num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) {
      num_threads = 2;
    }
    auto pool = std::make_shared<SearchThreadPool>(num_threads);
    LOG_INFO_BUILD("SearchThreadPool created with "
                   << num_threads << " threads (auto-detected, fallback)");
    return pool;
  }
}
