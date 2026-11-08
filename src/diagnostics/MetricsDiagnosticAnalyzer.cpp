/**
 * @file diagnostics/MetricsDiagnosticAnalyzer.cpp
 * @brief Implementation of the stateless MetricsDiagnosticAnalyzer rule engine.
 */

#include "diagnostics/MetricsDiagnosticAnalyzer.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace diagnostics {

namespace {

constexpr uint64_t kLockContentionMinProcessDeltaMs = 100;
constexpr double kLockContentionHighShareRatio = 0.35;
constexpr double kCpuSpikeMaxLockShareRatio = 0.15;
constexpr size_t kIngestStormQueueThreshold = 100;
constexpr double kIngestStormMinBuffersPerSec = 200.0;
constexpr double kIngestStormMaxAvgProcessMs = 2.0;
constexpr size_t kStalledQueueMinDepth = 10;
constexpr uint64_t kStalledQueueMinWindowMs = 1000;
constexpr size_t kPlaceholderWarnCount = 500;
constexpr uint64_t kPlaceholderWarnOldestAgeMs = 60000;
constexpr uint64_t kHeavySearchMinTotalMs = 200;
constexpr double kHeavySearchPostProcessRatio = 0.70;
constexpr uint32_t kCriticalUiStallMs = 1000U;
constexpr uint64_t kMsPerSecond = 1000U;
constexpr size_t kReservedFindingCapacity = 6U;

template <typename T>
[[nodiscard]] constexpr T SaturatingDelta(T current_val, T baseline_val) noexcept {
  return (current_val >= baseline_val) ? (current_val - baseline_val) : T{0};
}

[[nodiscard]] uint32_t EffectiveUiStallMs(const UiSample& ui) noexcept {
  if (ui.is_minimized || ui.is_event_polling || ui.is_window_moving_or_resizing) {
    return 0U;
  }
  return (std::max)(ui.last_frame_delta_ms, ui.unresponsive_ms);
}

void CheckBufferDropsRule(const MetricsWindowDelta& delta,
                          const MetricsSample& current,
                          std::vector<DiagnosticFinding>& findings) {
  const size_t dropped = (std::max)(delta.usn.buffers_dropped, current.usn_buffers.buffers_dropped);
  const size_t wraps = (std::max)(delta.workload.journal_wrap_errors, current.usn_ops.journal_wrap_errors);
  if (dropped == 0U && wraps == 0U) {
    return;
  }
  std::ostringstream oss;
  oss << "Critical: " << dropped << " USN buffer(s) dropped and " << wraps
      << " journal wrap error(s) observed. The in-memory index may be out of sync with NTFS "
         "until the next index rebuild.";
  findings.push_back({
      DiagnosticRuleId::UsnBufferDrops,
      DiagnosticSeverity::Critical,
      "DIAG_USN_BUFFER_DROPS",
      "USN Queue Overflow / Dropped Buffers",
      oss.str(),
  });
}

void CheckUiFrameStallRule(const MetricsWindowDelta& delta,
                           const MetricsSample& current,
                           std::vector<DiagnosticFinding>& findings) {
  const uint32_t peak_stall_ms =
      (std::max)(delta.peaks.max_ui_stall_ms, EffectiveUiStallMs(current.ui));
  if (peak_stall_ms < flight_recorder_constants::kTriggerUiFrameStallMs) {
    return;
  }
  std::ostringstream oss;
  oss << "Main UI thread stalled for " << peak_stall_ms << " ms";
  if (delta.peaks.index_write_lock_busy_samples > 0U) {
    oss << " (" << delta.peaks.index_write_lock_busy_samples
        << " sampler tick(s) observed FileIndex exclusive write lock held)";
  }
  oss << ".";
  findings.push_back({
      DiagnosticRuleId::UiFrameStall,
      peak_stall_ms >= kCriticalUiStallMs ? DiagnosticSeverity::Critical
                                          : DiagnosticSeverity::Warning,
      "DIAG_UI_FRAME_STALL",
      "UI Thread Stall Detected",
      oss.str(),
  });
}

void CheckIndexRebuildRule(const MetricsWindowDelta& delta,
                           const MetricsSample& current,
                           std::vector<DiagnosticFinding>& findings) {
  if (delta.workload.index_rebuilds == 0U) {
    return;
  }
  std::ostringstream oss;
  oss << "FileIndex path-buffer compaction ran " << delta.workload.index_rebuilds
      << " time(s) during the window (current deleted entries: "
      << current.index_search.index_deleted_count << "/"
      << current.index_search.index_total_entries
      << "). Compaction acquires an exclusive index lock and rebuilds trigram/extension indexes.";
  findings.push_back({
      DiagnosticRuleId::IndexRebuildOnUi,
      DiagnosticSeverity::Warning,
      "DIAG_INDEX_REBUILD_ON_UI",
      "Index Path-Buffer Compaction Executed",
      oss.str(),
  });
}

void CheckUsnLockContentionRule(const MetricsWindowDelta& delta,
                                std::vector<DiagnosticFinding>& findings) {
  if (delta.usn.process_time_ms < kLockContentionMinProcessDeltaMs) {
    return;
  }
  const double lock_share = static_cast<double>(delta.usn.lock_wait_ms) /
                            static_cast<double>(delta.usn.process_time_ms);
  if (lock_share < kLockContentionHighShareRatio) {
    return;
  }
  const double pct = lock_share * 100.0;
  std::array<char, 64> pct_buf{};
  (void)std::snprintf(pct_buf.data(), pct_buf.size(), "%.1f%%", pct);
  std::ostringstream oss;
  oss << "USN processor spent " << pct_buf.data() << " (" << delta.usn.lock_wait_ms << " ms of "
      << delta.usn.process_time_ms
      << " ms process time) waiting for the FileIndex exclusive lock while searches or "
         "maintenance held locks.";
  findings.push_back({
      DiagnosticRuleId::UsnLockContention,
      DiagnosticSeverity::Warning,
      "DIAG_USN_LOCK_CONTENTION",
      "USN Blocked by FileIndex Lock Contention",
      oss.str(),
  });
}

void CheckUsnCpuApplySpikeRule(const MetricsWindowDelta& delta,
                               std::vector<DiagnosticFinding>& findings) {
  const uint64_t jump_ms =
      (std::max)(delta.usn.max_buffer_process_jump_ms, delta.peaks.max_single_buffer_jump_ms);
  if (const bool has_process_spike =
          jump_ms >= flight_recorder_constants::kTriggerBufferLatencyJumpMs ||
          delta.usn.process_time_ms >= flight_recorder_constants::kTriggerBufferLatencyJumpMs;
      !has_process_spike) {
    return;
  }

  if (const double lock_share =
          (delta.usn.process_time_ms > 0U)
              ? (static_cast<double>(delta.usn.lock_wait_ms) /
                 static_cast<double>(delta.usn.process_time_ms))
              : 0.0;
      lock_share >= kCpuSpikeMaxLockShareRatio) {
    return;
  }
  if (jump_ms < flight_recorder_constants::kTriggerBufferLatencyJumpMs) {
    return;
  }

  std::ostringstream oss;
  oss << "Single USN buffer apply spiked by +" << jump_ms << " ms (new max: "
      << delta.usn.end_max_buffer_process_ms << " ms) with only " << delta.usn.lock_wait_ms
      << " ms lock wait across the window. Indicates CPU-bound buffer processing (e.g., subtree "
         "eviction or bulk directory operations) rather than lock contention.";
  findings.push_back({
      DiagnosticRuleId::UsnCpuApplySpike,
      DiagnosticSeverity::Warning,
      "DIAG_USN_CPU_APPLY_SPIKE",
      "USN CPU-Bound Buffer Apply Spike",
      oss.str(),
  });
}

void CheckUsnIngestStormRule(const MetricsWindowDelta& delta,
                             const MetricsSample& current,
                             std::vector<DiagnosticFinding>& findings) {
  const size_t peak_q = (std::max)(delta.peaks.max_queue_depth, current.usn_buffers.queue_depth);
  const double peak_rate =
      (std::max)(delta.peaks.max_live_buffers_per_sec, current.usn_buffers.live_buffers_per_sec);
  if (peak_q < kIngestStormQueueThreshold && delta.usn.buffers_processed < kIngestStormQueueThreshold) {
    return;
  }
  const double avg_proc_ms =
      (delta.usn.buffers_processed > 0U)
          ? (static_cast<double>(delta.usn.process_time_ms) /
             static_cast<double>(delta.usn.buffers_processed))
          : 0.0;
  const size_t total_ops = delta.workload.files_created + delta.workload.files_deleted +
                           delta.workload.files_renamed + delta.workload.files_modified;

  std::array<char, 64> rate_buf{};
  (void)std::snprintf(rate_buf.data(), rate_buf.size(), "%.0f", peak_rate);
  std::array<char, 64> avg_buf{};
  (void)std::snprintf(avg_buf.data(), avg_buf.size(), "%.2f", avg_proc_ms);

  std::ostringstream oss;
  oss << "USN queue peaked at " << peak_q << " buffers (processed "
      << delta.usn.buffers_processed << " buffers / " << delta.usn.records_processed
      << " records; +" << delta.workload.files_created << " created, +"
      << delta.workload.files_deleted << " deleted, +" << delta.workload.files_renamed
      << " renamed, +" << delta.workload.files_modified << " modified = " << total_ops
      << " file ops). Peak drain rate: " << rate_buf.data() << " buffers/sec (" << avg_buf.data()
      << " ms/buffer avg).";

  const DiagnosticSeverity sev =
      (peak_rate >= kIngestStormMinBuffersPerSec && avg_proc_ms <= kIngestStormMaxAvgProcessMs)
          ? DiagnosticSeverity::Info
          : DiagnosticSeverity::Warning;
  findings.push_back({
      DiagnosticRuleId::UsnIngestStorm,
      sev,
      "DIAG_USN_INGEST_STORM",
      "High-Volume Filesystem USN Burst",
      oss.str(),
  });
}

void CheckUsnQueueStalledRule(const MetricsWindowDelta& delta,
                              const MetricsSample& current,
                              std::vector<DiagnosticFinding>& findings) {
  if (current.usn_buffers.queue_depth < kStalledQueueMinDepth ||
      delta.elapsed_ms < kStalledQueueMinWindowMs ||
      delta.usn.buffers_processed > 0U) {
    return;
  }
  std::ostringstream oss;
  oss << "USN queue holds " << current.usn_buffers.queue_depth
      << " pending buffers, but 0 buffers completed over the last " << delta.elapsed_ms << " ms.";
  findings.push_back({
      DiagnosticRuleId::UsnQueueStalled,
      DiagnosticSeverity::Critical,
      "DIAG_USN_QUEUE_STALLED",
      "USN Processor Stalled With Active Backlog",
      oss.str(),
  });
}

void CheckPlaceholderAccumulationRule(const MetricsWindowDelta& delta,
                                      const MetricsSample& current,
                                      std::vector<DiagnosticFinding>& findings) {
  const size_t awaiting =
      (std::max)(delta.peaks.max_awaiting_count, current.usn_ops.awaiting_count);
  const uint64_t oldest_ms =
      (std::max)(delta.peaks.max_awaiting_oldest_age_ms, current.usn_ops.awaiting_oldest_age_ms);
  if (awaiting < kPlaceholderWarnCount || oldest_ms < kPlaceholderWarnOldestAgeMs) {
    return;
  }
  std::ostringstream oss;
  oss << awaiting << " index entries are waiting on unresolved parent directories (oldest age: "
      << (oldest_ms / kMsPerSecond) << "s).";
  findings.push_back({
      DiagnosticRuleId::PlaceholderAccumulation,
      DiagnosticSeverity::Warning,
      "DIAG_PLACEHOLDER_ACCUMULATION",
      "Unresolved Parent Placeholders Growing",
      oss.str(),
  });
}

void CheckSearchPostProcessHeavyRule(const MetricsSample& current,
                                     std::vector<DiagnosticFinding>& findings) {
  const uint64_t total_ms =
      current.index_search.search_last_search_ms + current.index_search.search_last_postprocess_ms;
  if (total_ms < kHeavySearchMinTotalMs) {
    return;
  }
  const double post_ratio =
      static_cast<double>(current.index_search.search_last_postprocess_ms) /
      static_cast<double>(total_ms);
  if (post_ratio < kHeavySearchPostProcessRatio) {
    return;
  }
  std::array<char, 64> pct_buf{};
  (void)std::snprintf(pct_buf.data(), pct_buf.size(), "%.0f%%", post_ratio * 100.0);
  std::ostringstream oss;
  oss << "Most recent search took " << total_ms << " ms ("
      << current.index_search.search_last_results_count << " results), spending "
      << pct_buf.data() << " (" << current.index_search.search_last_postprocess_ms
      << " ms) in result path/filter post-processing.";
  findings.push_back({
      DiagnosticRuleId::SearchPostProcessHeavy,
      DiagnosticSeverity::Info,
      "DIAG_SEARCH_POSTPROCESS_HEAVY",
      "Search Post-Processing Bottleneck",
      oss.str(),
  });
}

}  // namespace

