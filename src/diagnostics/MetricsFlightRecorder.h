#pragma once

/**
 * @file diagnostics/MetricsFlightRecorder.h
 * @brief Always-armed Flight Recorder with 15s pre-roll ring buffer, automatic incident
 *        triggers, dual JSONL/CSV streaming, and in-app diagnostic snapshots.
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "diagnostics/MetricsDiagnosticAnalyzer.h"
#include "diagnostics/MetricsRingBuffer.h"
#include "diagnostics/MetricsSample.h"
#include "usn/UsnThroughput.h"

// Forward declarations
class UsnMonitor;
class SearchWorker;
class FileIndex;

namespace diagnostics {

/**
 * @brief Operational state of the MetricsFlightRecorder state machine.
 */
enum class RecorderState : std::uint8_t {
  Armed = 0,
  Recording = 1,
  Cooldown = 2,
};

/**
 * @brief Thread-safe snapshot consumed by MetricsWindow on the UI thread.
 */
struct RecorderUiSnapshot {
  RecorderState state = RecorderState::Armed;
  size_t pre_roll_samples = 0;
  size_t incidents_recorded = 0;
  uint64_t latest_steady_ms = 0;
  DiagnosticSeverity live_severity = DiagnosticSeverity::Healthy;
  std::vector<DiagnosticFinding> live_findings;
  bool has_last_incident = false;
  IncidentSummary last_incident{};
  std::string diagnostics_dir;
};

/**
 * @class MetricsFlightRecorder
 * @brief Captures 250ms telemetry samples into a 60-slot ring buffer and streams
 *        incident traces to .jsonl and .csv files when anomalies occur.
 */
class MetricsFlightRecorder {
 public:
  /**
   * @brief Construct a flight recorder writing to diagnostics_dir (or default ~/.FindHelper/diagnostics).
   */
  explicit MetricsFlightRecorder(std::string_view diagnostics_dir = "");

  ~MetricsFlightRecorder();

  MetricsFlightRecorder(const MetricsFlightRecorder&) = delete;
  MetricsFlightRecorder& operator=(const MetricsFlightRecorder&) = delete;
  MetricsFlightRecorder(MetricsFlightRecorder&&) = delete;
  MetricsFlightRecorder& operator=(MetricsFlightRecorder&&) = delete;

  /**
   * @brief Start background 250ms sampling thread polling monitor, search_worker, and file_index.
   */
  void Start(const UsnMonitor* monitor,
             const SearchWorker* search_worker,
             const FileIndex* file_index,
             const std::atomic<bool>* is_index_building = nullptr);

  /**
   * @brief Stop background sampling thread and finalize any open incident file.
   */
  void Stop();

  /**
   * @brief Lock-free UI heartbeat called at the start of Application::ProcessFrame().
   */
  void OnUiFrameBegin(uint64_t steady_elapsed_ms, float fps) noexcept;

  /**
   * @brief Lock-free UI heartbeat called at the end of Application::ProcessFrame().
   */
  void OnUiFrameEnd(uint64_t steady_elapsed_ms) noexcept;

  /**
   * @brief Lock-free notification when the main window is minimized/iconified.
   */
  void SetUiWindowMinimized(bool minimized) noexcept;

  /**
   * @brief Lock-free notification when the UI thread begins polling/waiting for OS events.
   */
  void OnUiEventPollBegin() noexcept;

  /**
   * @brief Lock-free notification when the UI thread finishes polling/waiting for OS events.
   */
  void OnUiEventPollEnd() noexcept;

  /**
   * @brief Lock-free notification when the OS window is in a modal move or resize loop.
   */
  void SetUiWindowMovingOrResizing(bool moving_or_resizing) noexcept;

  /**
   * @brief RAII scope guard to bracket event polling (glfwPollEvents / glfwWaitEvents*).
   *
   * Automatically notifies the flight recorder so event polling time is not
   * misinterpreted as application frame processing stalls or hangs.
   */
  class ScopedEventPoll {
   public:
    explicit ScopedEventPoll(MetricsFlightRecorder* recorder) noexcept : recorder_(recorder) {
      if (recorder_ != nullptr) {
        recorder_->OnUiEventPollBegin();
      }
    }
    ~ScopedEventPoll() noexcept {
      if (recorder_ != nullptr) {
        recorder_->OnUiEventPollEnd();
      }
    }
    ScopedEventPoll(const ScopedEventPoll&) = delete;
    ScopedEventPoll& operator=(const ScopedEventPoll&) = delete;
    ScopedEventPoll(ScopedEventPoll&&) = delete;
    ScopedEventPoll& operator=(ScopedEventPoll&&) = delete;

