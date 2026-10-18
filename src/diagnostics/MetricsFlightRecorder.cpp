/**
 * @file diagnostics/MetricsFlightRecorder.cpp
 * @brief Implementation of MetricsFlightRecorder state machine, sampler thread,
 *        and dual JSONL + CSV writers.
 */

#include "diagnostics/MetricsFlightRecorder.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "diagnostics/MetricsDiagnosticAnalyzer.h"
#include "diagnostics/MetricsSample.h"
#include "index/FileIndex.h"
#include "path/PathUtils.h"
#include "search/SearchWorker.h"
#include "usn/UsnMonitor.h"
#include "utils/Logger.h"

namespace diagnostics {

namespace {

using nlohmann::json;  // NOLINT(readability-identifier-naming) - nlohmann convention

constexpr std::string_view kSettingsSubfolder = ".FindHelper";
constexpr std::string_view kDiagnosticsSubfolder = "diagnostics";
constexpr std::string_view kFilePrefix = "flight_record_";
constexpr float kFpsToMilliScale = 1000.0F;
constexpr float kMilliToFpsScale = 0.001F;

[[nodiscard]] std::string ResolveDefaultDiagnosticsDir() {
  if (const std::string home = path_utils::GetUserHomePath(); !home.empty()) {
    const std::string dot_dir = path_utils::JoinPath(home, kSettingsSubfolder);
    return path_utils::JoinPath(dot_dir, kDiagnosticsSubfolder);
  }
  if (const std::string exe_dir = path_utils::GetExecutableDirectory(); !exe_dir.empty()) {
    return path_utils::JoinPath(exe_dir, kDiagnosticsSubfolder);
  }
  return std::string(kDiagnosticsSubfolder);
}

[[nodiscard]] const char* PhaseToString(SamplePhase phase) noexcept {
  switch (phase) {
    case SamplePhase::PreRoll:
      return "pre_roll";
    case SamplePhase::Active:
      return "active";
    case SamplePhase::Cooldown:
      return "cooldown";
  }
  return "unknown";
}

[[nodiscard]] std::string FormatIncidentBaseFilename(uint64_t unix_ms, uint64_t incident_id) {
  const auto sys_tp =
      std::chrono::system_clock::time_point(std::chrono::milliseconds(unix_ms));
  const std::time_t tt = std::chrono::system_clock::to_time_t(sys_tp);
  struct tm tm_buf{};
#ifdef _WIN32
  (void)localtime_s(&tm_buf, &tt);
#else
  (void)localtime_r(&tt, &tm_buf);
#endif  // _WIN32
  std::ostringstream oss;
  oss << kFilePrefix << std::put_time(&tm_buf, "%Y%m%d_%H%M%S") << "_" << std::setw(4)
      << std::setfill('0') << incident_id;
  return oss.str();
}

void WriteCsvHeader(std::ofstream& csv) {
  csv << "phase,timestamp_unix_ms,steady_elapsed_ms,"
         "ui_last_frame_delta_ms,ui_unresponsive_ms,ui_fps,ui_is_minimized,"
         "ui_is_event_polling,ui_is_moving_or_resizing,"
         "usn_queue_depth,usn_max_queue_depth,usn_buffers_read,usn_buffers_processed,"
         "usn_buffers_dropped,usn_records_processed,usn_total_read_ms,usn_total_process_ms,"
         "usn_max_buffer_process_ms,usn_lock_wait_total_ms,usn_lock_wait_max_ms,"
         "live_buffers_per_sec,live_records_per_sec,"
         "usn_files_created,usn_files_deleted,usn_files_renamed,usn_files_modified,"
         "usn_awaiting_count,usn_awaiting_oldest_age_ms,usn_never_arriving_evicted,"
         "usn_healed_awaiting_total,usn_errors_total,usn_journal_wrap_errors,"
         "index_write_lock_held,is_index_building,is_search_busy,"
         "index_rebuild_count,index_deleted_count,index_total_entries,"
         "search_total_count,search_last_search_ms,search_last_postprocess_ms,"
         "search_last_results_count\n";
}

void WriteCsvRow(std::ofstream& csv, const MetricsSample& s, SamplePhase phase) {
  csv << PhaseToString(phase) << ',' << s.timestamp_unix_ms << ',' << s.steady_elapsed_ms << ','
      << s.ui.last_frame_delta_ms << ',' << s.ui.unresponsive_ms << ',' << std::fixed
      << std::setprecision(1) << s.ui.fps << ',' << (s.ui.is_minimized ? 1 : 0) << ','
      << (s.ui.is_event_polling ? 1 : 0) << ',' << (s.ui.is_window_moving_or_resizing ? 1 : 0) << ','
      << s.usn_buffers.queue_depth << ',' << s.usn_buffers.max_queue_depth << ','
      << s.usn_buffers.buffers_read << ',' << s.usn_buffers.buffers_processed << ','
      << s.usn_buffers.buffers_dropped << ',' << s.usn_buffers.records_processed << ','
      << s.usn_buffers.total_read_ms << ',' << s.usn_buffers.total_process_ms << ','
      << s.usn_buffers.max_buffer_process_ms << ',' << s.usn_buffers.lock_wait_total_ms << ','
      << s.usn_buffers.lock_wait_max_ms << ',' << std::setprecision(2)
      << s.usn_buffers.live_buffers_per_sec << ',' << std::setprecision(1)
      << s.usn_buffers.live_records_per_sec << ',' << s.usn_ops.files_created << ','
      << s.usn_ops.files_deleted << ',' << s.usn_ops.files_renamed << ','
      << s.usn_ops.files_modified << ',' << s.usn_ops.awaiting_count << ','
      << s.usn_ops.awaiting_oldest_age_ms << ',' << s.usn_ops.never_arriving_evicted << ','
      << s.usn_ops.healed_awaiting_total << ',' << s.usn_ops.errors_total << ','
      << s.usn_ops.journal_wrap_errors << ',' << (s.index_search.index_write_lock_held ? 1 : 0)
      << ',' << (s.index_search.is_index_building ? 1 : 0) << ','
      << (s.index_search.is_search_busy ? 1 : 0) << ',' << s.index_search.index_rebuild_count
      << ',' << s.index_search.index_deleted_count << ',' << s.index_search.index_total_entries
      << ',' << s.index_search.search_total_count << ',' << s.index_search.search_last_search_ms
      << ',' << s.index_search.search_last_postprocess_ms << ','
      << s.index_search.search_last_results_count << '\n';
}

void WriteJsonlSample(std::ofstream& jsonl, const MetricsSample& s, SamplePhase phase) {
  const json row = {
      {"type", "sample"},
      {"phase", PhaseToString(phase)},
      {"timestamp_unix_ms", s.timestamp_unix_ms},
      {"steady_elapsed_ms", s.steady_elapsed_ms},
      {
          "ui",
          {
              {"last_frame_delta_ms", s.ui.last_frame_delta_ms},
              {"unresponsive_ms", s.ui.unresponsive_ms},
              {"fps", s.ui.fps},
              {"is_minimized", s.ui.is_minimized},
              {"is_event_polling", s.ui.is_event_polling},
              {"is_window_moving_or_resizing", s.ui.is_window_moving_or_resizing},
          },
      },
      {
          "usn_buffers",
          {
              {"queue_depth", s.usn_buffers.queue_depth},
              {"max_queue_depth", s.usn_buffers.max_queue_depth},
              {"buffers_read", s.usn_buffers.buffers_read},
              {"buffers_processed", s.usn_buffers.buffers_processed},
              {"buffers_dropped", s.usn_buffers.buffers_dropped},
              {"records_processed", s.usn_buffers.records_processed},
              {"total_read_ms", s.usn_buffers.total_read_ms},
              {"total_process_ms", s.usn_buffers.total_process_ms},
              {"max_buffer_process_ms", s.usn_buffers.max_buffer_process_ms},
              {"lock_wait_total_ms", s.usn_buffers.lock_wait_total_ms},
              {"lock_wait_max_ms", s.usn_buffers.lock_wait_max_ms},
              {"live_buffers_per_sec", s.usn_buffers.live_buffers_per_sec},
              {"live_records_per_sec", s.usn_buffers.live_records_per_sec},
          },
      },
      {
          "usn_ops",
          {
              {"files_created", s.usn_ops.files_created},
              {"files_deleted", s.usn_ops.files_deleted},
              {"files_renamed", s.usn_ops.files_renamed},
              {"files_modified", s.usn_ops.files_modified},
              {"awaiting_count", s.usn_ops.awaiting_count},
              {"awaiting_oldest_age_ms", s.usn_ops.awaiting_oldest_age_ms},
              {"never_arriving_evicted", s.usn_ops.never_arriving_evicted},
              {"healed_awaiting_total", s.usn_ops.healed_awaiting_total},
              {"errors_total", s.usn_ops.errors_total},
              {"journal_wrap_errors", s.usn_ops.journal_wrap_errors},
          },
      },
      {
          "index_search",
          {
              {"index_write_lock_held", s.index_search.index_write_lock_held},
              {"is_index_building", s.index_search.is_index_building},
              {"is_search_busy", s.index_search.is_search_busy},
              {"index_rebuild_count", s.index_search.index_rebuild_count},
              {"index_deleted_count", s.index_search.index_deleted_count},
              {"index_total_entries", s.index_search.index_total_entries},
              {"search_total_count", s.index_search.search_total_count},
              {"search_last_search_ms", s.index_search.search_last_search_ms},
              {"search_last_postprocess_ms", s.index_search.search_last_postprocess_ms},
              {"search_last_results_count", s.index_search.search_last_results_count},
          },
      },
  };
  jsonl << row.dump() << '\n';
}

void WriteJsonlSummaryFooter(std::ofstream& jsonl,
                             const IncidentSummary& summary,
                             bool hit_max_cutoff) {
  json findings_json = json::array();
  for (const auto& f : summary.findings) {
    findings_json.push_back({
        {"code", f.code},
        {"severity", SeverityToString(f.severity)},
        {"title", f.title},
        {"explanation", f.explanation},
    });
  }
  const json footer = {
      {"type", "incident_summary"},
      {"incident_id", summary.incident_id},
      {"start_unix_ms", summary.start_unix_ms},
      {"duration_ms", summary.duration_ms},
      {"sample_count", summary.sample_count},
      {"hit_max_sample_cutoff", hit_max_cutoff},
      {"trigger_reasons_mask", summary.trigger_reasons_mask},
      {"trigger_reasons", FormatTriggerReasons(summary.trigger_reasons_mask)},
      {"overall_severity", SeverityToString(summary.overall_severity)},
      {
          "delta",
          {
              {"elapsed_ms", summary.delta.elapsed_ms},
              {"buffers_read", summary.delta.usn.buffers_read},
              {"buffers_processed", summary.delta.usn.buffers_processed},
              {"buffers_dropped", summary.delta.usn.buffers_dropped},
              {"records_processed", summary.delta.usn.records_processed},
              {"process_time_ms", summary.delta.usn.process_time_ms},
              {"lock_wait_ms", summary.delta.usn.lock_wait_ms},
              {"max_buffer_process_jump_ms", summary.delta.usn.max_buffer_process_jump_ms},
              {"files_created", summary.delta.workload.files_created},
              {"files_deleted", summary.delta.workload.files_deleted},
              {"files_renamed", summary.delta.workload.files_renamed},
              {"files_modified", summary.delta.workload.files_modified},
              {"index_rebuilds", summary.delta.workload.index_rebuilds},
              {"peak_queue_depth", summary.delta.peaks.max_queue_depth},
              {"peak_ui_stall_ms", summary.delta.peaks.max_ui_stall_ms},
              {"peak_live_buffers_per_sec", summary.delta.peaks.max_live_buffers_per_sec},
              {"index_write_lock_busy_samples", summary.delta.peaks.index_write_lock_busy_samples},
          },
      },
      {"findings", std::move(findings_json)},
  };
  jsonl << footer.dump() << '\n';
}

[[nodiscard]] DiagnosticSeverity ComputeMaxSeverity(
    const std::vector<DiagnosticFinding>& findings) noexcept {
  auto max_sev = DiagnosticSeverity::Healthy;
  for (const auto& f : findings) {
    if (static_cast<std::uint8_t>(f.severity) > static_cast<std::uint8_t>(max_sev)) {
      max_sev = f.severity;
    }
  }
  return max_sev;
}

}  // namespace

MetricsFlightRecorder::MetricsFlightRecorder(std::string_view diagnostics_dir)
    : start_steady_time_(std::chrono::steady_clock::now()),
      diagnostics_dir_(diagnostics_dir.empty() ? ResolveDefaultDiagnosticsDir()
                                               : std::string(diagnostics_dir)) {
  live_findings_.push_back({
      DiagnosticRuleId::Healthy,
      DiagnosticSeverity::Healthy,
      "DIAG_HEALTHY",
      "All Subsystems Healthy",
      "Flight recorder armed (15s pre-roll buffer active).",
  });
}

MetricsFlightRecorder::~MetricsFlightRecorder() {
  try {
    Stop();
  } catch (...) {  // NOSONAR(cpp:S2738, cpp:S2486) - Destructor must never throw
    LOG_ERROR("MetricsFlightRecorder: unexpected exception in destructor");
  }
}

void MetricsFlightRecorder::Start(const UsnMonitor* monitor,
                                  const SearchWorker* search_worker,
                                  const FileIndex* file_index,
                                  const std::atomic<bool>* is_index_building) {
  const std::scoped_lock lock(mutex_);
  if (sampler_thread_.joinable()) {
    return;
  }
  sampler_ctx_.monitor = monitor;
  sampler_ctx_.search_worker = search_worker;
  sampler_ctx_.file_index = file_index;
  sampler_ctx_.is_index_building = is_index_building;
  stop_requested_ = false;
  sampler_thread_ = std::thread(&MetricsFlightRecorder::SamplerThreadLoop, this);
}

void MetricsFlightRecorder::Stop() {
  {
    const std::scoped_lock lock(mutex_);
    stop_requested_ = true;
  }
  cv_.notify_all();
  if (sampler_thread_.joinable()) {
    sampler_thread_.join();
  }
  const std::scoped_lock lock(mutex_);
  if (state_ != RecorderState::Armed) {
    FinalizeActiveIncidentLocked(last_sample_, false);
    state_ = RecorderState::Armed;
  }
}

uint64_t MetricsFlightRecorder::ElapsedSteadyMs() const noexcept {
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start_steady_time_);
  return static_cast<uint64_t>((std::max)(int64_t{0}, elapsed.count()));
}

