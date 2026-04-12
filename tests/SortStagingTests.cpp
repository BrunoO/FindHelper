/**
 * @file SortStagingTests.cpp
 * @brief Tests for AsyncSortState and async sort attribute loading.
 *
 * Targets the invariants whose violation caused intermittent Windows crashes:
 *  - AsyncSortState construction, Reset(), and BeginNewSort()
 *  - No tasks enqueued when all attributes are pre-loaded (counter stays null)
 *  - Stale staging generation is silently discarded by ApplyPendingSortAttributeUpdates
 *  - Matching generation correctly promotes staged values into results
 *  - Concurrent writer vs. reset: bounds check prevents OOB (TOCTOU fix)
 *  - Counter and loading flag are reset after sort completes
 */

// Mark test thread as UI thread so UIThreadOwned<T> asserts pass in tests.
// All GuiState mutations in tests happen on this (single test) thread.
#define DOCTEST_CONFIG_IMPLEMENT

#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

#include "SortStagingTestHelpers.h"
#include "doctest/doctest.h"
#include "gui/GuiState.h"
#include "index/FileIndex.h"
#include "search/SearchResultUtils.h"
#include "search/SearchTypes.h"
#include "utils/FileAttributeConstants.h"
#include "utils/FileTimeTypes.h"
#include "utils/ThreadPool.h"
#include "utils/ThreadUtils.h"

int main(int argc, char** argv) {
    MarkCurrentThreadAsUI();
    doctest::Context context(argc, argv);
    return context.run();
}

namespace {

// Build a SearchResult whose size and time are already loaded so that
// StartAttributeLoadingAsync enqueues no tasks for it.
// The path string literal has static storage duration — the string_view is safe.
SearchResult MakePreloadedResult(std::string_view path, uint64_t size, uint32_t time_low) {
    SearchResult r;
    r.fullPath = path;
    r.fileSize = size;
    r.lastModificationTime = {time_low, 0};  // Not sentinel ({UINT32_MAX,UINT32_MAX}), not failed ({1,0})
    r.isDirectory = false;
    return r;
}

void RunCompletedSingleResultSizeSort(GuiState& state, ThreadPool& thread_pool) {
    std::vector<SearchResult> results = {
        MakePreloadedResult(R"(C:\dir\a.txt)", 100, 1000),
    };

    FileIndex file_index;
    StartSortAttributeLoading(state, results, ResultColumn::Size, file_index, thread_pool);
    const SortGeneration gen = state.async_sort_.generation_;
    static_cast<void>(CheckSortAttributeLoadingAndSort(
        state, results, ResultColumn::Size, ImGuiSortDirection_Ascending, gen));
}

}  // namespace

// ---------------------------------------------------------------------------
// SortStagingBuffer construction and reset
// ---------------------------------------------------------------------------

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_SUITE("AsyncSortState — construction and reset") {

TEST_CASE("Default construction — all fields empty / zero") {
    const AsyncSortState staging;
    CHECK(staging.sizes_.empty());
    CHECK(staging.times_.empty());
    CHECK(staging.flags_.empty());
    CHECK(staging.generation_ == 0);
    CHECK(staging.staging_generation_ == 0);
    CHECK(staging.counter == nullptr);
    CHECK(staging.sort_ready_state_ == SortReadyState::Idle);
}

TEST_CASE("Reset() clears staging buffers, counter, and state machine") {
    AsyncSortState staging;
    {
        const std::scoped_lock lock(staging.mutex_);
        staging.sizes_.assign(10, 42ULL);
        staging.times_.assign(10, FILETIME{100, 0});
        staging.flags_.assign(10, std::byte{3});
    }
    staging.generation_ = 7;
    staging.sort_ready_state_ = SortReadyState::Loading;
    staging.counter = std::make_shared<std::atomic<int>>(3);

    staging.staging_generation_ = 3;

    staging.Reset();

    CHECK(staging.sizes_.empty());
    CHECK(staging.times_.empty());
    CHECK(staging.flags_.empty());
    CHECK(staging.staging_generation_ == 0);
    // generation_ is NOT zeroed by Reset() — monotonically increasing
    CHECK(staging.generation_ == 7);
    CHECK(staging.counter == nullptr);
    CHECK(staging.sort_ready_state_ == SortReadyState::Idle);
}

TEST_CASE("Reset() on an already-empty state is a no-op") {
    AsyncSortState staging;
    CHECK_NOTHROW(staging.Reset());
    CHECK(staging.sizes_.empty());
    CHECK(staging.generation_ == 0);
    CHECK(staging.counter == nullptr);
}

TEST_CASE("BeginNewSort() bumps generation and replaces token") {
    AsyncSortState staging;
    const SortGeneration gen1 = staging.BeginNewSort();
    CHECK(gen1 == 1);
    CHECK(staging.generation_ == 1);
    const SortGeneration gen2 = staging.BeginNewSort();
    CHECK(gen2 == 2);
    CHECK(staging.generation_ == 2);
}

}  // TEST_SUITE("AsyncSortState — construction and reset")

