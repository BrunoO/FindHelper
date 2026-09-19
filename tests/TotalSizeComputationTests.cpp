/**
 * @file TotalSizeComputationTests.cpp
 * @brief Unit tests for displayed total size progressive computation
 */

// Mark test thread as UI thread so UIThreadOwned<T> asserts pass in tests.
// All GuiState mutations in tests happen on this (single test) thread.
#define DOCTEST_CONFIG_IMPLEMENT

#include <vector>

#include "doctest/doctest.h"
#include "gui/GuiState.h"
#include "index/FileIndex.h"
#include "search/SearchResultUtils.h"
#include "utils/FileAttributeConstants.h"
#include "utils/ThreadUtils.h"

int main(int argc, char** argv) {
    MarkCurrentThreadAsUI();
    doctest::Context context(argc, argv);
    return context.run();
}

TEST_CASE("Total size computation - optimization for loaded items") {
    GuiState state;
    FileIndex file_index;

    // Create 1000 search results
    for (uint64_t i = 1; i <= 1000; ++i) {
        SearchResult r;
        r.fileId = i;
        r.fileSize = 100; // Already loaded
        r.isDirectory = false;
        state.result_pool_->Results().push_back(r);
    }

    state.search_pipeline.results_complete = true;
    state.filter_caches.total_size.valid = false;
    state.filter_caches.total_size.computation_index = 0;
    state.filter_caches.total_size.computation_bytes = 0;

    // Call UpdateDisplayedTotalSizeIfNeeded.
    // With optimization, it should scan all 1000 items in ONE frame because they are already loaded
    // and 1000 < 10000 (scans per frame budget).
    UpdateDisplayedTotalSizeIfNeeded(state, file_index);

    CHECK(state.filter_caches.total_size.valid == true);
    CHECK(state.filter_caches.total_size.bytes == 1000 * 100);
}

TEST_CASE("Total size computation - budget for unloaded items") {
    GuiState state;
    FileIndex file_index;

    // Create 500 search results, all NOT loaded
    for (uint64_t i = 1; i <= 500; ++i) {
        SearchResult r;
        r.fileId = i;
        r.fileSize = kFileSizeNotLoaded;
        r.isDirectory = false;
        state.result_pool_->Results().push_back(r);
    }

    state.search_pipeline.results_complete = true;
    state.filter_caches.total_size.valid = false;

    // First call: should process 100 items (new budget)
    UpdateDisplayedTotalSizeIfNeeded(state, file_index);
    CHECK(state.filter_caches.total_size.valid == false);
    CHECK(state.filter_caches.total_size.computation_index == 100);

    // Call 4 more times
    for(int i=0; i<4; ++i) {
        UpdateDisplayedTotalSizeIfNeeded(state, file_index);
    }

    CHECK(state.filter_caches.total_size.valid == true);
    CHECK(state.filter_caches.total_size.computation_index == 0);
}