void MetricsFlightRecorder::OnUiFrameBegin(uint64_t steady_elapsed_ms, float fps) noexcept {
  ui_heartbeat_.cumulative_poll_ms_this_frame.store(0U);
  ui_heartbeat_.frame_begin_steady_ms.store(steady_elapsed_ms);
  const auto fps_clamped = (std::max)(0.0F, fps);
  ui_heartbeat_.fps_milli.store(static_cast<uint32_t>(fps_clamped * kFpsToMilliScale));
  ui_heartbeat_.in_process_frame.store(true);
}

void MetricsFlightRecorder::OnUiFrameEnd(uint64_t steady_elapsed_ms) noexcept {
  const uint64_t begin_ms = ui_heartbeat_.frame_begin_steady_ms.load();
  const uint64_t delta_ms = (steady_elapsed_ms >= begin_ms) ? (steady_elapsed_ms - begin_ms) : 0U;
  const uint64_t poll_ms = ui_heartbeat_.cumulative_poll_ms_this_frame.load();
  const uint64_t app_delta_ms = (delta_ms >= poll_ms) ? (delta_ms - poll_ms) : 0U;
  ui_heartbeat_.last_frame_delta_ms.store(app_delta_ms);
  ui_heartbeat_.in_process_frame.store(false);
}