// ---------------------------------------------------------------------------
// Pre-loaded results: no tasks enqueued
// ---------------------------------------------------------------------------

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_SUITE("StartSortAttributeLoading — pre-loaded data") {

TEST_CASE("Counter stays null when all attributes are already loaded") {
    GuiState state;
    FileIndex file_index;
    ThreadPool thread_pool(1);

    std::vector<SearchResult> results = {
        MakePreloadedResult(R"(C:\dir\a.txt)", 100, 1000),
        MakePreloadedResult(R"(C:\dir\b.txt)", 200, 2000),
    };

    StartSortAttributeLoading(state, results, ResultColumn::Size, file_index, thread_pool);

    // StartAttributeLoadingAsync found nothing to load → no counter created.
    CHECK(state.async_sort_.counter == nullptr);
    // State transitions to Ready immediately so the next poll completes at once.
    CHECK(state.async_sort_.sort_ready_state_ == SortReadyState::Ready);
}

TEST_CASE("CheckSortAttributeLoadingAndSort returns true and sorts for pre-loaded data") {
    GuiState state;
    FileIndex file_index;
    ThreadPool thread_pool(1);

    std::vector<SearchResult> results = {
        MakePreloadedResult(R"(C:\dir\c.txt)", 300, 3000),
        MakePreloadedResult(R"(C:\dir\a.txt)", 100, 1000),
        MakePreloadedResult(R"(C:\dir\b.txt)", 200, 2000),
    };

    StartSortAttributeLoading(state, results, ResultColumn::Size, file_index, thread_pool);
    const SortGeneration gen = state.async_sort_.generation_;

    const bool sorted = CheckSortAttributeLoadingAndSort(
        state, results, ResultColumn::Size, ImGuiSortDirection_Ascending, gen);

    CHECK(sorted == true);
    CHECK(results[0].fileSize == 100);
    CHECK(results[1].fileSize == 200);
    CHECK(results[2].fileSize == 300);
}

TEST_CASE("CheckSortAttributeLoadingAndSort descending sort for pre-loaded data") {
    GuiState state;
    FileIndex file_index;
    ThreadPool thread_pool(1);

    std::vector<SearchResult> results = {
        MakePreloadedResult(R"(C:\dir\a.txt)", 100, 1000),
        MakePreloadedResult(R"(C:\dir\b.txt)", 200, 2000),
        MakePreloadedResult(R"(C:\dir\c.txt)", 300, 3000),
    };

    StartSortAttributeLoading(state, results, ResultColumn::Size, file_index, thread_pool);
    const SortGeneration gen = state.async_sort_.generation_;

    const bool sorted = CheckSortAttributeLoadingAndSort(
        state, results, ResultColumn::Size, ImGuiSortDirection_Descending, gen);

    CHECK(sorted == true);
    CHECK(results[0].fileSize == 300);
    CHECK(results[1].fileSize == 200);
    CHECK(results[2].fileSize == 100);
}

}  // TEST_SUITE("StartSortAttributeLoading — pre-loaded data")

// ---------------------------------------------------------------------------
// Stale generation guard in ApplyPendingSortAttributeUpdates
// ---------------------------------------------------------------------------

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_SUITE("Stale generation guard") {

// These tests exercise ApplyPendingSortAttributeUpdates indirectly via
// CheckSortAttributeLoadingAndSort. sortDataReady=true bypasses the counter
// check so the apply/sort path runs unconditionally. The staging generation
// is then the only gate that decides whether staged values are promoted.
//
// kFlagSizeLoaded = std::byte{1} matches kPendingSortFlagSizeLoaded defined
// in the anonymous namespace of SearchResultUtils.cpp.

TEST_CASE("Staging with MISMATCHED generation is not applied — sizes stay kFileSizeNotLoaded") {
    constexpr std::byte kFlagSizeLoaded{1};

    GuiState state;
    constexpr SortGeneration kCurrentGen = 10;
    state.async_sort_.generation_ = kCurrentGen;
    state.async_sort_.ForceReadyForTest();  // Bypass counter; force apply/sort path to run.

    // Staging was prepared for an older sort (generation 5).
    constexpr SortGeneration kStaleStagingGen = 5;
    constexpr uint64_t kStagedSize = 999ULL;
    PrepareStaging(state.async_sort_, 2, kStaleStagingGen, kStagedSize, FILETIME{100, 0}, kFlagSizeLoaded);

    std::vector<SearchResult> results = {
        MakePreloadedResult(R"(C:\dir\a.txt)", kFileSizeNotLoaded, 500),
        MakePreloadedResult(R"(C:\dir\b.txt)", kFileSizeNotLoaded, 600),
    };

    // Call with the CURRENT generation — mismatch against staging generation 5.
    const bool sorted = CheckSortAttributeLoadingAndSort(
        state, results, ResultColumn::Size, ImGuiSortDirection_Ascending, kCurrentGen);

    CHECK(sorted == true);
    // Apply was skipped — staged values not written into results.
    CHECK(results[0].fileSize == kFileSizeNotLoaded);
    CHECK(results[1].fileSize == kFileSizeNotLoaded);
}

TEST_CASE("Staging with MATCHING generation is applied — sizes updated from staging") {
    constexpr std::byte kFlagSizeLoaded{1};

    GuiState state;
    constexpr SortGeneration kGen = 5;
    state.async_sort_.generation_ = kGen;
    state.async_sort_.ForceReadyForTest();

    constexpr uint64_t kStagedSize = 42ULL;
    PrepareStaging(state.async_sort_, 2, kGen, kStagedSize, FILETIME{100, 0}, kFlagSizeLoaded);

    std::vector<SearchResult> results = {
        MakePreloadedResult(R"(C:\dir\a.txt)", kFileSizeNotLoaded, 500),
        MakePreloadedResult(R"(C:\dir\b.txt)", kFileSizeNotLoaded, 600),
    };

    const bool sorted = CheckSortAttributeLoadingAndSort(
        state, results, ResultColumn::Size, ImGuiSortDirection_Ascending, kGen);

    CHECK(sorted == true);
    // Matching generation — staged values promoted into results.
    CHECK(results[0].fileSize == kStagedSize);
    CHECK(results[1].fileSize == kStagedSize);
}

}  // TEST_SUITE("Stale generation guard")