   private:
    MetricsFlightRecorder* recorder_ = nullptr;
  };

  /**
   * @brief Feed a single sample into the recorder state machine (public for deterministic unit tests).
   */
  void RecordSample(const MetricsSample& sample);

  /**
   * @brief Immediately dump the current pre-roll buffer + live diagnostics to .jsonl and .csv.
   * @return Path to the written .jsonl file, or empty string on I/O failure.
   */
  std::string TriggerManualDump();

  /**
   * @brief Obtain a thread-safe snapshot of current diagnostics and the last incident post-mortem.
   */
  [[nodiscard]] RecorderUiSnapshot GetUiSnapshot() const;

  /**
   * @brief Return the resolved directory where flight records are stored.
   */
  [[nodiscard]] std::string GetDiagnosticsDirectory() const;

  /**
   * @brief Return monotonic milliseconds elapsed since this recorder was constructed.
   */
  [[nodiscard]] uint64_t ElapsedSteadyMs() const noexcept;

 private:
  struct UiHeartbeatAtomics {
    std::atomic<uint64_t> frame_begin_steady_ms{0};
    std::atomic<uint64_t> last_frame_delta_ms{0};
    std::atomic<uint32_t> fps_milli{flight_recorder_constants::kDefaultFpsMilli};
    std::atomic<bool> in_process_frame{false};
    std::atomic<bool> is_minimized{false};
    std::atomic<bool> in_event_polling{false};
    std::atomic<bool> is_moving_or_resizing{false};
    std::atomic<uint64_t> poll_begin_steady_ms{0};
    std::atomic<uint64_t> cumulative_poll_ms_this_frame{0};
  };

  struct ActiveIncidentState {
    uint64_t incident_id = 0;
    uint64_t start_unix_ms = 0;
    uint64_t start_steady_ms = 0;
    uint64_t cooldown_start_steady_ms = 0;
    uint32_t trigger_reasons_mask = 0;
    size_t sample_count = 0;
    MetricsSample baseline_sample{};
    IncidentPeaks peaks{};
    std::string jsonl_path;
    std::string csv_path;
    std::ofstream jsonl_stream;
    std::ofstream csv_stream;
  };

  struct SamplerContext {
    const UsnMonitor* monitor = nullptr;
    const SearchWorker* search_worker = nullptr;
    const FileIndex* file_index = nullptr;
    const std::atomic<bool>* is_index_building = nullptr;
    usn_throughput::ThroughputSample prev_throughput_sample{};
    uint64_t prev_throughput_steady_ms = 0;
    size_t cached_rebuild_count = 0;
    size_t cached_deleted_count = 0;
    size_t cached_total_entries = 0;
  };

  void SamplerThreadLoop();
  [[nodiscard]] MetricsSample CollectLiveSample();
  void HandleArmedSampleLocked(const MetricsSample& sample, uint32_t reasons);
  void HandleRecordingSampleLocked(const MetricsSample& sample, uint32_t reasons);
  void HandleCooldownSampleLocked(const MetricsSample& sample, uint32_t reasons);
  void BeginIncidentLocked(const MetricsSample& trigger_sample, uint32_t reasons_mask);
  void WriteSampleLocked(const MetricsSample& sample, SamplePhase phase);
  void FinalizeActiveIncidentLocked(const MetricsSample& final_sample, bool hit_max_cutoff);
  void RefreshLiveDiagnosticsLocked(const MetricsSample& current_sample);
  void PruneOldFlightRecordsLocked() const;

  std::chrono::steady_clock::time_point start_steady_time_ = std::chrono::steady_clock::now();
  std::string diagnostics_dir_;
  UiHeartbeatAtomics ui_heartbeat_{};

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  bool stop_requested_ = false;
  std::thread sampler_thread_;

  SamplerContext sampler_ctx_{};
  RecorderState state_ = RecorderState::Armed;
  MetricsRingBuffer<flight_recorder_constants::kPreRollSampleCapacity> pre_roll_;
  bool has_last_sample_ = false;
  MetricsSample last_sample_{};
  uint64_t last_cutoff_steady_ms_ = 0;
  uint64_t next_incident_id_ = 1;
  size_t incidents_recorded_ = 0;

  ActiveIncidentState active_incident_{};
  DiagnosticSeverity live_severity_ = DiagnosticSeverity::Healthy;
  std::vector<DiagnosticFinding> live_findings_;
  bool has_last_incident_ = false;
  IncidentSummary last_incident_{};
};

}  // namespace diagnostics
