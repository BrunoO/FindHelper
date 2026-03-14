#include "crawler/FolderCrawler.h"

#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <exception>
#include <memory>
#include <new>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <thread>

#ifdef _WIN32
#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem
#endif  // _WIN32

#if defined(__APPLE__)
#include <dirent.h>
#include <fcntl.h>
#include <sys/attr.h>
#include <sys/stat.h>
#include <sys/vnode.h>
#include <unistd.h>
#endif  // defined(__APPLE__)

#if defined(__linux__)
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif  // defined(__linux__)

#include "index/FileIndex.h"
#include "path/PathUtils.h"
#include "utils/Logger.h"
#include "utils/StringUtils.h"
#include "utils/ThreadUtils.h"

namespace {

constexpr std::chrono::milliseconds::rep kWaitPollIntervalMs = 10;
constexpr size_t kDefaultDirEntryReserve = 100;
// Fallback when std::thread::hardware_concurrency() returns 0 (e.g. Linux in Docker/cgroups).
// Use at least 2 threads so crawling retains some parallelism; matches SearchThreadPool fallback.
constexpr size_t kCrawlerMinThreadsWhenHardwareUnknown = 2;

#ifndef _WIN32
// Helper: translate an open()/opendir() errno into a log message and return code.
// Returns true  when the error means "directory does not exist" (callers treat it as success).
// Returns false for all other errors after logging an appropriate warning.
[[nodiscard]] inline bool HandleOpenErrno(int error, std::string_view dir_path,
                                          const char* syscall_name) {
  if (error == ENOENT) {
    return true;
  }
  if (error == EACCES) {
    LOG_WARNING_BUILD("Access denied to directory: " << dir_path);
    return false;
  }
  LOG_WARNING_BUILD(syscall_name << " failed for: " << dir_path << ", error: " << error);
  return false;
}
#endif  // _WIN32

}  // namespace

// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init,hicpp-member-init) - worker_threads_, queue_mutex_, queue_cv_, completion_cv_, work_queue_ are default-constructed
FolderCrawler::FolderCrawler(FileIndex& file_index, const FolderCrawlerConfig& config)
    : file_index_(file_index), config_(config) {
  // Validate config (assert messages omitted to satisfy readability-simplify-boolean-expr)
  assert(config_.batch_size > 0);
  assert(config_.progress_update_interval_ms > 0);
}

FolderCrawler::~FolderCrawler() {
  StopWorkerThreads();
}

void FolderCrawler::StopWorkerThreads() {
  // Signal all threads to stop
  should_stop_.store(true, std::memory_order_release);
  {
    const std::scoped_lock idle_lock(idle_mutex_);
    idle_cv_.notify_all();
  }

  // Wait for all threads to finish
  for (auto& thread : worker_threads_) {
    if (thread.joinable()) {
      thread.join();
    }
  }
}