void MetricsFlightRecorder::OnUiEventPollBegin() noexcept {
  ui_heartbeat_.poll_begin_steady_ms.store(ElapsedSteadyMs());
  ui_heartbeat_.in_event_polling.store(true);
}

void MetricsFlightRecorder::OnUiEventPollEnd() noexcept {
  if (const uint64_t begin_ms = ui_heartbeat_.poll_begin_steady_ms.load(),
                     now_ms = ElapsedSteadyMs();
      now_ms >= begin_ms) {
    ui_heartbeat_.cumulative_poll_ms_this_frame.fetch_add(now_ms - begin_ms);
  }
  ui_heartbeat_.in_event_polling.store(false);
}

void MetricsFlightRecorder::SetUiWindowMovingOrResizing(bool moving_or_resizing) noexcept {
  ui_heartbeat_.is_moving_or_resizing.store(moving_or_resizing);
}

void MetricsFlightRecorder::SetUiWindowMinimized(bool minimized) noexcept {
  ui_heartbeat_.is_minimized.store(minimized);
}

void MetricsFlightRecorder::SamplerThreadLoop() {
  while (true) {
    {
      std::unique_lock lock(mutex_);
      if (cv_.wait_for(
              lock,
              std::chrono::milliseconds(flight_recorder_constants::kSampleIntervalMs),
              [this] { return stop_requested_; })) {
        break;
      }
    }
    const MetricsSample sample = CollectLiveSample();
    RecordSample(sample);
  }
}