// ---------------------------------------------------------------------------
// Counter and loadingAttributes lifecycle
// ---------------------------------------------------------------------------

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_SUITE("AttributeLoadingCounter lifecycle") {

TEST_CASE("Counter is null on a freshly constructed GuiState") {
    const GuiState state;
    CHECK(state.async_sort_.counter == nullptr);
    CHECK(state.async_sort_.sort_ready_state_ == SortReadyState::Idle);
}

TEST_CASE("Counter is null and loading is false after sort completes") {
    GuiState state;
    ThreadPool thread_pool(1);

    RunCompletedSingleResultSizeSort(state, thread_pool);

    CHECK(state.async_sort_.counter == nullptr);
    CHECK(state.async_sort_.sort_ready_state_ == SortReadyState::Idle);
}

TEST_CASE("Staging is cleared after sort completes") {
    GuiState state;
    ThreadPool thread_pool(1);

    RunCompletedSingleResultSizeSort(state, thread_pool);

    // async_sort_.Reset() is called at the end of a completed sort.
    CHECK(state.async_sort_.sizes_.empty());
    CHECK(state.async_sort_.times_.empty());
    CHECK(state.async_sort_.flags_.empty());
    CHECK(state.async_sort_.staging_generation_ == 0);
    CHECK(state.async_sort_.counter == nullptr);
    CHECK(state.async_sort_.sort_ready_state_ == SortReadyState::Idle);
}

}  // TEST_SUITE("AttributeLoadingCounter lifecycle")

// ---------------------------------------------------------------------------
// Concurrent writer vs. reset — TOCTOU bounds-check invariant (Fix B)
// ---------------------------------------------------------------------------

TEST_SUITE("Concurrent staging stress") {

TEST_CASE("Concurrent writes and resets do not corrupt or crash") {
    // Models the TOCTOU scenario that Fix B addresses: a worker thread holds
    // the mutex, passes the bounds check, then the UI thread calls
    // ResetPendingSortStaging and clears the vectors before the worker writes.
    // The actual WriteStagingXxx helpers re-check the bound under the lock, so
    // the write is simply dropped. This stress test exercises that path.
    //
    // The writer here directly mirrors the pattern inside WriteStagingXxx:
    // acquire mutex → bounds-check → write. The reset mirrors AsyncSortState::Reset().

    AsyncSortState staging;

    constexpr int kIterations = 2000;
    constexpr size_t kSlots = 64;
    static constexpr size_t kTargetSlot = 31;

    std::atomic<bool> stop_flag{false};  // NOSONAR(cpp:S6012) - C++17: std::atomic requires an explicit template argument

    auto writer = std::thread([&staging, &stop_flag] {
        while (!stop_flag.load()) {
            const std::scoped_lock lock(staging.mutex_);
            if (kTargetSlot < staging.sizes_.size()) {
                staging.sizes_[kTargetSlot] = 99ULL;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - guarded by size check above
                staging.flags_[kTargetSlot] |= std::byte{1};
            }
        }
    });

    for (int i = 0; i < kIterations; ++i) {
        {
            const std::scoped_lock lock(staging.mutex_);
            staging.sizes_.assign(kSlots, kFileSizeNotLoaded);
            staging.times_.assign(kSlots, kFileTimeNotLoaded);
            staging.flags_.assign(kSlots, std::byte{0});
            staging.staging_generation_ = static_cast<SortGeneration>(i);
        }
        staging.Reset();
    }

    stop_flag.store(true);
    writer.join();

    // Reaching here without a crash or ASAN/TSAN report confirms the invariant.
    CHECK(staging.sizes_.empty());
}

}  // TEST_SUITE("Concurrent staging stress")