// NOLINTNEXTLINE(readability-identifier-naming) - Crawl is public API name
bool FolderCrawler::Crawl(std::string_view root_path, std::atomic<size_t>* indexed_file_count,  // NOLINT(readability-function-cognitive-complexity) NOSONAR(cpp:S3776) - Complex error handling and state management required for robust crawling
                          const std::atomic<bool>* cancel_flag) {
  // Input validation
  if (root_path.empty()) {
    LOG_ERROR("FolderCrawler::Crawl called with empty root path");
    return false;
  }

  // Validate config values
  if (config_.batch_size == 0) {
    LOG_ERROR("FolderCrawler::Crawl: Invalid batch_size (must be > 0)");
    return false;
  }

  try {
    LOG_INFO_BUILD("Starting folder crawl from: " << root_path);

    // Reset statistics (in case Crawl() is called multiple times)
    files_processed_.store(0, std::memory_order_relaxed);
    dirs_processed_.store(0, std::memory_order_relaxed);
    error_count_.store(0, std::memory_order_relaxed);
    should_stop_.store(false, std::memory_order_release);
    total_queued_.store(0, std::memory_order_relaxed);
    last_progress_update_ms_.store(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count(),
        std::memory_order_relaxed);

    // Determine thread count
    size_t thread_count = config_.thread_count;
    if (thread_count == 0) {
      thread_count = std::thread::hardware_concurrency();
      if (thread_count == 0) {
        thread_count = kCrawlerMinThreadsWhenHardwareUnknown;
        LOG_WARNING_BUILD("hardware_concurrency() returned 0 (e.g. cgroups/Docker); using "
                          << thread_count << " worker threads for crawling");
      }
    }

    LOG_INFO_BUILD("Using " << thread_count << " worker threads for crawling");

    // (Re-)initialise per-worker queues; handles repeated Crawl() calls.
    num_workers_ = thread_count;
    wq_items_.assign(num_workers_, std::deque<std::string>{});
    wq_mutexes_.clear();
    wq_mutexes_.reserve(num_workers_);
    for (size_t i = 0; i < num_workers_; ++i) {
      wq_mutexes_.emplace_back(std::make_unique<std::mutex>());
    }

    // Create worker threads
    worker_threads_.clear();
    worker_threads_.reserve(thread_count);
    for (size_t i = 0; i < thread_count; ++i) {
      worker_threads_.emplace_back(&FolderCrawler::WorkerThread, this, i,
                                   indexed_file_count, cancel_flag);
    }

    // Seed work: push the root directory to worker 0's queue
    {
      const std::scoped_lock wq_lock(*wq_mutexes_[0]);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - index 0 valid: num_workers_ >= 1
      wq_items_[0].emplace_back(root_path);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    }
    total_queued_.fetch_add(1, std::memory_order_release);
    {
      const std::scoped_lock idle_lock(idle_mutex_);
      idle_cv_.notify_one();
    }

    // Wait for all work to complete using event-driven synchronization
    // Threads finish when: queue is empty AND no workers are active
    size_t last_logged_count = 0;
    auto last_log_time = std::chrono::steady_clock::now();
    const auto log_interval = std::chrono::seconds(2);  // Log every 2 seconds

    {
      std::unique_lock comp_lock(completion_mutex_);

      // Wait until completion signal or cancellation.
      // Use wait_for with timeout to poll for cancellation even if not notified.
      while (!completion_cv_.wait_for(comp_lock, std::chrono::milliseconds(kWaitPollIntervalMs),
             [this, cancel_flag]() {
               if (cancel_flag && cancel_flag->load(std::memory_order_acquire)) {
                 return true;  // Exit on cancellation
               }
               return total_queued_.load(std::memory_order_acquire) == 0 &&
                      active_workers_.load(std::memory_order_acquire) == 0;
             })) {
        // Timeout - loop repeats to check predicate
      }

      // Log periodic progress (final state when woken)
      auto now = std::chrono::steady_clock::now();
      if (const size_t total_processed = files_processed_.load(std::memory_order_relaxed) +
                                         dirs_processed_.load(std::memory_order_relaxed);
          now - last_log_time >= log_interval && total_processed > last_logged_count) {
        LOG_INFO_BUILD("Crawling progress - Files: "
                       << files_processed_.load() << ", Dirs: " << dirs_processed_.load()
                       << ", Active workers: " << active_workers_.load(std::memory_order_acquire)
                       << ", Total queued: " << total_queued_.load(std::memory_order_acquire));
        last_logged_count = total_processed;  // NOSONAR(cpp:S1854) - Value is used in condition above
        last_log_time = now;
      }

      // Signal workers to stop
      should_stop_.store(true, std::memory_order_release);
    }

    // Wake all idle workers so they see should_stop_ and exit
    {
      const std::scoped_lock idle_lock(idle_mutex_);
      idle_cv_.notify_all();
    }

    // Check if cancelled
    const bool cancelled = (cancel_flag != nullptr && cancel_flag->load(std::memory_order_acquire));
    LOG_INFO(cancelled ? "Folder crawl cancelled by user" : "All work completed, threads stopping");

    // Wait for all threads to finish
    for (auto& thread : worker_threads_) {
      if (thread.joinable()) {
        thread.join();
      }
    }

    if (cancelled) {
      return false;
    }

    const size_t total_processed = files_processed_.load() + dirs_processed_.load();
    LOG_INFO_BUILD("Folder crawl completed - Files: "
                   << files_processed_.load() << ", Directories: " << dirs_processed_.load()
                   << ", Errors: " << error_count_.load() << ", Total: " << total_processed);

    return error_count_.load() == 0 || total_processed > 0;
  } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - Catch-all safety net for entire Crawl() operation
    (void)e;       // Suppress unused variable warning in Release mode
    LOG_ERROR_BUILD("FolderCrawler::Crawl: Fatal exception: " << e.what());
    StopWorkerThreads();
    return false;
  } catch (...) {  // NOSONAR(cpp:S2738) - Catch-all needed for non-standard exceptions from filesystem operations
    LOG_ERROR_BUILD("FolderCrawler::Crawl: Unknown fatal exception");
    StopWorkerThreads();
    return false;
  }
}

namespace {
// Helper function to check if worker should stop
bool ShouldStopWorker(const std::atomic<bool>* cancel_flag, const std::atomic<bool>& should_stop) {
  if (cancel_flag != nullptr && cancel_flag->load(std::memory_order_acquire)) {
    return true;
  }
  return should_stop.load(std::memory_order_acquire);
}

// Helper function to signal stop to all workers
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables,readability-identifier-naming) - free function in anonymous namespace, PascalCase per project
void SignalStop(std::atomic<bool>& should_stop, std::condition_variable& idle_cv) {
  should_stop.store(true, std::memory_order_release);
  idle_cv.notify_all();
}

