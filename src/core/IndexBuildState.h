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

// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init,hicpp-member-init) - struct with public members; string/mutex default-inits  // NOSONAR(cpp:S125) - directive, not commented-out code
struct IndexBuildState {
  // Lifecycle flags
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
  std::atomic<bool> active{false};            ///< true while an index build is in progress
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
  std::atomic<bool> completed{false};         ///< true when build finished successfully
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
  std::atomic<bool> failed{false};            ///< true when build terminated with an error
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
  std::atomic<bool> cancel_requested{false};  ///< cooperative cancellation flag
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
  std::atomic<bool> finalizing_population{false};  ///< true while FinalizeInitialPopulation() is running (prevents search race condition)

  // Progress metrics (coarse-grained, safe for UI polling)
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
  std::atomic<size_t> entries_processed{0};   ///< total entries (files + dirs) seen so far
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
  std::atomic<size_t> files_processed{0};     ///< files processed so far
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
  std::atomic<size_t> dirs_processed{0};      ///< directories processed so far
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
  std::atomic<size_t> errors{0};              ///< non-fatal errors encountered

  // Human-readable description of the current/last build source
  // Examples: "USN Journal", "Folder crawl: /home/user", "Index file: /tmp/index.json"
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,cppcoreguidelines-pro-type-member-init) - Struct with public members; std::string/mutex default-inits
  std::string source_description;

  // Optional last error message (guarded by mutex for rare writes)
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,cppcoreguidelines-pro-type-member-init) - Struct with public members
  std::string last_error_message;
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,cppcoreguidelines-pro-type-member-init) - Struct with public members
  mutable std::mutex last_error_mutex;

  void Reset() {
    active.store(false, std::memory_order_relaxed);
    completed.store(false, std::memory_order_relaxed);
    failed.store(false, std::memory_order_relaxed);
    cancel_requested.store(false, std::memory_order_relaxed);
    finalizing_population.store(false, std::memory_order_relaxed);
    entries_processed.store(0, std::memory_order_relaxed);
    files_processed.store(0, std::memory_order_relaxed);
    dirs_processed.store(0, std::memory_order_relaxed);
    errors.store(0, std::memory_order_relaxed);
    {
      const std::scoped_lock lock(last_error_mutex);
      last_error_message.clear();
    }
    source_description.clear();
  }

  void SetLastErrorMessage(const std::string& message) {  // NOSONAR(cpp:S6009) - Message is stored, needs std::string
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
    completed.store(true, std::memory_order_release);
    failed.store(false, std::memory_order_release);
  }

  /**
   * @brief Mark the build as failed.
   *
   * Sets failed=true and completed=false with release memory ordering.
   * Used when index building fails or encounters an error.
   */
  void MarkFailed() {
    failed.store(true, std::memory_order_release);
    completed.store(false, std::memory_order_release);
  }

  /**
   * @brief Mark the build as inactive (worker thread finished).
   *
   * Sets active=false with release memory ordering.
   * Used at the end of worker threads to signal completion.
   */
  void MarkInactive() {
    active.store(false, std::memory_order_release);
  }
};


