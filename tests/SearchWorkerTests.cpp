#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "index/IIndexSearch.h"
#include "index/recycle/RecycleBinMetadataStore.h"
#include "path/PathUtils.h"
#include "search/SearchTypes.h"
#include "search/SearchWorker.h"

namespace {

// IIndexSearch stub whose scan blocks until the worker's cancel flag is set.
// A watchdog records a timeout instead of hanging the suite, so the test
// fails deterministically (red) when destruction does not cancel.
class CancelGatedIndexSearch : public IIndexSearch {
 public:
  std::vector<std::future<SearchResultBatch>> SearchAsyncWithData(  // NOLINT(readability-function-size) - override must mirror the 10-param IIndexSearch interface; body delegates to RunGate
      std::string_view /*query*/, int /*thread_count*/, SearchStats* /*stats*/,
      std::string_view /*path_query*/, const std::vector<std::string>* /*extensions*/,
      ItemTypeFilter /*item_type_filter*/, bool /*case_sensitive*/,
      std::vector<ThreadTiming>* /*thread_timings*/,
      const std::atomic<bool>* cancel_flag,
      const AppSettings* /*optional_settings*/) override {
    std::vector<std::future<SearchResultBatch>> futures;
    // Explicit thread + promise (no std::async, Sonar cpp:S8460). The shared
    // promise outlives the detached worker: set exactly once on every path.
    auto gate = std::make_shared<std::promise<SearchResultBatch>>();
    futures.push_back(gate->get_future());
    // Joined (not detached) in the destructor: the gate touches this mock,
    // so the thread must not outlive it. The watchdog bounds the join.
    gate_thread_ = std::thread([this, cancel_flag, gate] { RunGate(cancel_flag, gate); });
    return futures;
  }

  ~CancelGatedIndexSearch() override {
    if (gate_thread_.joinable()) {
      gate_thread_.join();
    }
  }

