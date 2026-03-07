#include "core/IndexBuilder.h"

#include "crawler/FolderCrawler.h"
#include "index/FileIndex.h"
#include "utils/Logger.h"

#include <atomic>
#include <thread>

namespace {

bool HasCrawlFolder(const IndexBuilderConfig& config) {
  return !config.crawl_folder.empty();
}

void MarkFailedWithMessage(IndexBuildState* state, const char* message) {
  state->MarkFailed();
  state->SetLastErrorMessage(message);
}

class FolderCrawlerIndexBuilder final : public IIndexBuilder {
public:
  FolderCrawlerIndexBuilder(FileIndex& file_index, IndexBuilderConfig config)
      : file_index_(file_index)
      , config_(std::move(config)) {}

  ~FolderCrawlerIndexBuilder() override {
    Stop();
  }
  
  // Non-copyable, non-movable (manages indexing state)
  FolderCrawlerIndexBuilder(const FolderCrawlerIndexBuilder&) = delete;
  FolderCrawlerIndexBuilder& operator=(const FolderCrawlerIndexBuilder&) = delete;
  FolderCrawlerIndexBuilder(FolderCrawlerIndexBuilder&&) = delete;
  FolderCrawlerIndexBuilder& operator=(FolderCrawlerIndexBuilder&&) = delete;

  void Start(IndexBuildState& state) override {  // NOLINT(readability-function-cognitive-complexity) - Lambda body contributes complexity; splitting would require complex state sharing (see NOSONAR cpp:S1188)
    // If no crawl folder is configured, there is nothing to do.
    if (!HasCrawlFolder(config_)) {
      LOG_INFO_BUILD("FolderCrawlerIndexBuilder: no crawl folder specified, skipping background build");
      return;
    }

    // Ensure we don't start twice.
    if (bool expected = false; !running_.compare_exchange_strong(expected, true)) {  // NOLINT(misc-const-correctness) - expected must be non-const because compare_exchange_strong modifies it if exchange fails
      LOG_WARNING("FolderCrawlerIndexBuilder::Start called while already running");
      return;
    }

    state.active.store(true, std::memory_order_release);
    state.completed.store(false, std::memory_order_release);
    state.failed.store(false, std::memory_order_release);
    state.cancel_requested.store(false, std::memory_order_release);

    // Remember the shared state so Stop() can signal cancellation.
    shared_state_ = &state;

    // Launch background thread to perform the crawl.
    worker_thread_ = std::thread([this]() {  // NOLINT(readability-function-cognitive-complexity) NOSONAR(cpp:S1188) - Lambda encapsulates crawl + finalize; splitting would complicate state sharing
      if (IndexBuildState* state = shared_state_; state != nullptr) {
        try {
          LOG_INFO_BUILD("FolderCrawlerIndexBuilder: starting crawl for folder: " << config_.crawl_folder);

          const FolderCrawlerConfig crawler_config{};
          // Use default thread count (0 = auto) and default batch/progress settings.
          FolderCrawler crawler(file_index_, crawler_config);

          // Use the shared entries_processed as the progress counter for total entries.
          std::atomic<size_t>* progress_counter = &state->entries_processed;
          const std::atomic<bool>* cancel_flag = &state->cancel_requested;

          const bool success = crawler.Crawl(config_.crawl_folder, progress_counter, cancel_flag);

          // After crawl completes (or fails), finalize index if successful.
          if (success) {
#ifdef _WIN32
            // On Windows, RecomputeAllPaths resets OneDrive cloud-placeholder
            // files to lazy-load sentinels so their size/mtime are fetched via
            // IShellItem2 rather than cached as zero. It also clears and rebuilds
            // PathStorage entirely, so the finalizing flag is set to block
            // concurrent searches from accessing freed path offsets.
            state->finalizing_population.store(true, std::memory_order_release);
            file_index_.RecomputeAllPaths();
            state->finalizing_population.store(false, std::memory_order_release);
#endif  // _WIN32
            // On macOS/Linux there are no OneDrive placeholders, and paths are
            // already correct in PathStorage after the crawl (each entry is
            // inserted with its full path via InsertPathUnderLock, and
            // DirectoryResolver stores synthetic parent paths through the same
            // IndexOperations::Insert path). The O(N) recompute pass is skipped.

            // Update final metrics
            state->files_processed.store(crawler.GetFilesProcessed(), std::memory_order_relaxed);
            state->dirs_processed.store(crawler.GetDirectoriesProcessed(), std::memory_order_relaxed);
            state->errors.store(crawler.GetErrorCount(), std::memory_order_relaxed);
            state->MarkCompleted();
            LOG_INFO_BUILD("FolderCrawlerIndexBuilder: crawl completed successfully");
          } else {
            state->MarkFailed();
            if (!state->cancel_requested.load(std::memory_order_acquire)) {
              state->SetLastErrorMessage("Folder crawl failed or was incomplete");
              LOG_ERROR_BUILD("FolderCrawlerIndexBuilder: crawl failed or was cancelled");
            } else {
              LOG_INFO_BUILD("FolderCrawlerIndexBuilder: crawl cancelled by user");
            }
          }
        } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - Catch-all needed: FolderCrawler and FileIndex operations can throw various exception types (runtime_error, bad_alloc, system_error, etc.)
          MarkFailedWithMessage(state, e.what());
          LOG_ERROR_BUILD("FolderCrawlerIndexBuilder: exception during crawl: " << e.what());
        } catch (...) {  // NOSONAR(cpp:S2738) - Catch-all needed for non-standard exceptions from filesystem operations
          MarkFailedWithMessage(state, "Unknown error during folder crawl");
          LOG_ERROR("FolderCrawlerIndexBuilder: unknown exception during crawl");
        }
        state->MarkInactive();
      }
      running_.store(false, std::memory_order_release);
    });
  }

  void Stop() override {
    // If the worker thread was ever started, make sure we always join it.
    if (worker_thread_.joinable()) {
      // Request cooperative cancellation if the crawl is still running.
      if (running_.load(std::memory_order_acquire) && shared_state_ != nullptr) {
        shared_state_->cancel_requested.store(true, std::memory_order_release);
      }
      try {
        worker_thread_.join();
      } catch (...) {  // NOSONAR(cpp:S2738) - Destructors must not throw exceptions - silently ignore join errors
        // Best-effort: do not throw from destructor/Stop
        LOG_ERROR("FolderCrawlerIndexBuilder: exception while joining worker thread");
      }
    }
  }

private:
  FileIndex& file_index_;           // NOLINT(readability-identifier-naming) - Project convention: snake_case_ (CXX17_NAMING_CONVENTIONS)
  IndexBuilderConfig config_;       // NOLINT(readability-identifier-naming) - Project convention: snake_case_
  std::atomic<bool> running_{false};  // NOLINT(readability-identifier-naming) - Project convention: snake_case_
  std::thread worker_thread_;       // NOLINT(readability-identifier-naming) - Project convention: snake_case_
  IndexBuildState* shared_state_ = nullptr;  // NOLINT(readability-identifier-naming) - Project convention: snake_case_
};

} // namespace

std::unique_ptr<IIndexBuilder> CreateFolderCrawlerIndexBuilder(
    FileIndex& file_index, const IndexBuilderConfig& config) {
  // Only create a builder if we actually have a folder to crawl.
  if (!HasCrawlFolder(config)) {
    return nullptr;
  }
  return std::make_unique<FolderCrawlerIndexBuilder>(file_index, config);
}