// Helper function to enumerate directory with exception handling
bool EnumerateDirectorySafe(
  const FolderCrawler* crawler,
  std::string_view dir_path,
  std::vector<FolderCrawler::DirectoryEntry>& entries, std::atomic<size_t>& error_count,
  std::atomic<size_t>& active_workers) {
  bool enum_success = false;
  try {  // NOSONAR(cpp:S1141) - Nested try acceptable here: exception handling for directory
         // enumeration within worker thread loop
    enum_success = crawler->EnumerateDirectory(dir_path, entries);
  } catch (
    const std::exception& e) {  // NOSONAR(cpp:S1181) - Catch-all safety net for EnumerateDirectory
                                // (platform-specific implementations may throw various exceptions)
    (void)e;                    // Suppress unused variable warning in Release mode
    LOG_ERROR_BUILD("FolderCrawler: Exception in EnumerateDirectory for: " << dir_path << " - "
                                                                           << e.what());
    error_count.fetch_add(1, std::memory_order_relaxed);
    active_workers.fetch_sub(1, std::memory_order_relaxed);
    return false;
  } catch (...) {  // NOSONAR(cpp:S2738) - Catch-all needed for non-standard exceptions from
                   // platform-specific EnumerateDirectory
    LOG_ERROR_BUILD("FolderCrawler: Unknown exception in EnumerateDirectory for: " << dir_path);
    error_count.fetch_add(1, std::memory_order_relaxed);
    active_workers.fetch_sub(1, std::memory_order_relaxed);
    return false;
  }

  if (!enum_success) {
    error_count.fetch_add(1, std::memory_order_relaxed);
    active_workers.fetch_sub(1, std::memory_order_relaxed);
    return false;
  }
  return true;
}

// Helper function to process a single entry.
// Subdirectories are collected into pending_subdirs for a single deferred
// queue push by ProcessEntries, avoiding one lock acquisition per subdirectory.
void ProcessEntry(  // NOSONAR(cpp:S107) - Function has 10 parameters
  const FolderCrawler::DirectoryEntry& entry,
  std::string_view dir_path,
  std::vector<std::pair<std::string, bool>>& batch, FolderCrawler* crawler,
  std::vector<std::string>& pending_subdirs,
  std::atomic<size_t>& files_processed, std::atomic<size_t>& dirs_processed, size_t batch_size,
  std::atomic<size_t>* indexed_file_count, std::atomic<size_t>& error_count) {
  const std::string full_path = path_utils::JoinPath(dir_path, entry.name);

  // Skip symlinks
  if (entry.is_symlink) {
    LOG_INFO_BUILD("Skipping symlink: " << full_path);
    return;
  }

  // Add to batch with explicit is_directory (crawler knows type from enumeration)
  batch.emplace_back(full_path, entry.is_directory);

  // If directory, stage for deferred queue push; otherwise count as file.
  // The actual work_queue push happens once per directory in ProcessEntries,
  // replacing O(subdirs) lock acquisitions with a single scoped_lock.
  if (entry.is_directory) {
    pending_subdirs.push_back(full_path);
    dirs_processed.fetch_add(1, std::memory_order_relaxed);
  } else {
    files_processed.fetch_add(1, std::memory_order_relaxed);
  }

  // Flush batch if full
  if (batch.size() >= batch_size) {
    try {  // NOSONAR(cpp:S1141) - Nested try acceptable here: exception handling for batch flush
           // within worker thread loop
      crawler->FlushBatch(batch, indexed_file_count);
    } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - Catch-all safety net for FlushBatch (already catches std::bad_alloc and std::runtime_error internally)
      (void)e;       // Suppress unused variable warning in Release mode
      LOG_ERROR_BUILD("FolderCrawler: Exception in FlushBatch: " << e.what());
      error_count.fetch_add(1, std::memory_order_relaxed);
    } catch (...) {  // NOSONAR(cpp:S2738) - Catch-all needed for non-standard exceptions from FlushBatch
      LOG_ERROR_BUILD("FolderCrawler: Unknown exception in FlushBatch");
      error_count.fetch_add(1, std::memory_order_relaxed);
    }
    batch.clear();
  }
}

// Helper function to process all entries in a directory.
// Subdirectories are staged locally then pushed to the owning worker's deque
// via PushSubdirs — one lock per directory and no shared queue contention.
bool ProcessEntries(  // NOSONAR(cpp:S107) - Function has 13 parameters
  const std::vector<FolderCrawler::DirectoryEntry>& entries,
  std::string_view dir_path, std::vector<std::pair<std::string, bool>>& batch, FolderCrawler* crawler,  // NOLINT(hicpp-named-parameter,readability-named-parameter)
  size_t worker_idx,
  const std::atomic<bool>* cancel_flag, std::atomic<bool>& should_stop,
  std::condition_variable& idle_cv,
  std::atomic<size_t>& files_processed, std::atomic<size_t>& dirs_processed, size_t batch_size,
  std::atomic<size_t>* indexed_file_count, std::atomic<size_t>& error_count) {  // NOLINT(hicpp-named-parameter,readability-named-parameter)
  std::vector<std::string> pending_subdirs;
  pending_subdirs.reserve(entries.size());

  for (const auto& entry : entries) {
    if (ShouldStopWorker(cancel_flag, should_stop)) {
      if (cancel_flag != nullptr && cancel_flag->load(std::memory_order_acquire)) {
        SignalStop(should_stop, idle_cv);
      }
      return false;
    }

    ProcessEntry(entry, dir_path, batch, crawler, pending_subdirs,
                 files_processed, dirs_processed, batch_size, indexed_file_count, error_count);
  }

  // Push all collected subdirectories to this worker's own deque.
  // PushSubdirs acquires only wq_mutexes_[worker_idx] (not the global lock).
  crawler->PushSubdirs(worker_idx, pending_subdirs);

  return true;
}
}  // namespace

