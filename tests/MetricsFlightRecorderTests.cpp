#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

#include "diagnostics/MetricsDiagnosticAnalyzer.h"
#include "diagnostics/MetricsFlightRecorder.h"
#include "diagnostics/MetricsRingBuffer.h"
#include "diagnostics/MetricsSample.h"

namespace {

namespace fs = std::filesystem;
using nlohmann::json;

struct ScopedTempDiagnosticsDir {
  fs::path path;

  explicit ScopedTempDiagnosticsDir(std::string_view suffix) {
    path = fs::temp_directory_path() / ("findhelper_flight_rec_test_" + std::string(suffix));
    std::error_code ec;
    fs::remove_all(path, ec);
    fs::create_directories(path, ec);
  }

  ~ScopedTempDiagnosticsDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

diagnostics::MetricsSample MakeHealthySample(uint64_t steady_ms, uint64_t buffers_proc = 100) {
  diagnostics::MetricsSample s{};
  s.timestamp_unix_ms = 1760000000000ULL + steady_ms;
  s.steady_elapsed_ms = steady_ms;
  s.ui.last_frame_delta_ms = 16;
  s.ui.unresponsive_ms = 0;
  s.ui.fps = 60.0F;
  s.ui.is_minimized = false;
  s.usn_buffers.queue_depth = 0;
  s.usn_buffers.buffers_read = buffers_proc;
  s.usn_buffers.buffers_processed = buffers_proc;
  s.usn_buffers.records_processed = buffers_proc * 40U;
  s.usn_buffers.total_process_ms = buffers_proc;
  s.usn_buffers.max_buffer_process_ms = 5;
  return s;
}

bool HasFindingCode(const std::vector<diagnostics::DiagnosticFinding>& findings,
                    std::string_view code) {
  return std::any_of(findings.begin(), findings.end(),  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
                     [code](const diagnostics::DiagnosticFinding& f) { return f.code == code; });
}

}  // namespace

TEST_CASE("MetricsRingBuffer preserves chronological order before and after wrap-around") {
  diagnostics::MetricsRingBuffer<4> ring;
  CHECK(ring.Empty());
  CHECK(ring.Size() == 0);

  // Partial fill (3 of 4)
  ring.Push(MakeHealthySample(100));
  ring.Push(MakeHealthySample(200));
  ring.Push(MakeHealthySample(300));
  CHECK(ring.Size() == 3);
  CHECK(ring.Oldest().steady_elapsed_ms == 100);
  CHECK(ring.Newest().steady_elapsed_ms == 300);

  std::vector<uint64_t> partial_order;
  ring.ForEachChronological([&partial_order](const diagnostics::MetricsSample& s) {
    partial_order.push_back(s.steady_elapsed_ms);
  });
  CHECK(partial_order == std::vector<uint64_t>{100, 200, 300});

  // Wrap around (push 400, 500, 600 -> capacity 4 holds 300, 400, 500, 600)
  ring.Push(MakeHealthySample(400));
  ring.Push(MakeHealthySample(500));
  ring.Push(MakeHealthySample(600));
  CHECK(ring.Size() == 4);
  CHECK(ring.Oldest().steady_elapsed_ms == 300);
  CHECK(ring.Newest().steady_elapsed_ms == 600);

  std::vector<uint64_t> wrapped_order;
  ring.ForEachChronological([&wrapped_order](const diagnostics::MetricsSample& s) {
    wrapped_order.push_back(s.steady_elapsed_ms);
  });
  CHECK(wrapped_order == std::vector<uint64_t>{300, 400, 500, 600});
}

TEST_CASE("MetricsDiagnosticAnalyzer trigger evaluation respects guards") {
  const auto prev = MakeHealthySample(1000);

  SUBCASE("Healthy sample triggers nothing") {
    const auto cur = MakeHealthySample(1250);
    CHECK(diagnostics::EvaluateTriggerReasons(prev, cur) == 0U);
  }

  SUBCASE("USN queue backlog triggers when not building index, suppressed during build") {
    auto cur = MakeHealthySample(1250);
    cur.usn_buffers.queue_depth = 30;
    CHECK(diagnostics::HasTriggerReason(diagnostics::EvaluateTriggerReasons(prev, cur),
                                        diagnostics::TriggerReason::UsnQueueDepth));

    cur.index_search.is_index_building = true;
    CHECK_FALSE(diagnostics::HasTriggerReason(diagnostics::EvaluateTriggerReasons(prev, cur),
                                              diagnostics::TriggerReason::UsnQueueDepth));
  }

  SUBCASE("Single buffer process spike >= 100ms triggers") {
    auto cur = MakeHealthySample(1250);
    cur.usn_buffers.max_buffer_process_ms = 140;
    CHECK(diagnostics::HasTriggerReason(diagnostics::EvaluateTriggerReasons(prev, cur),
                                        diagnostics::TriggerReason::UsnBufferLatencySpike));
  }

  SUBCASE("UI stall triggers when visible, suppressed when minimized") {
    auto cur = MakeHealthySample(1250);
    cur.ui.unresponsive_ms = 320;
    CHECK(diagnostics::HasTriggerReason(diagnostics::EvaluateTriggerReasons(prev, cur),
                                        diagnostics::TriggerReason::UiFrameStall));

    cur.ui.is_minimized = true;
    CHECK_FALSE(diagnostics::HasTriggerReason(diagnostics::EvaluateTriggerReasons(prev, cur),
                                              diagnostics::TriggerReason::UiFrameStall));
  }

  SUBCASE("UI stall suppressed during event polling (glfwPollEvents / modal loop)") {
    auto cur = MakeHealthySample(1250);
    cur.ui.unresponsive_ms = 3200;
    cur.ui.is_event_polling = true;
    CHECK_FALSE(diagnostics::HasTriggerReason(diagnostics::EvaluateTriggerReasons(prev, cur),
                                              diagnostics::TriggerReason::UiFrameStall));
  }

  SUBCASE("UI stall suppressed during OS window move/resize (WM_ENTERSIZEMOVE)") {
    auto cur = MakeHealthySample(1250);
    cur.ui.unresponsive_ms = 3200;
    cur.ui.is_window_moving_or_resizing = true;
    CHECK_FALSE(diagnostics::HasTriggerReason(diagnostics::EvaluateTriggerReasons(prev, cur),
                                              diagnostics::TriggerReason::UiFrameStall));
  }

  SUBCASE("Buffer drop triggers immediately") {
    auto cur = MakeHealthySample(1250);
    cur.usn_buffers.buffers_dropped = 1;
    CHECK(diagnostics::HasTriggerReason(diagnostics::EvaluateTriggerReasons(prev, cur),
                                        diagnostics::TriggerReason::UsnBufferDropped));
  }
}

TEST_CASE("MetricsDiagnosticAnalyzer reproduces Windows 5051-queue screenshot diagnosis") {
  // Exact numbers from the user's two Windows screenshots (during vs after the 5,051-buffer storm):
  // Screenshot 1 (baseline): Buffers Read=6427, Processed=1376, Records=109352,
  //   Total Process=3349ms, Max Buffer=72ms, Lock Wait Total=20ms
  // Screenshot 2 (after):    Buffers Read=6469, Processed=2865, Records=164352,
  //   Total Process=10147ms, Max Buffer=238ms, Lock Wait Total=31ms
  diagnostics::MetricsSample oldest = MakeHealthySample(10000);
  oldest.usn_buffers.buffers_read = 6427;
  oldest.usn_buffers.buffers_processed = 1376;
  oldest.usn_buffers.records_processed = 109352;
  oldest.usn_buffers.total_process_ms = 3349;
  oldest.usn_buffers.max_buffer_process_ms = 72;
  oldest.usn_buffers.lock_wait_total_ms = 20;
  oldest.usn_buffers.lock_wait_max_ms = 8;

  diagnostics::MetricsSample newest = MakeHealthySample(35000);
  newest.usn_buffers.queue_depth = 0;
  newest.usn_buffers.max_queue_depth = 5051;
  newest.usn_buffers.buffers_read = 6469;
  newest.usn_buffers.buffers_processed = 2865;
  newest.usn_buffers.records_processed = 164352;
  newest.usn_buffers.total_process_ms = 10147;
  newest.usn_buffers.max_buffer_process_ms = 238;
  newest.usn_buffers.lock_wait_total_ms = 31;
  newest.usn_buffers.lock_wait_max_ms = 8;

  diagnostics::IncidentPeaks peaks{};
  diagnostics::MetricsSample peak_sample = oldest;
  peak_sample.usn_buffers.queue_depth = 5051;
  peak_sample.usn_buffers.max_buffer_process_ms = 238;
  diagnostics::UpdateIncidentPeaks(peaks, oldest, peak_sample);
  diagnostics::UpdateIncidentPeaks(peaks, oldest, newest);

  const auto delta = diagnostics::ComputeWindowDelta(oldest, newest, peaks);
  CHECK(delta.usn.buffers_processed == 1489);
  CHECK(delta.usn.records_processed == 55000);
  CHECK(delta.usn.process_time_ms == 6798);
  CHECK(delta.usn.lock_wait_ms == 11);
  CHECK(delta.usn.max_buffer_process_jump_ms == 166);

  const auto findings = diagnostics::AnalyzeWindow(oldest, newest, peaks);
  CHECK(HasFindingCode(findings, "DIAG_USN_CPU_APPLY_SPIKE"));
  CHECK(HasFindingCode(findings, "DIAG_USN_INGEST_STORM"));
  CHECK_FALSE(HasFindingCode(findings, "DIAG_USN_LOCK_CONTENTION"));
}

TEST_CASE("MetricsDiagnosticAnalyzer covers remaining rules") {
  SUBCASE("DIAG_INDEX_REBUILD_ON_UI and DIAG_UI_FRAME_STALL") {
    auto oldest = MakeHealthySample(1000);
    auto newest = MakeHealthySample(3000);
    newest.index_search.index_rebuild_count = 1;
    newest.ui.last_frame_delta_ms = 1200;

    diagnostics::IncidentPeaks peaks{};
    diagnostics::UpdateIncidentPeaks(peaks, oldest, newest);
    const auto findings = diagnostics::AnalyzeWindow(oldest, newest, peaks);
    CHECK(HasFindingCode(findings, "DIAG_UI_FRAME_STALL"));
    CHECK(HasFindingCode(findings, "DIAG_INDEX_REBUILD_ON_UI"));
  }

  SUBCASE("DIAG_USN_LOCK_CONTENTION when lock wait share >= 35%") {
    auto oldest = MakeHealthySample(1000);
    auto newest = MakeHealthySample(5000);
    newest.usn_buffers.buffers_processed = oldest.usn_buffers.buffers_processed + 20;
    newest.usn_buffers.total_process_ms = oldest.usn_buffers.total_process_ms + 500;
    newest.usn_buffers.lock_wait_total_ms = oldest.usn_buffers.lock_wait_total_ms + 250;
    newest.usn_buffers.lock_wait_max_ms = 90;

    diagnostics::IncidentPeaks peaks{};
    peaks.max_queue_depth = 40;
    const auto findings = diagnostics::AnalyzeWindow(oldest, newest, peaks);
    CHECK(HasFindingCode(findings, "DIAG_USN_LOCK_CONTENTION"));
  }

  SUBCASE("DIAG_USN_QUEUE_STALLED when queue >= 10, 0 processed, elapsed >= 1000ms") {
    auto oldest = MakeHealthySample(1000);
    auto newest = MakeHealthySample(3000);
    newest.usn_buffers.queue_depth = 80;
    newest.usn_buffers.buffers_processed = oldest.usn_buffers.buffers_processed;
    newest.index_search.index_write_lock_held = true;

    diagnostics::IncidentPeaks peaks{};
    diagnostics::UpdateIncidentPeaks(peaks, oldest, newest);
    const auto findings = diagnostics::AnalyzeWindow(oldest, newest, peaks);
    CHECK(HasFindingCode(findings, "DIAG_USN_QUEUE_STALLED"));
  }

  SUBCASE("DIAG_USN_INGEST_STORM when queue bursts with fast apply rate") {
    auto oldest = MakeHealthySample(1000);
    auto newest = MakeHealthySample(3000);
    newest.usn_buffers.buffers_read = oldest.usn_buffers.buffers_read + 500;
    newest.usn_buffers.buffers_processed = oldest.usn_buffers.buffers_processed + 200;
    newest.usn_buffers.total_process_ms = oldest.usn_buffers.total_process_ms + 200;  // 1.0 ms/buf

    diagnostics::IncidentPeaks peaks{};
    peaks.max_queue_depth = 300;
    peaks.max_live_buffers_per_sec = 400.0;
    const auto findings = diagnostics::AnalyzeWindow(oldest, newest, peaks);
    CHECK(HasFindingCode(findings, "DIAG_USN_INGEST_STORM"));
  }

  SUBCASE("DIAG_PLACEHOLDER_ACCUMULATION and DIAG_SEARCH_POSTPROCESS_HEAVY") {
    auto oldest = MakeHealthySample(1000);
    auto newest = MakeHealthySample(3000);
    newest.usn_ops.awaiting_count = 6000;
    newest.usn_ops.awaiting_oldest_age_ms = 120000;
    newest.index_search.search_last_search_ms = 20;
    newest.index_search.search_last_postprocess_ms = 280;

    diagnostics::IncidentPeaks peaks{};
    diagnostics::UpdateIncidentPeaks(peaks, oldest, newest);
    const auto findings = diagnostics::AnalyzeWindow(oldest, newest, peaks);
    CHECK(HasFindingCode(findings, "DIAG_PLACEHOLDER_ACCUMULATION"));
    CHECK(HasFindingCode(findings, "DIAG_SEARCH_POSTPROCESS_HEAVY"));
  }
}

TEST_CASE("MetricsFlightRecorder state transitions, dual JSONL/CSV output, and pruning") {
  ScopedTempDiagnosticsDir temp_dir("state_and_pruning");
  diagnostics::MetricsFlightRecorder recorder(temp_dir.path.string());

  // 1. Feed 5 pre-roll samples while Armed
  uint64_t steady_ms = 1000;
  for (uint64_t i = 0; i < 5; ++i) {
    recorder.RecordSample(MakeHealthySample(steady_ms, 100U + i));
    steady_ms += 250;
  }
  auto snap = recorder.GetUiSnapshot();
  CHECK(snap.state == diagnostics::RecorderState::Armed);
  CHECK(snap.pre_roll_samples == 5);
  CHECK_FALSE(snap.has_last_incident);

  // 2. Trigger incident via USN queue spike
  auto spike = MakeHealthySample(steady_ms, 110);
  spike.usn_buffers.queue_depth = 150;
  spike.usn_buffers.max_buffer_process_ms = 180;
  recorder.RecordSample(spike);
  steady_ms += 250;

  snap = recorder.GetUiSnapshot();
  CHECK(snap.state == diagnostics::RecorderState::Recording);
  CHECK(snap.pre_roll_samples == 0);  // Pre-roll flushed to incident file

  // 3. Feed 1 healthy sample -> enters Cooldown
  recorder.RecordSample(MakeHealthySample(steady_ms, 120));
  snap = recorder.GetUiSnapshot();
  CHECK(snap.state == diagnostics::RecorderState::Cooldown);

  // 4. Re-trigger during Cooldown -> returns to Recording on same incident
  steady_ms += 1000;
  auto second_spike = MakeHealthySample(steady_ms, 125);
  second_spike.ui.unresponsive_ms = 400;
  recorder.RecordSample(second_spike);
  snap = recorder.GetUiSnapshot();
  CHECK(snap.state == diagnostics::RecorderState::Recording);

  // 5. Recover and complete 10s cooldown -> returns to Armed and finalizes incident
  steady_ms += 250;
  recorder.RecordSample(MakeHealthySample(steady_ms, 130));
  CHECK(recorder.GetUiSnapshot().state == diagnostics::RecorderState::Cooldown);

  steady_ms += diagnostics::flight_recorder_constants::kCooldownDurationMs;
  recorder.RecordSample(MakeHealthySample(steady_ms, 140));
  snap = recorder.GetUiSnapshot();
  CHECK(snap.state == diagnostics::RecorderState::Armed);
  CHECK(snap.incidents_recorded == 1);
  REQUIRE(snap.has_last_incident);
  CHECK(fs::exists(snap.last_incident.jsonl_file_path));
  CHECK(fs::exists(snap.last_incident.csv_file_path));

  // Verify JSONL contains header, pre_roll samples, active samples, cooldown samples, and incident_summary
  std::ifstream jsonl_in(snap.last_incident.jsonl_file_path);
  REQUIRE(jsonl_in.is_open());
  std::string line;
  size_t start_count = 0;
  size_t preroll_count = 0;
  size_t active_count = 0;
  size_t cooldown_count = 0;
  size_t end_count = 0;
  while (std::getline(jsonl_in, line)) {
    if (line.empty()) {
      continue;
    }
    const auto parsed = json::parse(line);
    if (const std::string type = parsed.at("type").get<std::string>(); type == "header") {
      ++start_count;
    } else if (type == "sample") {
      if (const std::string phase = parsed.at("phase").get<std::string>(); phase == "pre_roll") {
        ++preroll_count;
      } else if (phase == "active") {
        ++active_count;
      } else if (phase == "cooldown") {
        ++cooldown_count;
      }
    } else if (type == "incident_summary") {
      ++end_count;
    }
  }
  CHECK(start_count == 1);
  CHECK(preroll_count == 5);
  CHECK(active_count == 2);
  CHECK(cooldown_count == 3);
  CHECK(end_count == 1);
  jsonl_in.close();

  // Verify companion CSV header and row count (1 header + 5 pre_roll + 2 active + 3 cooldown = 11 lines)
  std::ifstream csv_in(snap.last_incident.csv_file_path);
  REQUIRE(csv_in.is_open());
  size_t csv_lines = 0;
  while (std::getline(csv_in, line)) {
    if (!line.empty()) {
      ++csv_lines;
    }
  }
  CHECK(csv_lines == 11);
  csv_in.close();

  // 6. Test manual dump and retention pruning (create 11 more dumps -> total 12 incidents -> pruned to 10 pairs)
  for (uint64_t i = 0; i < 11; ++i) {
    steady_ms += 250;
    recorder.RecordSample(MakeHealthySample(steady_ms, 200U + i));
    const std::string dumped_path = recorder.TriggerManualDump();
    CHECK_FALSE(dumped_path.empty());
  }

  size_t jsonl_files = 0;
  size_t csv_files = 0;
  for (const auto& entry : fs::directory_iterator(temp_dir.path)) {
    if (entry.path().extension() == ".jsonl") {
      ++jsonl_files;
    } else if (entry.path().extension() == ".csv") {
      ++csv_files;
    }
  }
  CHECK(jsonl_files == diagnostics::flight_recorder_constants::kMaxRetainedIncidents);
  CHECK(csv_files == diagnostics::flight_recorder_constants::kMaxRetainedIncidents);
}

TEST_CASE("MetricsFlightRecorder ScopedEventPoll brackets polling and adjusts frame delta") {
  ScopedTempDiagnosticsDir temp_dir("scoped_event_poll");
  diagnostics::MetricsFlightRecorder recorder(temp_dir.path.string());

  recorder.OnUiFrameBegin(1000, 60.0F);
  {
    const diagnostics::MetricsFlightRecorder::ScopedEventPoll poll_guard(&recorder);
    recorder.SetUiWindowMovingOrResizing(true);
  }
  recorder.SetUiWindowMovingOrResizing(false);
  recorder.OnUiFrameEnd(1200);

  // When visible and healthy, trigger evaluation produces 0
  const auto sample = MakeHealthySample(1250);
  CHECK(diagnostics::EvaluateTriggerReasons(sample, sample) == 0U);
}