MetricsSample MetricsFlightRecorder::CollectLiveSample() {
  MetricsSample s{};
  const auto now_sys = std::chrono::system_clock::now();
  s.timestamp_unix_ms = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(now_sys.time_since_epoch()).count());
  s.steady_elapsed_ms = ElapsedSteadyMs();

  // 1. UI Heartbeat
  s.ui.is_minimized = ui_heartbeat_.is_minimized.load();
  s.ui.is_event_polling = ui_heartbeat_.in_event_polling.load();
  s.ui.is_window_moving_or_resizing = ui_heartbeat_.is_moving_or_resizing.load();
  s.ui.last_frame_delta_ms =
      static_cast<uint32_t>(ui_heartbeat_.last_frame_delta_ms.load());
  s.ui.fps = static_cast<float>(ui_heartbeat_.fps_milli.load()) * kMilliToFpsScale;
  if (ui_heartbeat_.in_process_frame.load() && !s.ui.is_event_polling &&
      !s.ui.is_window_moving_or_resizing) {
    const uint64_t frame_begin = ui_heartbeat_.frame_begin_steady_ms.load();
    s.ui.unresponsive_ms = (s.steady_elapsed_ms >= frame_begin)
                               ? static_cast<uint32_t>(s.steady_elapsed_ms - frame_begin)
                               : 0U;
  } else {
    s.ui.unresponsive_ms = 0U;
  }

  // 2. USN Metrics
  if (sampler_ctx_.monitor != nullptr) {
#ifdef _WIN32
    const auto usn = sampler_ctx_.monitor->GetMetricsSnapshot();
    s.usn_buffers.queue_depth = usn.current_queue_depth;
    s.usn_buffers.max_queue_depth = usn.max_queue_depth;
    s.usn_buffers.buffers_read = usn.buffers_read;
    s.usn_buffers.buffers_processed = usn.buffers_processed;
    s.usn_buffers.buffers_dropped = usn.buffers_dropped;
    s.usn_buffers.records_processed = usn.records_processed;
    s.usn_buffers.total_read_ms = usn.total_read_time_ms;
    s.usn_buffers.total_process_ms = usn.total_process_time_ms;
    s.usn_buffers.max_buffer_process_ms = usn.max_buffer_process_time_ms;
    s.usn_buffers.lock_wait_total_ms = usn.lock_wait.total_ms;
    s.usn_buffers.lock_wait_max_ms = usn.lock_wait.max_ms;

    s.usn_ops.files_created = usn.files_created;
    s.usn_ops.files_deleted = usn.files_deleted;
    s.usn_ops.files_renamed = usn.files_renamed;
    s.usn_ops.files_modified = usn.files_modified;
    s.usn_ops.awaiting_count = usn.awaiting_count;
    s.usn_ops.awaiting_oldest_age_ms = usn.awaiting_oldest_age_ms;
    s.usn_ops.never_arriving_evicted = usn.never_arriving_evicted;
    s.usn_ops.healed_awaiting_total = usn.healed_awaiting_total;
    s.usn_ops.errors_total = usn.errors.errors_encountered;
    s.usn_ops.journal_wrap_errors = usn.errors.journal_wrap_errors;

    const usn_throughput::ThroughputSample cur_tp{
        usn.buffers_processed, usn.records_processed, usn.current_queue_depth};
    if (sampler_ctx_.prev_throughput_steady_ms > 0U &&
        s.steady_elapsed_ms > sampler_ctx_.prev_throughput_steady_ms) {
      const double elapsed_sec =
          static_cast<double>(s.steady_elapsed_ms - sampler_ctx_.prev_throughput_steady_ms) *
          0.001;
      const auto rates = usn_throughput::ComputeThroughput(
          sampler_ctx_.prev_throughput_sample, cur_tp, elapsed_sec);
      s.usn_buffers.live_buffers_per_sec = rates.buffers_per_sec;
      s.usn_buffers.live_records_per_sec = rates.records_per_sec;
    }
    sampler_ctx_.prev_throughput_sample = cur_tp;
    sampler_ctx_.prev_throughput_steady_ms = s.steady_elapsed_ms;
#endif  // _WIN32
    s.index_search.is_index_building = sampler_ctx_.monitor->IsPopulatingIndex();
  }
  if (sampler_ctx_.is_index_building != nullptr && sampler_ctx_.is_index_building->load()) {
    s.index_search.is_index_building = true;
  }

  // 3. FileIndex Maintenance (non-blocking try_lock)
  if (sampler_ctx_.file_index != nullptr) {
    if (const auto stats = sampler_ctx_.file_index->TryGetMaintenanceStats(); stats.has_value()) {
      s.index_search.index_write_lock_held = false;
      sampler_ctx_.cached_rebuild_count = stats->rebuild_count;
      sampler_ctx_.cached_deleted_count = stats->deleted_count;
      sampler_ctx_.cached_total_entries = stats->total_entries;
    } else {
      s.index_search.index_write_lock_held = true;
    }
    s.index_search.index_rebuild_count = sampler_ctx_.cached_rebuild_count;
    s.index_search.index_deleted_count = sampler_ctx_.cached_deleted_count;
    s.index_search.index_total_entries = sampler_ctx_.cached_total_entries;
  }

  // 4. SearchWorker Metrics
  if (sampler_ctx_.search_worker != nullptr) {
    s.index_search.is_search_busy = sampler_ctx_.search_worker->IsBusy();
    const auto search_snap = sampler_ctx_.search_worker->GetMetricsSnapshot();
    s.index_search.search_total_count = search_snap.total_searches_;
    s.index_search.search_last_search_ms = search_snap.last_search_time_ms_;
    s.index_search.search_last_postprocess_ms = search_snap.last_postprocess_time_ms_;
    s.index_search.search_last_results_count = search_snap.last_results_count_;
  }

  return s;
}