// NOLINTNEXTLINE(readability-identifier-naming,readability-function-cognitive-complexity) - WorkerThread is public API; loop and error handling required for crawler
void FolderCrawler::WorkerThread(size_t worker_idx,
                                 std::atomic<size_t>* indexed_file_count,
                                 const std::atomic<bool>* cancel_flag) {
  // Set thread name for debugging
  SetThreadName("FolderCrawler-Worker");

  std::vector<std::pair<std::string, bool>> batch;
  batch.reserve(config_.batch_size);

  // Declared outside the loop so the heap allocation is reused across
  // directories; EnumerateDirectory calls entries.clear() on each use.
  std::vector<DirectoryEntry> entries;

  try {
    bool should_exit = false;
    while (!should_exit) {
      // Check for cancellation or stop signal
      if (ShouldStopWorker(cancel_flag, should_stop_)) {
        if (cancel_flag != nullptr && cancel_flag->load(std::memory_order_acquire)) {
          SignalStop(should_stop_, idle_cv_);
        }
        should_exit = true;
        continue;
      }

      // Try own queue first; if empty, steal from peers.
      std::string dir_path = TryGetWork(worker_idx);
      if (dir_path.empty()) {
        // No work found — wait for a notification or poll timeout.
        std::unique_lock idle_lock(idle_mutex_);
        idle_cv_.wait_for(idle_lock, std::chrono::milliseconds(kWaitPollIntervalMs),
                          [this]() { return should_stop_.load() || total_queued_.load() > 0; });
        dir_path = TryGetWork(worker_idx);
      }
      if (dir_path.empty()) {
        continue;
      }

      // Enumerate directory (with exception handling)
      if (!EnumerateDirectorySafe(this, dir_path, entries, error_count_, active_workers_)) {
        continue;
      }

      // Process entries; subdirs pushed to wq_items_[worker_idx] via PushSubdirs
      if (!ProcessEntries(entries, dir_path, batch, this, worker_idx, cancel_flag, should_stop_,
                          idle_cv_, files_processed_, dirs_processed_,
                          config_.batch_size, indexed_file_count, error_count_)) {
        should_exit = true;
      }

      active_workers_.fetch_sub(1, std::memory_order_release);
      SignalCompletionIfLastWorker();
    }
  } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - Catch-all safety net for entire WorkerThread operation
    (void)e;       // Suppress unused variable warning in Release mode
    LOG_ERROR_BUILD("FolderCrawler: Fatal exception in WorkerThread: " << e.what());
    error_count_.fetch_add(1, std::memory_order_relaxed);
  } catch (...) {  // NOSONAR(cpp:S2738) - Catch-all needed for non-standard exceptions from worker thread operations
    LOG_ERROR_BUILD("FolderCrawler: Unknown fatal exception in WorkerThread");
    error_count_.fetch_add(1, std::memory_order_relaxed);
  }

  // Flush remaining batch (with exception handling)
  if (!batch.empty()) {
    try {
      FlushBatch(batch, indexed_file_count);
    } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - Catch-all safety net for final FlushBatch (already catches std::bad_alloc and std::runtime_error internally)
      (void)e;       // Suppress unused variable warning in Release mode
      LOG_ERROR_BUILD("FolderCrawler: Exception in final FlushBatch: " << e.what());
      error_count_.fetch_add(1, std::memory_order_relaxed);
    } catch (...) {  // NOSONAR(cpp:S2738) - Catch-all needed for non-standard exceptions from final FlushBatch
      LOG_ERROR_BUILD("FolderCrawler: Unknown exception in final FlushBatch");
      error_count_.fetch_add(1, std::memory_order_relaxed);
    }
  }
}

void FolderCrawler::SignalCompletionIfLastWorker() {
  if (total_queued_.load(std::memory_order_acquire) == 0 &&
      active_workers_.load(std::memory_order_acquire) == 0) {
    const std::scoped_lock lock(completion_mutex_);
    completion_cv_.notify_all();
  }
}

// NOLINTNEXTLINE(hicpp-named-parameter,readability-named-parameter) - parameters are named (batch, indexed_file_count)
void FolderCrawler::FlushBatch(const std::vector<std::pair<std::string, bool>>& batch,
                               std::atomic<size_t>* indexed_file_count) {  // NOLINT(readability-non-const-parameter) - Parameter modifies atomic value via store()
  // Precondition: calling FlushBatch with an empty batch wastes a lock acquisition.
  assert(!batch.empty() && "FlushBatch must not be called with an empty batch");
  file_index_.InsertPaths(batch, &error_count_);

  // Update progress counter
  if (indexed_file_count != nullptr) {
    const size_t total = files_processed_.load(std::memory_order_relaxed) +
                         dirs_processed_.load(std::memory_order_relaxed);
    const int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now().time_since_epoch())
                               .count();
    int64_t last_update = last_progress_update_ms_.load(std::memory_order_relaxed);

    if (now_ms - last_update >= static_cast<int64_t>(config_.progress_update_interval_ms) &&
        last_progress_update_ms_.compare_exchange_strong(last_update, now_ms,
                                                        std::memory_order_relaxed)) {
      indexed_file_count->store(total, std::memory_order_relaxed);
      LOG_INFO_BUILD("Crawled " << total << " entries...");
    }
  }
}

