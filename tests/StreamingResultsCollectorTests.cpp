#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "search/StreamingResultsCollector.h"
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

TEST_CASE("StreamingResultsCollector - Basic Batching") {
    StreamingResultsCollector collector(10, 100);  // batch_size=10, interval=100ms

    std::vector<std::string> paths;
    paths.reserve(11);
    for (int i = 0; i < 11; ++i) {
        paths.push_back("path" + std::to_string(i));
    }

    for (int i = 0; i < 9; ++i) {
        SearchResultData data;
        data.id = static_cast<uint64_t>(i);
        data.fullPath = paths[static_cast<size_t>(i)];
        data.isDirectory = false;
        collector.AddResult(data);
    }

    CHECK_FALSE(collector.HasNewBatch());
    CHECK(collector.GetAllPendingBatches().empty());

    SearchResultData data10;
    data10.id = 10;
    data10.fullPath = paths[10];
    data10.isDirectory = false;
    collector.AddResult(data10);

    CHECK(collector.HasNewBatch());
    auto batches = collector.GetAllPendingBatches();
    CHECK(batches.size() == 10);
    CHECK_FALSE(collector.HasNewBatch());
}

TEST_CASE("StreamingResultsCollector - Time-based Batching") {
    StreamingResultsCollector collector(100, 50);  // batch_size=100, interval=50ms

    SearchResultData data;
    data.id = 0;
    data.fullPath = "path";
    data.isDirectory = false;
    collector.AddResult(data);

    CHECK_FALSE(collector.HasNewBatch());
    std::this_thread::sleep_for(std::chrono::milliseconds(60));

    SearchResultData data2;
    data2.id = 1;
    data2.fullPath = "path2";
    data2.isDirectory = false;
    collector.AddResult(data2);

    CHECK(collector.HasNewBatch());
    auto batches = collector.GetAllPendingBatches();
    CHECK(batches.size() == 2);
}

TEST_CASE("StreamingResultsCollector - MarkSearchComplete") {
    StreamingResultsCollector collector(100, 1000);

    SearchResultData data;
    data.id = 0;
    data.fullPath = "path";
    data.isDirectory = false;
    collector.AddResult(data);

    CHECK_FALSE(collector.HasNewBatch());
    CHECK_FALSE(collector.IsSearchComplete());

    collector.MarkSearchComplete();

    CHECK(collector.IsSearchComplete());
    CHECK(collector.HasNewBatch());
    auto batches = collector.GetAllPendingBatches();
    CHECK(batches.size() == 1);
}

TEST_CASE("StreamingResultsCollector - SetError") {
    StreamingResultsCollector collector;

    collector.SetError("Test Error");

    CHECK(collector.HasError());
    CHECK(collector.GetError() == "Test Error");
    CHECK(collector.IsSearchComplete());
}

TEST_CASE("StreamingResultsCollector - GetPendingBatchesUpTo limits per-frame work") {
    StreamingResultsCollector collector(10, 1000);  // batch_size=10, long interval

    std::vector<std::string> paths;
    paths.reserve(30);
    for (int i = 0; i < 30; ++i) {
        paths.push_back("path" + std::to_string(i));
    }

    for (int i = 0; i < 30; ++i) {
        SearchResultData data;
        data.id = static_cast<uint64_t>(i);
        data.fullPath = paths[static_cast<size_t>(i)];
        data.isDirectory = false;
        collector.AddResult(data);
    }

    CHECK(collector.HasNewBatch());

    // First call: get up to 10; 20 remain in pending
    auto batch1 = collector.GetPendingBatchesUpTo(10);
    CHECK(batch1.size() == 10);
    CHECK(collector.HasNewBatch());

    // Second call: get up to 10; 10 remain
    auto batch2 = collector.GetPendingBatchesUpTo(10);
    CHECK(batch2.size() == 10);
    CHECK(collector.HasNewBatch());

    // Third call: get remaining 10; all drained from pending
    auto batch3 = collector.GetPendingBatchesUpTo(10);
    CHECK(batch3.size() == 10);
    CHECK_FALSE(collector.HasNewBatch());

    // When pending <= max, behaves like GetAllPendingBatches (returns all, clears)
    collector.AddResult(SearchResultData{30, "path30", false});
    collector.AddResult(SearchResultData{31, "path31", false});
    collector.MarkSearchComplete();  // flushes remaining 2 from current_batch
    auto batch4 = collector.GetPendingBatchesUpTo(10);
    CHECK(batch4.size() == 2);
    CHECK_FALSE(collector.HasNewBatch());
}

TEST_CASE("StreamingResultsCollector - Thread Safety") {
    // batch_size > total results and a long interval so nothing flushes to pending until
    // MarkSearchComplete (avoids timing-dependent partial flushes under load on slow CI VMs).
    constexpr int k_num_threads = 4;
    constexpr int k_results_per_thread = 100;
    constexpr uint32_t k_long_interval_ms = 24U * 60U * 60U * 1000U;  // 24h — time flush disabled for test duration
    StreamingResultsCollector collector(500, k_long_interval_ms);

    // Pre-allocate path strings so string_view in SearchResultData stays valid
    std::vector<std::vector<std::string>> thread_paths(static_cast<size_t>(k_num_threads));
    for (int t = 0; t < k_num_threads; ++t) {
        thread_paths[static_cast<size_t>(t)].reserve(static_cast<size_t>(k_results_per_thread));
        for (int i = 0; i < k_results_per_thread; ++i) {
            thread_paths[static_cast<size_t>(t)].push_back(
                "thread" + std::to_string(t) + "_result" + std::to_string(i));
        }
    }

    std::vector<std::thread> threads;
    threads.reserve(static_cast<size_t>(k_num_threads));
    for (int t = 0; t < k_num_threads; ++t) {
        threads.emplace_back([&collector, &thread_paths, t]() {
            const auto per_thread_count =
                static_cast<int>(thread_paths[static_cast<size_t>(t)].size());
            for (int i = 0; i < per_thread_count; ++i) {
                SearchResultData data;
                data.id = static_cast<uint64_t>(t) * 100ULL + static_cast<uint64_t>(i);
                data.fullPath = thread_paths[static_cast<size_t>(t)][static_cast<size_t>(i)];
                data.isDirectory = false;
                collector.AddResult(data);
            }
        });
    }

    // Join all producers before flushing — ensures all AddResult calls are complete before
    // MarkSearchComplete, so every result is either in current_batch_ or already in pending_batches_.
    for (auto& t : threads) {
        t.join();
    }

    // MarkSearchComplete flushes current_batch_ → pending_batches_, so after this call every
    // result is in pending_batches_.  One GetAllPendingBatches() drains the whole thing — no
    // loop needed and no possibility of hanging on Windows CI.
    collector.MarkSearchComplete();
    const std::vector<SearchResultData> all_results = collector.GetAllPendingBatches();

    CHECK(all_results.size() == static_cast<size_t>(k_num_threads * k_results_per_thread));
}
