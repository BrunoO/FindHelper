#pragma once

/**
 * @file IndexBuildState.h
 * @brief Shared state for tracking index-building progress across all platforms.
 *
 * This struct is intentionally platform-agnostic so that Windows (USN-based),
 * macOS, and Linux (FolderCrawler-based) initial indexing can all report
 * progress through the same interface. The UI and ApplicationLogic only
 * depend on this struct and do not need to know how the index is built.
 */

#include <atomic>
#include <mutex>
#include <string>
#include <string_view>

namespace index_build {

/** @brief IndexBuildState::source_description when the Windows USN path is active. */
inline constexpr std::string_view kUsnJournalSourceDescription = "USN Journal";

/**
 * @brief Status-bar label while an index build is in progress.
 *
 * Prefer "Indexing USN Journal" whenever the USN monitor is populating, even if
 * a folder crawler was also started (e.g. stale settings.crawlFolder). Otherwise
 * use source_description (e.g. "Folder: C:\\..." or "USN Journal").
 *
 * @param source_description IndexBuildState::source_description (may be empty)
 * @param usn_populating UsnMonitor::IsPopulatingIndex() (false on non-Windows)
 */
[[nodiscard]] inline std::string FormatInProgressStatus(  // NOLINT(google-objc-function-naming) - C++ helper in header shared with ObjC++; project PascalCase
    std::string_view source_description, bool usn_populating) {
  if (usn_populating) {
    return std::string("Indexing ") + std::string(kUsnJournalSourceDescription);
  }
  if (!source_description.empty()) {
    return std::string("Indexing ") + std::string(source_description);
  }
  return {"Indexing index"};
}

}  // namespace index_build

// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init,hicpp-member-init) - struct with public members; string/mutex default-inits  // NOSONAR(cpp:S125) - directive, not commented-out code
struct IndexBuildState {  // NOSONAR(cpp:S5414) - Public atomics for UI polling; private mutex guards last_error_message only  // NOSONAR(cpp:S5414) - Public atomics for UI polling; private mutex guards last_error_message only
  // Lifecycle flags
  // NOLINTBEGIN(readability-identifier-naming) - public atomics use snake_case (IndexBuildState API convention)

  std::atomic<bool> active{false};            ///< true while an index build is in progress

  std::atomic<bool> completed{false};         ///< true when build finished successfully

  std::atomic<bool> failed{false};            ///< true when build terminated with an error

  std::atomic<bool> cancel_requested{false};  ///< cooperative cancellation flag

  std::atomic<bool> finalizing_population{false};  ///< true while FinalizeInitialPopulation() is running (prevents search race condition)

  // Progress metrics (coarse-grained, safe for UI polling)

  std::atomic<size_t> entries_processed{0};   ///< total entries (files + dirs) seen so far

  std::atomic<size_t> files_processed{0};     ///< files processed so far

  std::atomic<size_t> dirs_processed{0};      ///< directories processed so far

  std::atomic<size_t> errors{0};              ///< non-fatal errors encountered

  // Human-readable description of the current/last build source
  // Examples: "USN Journal", "Folder: /home/user", "Index file: /tmp/index.json"
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init) - Struct with public members; std::string/mutex default-inits
  std::string source_description;
  // NOLINTEND(readability-identifier-naming)

  /**
   * @brief Mark the build as active and reset completion / failure flags.
   *
   * Called at the start of every index-builder Start() implementation to
   * initialise the lifecycle flags with release ordering before any worker
   * thread is launched.
   */
  void MarkStarting() {
    active.store(true);
    completed.store(false);
    failed.store(false);
    cancel_requested.store(false);
  }

  void Reset() {
    active.store(false);
    completed.store(false);
    failed.store(false);
    cancel_requested.store(false);
    finalizing_population.store(false);
    entries_processed.store(0);
    files_processed.store(0);
    dirs_processed.store(0);
    errors.store(0);
    {
      const std::scoped_lock lock(last_error_mutex);
      last_error_message.clear();
    }
    source_description.clear();
  }

  void SetLastErrorMessage(std::string_view message) {
    const std::scoped_lock lock(last_error_mutex);
    last_error_message = message;
  }

  [[nodiscard]] std::string GetLastErrorMessage() const {
    const std::scoped_lock lock(last_error_mutex);
    return last_error_message;
  }

  /**
   * @brief Mark the build as successfully completed.
   *
   * Sets completed=true and failed=false with release memory ordering.
   * Used when index building finishes successfully.
   */
  void MarkCompleted() {
    completed.store(true);
    failed.store(false);
  }

  /**
   * @brief Mark the build as failed.
   *
   * Sets failed=true and completed=false with release memory ordering.
   * Used when index building fails or encounters an error.
   */
  void MarkFailed() {
    failed.store(true);
    completed.store(false);
  }

  /**
   * @brief Mark the build as inactive (worker thread finished).
   *
   * Sets active=false with release memory ordering.
   * Used at the end of worker threads to signal completion.
   */
  void MarkInactive() {
    active.store(false);
  }

 private:
  // NOLINTBEGIN(readability-identifier-naming) - IndexBuildState API uses snake_case without trailing _
  std::string last_error_message;  // guarded by last_error_mutex_
  mutable std::mutex last_error_mutex;
  // NOLINTEND(readability-identifier-naming)
};