void FolderCrawler::AcquireWorkItem() {
  const size_t prev_active = active_workers_.fetch_add(1, std::memory_order_relaxed);  // BEFORE sub
  // Invariant: active workers never exceed the pool size (one slot per thread).
  assert(prev_active < num_workers_ && "Active worker count must not exceed pool size");
  total_queued_.fetch_sub(1, std::memory_order_relaxed);
}

// Pop work from own queue; on empty, attempt to steal from peers (try_lock, no blocking).
// Increments active_workers_ BEFORE decrementing total_queued_ to prevent a completion
// false-positive window where both counters simultaneously read zero.
std::string FolderCrawler::TryGetWork(size_t worker_idx) {
  // Try own queue first — typically no contention with other workers.
  {
    const std::scoped_lock lock(*wq_mutexes_[worker_idx]);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - worker_idx < num_workers_ == vector size
    if (!wq_items_[worker_idx].empty()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
      std::string dir = std::move(wq_items_[worker_idx].front());  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
      wq_items_[worker_idx].pop_front();  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
      AcquireWorkItem();
      return dir;
    }
  }

  // Own queue empty — try stealing from peers in round-robin order.
  for (size_t j = 1; j < num_workers_; ++j) {
    const size_t victim = (worker_idx + j) % num_workers_;
    const std::unique_lock lock(*wq_mutexes_[victim], std::try_to_lock);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - victim = (worker_idx+j) % num_workers_
    if (lock.owns_lock() && !wq_items_[victim].empty()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
      std::string dir = std::move(wq_items_[victim].front());  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
      wq_items_[victim].pop_front();  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
      AcquireWorkItem();
      return dir;
    }
  }

  return {};  // No work available
}

// Push a batch of subdirectory paths to worker_idx's own deque, then wake idle workers.
// Notifying under idle_mutex_ creates a happens-before edge that prevents missed wakeups.
void FolderCrawler::PushSubdirs(size_t worker_idx, std::vector<std::string>& subdirs) {
  if (subdirs.empty()) {
    return;
  }
  const size_t n = subdirs.size();
  {
    const std::scoped_lock wq_lock(*wq_mutexes_[worker_idx]);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - worker_idx < num_workers_ == vector size
    for (auto& s : subdirs) {
      wq_items_[worker_idx].push_back(std::move(s));  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    }
  }
  total_queued_.fetch_add(n, std::memory_order_release);
  {
    const std::scoped_lock idle_lock(idle_mutex_);
    idle_cv_.notify_all();
  }
}

// Platform-specific implementation of EnumerateDirectory
#ifdef _WIN32
namespace {
struct FindHandleDeleter {
  void operator()(HANDLE h) const noexcept {
    if (h != INVALID_HANDLE_VALUE) {
      FindClose(h);
    }
  }
};
using UniqueFindHandle = std::unique_ptr<void, FindHandleDeleter>;
}  // namespace

// NOLINTNEXTLINE(readability-identifier-naming) - EnumerateDirectory is public API name
bool FolderCrawler::EnumerateDirectory(std::string_view dir_path,
                                       std::vector<DirectoryEntry>& entries)
  const {  // NOSONAR(cpp:S5817) - Already const, SonarQube false positive
  entries.clear();

  // OPTIMIZATION: Reserve capacity to avoid multiple reallocations
  // Conservative estimate: typical directories have 10-50 entries
  // Reserve for worst case to minimize reallocations
  entries.reserve(kDefaultDirEntryReserve);

  // Convert UTF-8 path to wide string for Windows API (convert to string for Utf8ToWide)
  std::wstring wdir_path = Utf8ToWide(std::string(dir_path));
  if (wdir_path.empty() && !dir_path.empty()) {
    LOG_WARNING_BUILD("Failed to convert path to wide string: " << dir_path);
    return false;
  }

  // Append wildcard for FindFirstFile
  std::wstring search_path = wdir_path;
  if (!search_path.empty() && search_path.back() != L'\\' && search_path.back() != L'/') {
    search_path += L"\\";
  }
  search_path += L"*";

  // Use FindFirstFileEx with FIND_FIRST_EX_LARGE_FETCH for performance.
  // RAII: UniqueFindHandle ensures FindClose is called on scope exit or exception.
  WIN32_FIND_DATAW find_data;
  UniqueFindHandle find_handle(
    FindFirstFileExW(search_path.c_str(),
                     FindExInfoBasic,  // Use basic info (faster, no short name)
                     &find_data,
                     FindExSearchNameMatch,  // Search by name
                     nullptr,
                     FIND_FIRST_EX_LARGE_FETCH  // Large fetch for better performance
    ));

  if (find_handle.get() == INVALID_HANDLE_VALUE) {
    DWORD error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_NO_MORE_FILES) {
      // Empty directory - not an error
      return true;
    }
    if (error == ERROR_ACCESS_DENIED) {
      LOG_WARNING_BUILD("Access denied to directory: " << dir_path);
      return false;
    }
    LOG_WARNING_BUILD("FindFirstFileEx failed for: " << dir_path << ", error: " << error);
    return false;
  }

  // Process first entry
  do {
    // Skip "." and ".."
    if (wcscmp(find_data.cFileName, L".") == 0 || wcscmp(find_data.cFileName, L"..") == 0) {
      continue;
    }

    DirectoryEntry entry;
    entry.name = WideToUtf8(find_data.cFileName);
    if (entry.name.empty()) {
      LOG_WARNING_BUILD("Failed to convert filename to UTF-8, skipping");
      continue;
    }

    // Check if directory
    entry.is_directory = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

    // Check if symlink or junction point
    // FILE_ATTRIBUTE_REPARSE_POINT indicates a symlink, junction, or mount point
    entry.is_symlink = (find_data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;

    // NOTE: We skip symlinks and junction points to avoid infinite loops
    // and redundant processing. If you need to follow symlinks in the future:
    // 1. Add cycle detection (track visited inodes)
    // 2. Add a config option to enable symlink following
    // 3. Use GetFileInformationByHandle to get the actual target
    // See:
    // https://docs.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getfinalpathnamebyhandlew

    entries.push_back(std::move(entry));
  } while (FindNextFileW(find_handle.get(), &find_data) != 0);

  if (DWORD error = GetLastError();
      error != ERROR_NO_MORE_FILES) {  // NOSONAR(cpp:S1854) - GetLastError() must be called
                                       // immediately after FindNextFileW
    LOG_WARNING_BUILD("FindNextFile failed for: " << dir_path << ", error: " << error);
  }

  return true;
}

