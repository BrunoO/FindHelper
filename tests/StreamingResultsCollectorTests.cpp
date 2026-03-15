#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "search/StreamingResultsCollector.h"
#include <chrono>
#include <thread>
#include <vector>

TEST_CASE("StreamingResultsCollector - Basic Batching") {
    StreamingResultsCollector collector(10, 100);  // batch_size=10, interval=100ms

    std::vector<std::string> paths;
    for (int i = 0; i < 11; ++i) {
        paths.push_back("path" + std::to_string(i));
    }

    for (int i = 0; i < 9; ++i) {
        SearchResultData data;
        data.id = static_cast<uint64_t>(i);
        data.fullPath = paths[static_cast<size_t>(i)];
        data.isDirectory = false;
        collector.AddResult(std::move(data));
    }

    CHECK_FALSE(collector.HasNewBatch());
    CHECK(collector.GetAllPendingBatches().empty());

    SearchResultData data10;
    data10.id = 10;
    data10.fullPath = paths[10];
    data10.isDirectory = false;
    collector.AddResult(std::move(data10));

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
    collector.AddResult(std::move(data));

    CHECK_FALSE(collector.HasNewBatch());
    std::this_thread::sleep_for(std::chrono::milliseconds(60));

    SearchResultData data2;
    data2.id = 1;
    data2.fullPath = "path2";
    data2.isDirectory = false;
    collector.AddResult(std::move(data2));

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
    collector.AddResult(std::move(data));

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
    for (int i = 0; i < 30; ++i) {
        paths.push_back("path" + std::to_string(i));
    }

    for (int i = 0; i < 30; ++i) {
        SearchResultData data;
        data.id = static_cast<uint64_t>(i);
        data.fullPath = paths[static_cast<size_t>(i)];
        data.isDirectory = false;
        collector.AddResult(std::move(data));
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
    StreamingResultsCollector collector(10, 10);
    const int num_threads = 4;
    const int results_per_thread = 100;

    // Pre-allocate path strings so string_view in SearchResultData stays valid
    std::vector<std::vector<std::string>> thread_paths(static_cast<size_t>(num_threads));
    for (int t = 0; t < num_threads; ++t) {
        thread_paths[static_cast<size_t>(t)].reserve(static_cast<size_t>(results_per_thread));
        for (int i = 0; i < results_per_thread; ++i) {
            thread_paths[static_cast<size_t>(t)].push_back(
                "thread" + std::to_string(t) + "_result" + std::to_string(i));
        }
    }

    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&collector, &thread_paths, t]() {
            const auto per_thread_count =
                static_cast<int>(thread_paths[static_cast<size_t>(t)].size());
            for (int i = 0; i < per_thread_count; ++i) {
                SearchResultData data;
                data.id = static_cast<uint64_t>(t * 100 + i);
                data.fullPath = thread_paths[static_cast<size_t>(t)][static_cast<size_t>(i)];
                data.isDirectory = false;
                collector.AddResult(std::move(data));
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
        });
    }

    size_t total_collected = 0;
    while (total_collected < num_threads * results_per_thread) {
        if (collector.HasNewBatch()) {
            auto batch = collector.GetAllPendingBatches();
            total_collected += batch.size();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    for (auto& t : threads) {
        t.join();
    }

    collector.MarkSearchComplete();

    // Add any remaining results after MarkSearchComplete
    auto final_batch = collector.GetAllPendingBatches();
    total_collected += final_batch.size();

    CHECK(total_collected == num_threads * results_per_thread);
}
