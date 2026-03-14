#ifdef _WIN32

#include "core/IndexBuilder.h"

#include "index/FileIndex.h"
#include "usn/UsnMonitor.h"
#include "utils/Logger.h"

#include <atomic>
#include <thread>

namespace {

class WindowsIndexBuilder final : public IIndexBuilder {
public:
  WindowsIndexBuilder(FileIndex& file_index,
                      UsnMonitor* monitor,
                      IndexBuilderConfig config)
      : file_index_(file_index)
      , monitor_(monitor)
      , config_(std::move(config)) {}

  ~WindowsIndexBuilder() override {
    Stop();
  }

  void Start(IndexBuildState& state) override {
    if (!monitor_) {
      LOG_WARNING("WindowsIndexBuilder::Start called with null UsnMonitor");
      return;
    }

    if (bool expected = false; !running_.compare_exchange_strong(expected, true)) {
      LOG_WARNING("WindowsIndexBuilder::Start called while already running");
      return;
    }

    state.MarkStarting();

    shared_state_ = &state;

    // USN monitor already runs its own background threads. This builder
    // simply polls its metrics periodically and updates IndexBuildState.
    worker_thread_ = std::thread([this]() {
      IndexBuildState* state = shared_state_;
      if (state == nullptr || monitor_ == nullptr) {
        running_.store(false, std::memory_order_release);
        return;
      }

      LOG_INFO_BUILD("WindowsIndexBuilder: monitoring USN-based initial index population");

      try {
        // Read initial metrics snapshot
        auto metrics = monitor_->GetMetricsSnapshot();
        size_t indexed_count = monitor_->GetIndexedFileCount();
        state->entries_processed.store(indexed_count, std::memory_order_relaxed);
        state->errors.store(metrics.errors_encountered, std::memory_order_relaxed);

        // Poll at intervals for population completion (we can't use callbacks here)
        // Use exponential backoff: start at 50ms, increase to 500ms max
        std::chrono::milliseconds poll_interval(50);
        const std::chrono::milliseconds max_interval(500);
        const std::chrono::milliseconds interval_increment(50);

        while (!state->cancel_requested.load(std::memory_order_acquire)) {
          // Check if index population is complete or failed
          if (!monitor_->IsPopulatingIndex()) {
            if (monitor_->InitialPopulationFailed()) {
              state->MarkFailed();
              state->SetLastErrorMessage("USN initial index population or initialization failed");
              LOG_ERROR_BUILD("WindowsIndexBuilder: USN initial population failed");
            } else {
              // RecomputeAllPaths was already called inside
              // UsnMonitor::RunInitialPopulationAndPrivileges, before
              // is_populating_index_ was set to false. By the time we reach
              // here all paths are fully resolved and monitoring is already
              // running against correct PathStorage state.
              size_t final_count = monitor_->GetIndexedFileCount();
              state->entries_processed.store(final_count, std::memory_order_release);
              state->MarkCompleted();
              LOG_INFO_BUILD("WindowsIndexBuilder: USN initial population completed with "
                             << final_count << " entries");
            }
            break;
          }

          // Update metrics before sleeping
          metrics = monitor_->GetMetricsSnapshot();
          indexed_count = monitor_->GetIndexedFileCount();
          state->entries_processed.store(indexed_count, std::memory_order_relaxed);
          state->errors.store(metrics.errors_encountered, std::memory_order_relaxed);

          // Sleep before checking again (with adaptive backoff)
          std::this_thread::sleep_for(poll_interval);
          poll_interval = (std::min)(poll_interval + interval_increment, max_interval);
        }
      } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - Catch-all needed: UsnMonitor and FileIndex operations can throw various exception types
        state->MarkFailed();
        state->SetLastErrorMessage("Error while monitoring USN: " + std::string(e.what()));
        LOG_ERROR_BUILD("WindowsIndexBuilder: exception while monitoring USN: " << e.what());
      } catch (...) {  // NOSONAR(cpp:S2738) - Catch-all needed for non-standard exceptions
        state->MarkFailed();
        state->SetLastErrorMessage("Unknown error while monitoring USN");
        LOG_ERROR_BUILD("WindowsIndexBuilder: unknown exception while monitoring USN");
      }

      state->MarkInactive();
      running_.store(false, std::memory_order_release);
    });
  }

  void Stop() override {
    if (shared_state_ != nullptr) {
      shared_state_->cancel_requested.store(true, std::memory_order_release);
    }

    if (worker_thread_.joinable()) {
      try {
        worker_thread_.join();
      } catch (const std::exception& e) {
        LOG_ERROR_BUILD("WindowsIndexBuilder: exception while joining worker thread: " << e.what());
      } catch (...) {
        LOG_ERROR_BUILD("WindowsIndexBuilder: unknown exception while joining worker thread");
      }
    }

    running_.store(false, std::memory_order_release);
  }

private:
  FileIndex& file_index_;
  UsnMonitor* monitor_;
  IndexBuilderConfig config_;
  std::atomic<bool> running_{false};
  std::thread worker_thread_;
  IndexBuildState* shared_state_ = nullptr;
};

} // namespace

std::unique_ptr<IIndexBuilder> CreateWindowsIndexBuilder(
    FileIndex& file_index, UsnMonitor* monitor, const IndexBuilderConfig& config) {
  if (!monitor) {
    return nullptr;
  }
  return std::make_unique<WindowsIndexBuilder>(file_index, monitor, config);
}

#endif  // _WIN32