std::uint32_t EvaluateTriggerReasons(const MetricsSample& prev,
                                     const MetricsSample& cur) noexcept {
  std::uint32_t mask = 0U;

  // Gate USN queue and buffer-time triggers while initial index population is running.
  if (!cur.index_search.is_index_building) {
    if (cur.usn_buffers.queue_depth >= flight_recorder_constants::kTriggerUsnQueueDepth) {
      mask |= ToMask(TriggerReason::UsnQueueDepth);
    }
    const uint64_t max_buf_jump =
        SaturatingDelta(cur.usn_buffers.max_buffer_process_ms,
                        prev.usn_buffers.max_buffer_process_ms);
    if (prev.steady_elapsed_ms > 0U &&
        max_buf_jump >= flight_recorder_constants::kTriggerBufferLatencyJumpMs) {
      mask |= ToMask(TriggerReason::UsnBufferLatencySpike);
    }
  }

  if (EffectiveUiStallMs(cur.ui) >= flight_recorder_constants::kTriggerUiFrameStallMs) {
    mask |= ToMask(TriggerReason::UiFrameStall);
  }

  if (SaturatingDelta(cur.usn_buffers.buffers_dropped, prev.usn_buffers.buffers_dropped) > 0U) {
    mask |= ToMask(TriggerReason::UsnBufferDropped);
  }

  return mask;
}

