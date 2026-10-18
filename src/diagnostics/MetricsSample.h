#pragma once

/**
 * @file diagnostics/MetricsSample.h
 * @brief Point-in-time telemetry sample and trigger definitions for the Metrics Flight Recorder.
 */

#include <cstddef>
#include <cstdint>

namespace diagnostics {

namespace flight_recorder_constants {
inline constexpr uint64_t kSampleIntervalMs = 250;
inline constexpr size_t kPreRollSampleCapacity = 60;       // 60 * 250ms = 15s pre-roll
inline constexpr uint64_t kCooldownDurationMs = 10000;     // 10s post-recovery tail
inline constexpr size_t kMaxSamplesPerIncident = 1200;     // 5 minutes max per incident file
inline constexpr uint64_t kRefractoryPeriodMs = 30000;     // 30s cooldown after max-sample cutoff
inline constexpr size_t kMaxRetainedIncidents = 10;        // Max 10 (.jsonl + .csv) pairs

// Automatic trigger thresholds
inline constexpr size_t kTriggerUsnQueueDepth = 25;
inline constexpr uint64_t kTriggerBufferLatencyJumpMs = 100;
inline constexpr uint32_t kTriggerUiFrameStallMs = 250;
inline constexpr uint32_t kHealthyUiFrameMaxMs = 50;
inline constexpr uint32_t kDefaultFpsMilli = 60000U;
}  // namespace flight_recorder_constants

/**
 * @brief Phase of a sample within a recorded incident file.
 */
enum class SamplePhase : std::uint8_t {
  PreRoll = 0,
  Active = 1,
  Cooldown = 2,
};

/**
 * @brief Bitmask of reasons that triggered an incident recording.
 */
enum class TriggerReason : std::uint8_t {
  None = 0U,
  UsnQueueDepth = 1U << 0U,
  UsnBufferLatencySpike = 1U << 1U,
  UiFrameStall = 1U << 2U,
  UsnBufferDropped = 1U << 3U,
  ManualDump = 1U << 4U,
};

[[nodiscard]] constexpr std::uint32_t ToMask(TriggerReason reason) noexcept {
  return static_cast<std::uint32_t>(reason);
}

[[nodiscard]] constexpr bool HasTriggerReason(std::uint32_t mask,
                                              TriggerReason reason) noexcept {
  return (mask & ToMask(reason)) != 0U;
}

/**
 * @brief UI frame responsiveness telemetry (updated via lock-free atomics from the UI thread).
 */
struct UiSample {
  uint32_t last_frame_delta_ms = 0;           // Duration of last completed ProcessFrame()
  uint32_t unresponsive_ms = 0;               // Elapsed ms if currently inside ProcessFrame() or stalled
  float fps = 0.0F;                           // ImGui::GetIO().Framerate
  bool is_minimized = false;                  // True when window is iconified (excluded from stall trigger)
  bool is_event_polling = false;              // True when UI thread is inside glfwPollEvents/wait
  bool is_window_moving_or_resizing = false;  // True during OS window move/resize modal loops (Win32 WM_ENTERSIZEMOVE)
};

/**
 * @brief USN buffer, queue, timing,lock-wait, and windowed rate telemetry.
 */
struct UsnBufferSample {
  size_t queue_depth = 0;
  size_t max_queue_depth = 0;
  size_t buffers_read = 0;
  size_t buffers_processed = 0;
  size_t buffers_dropped = 0;
  size_t records_processed = 0;
  uint64_t total_read_ms = 0;
  uint64_t total_process_ms = 0;
  uint64_t max_buffer_process_ms = 0;
  uint64_t lock_wait_total_ms = 0;
  uint64_t lock_wait_max_ms = 0;
  double live_buffers_per_sec = 0.0;
  double live_records_per_sec = 0.0;
};

/**
 * @brief USN file operation counters, parent placeholders, and read errors.
 */
struct UsnOperationSample {
  size_t files_created = 0;
  size_t files_deleted = 0;
  size_t files_renamed = 0;
  size_t files_modified = 0;
  size_t awaiting_count = 0;
  uint64_t awaiting_oldest_age_ms = 0;
  size_t never_arriving_evicted = 0;
  size_t healed_awaiting_total = 0;
  size_t errors_total = 0;
  size_t journal_wrap_errors = 0;
};

/**
 * @brief FileIndex maintenance (sampled via non-blocking try_lock) and SearchWorker telemetry.
 */
struct IndexSearchSample {
  bool index_write_lock_held = false;  // True when TryGetMaintenanceStats() could not acquire shared_lock
  bool is_index_building = false;
  bool is_search_busy = false;
  size_t index_rebuild_count = 0;
  size_t index_deleted_count = 0;
  size_t index_total_entries = 0;
  size_t search_total_count = 0;
  uint64_t search_last_search_ms = 0;
  uint64_t search_last_postprocess_ms = 0;
  size_t search_last_results_count = 0;
};

/**
 * @brief Complete point-in-time telemetry sample stored in the pre-roll ring buffer.
 *
 * Grouped into cohesive sub-structs to keep field counts small (Sonar cpp:S1820)
 * while remaining trivially copyable for zero-allocation circular buffering.
 */
struct MetricsSample {
  uint64_t timestamp_unix_ms = 0;  // UTC wall-clock milliseconds
  uint64_t steady_elapsed_ms = 0;  // Monotonic milliseconds since recorder start
  UiSample ui{};
  UsnBufferSample usn_buffers{};
  UsnOperationSample usn_ops{};
  IndexSearchSample index_search{};
};

/**
 * @brief Peak values observed across an active incident window.
 */
struct IncidentPeaks {
  size_t max_queue_depth = 0;
  uint32_t max_ui_stall_ms = 0;
  double max_live_buffers_per_sec = 0.0;
  double max_live_records_per_sec = 0.0;
  uint64_t max_single_buffer_jump_ms = 0;
  size_t max_awaiting_count = 0;
  uint64_t max_awaiting_oldest_age_ms = 0;
  size_t index_write_lock_busy_samples = 0;
};

}  // namespace diagnostics
