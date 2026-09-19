#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "index/IIndexSearch.h"
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
      bool /*folders_only*/, bool /*case_sensitive*/,
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