bool IsSampleHealthyForCooldown(const MetricsSample& cur) noexcept {
  const bool queue_drained = cur.usn_buffers.queue_depth == 0U;
  const bool ui_responsive =
      EffectiveUiStallMs(cur.ui) <= flight_recorder_constants::kHealthyUiFrameMaxMs;
  return queue_drained && ui_responsive;
}

void UpdateIncidentPeaks(IncidentPeaks& peaks,
                         const MetricsSample& baseline,
                         const MetricsSample& cur) noexcept {
  peaks.max_queue_depth = (std::max)(peaks.max_queue_depth, cur.usn_buffers.queue_depth);
  peaks.max_ui_stall_ms = (std::max)(peaks.max_ui_stall_ms, EffectiveUiStallMs(cur.ui));
  peaks.max_live_buffers_per_sec =
      (std::max)(peaks.max_live_buffers_per_sec, cur.usn_buffers.live_buffers_per_sec);
  peaks.max_live_records_per_sec =
      (std::max)(peaks.max_live_records_per_sec, cur.usn_buffers.live_records_per_sec);
  const uint64_t jump_ms = SaturatingDelta(cur.usn_buffers.max_buffer_process_ms,
                                           baseline.usn_buffers.max_buffer_process_ms);
  peaks.max_single_buffer_jump_ms = (std::max)(peaks.max_single_buffer_jump_ms, jump_ms);
  peaks.max_awaiting_count = (std::max)(peaks.max_awaiting_count, cur.usn_ops.awaiting_count);
  peaks.max_awaiting_oldest_age_ms =
      (std::max)(peaks.max_awaiting_oldest_age_ms, cur.usn_ops.awaiting_oldest_age_ms);
  if (cur.index_search.index_write_lock_held) {
    ++peaks.index_write_lock_busy_samples;
  }
}