#elif defined(__APPLE__)

namespace {

// 256 KB buffer for getattrlistbulk — holds ~900 typical entries per syscall,
// replacing the per-entry readdir loop with a single bulk kernel call.
constexpr size_t kBulkAttrBufSize = 256UL * 1024UL;

// Helper: run lstat on a full path and set is_directory / is_symlink from the mode bits.
// Returns false when lstat fails (caller should skip the entry); true on success.
inline bool StatAndClassify(const std::string& full_path,
                            bool& out_is_directory,
                            bool& out_is_symlink) {
  struct stat stat_buf = {};
  if (lstat(full_path.c_str(), &stat_buf) != 0) {
    LOG_WARNING_BUILD("lstat failed for: " << full_path << ", error: " << errno);
    return false;
  }
  const auto mode = static_cast<unsigned>(stat_buf.st_mode);
  out_is_directory = (S_ISDIR(mode) != 0);  // NOLINT(hicpp-signed-bitwise) - S_ISDIR uses bitwise ops on potentially signed constants
  out_is_symlink   = (S_ISLNK(mode) != 0);  // NOLINT(hicpp-signed-bitwise) - S_ISLNK uses bitwise ops on potentially signed constants
  return true;
}

// RAII wrapper for POSIX file descriptors.
struct AutoFd {
  int fd = -1;
  AutoFd() = default;  // Must be explicit: deleted copy/move ctors suppress implicit default ctor
  ~AutoFd() {
    if (fd != -1) {
      close(fd);
    }
  }
  AutoFd(const AutoFd&) = delete;
  AutoFd& operator=(const AutoFd&) = delete;
  AutoFd(AutoFd&&) = delete;
  AutoFd& operator=(AutoFd&&) = delete;
};

// opendir/readdir fallback used when getattrlistbulk returns ENOTSUP
// (e.g. network shares, FAT/ExFAT volumes, older file systems).
bool EnumerateDirectoryOpenDir(std::string_view dir_path,
                               std::vector<FolderCrawler::DirectoryEntry>& entries) {
  DIR* dir = opendir(std::string(dir_path).c_str());
  if (dir == nullptr) {
    return HandleOpenErrno(errno, dir_path, "opendir");
  }

  const struct dirent* entry = nullptr;
  while ((entry = readdir(dir)) != nullptr) {  // NOLINT(concurrency-mt-unsafe) - each call uses its own DIR*; streams not shared across threads
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {  // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay,hicpp-no-array-decay) - d_name is an array
      continue;
    }

    FolderCrawler::DirectoryEntry dir_entry;
    dir_entry.name = entry->d_name;  // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay,hicpp-no-array-decay) - d_name is an array

    if (entry->d_type != DT_UNKNOWN) {
      dir_entry.is_directory = (entry->d_type == DT_DIR);
      dir_entry.is_symlink = (entry->d_type == DT_LNK);
    } else {
      // d_type not filled (e.g. some network FS): fall back to lstat
      std::string full_path(dir_path);
      if (full_path.back() != '/') {
        full_path += "/";
      }
      full_path += entry->d_name;  // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay,hicpp-no-array-decay) - d_name is an array
      if (!StatAndClassify(full_path, dir_entry.is_directory, dir_entry.is_symlink)) {
        continue;
      }
    }

    entries.push_back(std::move(dir_entry));
  }

  closedir(dir);
  return true;
}

}  // namespace