void MetricsFlightRecorder::HandleArmedSampleLocked(const MetricsSample& sample,
                                                    uint32_t reasons) {
  const bool in_refractory =
      last_cutoff_steady_ms_ > 0U &&
      sample.steady_elapsed_ms > last_cutoff_steady_ms_ &&
      (sample.steady_elapsed_ms - last_cutoff_steady_ms_) <
          flight_recorder_constants::kRefractoryPeriodMs;
  if (reasons != 0U && !in_refractory) {
    BeginIncidentLocked(sample, reasons);
    return;
  }
  pre_roll_.Push(sample);
}

void MetricsFlightRecorder::HandleRecordingSampleLocked(const MetricsSample& sample,
                                                        uint32_t reasons) {
  active_incident_.trigger_reasons_mask |= reasons;
  UpdateIncidentPeaks(active_incident_.peaks, active_incident_.baseline_sample, sample);
  const bool healthy_for_cooldown = (reasons == 0U) && IsSampleHealthyForCooldown(sample);
  WriteSampleLocked(sample, healthy_for_cooldown ? SamplePhase::Cooldown : SamplePhase::Active);

  if (active_incident_.sample_count >= flight_recorder_constants::kMaxSamplesPerIncident) {
    last_cutoff_steady_ms_ = sample.steady_elapsed_ms;
    FinalizeActiveIncidentLocked(sample, true);
    state_ = RecorderState::Armed;
    return;
  }
  if (healthy_for_cooldown) {
    state_ = RecorderState::Cooldown;
    active_incident_.cooldown_start_steady_ms = sample.steady_elapsed_ms;
  }
}