MetricsWindowDelta ComputeWindowDelta(const MetricsSample& baseline,
                                      const MetricsSample& current,
                                      const IncidentPeaks& peaks) noexcept {
  MetricsWindowDelta delta{};
  delta.elapsed_ms = SaturatingDelta(current.steady_elapsed_ms, baseline.steady_elapsed_ms);

  delta.usn.buffers_read =
      SaturatingDelta(current.usn_buffers.buffers_read, baseline.usn_buffers.buffers_read);
  delta.usn.buffers_processed =
      SaturatingDelta(current.usn_buffers.buffers_processed,
                      baseline.usn_buffers.buffers_processed);
  delta.usn.buffers_dropped =
      SaturatingDelta(current.usn_buffers.buffers_dropped, baseline.usn_buffers.buffers_dropped);
  delta.usn.records_processed =
      SaturatingDelta(current.usn_buffers.records_processed,
                      baseline.usn_buffers.records_processed);
  delta.usn.read_time_ms =
      SaturatingDelta(current.usn_buffers.total_read_ms, baseline.usn_buffers.total_read_ms);
  delta.usn.process_time_ms =
      SaturatingDelta(current.usn_buffers.total_process_ms, baseline.usn_buffers.total_process_ms);
  delta.usn.lock_wait_ms =
      SaturatingDelta(current.usn_buffers.lock_wait_total_ms,
                      baseline.usn_buffers.lock_wait_total_ms);
  delta.usn.max_buffer_process_jump_ms =
      SaturatingDelta(current.usn_buffers.max_buffer_process_ms,
                      baseline.usn_buffers.max_buffer_process_ms);
  delta.usn.end_max_buffer_process_ms = current.usn_buffers.max_buffer_process_ms;

  delta.workload.files_created =
      SaturatingDelta(current.usn_ops.files_created, baseline.usn_ops.files_created);
  delta.workload.files_deleted =
      SaturatingDelta(current.usn_ops.files_deleted, baseline.usn_ops.files_deleted);
  delta.workload.files_renamed =
      SaturatingDelta(current.usn_ops.files_renamed, baseline.usn_ops.files_renamed);
  delta.workload.files_modified =
      SaturatingDelta(current.usn_ops.files_modified, baseline.usn_ops.files_modified);
  delta.workload.index_rebuilds =
      SaturatingDelta(current.index_search.index_rebuild_count,
                      baseline.index_search.index_rebuild_count);
  delta.workload.journal_wrap_errors =
      SaturatingDelta(current.usn_ops.journal_wrap_errors, baseline.usn_ops.journal_wrap_errors);
  delta.workload.searches_completed =
      SaturatingDelta(current.index_search.search_total_count,
                      baseline.index_search.search_total_count);

  delta.peaks = peaks;
  UpdateIncidentPeaks(delta.peaks, baseline, current);
  return delta;
}