// NOLINTNEXTLINE(readability-identifier-naming,readability-function-cognitive-complexity) - EnumerateDirectory is public API; bulk-parse loop requires nested conditionals NOSONAR(cpp:S3776)
bool FolderCrawler::EnumerateDirectory(std::string_view dir_path,
                                       std::vector<DirectoryEntry>& entries)
  const {  // NOSONAR(cpp:S5350) - Function has no pointer parameters; pointer-to-const suggestion is a false positive
  entries.clear();
  entries.reserve(kDefaultDirEntryReserve);

  // Open the directory as a file descriptor for getattrlistbulk.
  // getattrlistbulk returns name + type for hundreds of entries in one syscall,
  // replacing the per-entry readdir loop (APFS and most local macOS filesystems
  // support this; falls back to opendir/readdir on ENOTSUP).
  AutoFd afd;
  afd.fd = open(std::string(dir_path).c_str(), O_RDONLY);
  if (afd.fd == -1) {
    return HandleOpenErrno(errno, dir_path, "open");
  }

  // Request returned-attribute mask, name, and object type.
  // Per-entry buffer layout (ascending bit order, ATTR_CMN_RETURNED_ATTRS first):
  //   [uint32_t length]
  //   [attribute_set_t returned]    <- ATTR_CMN_RETURNED_ATTRS (0x80000000, placed first)
  //   [attrreference_t name_ref]    <- ATTR_CMN_NAME           (bit 0x00000001)
  //   [fsobj_type_t    obj_type]    <- ATTR_CMN_OBJTYPE        (bit 0x00000008)
  //   [...inline name string...]
  struct attrlist alist = {};
  alist.bitmapcount = ATTR_BIT_MAP_COUNT;
  alist.commonattr = ATTR_CMN_RETURNED_ATTRS | ATTR_CMN_NAME | ATTR_CMN_OBJTYPE;  // NOLINT(hicpp-signed-bitwise) - ATTR_CMN_* macros use signed integer constants

  std::vector<char> buf(kBulkAttrBufSize);

  for (;;) {
    const int count = getattrlistbulk(afd.fd, &alist, buf.data(), buf.size(), 0);
    if (count == 0) {
      break;  // End of directory
    }
    if (count < 0) {
      if (errno == ENOTSUP) {
        // Volume does not support getattrlistbulk (network mount, FAT, etc.)
        // — fall back to opendir/readdir.
        entries.clear();  // Safety: discard any partial results
        return EnumerateDirectoryOpenDir(dir_path, entries);
      }
      LOG_WARNING_BUILD("getattrlistbulk failed for: " << dir_path << ", error: " << errno);
      return false;
    }

    // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
    // Buffer layout parsing uses reinterpret_cast and pointer arithmetic as required
    // by the getattrlistbulk API contract; the layout is documented above and
    // guaranteed by the kernel. All casts match documented field sizes and alignments.
    const char* ptr = buf.data();
    for (int i = 0; i < count; ++i) {
      const char* const entry_start = ptr;

      // Entry length (uint32_t at offset 0 of each entry)
      const uint32_t entry_len = *reinterpret_cast<const uint32_t*>(ptr);  // NOSONAR(cpp:S3630)
      ptr += sizeof(uint32_t);
      // Guard against malformed or empty entry to avoid infinite loop
      if (entry_len < sizeof(uint32_t)) {
        break;
      }

      // Returned-attribute mask (attribute_set_t immediately follows length)
      const attribute_set_t returned = *reinterpret_cast<const attribute_set_t*>(ptr);  // NOSONAR(cpp:S3630)
      ptr += sizeof(attribute_set_t);

      // ATTR_CMN_NAME (bit 0x1) — attrreference_t pointing to inline name string
      std::string_view name;
      if ((returned.commonattr & ATTR_CMN_NAME) != 0) {  // NOLINT(hicpp-signed-bitwise) - ATTR_CMN_NAME is a signed integer constant
        const auto* name_ref = reinterpret_cast<const attrreference_t*>(ptr);  // NOSONAR(cpp:S3630)
        // attr_dataoffset is relative to the start of the attrreference_t itself
        const char* name_ptr = reinterpret_cast<const char*>(name_ref) + name_ref->attr_dataoffset;  // NOSONAR(cpp:S3630)
        // attr_length includes the null terminator; guard against 0
        name = std::string_view(name_ptr, name_ref->attr_length > 0U ? name_ref->attr_length - 1U : 0U);
        ptr += sizeof(attrreference_t);
      }

      // ATTR_CMN_OBJTYPE (bit 0x8) — fsobj_type_t (VDIR, VLNK, VREG, etc.)
      fsobj_type_t obj_type = VNON;
      bool type_known = false;
      if ((returned.commonattr & ATTR_CMN_OBJTYPE) != 0) {  // NOLINT(hicpp-signed-bitwise) - ATTR_CMN_OBJTYPE is a signed integer constant
        obj_type = *reinterpret_cast<const fsobj_type_t*>(ptr);  // NOSONAR(cpp:S3630)
        ptr += sizeof(fsobj_type_t);
        type_known = true;
      }

      // Always advance to the next entry via the length field, regardless of
      // how many attribute fields were actually consumed above.
      ptr = entry_start + entry_len;

      if (name.empty() || name == "." || name == "..") {
        continue;
      }

      DirectoryEntry dir_entry;
      dir_entry.name = std::string(name);
      dir_entry.is_directory = (obj_type == VDIR);
      dir_entry.is_symlink = (obj_type == VLNK);

      // If ATTR_CMN_OBJTYPE was not returned by this filesystem, fall back to
      // lstat for this individual entry (rare; most local FS always fill it).
      if (!type_known) {
        std::string full_path(dir_path);
        if (full_path.back() != '/') {
          full_path += '/';
        }
        full_path += dir_entry.name;
        if (!StatAndClassify(full_path, dir_entry.is_directory, dir_entry.is_symlink)) {
          continue;
        }
      }

      entries.push_back(std::move(dir_entry));
    }
    // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)
  }

  return true;
}

