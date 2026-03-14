#pragma once

#include <atomic>  // NOLINT(clang-diagnostic-error) - System header, unavoidable on macOS (header-only analysis limitation)
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// Forward declaration
class FileIndex;

/**
 * Configuration for FolderCrawler
 */
struct FolderCrawlerConfig {
  size_t thread_count = 0;  // 0 = auto (hardware_concurrency())
  size_t batch_size = 2500;  // Paths per batch insertion
  size_t progress_update_interval_ms = 1000;  // Update progress every N milliseconds
};

/**
 * FolderCrawler - Cross-platform file system crawler for initial index population
 *
 * This class provides recursive directory traversal for populating FileIndex
 * when USN Journal is unavailable (Windows non-admin) or not available (macOS).
 *
 * THREADING MODEL:
 * - Uses a work-stealing queue pattern with multiple worker threads
 * - Each thread processes directories from a shared queue
 * - When a thread discovers subdirectories, it adds them to the queue
 * - Natural load balancing as threads grab work from the queue
 *
 * PERFORMANCE:
 * - Uses platform-specific fast enumeration APIs
 * - Batches insertions to reduce FileIndex lock contention
 * - Skips symbolic links and junction points (documented for future changes)
 *
 * ERROR HANDLING:
 * - Continues on permission errors (skips and logs)
 * - Tolerates individual file/directory failures
 */
class FolderCrawler {
public:
  explicit FolderCrawler(FileIndex& file_index, const FolderCrawlerConfig& config = FolderCrawlerConfig());
  ~FolderCrawler();

  // Non-copyable, non-movable
  FolderCrawler(const FolderCrawler&) = delete;
  FolderCrawler& operator=(const FolderCrawler&) = delete;
  FolderCrawler(FolderCrawler&&) = delete;
  FolderCrawler& operator=(FolderCrawler&&) = delete;

  /**
   * Crawl a root directory and populate the FileIndex
   *
   * @param root_path Root directory to start crawling from
   * @param indexed_file_count Optional atomic counter for progress updates
   * @param cancel_flag Optional atomic flag to cancel crawling
   * @return true on success, false on failure
   */
  bool Crawl(std::string_view root_path,
             std::atomic<size_t>* indexed_file_count = nullptr,
             const std::atomic<bool>* cancel_flag = nullptr);

  /**
   * Get the number of files processed so far (thread-safe)
   */
  [[nodiscard]] size_t GetFilesProcessed() const { return files_processed_.load(); }

  /**
   * Get the number of directories processed so far (thread-safe)
   */
  [[nodiscard]] size_t GetDirectoriesProcessed() const { return dirs_processed_.load(); }

  /**
   * Get the number of errors encountered (thread-safe)
   */
  [[nodiscard]] size_t GetErrorCount() const { return error_count_.load(); }

  // Platform-specific directory enumeration (public for helper functions)
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init,hicpp-member-init) - name default-inits
  struct DirectoryEntry {
    std::string name;
    bool is_directory = false;
    bool is_symlink = false;  // For skipping symlinks
  };

  // Enumerate entries in a directory (platform-specific, public for helper functions)
  bool EnumerateDirectory(std::string_view dir_path,
                          std::vector<DirectoryEntry>& entries) const;

  // Batch insertion helper (public for helper functions). Each element is (path, is_directory).
  void FlushBatch(const std::vector<std::pair<std::string, bool>>& batch,  // NOLINT(readability-avoid-const-params-in-decls) - keep const in decl for API clarity
                  std::atomic<size_t>* indexed_file_count);

  // Push subdirectory paths to worker_idx's per-worker queue and wake idle
  // workers (public for ProcessEntries helper in FolderCrawler.cpp).
  void PushSubdirs(size_t worker_idx, std::vector<std::string>& subdirs);

  // Worker thread function (public for thread creation)
  void WorkerThread(size_t worker_idx,
                    std::atomic<size_t>* indexed_file_count,
                    const std::atomic<bool>* cancel_flag);

  // Stop all worker threads (used in error handling and shutdown)
  void StopWorkerThreads();

private:
  FileIndex& file_index_;  // NOLINT(readability-identifier-naming) - project uses snake_case_ for members
  FolderCrawlerConfig config_;  // NOLINT(readability-identifier-naming)

  // Thread pool
  std::vector<std::thread> worker_threads_;  // NOLINT(readability-identifier-naming)
  std::atomic<bool> should_stop_{false};  // NOLINT(readability-identifier-naming)

  // Per-worker work-stealing queues.
  // Each worker owns a deque + mutex; idle workers steal from peers' deques.
  // Replaces the previous single work_queue_ + queue_mutex_ hot-path contention
  // point (analysis fix #10).
  size_t num_workers_{0};                                    // NOLINT(readability-identifier-naming) - set at Crawl() time, stable for duration
  std::vector<std::deque<std::string>> wq_items_;           // NOLINT(readability-identifier-naming) - one deque per worker
  std::vector<std::unique_ptr<std::mutex>> wq_mutexes_;     // NOLINT(readability-identifier-naming) - one mutex per worker
  std::atomic<size_t> total_queued_{0};                     // NOLINT(readability-identifier-naming) - total dirs across all per-worker queues
  std::mutex idle_mutex_;                                    // NOLINT(readability-identifier-naming) - guards idle_cv_
  std::condition_variable idle_cv_;                         // NOLINT(readability-identifier-naming) - wakes idle workers when work appears
  std::mutex completion_mutex_;                              // NOLINT(readability-identifier-naming) - guards completion_cv_
  std::condition_variable completion_cv_;                   // NOLINT(readability-identifier-naming) - Signals crawl complete
  std::atomic<size_t> active_workers_{0};                   // NOLINT(readability-identifier-naming)

  // Pop work from own queue; on empty, steal from other workers' queues.
  // On success, increments active_workers_ and decrements total_queued_.
  [[nodiscard]] std::string TryGetWork(size_t worker_idx);

  // Atomically claim a work item: increment active_workers_ before decrementing
  // total_queued_ to prevent a false completion signal. Hot-path; kept inline.
  inline void AcquireWorkItem();

  // Signal completion when no work remains and this was the last active worker.
  void SignalCompletionIfLastWorker();

  // Statistics
  std::atomic<size_t> files_processed_{0};  // NOLINT(readability-identifier-naming)
  std::atomic<size_t> dirs_processed_{0};  // NOLINT(readability-identifier-naming)
  std::atomic<size_t> error_count_{0};  // NOLINT(readability-identifier-naming)
  std::atomic<int64_t> last_progress_update_ms_{0};  // NOLINT(readability-identifier-naming)
};