void MetricsFlightRecorder::HandleCooldownSampleLocked(const MetricsSample& sample,
                                                       uint32_t reasons) {
  UpdateIncidentPeaks(active_incident_.peaks, active_incident_.baseline_sample, sample);
  if (reasons != 0U) {
    state_ = RecorderState::Recording;
    active_incident_.trigger_reasons_mask |= reasons;
    WriteSampleLocked(sample, SamplePhase::Active);
    return;
  }

  WriteSampleLocked(sample, SamplePhase::Cooldown);
  const uint64_t cooldown_elapsed =
      (sample.steady_elapsed_ms >= active_incident_.cooldown_start_steady_ms)
          ? (sample.steady_elapsed_ms - active_incident_.cooldown_start_steady_ms)
          : 0U;
  const bool hit_cutoff =
      active_incident_.sample_count >= flight_recorder_constants::kMaxSamplesPerIncident;
  if (cooldown_elapsed >= flight_recorder_constants::kCooldownDurationMs || hit_cutoff) {
    if (hit_cutoff) {
      last_cutoff_steady_ms_ = sample.steady_elapsed_ms;
    }
    FinalizeActiveIncidentLocked(sample, hit_cutoff);
    state_ = RecorderState::Armed;
  }
}

void MetricsFlightRecorder::RecordSample(const MetricsSample& sample) {
  const std::scoped_lock lock(mutex_);
  const MetricsSample prev = has_last_sample_ ? last_sample_ : sample;
  const std::uint32_t reasons = EvaluateTriggerReasons(prev, sample);

  if (state_ == RecorderState::Armed) {
    HandleArmedSampleLocked(sample, reasons);
  } else if (state_ == RecorderState::Recording) {
    HandleRecordingSampleLocked(sample, reasons);
  } else {
    HandleCooldownSampleLocked(sample, reasons);
  }

  RefreshLiveDiagnosticsLocked(sample);
  last_sample_ = sample;
  has_last_sample_ = true;
}

