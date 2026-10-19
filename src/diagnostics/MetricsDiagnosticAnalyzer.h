#pragma once

/**
 * @file diagnostics/MetricsDiagnosticAnalyzer.h
 * @brief Stateless rule engine that interprets windowed MetricsSample deltas into
 *        plain-English diagnostic findings.
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "diagnostics/MetricsSample.h"

namespace diagnostics {

/**
 * @brief Severity level of a diagnostic finding or overall incident.
 */
enum class DiagnosticSeverity : std::uint8_t {
  Healthy = 0,
  Info = 1,
  Warning = 2,
  Critical = 3,
};

/**
 * @brief Identifiers for automatic diagnostic rules.
 */
enum class DiagnosticRuleId : std::uint8_t {
  Healthy = 0,
  UiFrameStall = 1,
  IndexRebuildOnUi = 2,
  UsnLockContention = 3,
  UsnCpuApplySpike = 4,
  UsnIngestStorm = 5,
  UsnQueueStalled = 6,
  UsnBufferDrops = 7,
  PlaceholderAccumulation = 8,
  SearchPostProcessHeavy = 9,
};

/**
 * @brief Single human-readable diagnostic finding emitted by the analyzer.
 */
struct DiagnosticFinding {
  DiagnosticRuleId rule_id = DiagnosticRuleId::Healthy;
  DiagnosticSeverity severity = DiagnosticSeverity::Healthy;
  std::string code;
  std::string title;
  std::string explanation;
};

/**
 * @brief Windowed counter deltas between an incident baseline sample and end/current sample.
 *
 * Grouped into buffer/timing and operation/index sub-structs to stay well below
 * Sonar cpp:S1820 field limits.
 */
struct UsnWindowDelta {
  size_t buffers_read = 0;
  size_t buffers_processed = 0;
  size_t buffers_dropped = 0;
  size_t records_processed = 0;
  uint64_t read_time_ms = 0;
  uint64_t process_time_ms = 0;
  uint64_t lock_wait_ms = 0;
  uint64_t max_buffer_process_jump_ms = 0;
  uint64_t end_max_buffer_process_ms = 0;
};

struct WorkloadWindowDelta {
  size_t files_created = 0;
  size_t files_deleted = 0;
  size_t files_renamed = 0;
  size_t files_modified = 0;
  size_t index_rebuilds = 0;
  size_t journal_wrap_errors = 0;
  size_t searches_completed = 0;
};

struct MetricsWindowDelta {
  uint64_t elapsed_ms = 0;
  UsnWindowDelta usn{};
  WorkloadWindowDelta workload{};
  IncidentPeaks peaks{};
};

/**
 * @brief Post-mortem summary of a completed or active incident.
 */
struct IncidentSummary {
  uint64_t incident_id = 0;
  uint64_t start_unix_ms = 0;
  uint64_t start_steady_ms = 0;
  uint64_t duration_ms = 0;
  uint32_t trigger_reasons_mask = 0;
  size_t sample_count = 0;
  MetricsWindowDelta delta{};
  DiagnosticSeverity overall_severity = DiagnosticSeverity::Healthy;
  std::vector<DiagnosticFinding> findings;
  std::string jsonl_file_path;
  std::string csv_file_path;
};

/**
 * @brief Evaluate which automatic trigger conditions are satisfied on the transition prev -> cur.
 */
[[nodiscard]] std::uint32_t EvaluateTriggerReasons(const MetricsSample& prev,
                                                   const MetricsSample& cur) noexcept;

/**
 * @brief Return true when the current sample indicates the system has recovered to healthy state.
 */
[[nodiscard]] bool IsSampleHealthyForCooldown(const MetricsSample& cur) noexcept;

/**
 * @brief Update running incident peaks from a new sample (and optional previous sample).
 */
void UpdateIncidentPeaks(IncidentPeaks& peaks,
                         const MetricsSample& baseline,
                         const MetricsSample& cur) noexcept;

/**
 * @brief Compute saturating counter deltas across [baseline, current] combined with observed peaks.
 */
[[nodiscard]] MetricsWindowDelta ComputeWindowDelta(const MetricsSample& baseline,
                                                    const MetricsSample& current,
                                                    const IncidentPeaks& peaks) noexcept;

/**
 * @brief Run all diagnostic rules over [baseline, current] and return prioritized findings.
 */
[[nodiscard]] std::vector<DiagnosticFinding> AnalyzeWindow(const MetricsSample& baseline,
                                                           const MetricsSample& current,
                                                           const IncidentPeaks& peaks);

/**
 * @brief Format a bitmask of TriggerReason values as a human-readable comma-separated string.
 */
[[nodiscard]] std::string FormatTriggerReasons(std::uint32_t mask);

/**
 * @brief Convert DiagnosticSeverity to display label ("Healthy", "Info", "Warning", "Critical").
 */
[[nodiscard]] const char* SeverityToString(DiagnosticSeverity severity) noexcept;

}  // namespace diagnostics
