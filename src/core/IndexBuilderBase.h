#pragma once

/**
 * @file core/IndexBuilderBase.h
 * @brief Shared state and Stop() logic for single-threaded index builders.
 *
 * Holds the five members common to WindowsIndexBuilder and
 * FolderCrawlerIndexBuilder (file_index_, config_, running_,
 * worker_thread_, shared_state_) and provides:
 *  - A concrete Stop() that signals cancellation and joins the worker thread.
 *  - A BeginStart() guard that enforces single-entry, marks state, stores shared_state_.
 */

#include <atomic>
#include <system_error>
#include <thread>

#include "core/IndexBuilder.h"
#include "utils/Logger.h"

class FileIndex;

// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init,hicpp-member-init,cppcoreguidelines-virtual-class-destructor) - running_/shared_state_ zero-initialised by in-class initialisers; file_index_/config_ set by constructor; protected dtor is correct (base not deleted through this pointer)
class IndexBuilderBase : public IIndexBuilder {
public:
  void Stop() override {
    if (shared_state_ != nullptr) {
      shared_state_->cancel_requested.store(true);
    }
    if (worker_thread_.joinable()) {
      try {
        worker_thread_.join();
      } catch (const std::system_error& e) {
        (void)e;
        LOG_ERROR_BUILD("IndexBuilderBase: system_error joining worker thread: " << e.what());
      } catch (...) {  // NOSONAR(cpp:S2738) - catch-all needed; Stop() is called from destructors and must not throw
        LOG_ERROR_BUILD("IndexBuilderBase: unknown exception joining worker thread");
      }
    }
    running_.store(false);
  }

protected:
  IndexBuilderBase(FileIndex& file_index, IndexBuilderConfig config)
      : file_index_(file_index), config_(std::move(config)) {}

  ~IndexBuilderBase() override {  // NOLINT(bugprone-exception-escape,cppcoreguidelines-virtual-class-destructor) - Stop() wraps thread::join in catch-all; no exception escapes; protected dtor is correct for base class not deleted through this pointer
    IndexBuilderBase::Stop();
  }

  /**
   * @brief Start() preamble: single-entry guard, MarkStarting, shared_state_ assignment.
   *
   * Does NOT log on failure — caller should log a class-specific warning message.
   * @return true if the caller should proceed with launching the worker thread.
   */
  bool BeginStart(IndexBuildState& state) {
    if (bool expected = false;  // NOLINT(misc-const-correctness) - compare_exchange_strong modifies expected
        !running_.compare_exchange_strong(expected, true)) {
      return false;
    }
    state.MarkStarting();
    shared_state_ = &state;
    return true;
  }

  [[nodiscard]] FileIndex& GetFileIndex() noexcept { return file_index_; }
  [[nodiscard]] IndexBuilderConfig& GetConfig() noexcept { return config_; }
  [[nodiscard]] std::atomic<bool>& GetRunning() noexcept { return running_; }
  [[nodiscard]] std::thread& GetWorkerThread() noexcept { return worker_thread_; }
  [[nodiscard]] IndexBuildState*& GetSharedState() noexcept { return shared_state_; }

private:
  // NOLINTBEGIN(readability-identifier-naming) - project convention: snake_case_ for all members
  FileIndex& file_index_;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members) - reference member for context passing
  IndexBuilderConfig config_;
  std::atomic<bool> running_{false};
  std::thread worker_thread_;
  IndexBuildState* shared_state_ = nullptr;
  // NOLINTEND(readability-identifier-naming)
};
