#pragma once

// Windowed USN throughput derived from successive metric snapshots.
// Read-path only: no atomics, no hot-path cost. The processor thread keeps
// publishing cumulative counters (buffers_processed, records_processed);
// the UI samples them on a ~500ms cadence and derives buffers/s,
// records/s and queue-drain ETA. Cumulative averages hide stalls, max
// hides sustained rate — this windowed rate shows both.

#include <cstddef>
#include <cstdint>
#include <string>

namespace usn_throughput {

constexpr double kMinElapsedSec = 1e-6;
constexpr std::int64_t kSecondsPerHour = 3600;
constexpr std::int64_t kSecondsPerMinute = 60;
constexpr std::int64_t kSingleDigitThreshold = 10;

struct ThroughputSample {
  size_t buffers_processed = 0;
  size_t records_processed = 0;
  size_t queue_depth = 0;
};

struct ThroughputRates {
  double buffers_per_sec = 0.0;
  double records_per_sec = 0.0;
  double drain_eta_sec = 0.0;
  bool has_eta = false;
};

[[nodiscard]] inline ThroughputRates ComputeThroughput(const ThroughputSample& prev,
                                                       const ThroughputSample& cur,
                                                       double elapsed_sec) {
  ThroughputRates rates;
  if (elapsed_sec <= kMinElapsedSec) {
    return rates;
  }
  // Counters reset via ResetMetrics(): baseline moved, no valid delta.
  if (cur.buffers_processed < prev.buffers_processed ||
      cur.records_processed < prev.records_processed) {
    return rates;
  }
  const auto delta_buffers =
      static_cast<double>(cur.buffers_processed - prev.buffers_processed);
  const auto delta_records =
      static_cast<double>(cur.records_processed - prev.records_processed);
  rates.buffers_per_sec = delta_buffers / elapsed_sec;
  rates.records_per_sec = delta_records / elapsed_sec;
  if (rates.buffers_per_sec > 0.0 && cur.queue_depth > 0) {
    rates.drain_eta_sec =
        static_cast<double>(cur.queue_depth) / rates.buffers_per_sec;
    rates.has_eta = true;
  }
  return rates;
}

[[nodiscard]] inline std::string FormatDrainEta(double total_seconds) {
  if (total_seconds < 0.0) {
    return {"0s"};
  }
  const auto total = static_cast<std::int64_t>(total_seconds);
  const auto hours = total / kSecondsPerHour;
  const auto minutes = (total % kSecondsPerHour) / kSecondsPerMinute;
  const auto seconds = total % kSecondsPerMinute;
  if (hours > 0) {
    return std::to_string(hours) + "h " + std::to_string(minutes) + "m";
  }
  if (minutes > 0) {
    std::string out = std::to_string(minutes) + "m ";
    if (seconds < kSingleDigitThreshold) {
      out += '0';
    }
    out += std::to_string(seconds) + "s";
    return out;
  }
  return std::to_string(seconds) + "s";
}

}  // namespace usn_throughput