void MetricsFlightRecorder::BeginIncidentLocked(const MetricsSample& trigger_sample,
                                                uint32_t reasons_mask) {
  state_ = RecorderState::Recording;
  active_incident_ = ActiveIncidentState{};
  active_incident_.incident_id = next_incident_id_++;
  active_incident_.start_unix_ms = trigger_sample.timestamp_unix_ms;
  active_incident_.start_steady_ms = trigger_sample.steady_elapsed_ms;
  active_incident_.trigger_reasons_mask = reasons_mask;
  active_incident_.baseline_sample = has_last_sample_ ? last_sample_ : trigger_sample;
  UpdateIncidentPeaks(active_incident_.peaks, active_incident_.baseline_sample, trigger_sample);

  std::error_code ec;
  std::filesystem::create_directories(diagnostics_dir_, ec);
  const std::string base_name =
      FormatIncidentBaseFilename(trigger_sample.timestamp_unix_ms, active_incident_.incident_id);
  active_incident_.jsonl_path = path_utils::JoinPath(diagnostics_dir_, base_name + ".jsonl");
  active_incident_.csv_path = path_utils::JoinPath(diagnostics_dir_, base_name + ".csv");

  active_incident_.jsonl_stream.open(active_incident_.jsonl_path, std::ios::out | std::ios::trunc);
  active_incident_.csv_stream.open(active_incident_.csv_path, std::ios::out | std::ios::trunc);

  if (active_incident_.jsonl_stream.is_open()) {
    const json header = {
        {"type", "header"},
        {"incident_id", active_incident_.incident_id},
        {"start_unix_ms", active_incident_.start_unix_ms},
        {"start_steady_ms", active_incident_.start_steady_ms},
        {"trigger_reasons_mask", reasons_mask},
        {"trigger_reasons", FormatTriggerReasons(reasons_mask)},
        {"pre_roll_samples", pre_roll_.Size()},
        {"sample_interval_ms", flight_recorder_constants::kSampleIntervalMs},
    };
    active_incident_.jsonl_stream << header.dump() << '\n';
  } else {
    LOG_WARNING_BUILD("MetricsFlightRecorder: failed to open " << active_incident_.jsonl_path);
  }

  if (active_incident_.csv_stream.is_open()) {
    WriteCsvHeader(active_incident_.csv_stream);
  } else {
    LOG_WARNING_BUILD("MetricsFlightRecorder: failed to open " << active_incident_.csv_path);
  }

  pre_roll_.ForEachChronological([this](const MetricsSample& pre_sample) {
    WriteSampleLocked(pre_sample, SamplePhase::PreRoll);
  });
  pre_roll_.Clear();

  WriteSampleLocked(trigger_sample, SamplePhase::Active);
}

void MetricsFlightRecorder::WriteSampleLocked(const MetricsSample& sample, SamplePhase phase) {
  ++active_incident_.sample_count;
  if (active_incident_.jsonl_stream.is_open()) {
    WriteJsonlSample(active_incident_.jsonl_stream, sample, phase);
  }
  if (active_incident_.csv_stream.is_open()) {
    WriteCsvRow(active_incident_.csv_stream, sample, phase);
  }
}

void MetricsFlightRecorder::FinalizeActiveIncidentLocked(const MetricsSample& final_sample,
                                                         bool hit_max_cutoff) {
  IncidentSummary summary{};
  summary.incident_id = active_incident_.incident_id;
  summary.start_unix_ms = active_incident_.start_unix_ms;
  summary.start_steady_ms = active_incident_.start_steady_ms;
  summary.duration_ms =
      (final_sample.steady_elapsed_ms >= active_incident_.start_steady_ms)
          ? (final_sample.steady_elapsed_ms - active_incident_.start_steady_ms)
          : 0U;
  summary.trigger_reasons_mask = active_incident_.trigger_reasons_mask;
  summary.sample_count = active_incident_.sample_count;
  summary.delta = ComputeWindowDelta(
      active_incident_.baseline_sample, final_sample, active_incident_.peaks);
  summary.findings = AnalyzeWindow(
      active_incident_.baseline_sample, final_sample, active_incident_.peaks);
  summary.overall_severity = ComputeMaxSeverity(summary.findings);
  summary.jsonl_file_path = active_incident_.jsonl_path;
  summary.csv_file_path = active_incident_.csv_path;

  if (active_incident_.jsonl_stream.is_open()) {
    WriteJsonlSummaryFooter(active_incident_.jsonl_stream, summary, hit_max_cutoff);
    active_incident_.jsonl_stream.flush();
    active_incident_.jsonl_stream.close();
  }
  if (active_incident_.csv_stream.is_open()) {
    active_incident_.csv_stream.flush();
    active_incident_.csv_stream.close();
  }

  last_incident_ = std::move(summary);
  has_last_incident_ = true;
  ++incidents_recorded_;
  PruneOldFlightRecordsLocked();
}