std::vector<DiagnosticFinding> AnalyzeWindow(const MetricsSample& baseline,
                                             const MetricsSample& current,
                                             const IncidentPeaks& peaks) {
  const MetricsWindowDelta delta = ComputeWindowDelta(baseline, current, peaks);
  std::vector<DiagnosticFinding> findings;
  findings.reserve(kReservedFindingCapacity);

  CheckBufferDropsRule(delta, current, findings);
  CheckUiFrameStallRule(delta, current, findings);
  CheckIndexRebuildRule(delta, current, findings);
  CheckUsnLockContentionRule(delta, findings);
  CheckUsnCpuApplySpikeRule(delta, findings);
  CheckUsnQueueStalledRule(delta, current, findings);
  CheckUsnIngestStormRule(delta, current, findings);
  CheckPlaceholderAccumulationRule(delta, current, findings);
  CheckSearchPostProcessHeavyRule(current, findings);

  if (findings.empty()) {
    findings.push_back({
        DiagnosticRuleId::Healthy,
        DiagnosticSeverity::Healthy,
        "DIAG_HEALTHY",
        "All Subsystems Healthy",
        "USN queue is drained, UI frame times are nominal, and no lock contention or buffer "
        "spikes were detected.",
    });
  }
  return findings;
}

std::string FormatTriggerReasons(std::uint32_t mask) {
  if (mask == 0U) {
    return {"None"};
  }
  std::string out;
  const auto append_reason = [&out](const char* label) {
    if (!out.empty()) {
      out.append(", ", 2U);
    }
    out.append(label);
  };
  if (HasTriggerReason(mask, TriggerReason::UsnQueueDepth)) {
    append_reason("USN Queue Backlog");
  }
  if (HasTriggerReason(mask, TriggerReason::UsnBufferLatencySpike)) {
    append_reason("USN Buffer Latency Spike");
  }
  if (HasTriggerReason(mask, TriggerReason::UiFrameStall)) {
    append_reason("UI Frame Stall");
  }
  if (HasTriggerReason(mask, TriggerReason::UsnBufferDropped)) {
    append_reason("USN Buffer Dropped");
  }
  if (HasTriggerReason(mask, TriggerReason::ManualDump)) {
    append_reason("Manual Dump");
  }
  return out;
}

const char* SeverityToString(DiagnosticSeverity severity) noexcept {
  switch (severity) {
    case DiagnosticSeverity::Healthy:
      return "Healthy";
    case DiagnosticSeverity::Info:
      return "Info";
    case DiagnosticSeverity::Warning:
      return "Warning";
    case DiagnosticSeverity::Critical:
      return "Critical";
  }
  return "Unknown";
}

}  // namespace diagnostics