#elif defined(__linux__)

// 64KB read buffer: the C library passes ~32KB to getdents64 internally and
// returns one entry at a time. By calling getdents64 directly we batch all
// entries from large directories (e.g. node_modules, log dirs) in fewer
// kernel transitions. SYS_getdents64 is available on all Linux ≥ 2.6.4.
constexpr size_t kGetdents64BufSize = 64UL * 1024UL;

// Kernel ABI struct for SYS_getdents64.
// Defined inline to avoid depending on _GNU_SOURCE / _LARGEFILE64_SOURCE.
struct LinuxDirent64 {
  uint64_t d_ino;     // NOLINT(readability-identifier-naming) - kernel ABI
  int64_t  d_off;     // NOLINT(readability-identifier-naming) - kernel ABI
  uint16_t d_reclen;  // NOLINT(readability-identifier-naming) - kernel ABI
  uint8_t  d_type;    // NOLINT(readability-identifier-naming) - kernel ABI
  char     d_name[1]; // NOLINT(readability-identifier-naming,modernize-avoid-c-arrays) - kernel ABI (variable-length) NOSONAR(cpp:S5945)
};

namespace {
// Appends one getdents64 entry to entries; skips "." and "..". Reduces nesting in EnumerateDirectory.
void AppendLinuxDirent(const LinuxDirent64* d, std::string_view dir_path,
                       std::vector<FolderCrawler::DirectoryEntry>& entries) {
  if (strcmp(d->d_name, ".") == 0 || strcmp(d->d_name, "..") == 0) {
    return;
  }
  FolderCrawler::DirectoryEntry dir_entry;
  dir_entry.name = d->d_name;

  if (d->d_type != DT_UNKNOWN) {
    dir_entry.is_directory = (d->d_type == DT_DIR);
    dir_entry.is_symlink   = (d->d_type == DT_LNK);
  } else {
    std::string full_path(dir_path);
    if (full_path.back() != '/') {
      full_path += '/';
    }
    full_path += d->d_name;
    struct stat stat_buf = {};
    if (lstat(full_path.c_str(), &stat_buf) != 0) {
      LOG_WARNING_BUILD("lstat failed for: " << full_path << ", error: " << errno);
      return;
    }
    const auto mode = stat_buf.st_mode;
    dir_entry.is_directory = (S_ISDIR(mode) != 0);
    dir_entry.is_symlink   = (S_ISLNK(mode) != 0);
  }
  entries.push_back(std::move(dir_entry));
}

// Process one getdents64 buffer chunk to reduce nesting in EnumerateDirectory (S134).
void ProcessGetdents64Buffer(const char* buf_data, long nread, std::string_view dir_path,  // NOLINT(google-runtime-int) - must match syscall return type
                             std::vector<FolderCrawler::DirectoryEntry>& entries) {
  // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
  long offset = 0;  // NOLINT(google-runtime-int) - must match nread type for comparison
  while (offset < nread) {
    const auto* d = reinterpret_cast<const LinuxDirent64*>(buf_data + offset);  // NOSONAR(cpp:S3630) - kernel ABI layout
    if (d->d_reclen == 0) {
      break;  // Guard against malformed entry
    }
    offset += static_cast<long>(d->d_reclen);  // NOLINT(google-runtime-int) - match offset type
    AppendLinuxDirent(d, dir_path, entries);
  }
  // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)
}
}  // namespace

// NOLINTNEXTLINE(readability-identifier-naming) - EnumerateDirectory is public API name
bool FolderCrawler::EnumerateDirectory(std::string_view dir_path,
                                       std::vector<DirectoryEntry>& entries) const {
  entries.clear();
  entries.reserve(kDefaultDirEntryReserve);

  // Open directory as a plain fd — no DIR* needed; getdents64 works on the fd.
  const int fd = open(std::string(dir_path).c_str(),
                      O_RDONLY | O_DIRECTORY);         // NOLINT(hicpp-signed-bitwise) - O_RDONLY/O_DIRECTORY are POSIX signed constants
  if (fd == -1) {
    return HandleOpenErrno(errno, dir_path, "open");
  }

  std::vector<char> buf(kGetdents64BufSize);
  bool success = true;
  bool read_done = false;

  // Buffer parsing uses pointer arithmetic and reinterpret_cast in ProcessGetdents64Buffer
  // per the kernel's getdents64 ABI contract; layout is documented by the syscall spec.
  while (!read_done) {
    const long nread = syscall(SYS_getdents64, fd,  // NOLINT(google-runtime-int) - syscall() signature uses long
                               buf.data(), buf.size());
    if (nread < 0) {
      LOG_WARNING_BUILD("getdents64 failed for: " << dir_path << ", error: " << errno);
      success = false;
      read_done = true;
    } else if (nread == 0) {
      read_done = true;
    } else {
      ProcessGetdents64Buffer(buf.data(), nread, dir_path, entries);
    }
  }

  close(fd);
  return success;
}

#else
#error "FolderCrawler: Unsupported platform"
#endif  // _WIN32
