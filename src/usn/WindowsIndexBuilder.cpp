#ifdef _WIN32

#include "core/IndexBuilderBase.h"

#include "index/FileIndex.h"
#include "usn/UsnMonitor.h"
#include "utils/Logger.h"

namespace {

class WindowsIndexBuilder final : public IndexBuilderBase {
public:
  WindowsIndexBuilder(FileIndex& file_index,
                      UsnMonitor* monitor,
                      IndexBuilderConfig config)
      : IndexBuilderBase(file_index, std::move(config))
      , monitor_(monitor) {}

  ~WindowsIndexBuilder() override = default;

  void Start(IndexBuildState& state) override {
    if (!monitor_) {
      LOG_WARNING("WindowsIndexBuilder::Start called with null UsnMonitor");
      return;
    }

    if (!BeginStart(state)) {
      LOG_WARNING("WindowsIndexBuilder::Start called while already running");
      return;
    }

    // USN monitor already runs its own background threads. This builder
    // simply polls its metrics periodically and updates IndexBuildState.
    GetWorkerThread() = std::thread([this]() {
      IndexBuildState* state = GetSharedState();
      if (state == nullptr || monitor_ == nullptr) {
        GetRunning().store(false);
        return;
      }

      LOG_INFO_BUILD("WindowsIndexBuilder: monitoring USN-based initial index population");

      try {
        // Read initial metrics snapshot
        auto metrics = monitor_->GetMetricsSnapshot();
        size_t indexed_count = monitor_->GetIndexedFileCount();
        state->entries_processed.store(indexed_count);
        state->errors.store(metrics.errors_encountered);

        // Poll at intervals for population completion (we can't use callbacks here)
        // Use exponential backoff: start at 50ms, increase to 500ms max
        std::chrono::milliseconds poll_interval(50);
        const std::chrono::milliseconds max_interval(500);
        const std::chrono::milliseconds interval_increment(50);

        while (!state->cancel_requested.load()) {
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
              state->entries_processed.store(final_count);
              state->MarkCompleted();
              LOG_INFO_BUILD("WindowsIndexBuilder: USN initial population completed with "
                             << final_count << " entries");
            }
            break;
          }

          // Update metrics before sleeping
          metrics = monitor_->GetMetricsSnapshot();
          indexed_count = monitor_->GetIndexedFileCount();
          state->entries_processed.store(indexed_count);
          state->errors.store(metrics.errors_encountered);

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
      GetRunning().store(false);
    });
  }

private:
  UsnMonitor* monitor_;
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


