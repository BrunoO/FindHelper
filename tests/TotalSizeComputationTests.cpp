/**
 * @file TotalSizeComputationTests.cpp
 * @brief Unit tests for displayed total size progressive computation
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include <vector>

#include "doctest/doctest.h"
#include "gui/GuiState.h"
#include "index/FileIndex.h"
#include "search/SearchResultUtils.h"
#include "utils/FileAttributeConstants.h"

TEST_CASE("Total size computation - optimization for loaded items") {
    GuiState state;
    FileIndex file_index;

    // Create 1000 search results
    for (uint64_t i = 1; i <= 1000; ++i) {
        SearchResult r;
        r.fileId = i;
        r.fileSize = 100; // Already loaded
        r.isDirectory = false;
        state.searchResults.push_back(r);
    }

    state.resultsComplete = true;
    state.showingPartialResults = false;
    state.displayedTotalSizeValid = false;
    state.displayedTotalSizeComputationIndex = 0;
    state.displayedTotalSizeComputationBytes = 0;

    // Call UpdateDisplayedTotalSizeIfNeeded.
    // With optimization, it should scan all 1000 items in ONE frame because they are already loaded
    // and 1000 < 10000 (scans per frame budget).
    UpdateDisplayedTotalSizeIfNeeded(state, file_index);

    CHECK(state.displayedTotalSizeValid == true);
    CHECK(state.displayedTotalSizeBytes == 1000 * 100);
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
        state.searchResults.push_back(r);
    }

    state.resultsComplete = true;
    state.showingPartialResults = false;
    state.displayedTotalSizeValid = false;

    // First call: should process 100 items (new budget)
    UpdateDisplayedTotalSizeIfNeeded(state, file_index);
    CHECK(state.displayedTotalSizeValid == false);
    CHECK(state.displayedTotalSizeComputationIndex == 100);

    // Call 4 more times
    for(int i=0; i<4; ++i) {
        UpdateDisplayedTotalSizeIfNeeded(state, file_index);
    }

    CHECK(state.displayedTotalSizeValid == true);
    CHECK(state.displayedTotalSizeComputationIndex == 0);
}