  void RunGate(const std::atomic<bool>* cancel_flag,
               const std::shared_ptr<std::promise<SearchResultBatch>>& gate) {
    if (cancel_flag == nullptr) {
      gate->set_value(SearchResultBatch{});
      return;
    }
    const auto deadline = std::chrono::steady_clock::now() + kWatchdog;
    while (!cancel_flag->load()) {
      if (std::chrono::steady_clock::now() >= deadline) {
        timed_out_.store(true);
        gate->set_value(SearchResultBatch{});
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    cancel_observed_.store(true);
    gate->set_value(SearchResultBatch{});
  }

  static constexpr std::chrono::seconds kWatchdog{10};
  std::atomic<bool> cancel_observed_{false};  // NOSONAR(cpp:S6012) - CTAD is not allowed on non-static members; explicit argument required
  std::atomic<bool> timed_out_{false};  // NOSONAR(cpp:S6012) - CTAD is not allowed on non-static members; explicit argument required
  // Gate worker: joined in the destructor so it never outlives the mock.
  std::thread gate_thread_;
};

}  // namespace

TEST_CASE("SearchWorker destructor cancels a mid-scan search") {
  // Without the dtor cancel, join() would wait out the blocked scan until the
  // stub watchdog fires; with it, teardown drains in milliseconds.
  CancelGatedIndexSearch index;
  auto worker = std::make_unique<SearchWorker>(index);

  SearchParams params;
  params.filenameInput = "*.txt";
  worker->StartSearch(params);

  // Liveness guard: bound the wait so a stuck worker fails instead of hanging.
  const auto wait_start = std::chrono::steady_clock::now();
  while (!worker->IsSearching()) {
    REQUIRE(std::chrono::steady_clock::now() - wait_start < std::chrono::seconds(5));
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // Destroy mid-scan: the destructor must cancel so join() drains instead of
  // waiting out the scan.
  worker.reset();
  CHECK(index.cancel_observed_.load());
  CHECK_FALSE(index.timed_out_.load());
}

// ============================================================================
// Secondary (Approach-B) Recycle Bin snapshot pass: the worker scans the
// published snapshot's ORIGINAL names/paths after the primary futures drain and
// appends ONE ids-only batch. These cases drive the public search path
// (StartSearch -> poll HasNewResults -> GetResultsData), the same seam the
// destructor case above uses; RunFilteredSearchPath has no test-only accessor.
// ============================================================================

namespace {

constexpr uint64_t kPrimaryHitId = 77;
constexpr std::string_view kPrimaryHitPath = "C:\\data\\primary.txt";

// IIndexSearch stub that answers every scan with one already-fulfilled future
// holding a single primary hit. Records the item-type filter it was asked for, so
// a case can assert the secondary pass and the primary scan agree on it.
class ScriptedIndexSearch : public IIndexSearch {
 public:
  std::vector<std::future<SearchResultBatch>> SearchAsyncWithData(  // NOLINT(readability-function-size) - override must mirror the 10-param IIndexSearch interface
      std::string_view /*query*/, int /*thread_count*/, SearchStats* /*stats*/,
      std::string_view /*path_query*/, const std::vector<std::string>* /*extensions*/,
      ItemTypeFilter item_type_filter, bool /*case_sensitive*/,
      std::vector<ThreadTiming>* /*thread_timings*/,
      const std::atomic<bool>* /*cancel_flag*/,
      const AppSettings* /*optional_settings*/) override {
    item_type_filter_seen = item_type_filter;
    auto promise = std::make_shared<std::promise<SearchResultBatch>>();
    promise->set_value(MakePrimaryBatch());
    std::vector<std::future<SearchResultBatch>> futures;
    futures.push_back(promise->get_future());
    return futures;
  }

  [[nodiscard]] static SearchResultBatch MakePrimaryBatch() {
    SearchResultBatch batch;
    batch.AppendHit(kPrimaryHitId, kPrimaryHitPath.data(), kPrimaryHitPath.size(), false, 0, 0);
    return batch;
  }

  ItemTypeFilter item_type_filter_seen = ItemTypeFilter::All;
};

struct TestRecycleEntry {
  uint64_t file_id;
  std::string original_path;
  bool is_directory{false};
};

[[nodiscard]] std::shared_ptr<const recycle_bin::RecycleMetadataSnapshot> MakeSnapshot(
    std::initializer_list<TestRecycleEntry> entries) {
  auto snapshot = std::make_shared<recycle_bin::RecycleMetadataSnapshot>();
  snapshot->generation = 1;
  for (const auto& entry : entries) {
    recycle_bin::RecycleFileMetadata meta;
    meta.original_path = entry.original_path;
    meta.original_name = std::string(path_utils::GetFilename(entry.original_path));
    meta.is_directory = entry.is_directory;
    snapshot->by_file_id.emplace(entry.file_id, std::move(meta));
  }
  return snapshot;
}

// Runs one search through the public API and returns the batches the worker
// published. Bounded wait: a stuck worker fails the case instead of hanging it.
[[nodiscard]] std::vector<SearchResultBatch> RunSearchAndCollectBatches(SearchWorker& worker,
                                                                       const SearchParams& params) {
  worker.StartSearch(params);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!worker.HasNewResults()) {
    REQUIRE(std::chrono::steady_clock::now() < deadline);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return worker.GetResultsData();
}

}  // namespace

TEST_CASE("SearchWorker secondary pass - no snapshot publishes the primary batch only") {
  ScriptedIndexSearch index;
  SearchWorker worker(index);

  SearchParams params;
  params.filenameInput = "primary";
  const std::vector<SearchResultBatch> batches = RunSearchAndCollectBatches(worker, params);

  // Batch list unchanged: no recycle batch, and no empty placeholder either.
  REQUIRE(batches.size() == 1);
  CHECK(batches[0].Size() == 1);
  CHECK(batches[0].recycle_hit_file_ids.empty());

  const SearchMetrics::Snapshot metrics = worker.GetMetricsSnapshot();
  CHECK(metrics.last_results_count_ == 1);
  CHECK(metrics.total_results_found_ == 1);
}

TEST_CASE("SearchWorker secondary pass - snapshot hit appends one ids-only batch") {
  ScriptedIndexSearch index;
  SearchWorker worker(index);
  // The query matches the recycled item's ORIGINAL name; the primary scan cannot
  // see that name (it only ever sees the physical $R name).
  worker.SetRecycleSnapshot(MakeSnapshot({{100, "C:\\Users\\bob\\primary.txt", false}}));

  SearchParams params;
  params.filenameInput = "primary";
  const std::vector<SearchResultBatch> batches = RunSearchAndCollectBatches(worker, params);

  REQUIRE(batches.size() == 2);
  // Primary batch: untouched by the secondary pass.
  CHECK(batches[0].Size() == 1);
  CHECK(batches[0].recycle_hit_file_ids.empty());
  // Secondary batch: ids only. No arena bytes and no hits, so Size()/Empty() (the
  // hit-based contract) stay as they were, and the batch carries the snapshot id.
  CHECK(batches[1].Size() == 0);
  CHECK(batches[1].Empty());
  CHECK(batches[1].arena.empty());
  REQUIRE(batches[1].recycle_hit_file_ids.size() == 1);
  CHECK(batches[1].recycle_hit_file_ids[0] == 100);

  // Hit metrics count primary hits only: the ids-only batch adds none.
  const SearchMetrics::Snapshot metrics = worker.GetMetricsSnapshot();
  CHECK(metrics.last_results_count_ == 1);
  CHECK(metrics.total_results_found_ == 1);
}

TEST_CASE("SearchWorker secondary pass - FilesOnly keeps the file id") {
  ScriptedIndexSearch index;
  SearchWorker worker(index);
  worker.SetRecycleSnapshot(MakeSnapshot({
      {100, "C:\\Users\\bob\\primary.txt", false},
      {600, "C:\\Users\\bob\\primarydir", true},
  }));

  SearchParams params;
  params.filenameInput = "primary";
  params.itemTypeFilter = ItemTypeFilter::FilesOnly;
  const std::vector<SearchResultBatch> batches = RunSearchAndCollectBatches(worker, params);

  REQUIRE(batches.size() == 2);
  REQUIRE(batches[1].recycle_hit_file_ids.size() == 1);
  CHECK(batches[1].recycle_hit_file_ids[0] == 100);
  // Parity: the primary scan ran under the same effective filter.
  CHECK(index.item_type_filter_seen == ItemTypeFilter::FilesOnly);
}

TEST_CASE("SearchWorker secondary pass - FoldersOnly keeps the folder id") {
  ScriptedIndexSearch index;
  SearchWorker worker(index);
  worker.SetRecycleSnapshot(MakeSnapshot({
      {100, "C:\\Users\\bob\\primary.txt", false},
      {600, "C:\\Users\\bob\\primarydir", true},
  }));

  SearchParams params;
  params.filenameInput = "primary";
  params.itemTypeFilter = ItemTypeFilter::FoldersOnly;
  const std::vector<SearchResultBatch> batches = RunSearchAndCollectBatches(worker, params);

  REQUIRE(batches.size() == 2);
  REQUIRE(batches[1].recycle_hit_file_ids.size() == 1);
  CHECK(batches[1].recycle_hit_file_ids[0] == 600);
  CHECK(index.item_type_filter_seen == ItemTypeFilter::FoldersOnly);
}