void MetricsFlightRecorder::RefreshLiveDiagnosticsLocked(const MetricsSample& current_sample) {
  if (state_ != RecorderState::Armed) {
    live_findings_ = AnalyzeWindow(
        active_incident_.baseline_sample, current_sample, active_incident_.peaks);
  } else {
    const MetricsSample baseline = pre_roll_.Empty() ? current_sample : pre_roll_.Oldest();
    IncidentPeaks live_peaks{};
    UpdateIncidentPeaks(live_peaks, baseline, current_sample);
    live_findings_ = AnalyzeWindow(baseline, current_sample, live_peaks);
  }
  live_severity_ = ComputeMaxSeverity(live_findings_);
}

std::string MetricsFlightRecorder::TriggerManualDump() {
  const std::scoped_lock lock(mutex_);
  if (state_ != RecorderState::Armed) {
    // Already recording an incident: finalize it immediately so files are flushed for inspection.
    active_incident_.trigger_reasons_mask |= ToMask(TriggerReason::ManualDump);
    FinalizeActiveIncidentLocked(last_sample_, false);
    state_ = RecorderState::Armed;
    return last_incident_.jsonl_file_path;
  }

  const MetricsSample current = has_last_sample_ ? last_sample_ : MetricsSample{};
  const MetricsSample baseline = pre_roll_.Empty() ? current : pre_roll_.Oldest();
  IncidentPeaks peaks{};
  pre_roll_.ForEachChronological([&peaks, &baseline](const MetricsSample& s) {
    UpdateIncidentPeaks(peaks, baseline, s);
  });

  BeginIncidentLocked(current, ToMask(TriggerReason::ManualDump));
  active_incident_.baseline_sample = baseline;
  active_incident_.peaks = peaks;
  FinalizeActiveIncidentLocked(current, false);
  state_ = RecorderState::Armed;
  return last_incident_.jsonl_file_path;
}

RecorderUiSnapshot MetricsFlightRecorder::GetUiSnapshot() const {
  const std::scoped_lock lock(mutex_);
  RecorderUiSnapshot snap{};
  snap.state = state_;
  snap.pre_roll_samples = pre_roll_.Size();
  snap.incidents_recorded = incidents_recorded_;
  snap.latest_steady_ms = last_sample_.steady_elapsed_ms;
  snap.live_severity = live_severity_;
  snap.live_findings = live_findings_;
  snap.has_last_incident = has_last_incident_;
  snap.last_incident = last_incident_;
  snap.diagnostics_dir = diagnostics_dir_;
  return snap;
}

std::string MetricsFlightRecorder::GetDiagnosticsDirectory() const {
  const std::scoped_lock lock(mutex_);
  return diagnostics_dir_;
}

void MetricsFlightRecorder::PruneOldFlightRecordsLocked() const {
  std::error_code ec;
  if (!std::filesystem::exists(diagnostics_dir_, ec)) {
    return;
  }

  std::vector<std::filesystem::path> jsonl_files;
  for (const auto& entry : std::filesystem::directory_iterator(diagnostics_dir_, ec)) {
    if (ec || !entry.is_regular_file(ec)) {
      continue;
    }
    const auto& path = entry.path();
    const std::string filename = path.filename().string();
    if (path.extension() == ".jsonl" &&
        filename.compare(0U, kFilePrefix.size(), kFilePrefix) == 0) {
      jsonl_files.push_back(path);
    }
  }

  if (jsonl_files.size() <= flight_recorder_constants::kMaxRetainedIncidents) {
    return;
  }

  std::sort(jsonl_files.begin(), jsonl_files.end());
  const size_t to_remove = jsonl_files.size() - flight_recorder_constants::kMaxRetainedIncidents;
  for (size_t i = 0U; i < to_remove; ++i) {
    const auto& jsonl_path = jsonl_files[i];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - i < to_remove < jsonl_files.size()
    std::filesystem::path csv_path = jsonl_path;
    csv_path.replace_extension(".csv");
    (void)std::filesystem::remove(jsonl_path, ec);
    (void)std::filesystem::remove(csv_path, ec);
  }
}

}  // namespace diagnostics
