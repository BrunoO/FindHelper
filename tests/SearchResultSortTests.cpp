/**
 * @file SearchResultSortTests.cpp
 * @brief Unit tests for search result sorting logic
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "doctest/doctest.h"
#include "gui/GuiState.h"
#include "imgui.h"
#include "index/FileIndex.h"
#include "path/PathUtils.h"
#include "search/FolderSizeAggregator.h"
#include "search/SearchController.h"
#include "search/SearchControllerDetail.h"
#include "search/SearchResultUtils.h"
#include "search/SearchTypes.h"
#include "search/SearchWorker.h"
#include "ui/ResultsTable.h"
#include "utils/ThreadUtils.h"

// NOLINTNEXTLINE(readability-function-cognitive-complexity) - Aggregated doctest case with many subcases
TEST_CASE("SearchResult sorting by column") {
    // Setup test data
    // b_file.txt -> Filename: b_file, Ext: txt
    // Filename offset: 8, Dot at 14, Extension offset: 15
    constexpr int kFilenameOffset = 8;
    constexpr int kExtensionOffset = 15;
    constexpr std::uint64_t kSizeSmall = 500ULL;
    constexpr std::uint64_t kSizeMedium = 1000ULL;
    constexpr std::uint64_t kSizeLarge = 2000ULL;
    constexpr std::uint64_t kTimeEarly = 50ULL;
    constexpr std::uint64_t kTimeMiddle = 100ULL;
    constexpr std::uint64_t kTimeLate = 150ULL;
    SearchResult r1;
    r1.fullPath = R"(C:\path\b_file.txt)";
    r1.filename_offset = kFilenameOffset;
    r1.extension_offset = kExtensionOffset;
    r1.fileSize = kSizeMedium;
    r1.lastModificationTime.dwLowDateTime = kTimeMiddle;
    r1.lastModificationTime.dwHighDateTime = 0;

    // a_file.log -> Filename: a_file, Ext: log
    SearchResult r2;
    r2.fullPath = R"(C:\path\a_file.log)";
    r2.filename_offset = kFilenameOffset;
    r2.extension_offset = kExtensionOffset;
    r2.fileSize = kSizeLarge;
    r2.lastModificationTime.dwLowDateTime = kTimeEarly;
    r2.lastModificationTime.dwHighDateTime = 0;

    // c_file.txt -> Filename: c_file, Ext: txt
    SearchResult r3;
    r3.fullPath = R"(C:\path\c_file.txt)";
    r3.filename_offset = kFilenameOffset;
    r3.extension_offset = kExtensionOffset;
    r3.fileSize = kSizeSmall;
    r3.lastModificationTime.dwLowDateTime = kTimeLate;
    r3.lastModificationTime.dwHighDateTime = 0;

    std::vector<SearchResult> results = {r1, r2, r3};

    SUBCASE("Sort by Filename (Ascending - Flat Mode)") {
        auto comp = CreateSearchResultComparator(ResultColumn::Filename, ImGuiSortDirection_Ascending, false);
        std::sort(results.begin(), results.end(), comp);

        CHECK(results[0].GetFilename() == "a_file");
        CHECK(results[1].GetFilename() == "b_file");
        CHECK(results[2].GetFilename() == "c_file");
    }

    SUBCASE("Sort by Filename (Ascending - Hierarchical Mode)") {
        auto comp = CreateSearchResultComparator(ResultColumn::Filename, ImGuiSortDirection_Ascending, true);
        std::sort(results.begin(), results.end(), comp);

        CHECK(results[0].GetFilename() == "a_file");
        CHECK(results[1].GetFilename() == "b_file");
        CHECK(results[2].GetFilename() == "c_file");
    }

    SUBCASE("Sort by Size (Ascending)") {
        auto comp = CreateSearchResultComparator(ResultColumn::Size, ImGuiSortDirection_Ascending);
        std::sort(results.begin(), results.end(), comp);

        CHECK(results[0].fileSize == kSizeSmall);
        CHECK(results[1].fileSize == kSizeMedium);
        CHECK(results[2].fileSize == kSizeLarge);
    }

    SUBCASE("Sort by Size (Descending)") {
        auto comp = CreateSearchResultComparator(ResultColumn::Size, ImGuiSortDirection_Descending);
        std::sort(results.begin(), results.end(), comp);

        CHECK(results[0].fileSize == kSizeLarge);
        CHECK(results[1].fileSize == kSizeMedium);
        CHECK(results[2].fileSize == kSizeSmall);
    }

    SUBCASE("Sort by Last Modified (Ascending)") {
        auto comp = CreateSearchResultComparator(ResultColumn::LastModified, ImGuiSortDirection_Ascending);
        std::sort(results.begin(), results.end(), comp);

        CHECK(results[0].lastModificationTime.dwLowDateTime == kTimeEarly);
        CHECK(results[1].lastModificationTime.dwLowDateTime == kTimeMiddle);
        CHECK(results[2].lastModificationTime.dwLowDateTime == kTimeLate);
    }

    SUBCASE("Sort by Full Path (Ascending)") {
        auto comp = CreateSearchResultComparator(ResultColumn::FullPath, ImGuiSortDirection_Ascending);
        std::sort(results.begin(), results.end(), comp);

        CHECK(results[0].fullPath == "C:\\path\\a_file.log");
        CHECK(results[1].fullPath == "C:\\path\\b_file.txt");
        CHECK(results[2].fullPath == "C:\\path\\c_file.txt");
    }

    SUBCASE("Sort by Extension (Ascending)") {
        auto comp = CreateSearchResultComparator(ResultColumn::Extension, ImGuiSortDirection_Ascending);
        std::sort(results.begin(), results.end(), comp);

        CHECK(results[0].GetExtension() == "log");
        CHECK(results[1].GetExtension() == "txt");
        CHECK(results[2].GetExtension() == "txt");
        // Same-extension rows must order by full path (not unstable input order).
        CHECK(results[1].fullPath == "C:\\path\\b_file.txt");
        CHECK(results[2].fullPath == "C:\\path\\c_file.txt");
    }
}

TEST_CASE("CompareByColumn - equal primary keys tie-break by path then fileId") {
    constexpr size_t kFilenameOffset = 8;
    constexpr size_t kExtensionOffset = 15;

    SearchResult earlier_path{};
    earlier_path.fullPath = R"(C:\path\a_file.txt)";
    earlier_path.filename_offset = kFilenameOffset;
    earlier_path.extension_offset = kExtensionOffset;
    earlier_path.fileId = 200;
    earlier_path.fileSize = 100;
    earlier_path.lastModificationTime = {100, 0};
    earlier_path.folderFileCount = 5;

    SearchResult later_path{};
    later_path.fullPath = R"(C:\path\z_file.txt)";
    later_path.filename_offset = kFilenameOffset;
    later_path.extension_offset = kExtensionOffset;
    later_path.fileId = 100;
    later_path.fileSize = 100;
    later_path.lastModificationTime = {100, 0};
    later_path.folderFileCount = 5;

    SUBCASE("Extension equal → path order regardless of input order") {
        const auto comp =
            CreateSearchResultComparator(ResultColumn::Extension, ImGuiSortDirection_Ascending);
        std::vector<SearchResult> forward = {earlier_path, later_path};
        std::vector<SearchResult> reverse = {later_path, earlier_path};
        std::sort(forward.begin(), forward.end(), comp);
        std::sort(reverse.begin(), reverse.end(), comp);
        CHECK(forward[0].fullPath == earlier_path.fullPath);
        CHECK(forward[1].fullPath == later_path.fullPath);
        CHECK(reverse[0].fullPath == earlier_path.fullPath);
        CHECK(reverse[1].fullPath == later_path.fullPath);
    }

    SUBCASE("Filename flat equal basename → path order") {
        SearchResult same_name_other_dir = earlier_path;
        same_name_other_dir.fullPath = R"(C:\other\a_file.txt)";
        same_name_other_dir.filename_offset = 9;  // after "C:\\other\\"
        same_name_other_dir.extension_offset = 16;
        same_name_other_dir.fileId = 50;

        const auto comp = CreateSearchResultComparator(ResultColumn::Filename,
                                                       ImGuiSortDirection_Ascending, false);
        std::vector<SearchResult> results = {earlier_path, same_name_other_dir};
        std::sort(results.begin(), results.end(), comp);
        // Basename equal; "C:\other\..." < "C:\path\..."
        CHECK(results[0].fullPath == same_name_other_dir.fullPath);
        CHECK(results[1].fullPath == earlier_path.fullPath);
    }

    SUBCASE("Identical path → fileId breaks ties") {
        SearchResult twin = earlier_path;
        twin.fileId = 999;
        CHECK(ComparePathThenFileId(earlier_path, twin) < 0);
        CHECK(ComparePathThenFileId(twin, earlier_path) > 0);
        CHECK(CompareByColumn(earlier_path, twin, ResultColumn::Extension) < 0);
        CHECK(CompareByColumn(earlier_path, twin, ResultColumn::Size) < 0);
    }
}

TEST_CASE("ResultsTable::GetDirectoryPath - Windows hidden folder with leading dot") {
    // Simulate a Windows-style path where the directory itself starts with a dot,
    // e.g. C:\root\.hidden\file.txt. The directory path must include the ".hidden"
    // segment so that folder statistics aggregate correctly for that folder.
    SearchResult r;
    const std::string path = R"(C:\root\.hidden\file.txt)";
    r.fullPath = std::string_view(path);

    // Filename starts after the last backslash.
    // C:\root\.hidden\file.txt
    // 012345678901234567890123
    //               ^ filename_offset = 16 ('f')
    r.filename_offset = 16;
    r.extension_offset = std::string_view::npos;

    const std::string_view dir = ui::ResultsTable::GetDirectoryPath(r);
    CHECK(dir == "C:\\root\\.hidden");
}

TEST_CASE("ResultsTable::GetDirectoryPath - volume root files keep root separator") {
    SUBCASE("Windows drive-root file shows C:\\ not empty or C:") {
        SearchResult r;
        const std::string path = R"(C:\file.txt)";
        r.fullPath = std::string_view(path);
        // C:\file.txt → filename starts at index 3
        r.filename_offset = 3;
        r.extension_offset = 8;

        const std::string_view dir = ui::ResultsTable::GetDirectoryPath(r);
        CHECK(dir == "C:\\");
    }

    SUBCASE("Windows drive-root file with forward slash") {
        SearchResult r;
        const std::string path = "C:/file.txt";
        r.fullPath = std::string_view(path);
        r.filename_offset = 3;
        r.extension_offset = 8;

        const std::string_view dir = ui::ResultsTable::GetDirectoryPath(r);
        CHECK(dir == "C:/");
    }

    SUBCASE("Unix root file shows / not empty") {
        SearchResult r;
        const std::string path = "/file.txt";
        r.fullPath = std::string_view(path);
        r.filename_offset = 1;
        r.extension_offset = 6;

        const std::string_view dir = ui::ResultsTable::GetDirectoryPath(r);
        CHECK(dir == "/");
    }

    SUBCASE("nested path still omits trailing separator") {
        SearchResult r;
        const std::string path = R"(C:\Users\file.txt)";
        r.fullPath = std::string_view(path);
        r.filename_offset = 9;
        r.extension_offset = 14;

        const std::string_view dir = ui::ResultsTable::GetDirectoryPath(r);
        CHECK(dir == "C:\\Users");
    }

    SUBCASE("filename_offset 0 yields empty directory") {
        SearchResult r;
        const std::string path = "file.txt";
        r.fullPath = std::string_view(path);
        r.filename_offset = 0;
        r.extension_offset = 5;

        const std::string_view dir = ui::ResultsTable::GetDirectoryPath(r);
        CHECK(dir.empty());
    }
}

TEST_CASE("SearchResult::GetFilename and GetExtension - dotfile handling") {
    // Dotfiles (e.g. .bashrc): the only dot is the leading one.
    // ComputePathOffsets must NOT treat the leading dot as a separator,
    // so extension_offset must be npos and GetFilename() must return the full name.
    SUBCASE("Pure dotfile (.bashrc) - filename is full name, no extension") {
        // Offsets that ComputePathOffsets produces after the fix for "/home/user/.bashrc":
        //   filename_offset = 11, last_dot in ".bashrc" is at 0 -> extension_offset = npos
        SearchResult r;
        const std::string path = "/home/user/.bashrc";
        r.fullPath = std::string_view(path);
        r.filename_offset = 11;
        r.extension_offset = std::string_view::npos;

        CHECK(r.GetFilename() == ".bashrc");
        CHECK(r.GetExtension() == "");
    }

    SUBCASE("Dotfile with real extension (.profile.bak) - filename up to last dot") {
        // "/home/user/.profile.bak": last_dot in ".profile.bak" is at 8 (> 0)
        //   filename_offset = 11, extension_offset = 11 + 8 + 1 = 20
        constexpr size_t kProfileFilenameOffset = 11;
        constexpr size_t kProfileExtensionOffset = 20;
        SearchResult r;
        const std::string path = "/home/user/.profile.bak";
        r.fullPath = std::string_view(path);
        r.filename_offset = kProfileFilenameOffset;
        r.extension_offset = kProfileExtensionOffset;

        CHECK(r.GetFilename() == ".profile");
        CHECK(r.GetExtension() == "bak");
    }

    SUBCASE("Normal file (normal.txt) - unaffected by dotfile fix") {
        // R"(C:\path\normal.txt)": filename_offset=8, last_dot in "normal.txt" is at 6 (> 0)
        //   extension_offset = 8 + 6 + 1 = 15
        constexpr size_t kNormalFilenameOffset = 8;
        constexpr size_t kNormalExtensionOffset = 15;
        SearchResult r;
        const std::string path = R"(C:\path\normal.txt)";
        r.fullPath = std::string_view(path);
        r.filename_offset = kNormalFilenameOffset;
        r.extension_offset = kNormalExtensionOffset;

        CHECK(r.GetFilename() == "normal");
        CHECK(r.GetExtension() == "txt");
    }

    SUBCASE("File with no extension (Makefile) - no dot in filename") {
        // "/src/Makefile": filename_offset=5, extension_offset=npos
        constexpr size_t kMakefileFilenameOffset = 5;
        SearchResult r;
        const std::string path = "/src/Makefile";
        r.fullPath = std::string_view(path);
        r.filename_offset = kMakefileFilenameOffset;
        r.extension_offset = std::string_view::npos;

        CHECK(r.GetFilename() == "Makefile");
        CHECK(r.GetExtension() == "");
    }

    SUBCASE("Malformed extension_offset falls back to whole tail, never empty filename") {
        // Producer invariant: extension_offset >= filename_offset + 2. These
        // hand-built rows violate it (possible only via stale/hand-crafted
        // offsets); GetFilename must degrade to "treat as no extension".
        const std::string path = "/dir/file.txt";  // size 13, filename at 5

        SearchResult ext_at_first_char;  // ext == fn + 1: old code truncated to ""
        ext_at_first_char.fullPath = std::string_view(path);
        ext_at_first_char.filename_offset = 5;
        ext_at_first_char.extension_offset = 6;
        CHECK(ext_at_first_char.GetFilename() == "file.txt");

        SearchResult ext_before_filename;  // dot lies in the directory part
        ext_before_filename.fullPath = std::string_view(path);
        ext_before_filename.filename_offset = 5;
        ext_before_filename.extension_offset = 2;
        CHECK(ext_before_filename.GetFilename() == "file.txt");

        SearchResult ext_out_of_range;  // stale offset beyond the path
        ext_out_of_range.fullPath = std::string_view(path);
        ext_out_of_range.filename_offset = 5;
        ext_out_of_range.extension_offset = 100;
        CHECK(ext_out_of_range.GetFilename() == "file.txt");  // substr clamps
    }
}

TEST_CASE("CompareByColumn - sentinel file sizes sort as zero") {
    // kFileSizeNotLoaded (UINT64_MAX) and kFileSizeFailed (UINT64_MAX-1) are
    // mapped to 0 in the Size column comparator so that unloaded entries don't
    // float to the top of a descending sort.
    constexpr size_t kOffset = 8;

    SearchResult real;
    real.fullPath = R"(C:\path\real.txt)";
    real.filename_offset = kOffset;
    real.extension_offset = std::string_view::npos;
    constexpr uint64_t kRealSize = 500ULL;
    real.fileSize = kRealSize;

    SearchResult unloaded;
    unloaded.fullPath = R"(C:\path\unloaded.txt)";
    unloaded.filename_offset = kOffset;
    unloaded.extension_offset = std::string_view::npos;
    unloaded.fileSize = kFileSizeNotLoaded;

    SearchResult failed;
    failed.fullPath = R"(C:\path\failed.txt)";
    failed.filename_offset = kOffset;
    failed.extension_offset = std::string_view::npos;
    failed.fileSize = kFileSizeFailed;

    SUBCASE("descending sort: real file appears before sentinel entries") {
        std::vector<SearchResult> results = {unloaded, real, failed};
        const auto comp = CreateSearchResultComparator(ResultColumn::Size,
                                                       ImGuiSortDirection_Descending);
        std::sort(results.begin(), results.end(), comp);

        // real (500) > sentinel (treated as 0), so real sorts first descending
        CHECK(results[0].fileSize == kRealSize);
    }

    SUBCASE("ascending sort: sentinel entries (treated as 0) appear before real file") {
        std::vector<SearchResult> results = {real, unloaded, failed};
        const auto comp = CreateSearchResultComparator(ResultColumn::Size,
                                                       ImGuiSortDirection_Ascending);
        std::sort(results.begin(), results.end(), comp);

        // sentinels treated as 0 < 500, so they sort first ascending
        CHECK(results[2].fileSize == kRealSize);
    }
}

TEST_CASE("SearchResult sorting by Filename - Flat vs Hierarchical mode") {
    // Setup files in different folders where filename order differs from path order
    SearchResult r1;
    r1.fullPath = R"(C:\folder_z\a_file.txt)";
    r1.filename_offset = 12;  // "a_file.txt"
    r1.extension_offset = 19;

    SearchResult r2;
    r2.fullPath = R"(C:\folder_a\z_file.txt)";
    r2.filename_offset = 12;  // "z_file.txt"
    r2.extension_offset = 19;

    std::vector<SearchResult> results = {r1, r2};

    SUBCASE("Flat mode sorts purely by filename") {
        auto comp = CreateSearchResultComparator(ResultColumn::Filename, ImGuiSortDirection_Ascending, false);
        std::sort(results.begin(), results.end(), comp);

        // "a_file.txt" < "z_file.txt", so C:\folder_z\a_file.txt comes first
        CHECK(results[0].fullPath == R"(C:\folder_z\a_file.txt)");
        CHECK(results[1].fullPath == R"(C:\folder_a\z_file.txt)");
    }

    SUBCASE("Hierarchical mode sorts by full path structure") {
        auto comp = CreateSearchResultComparator(ResultColumn::Filename, ImGuiSortDirection_Ascending, true);
        std::sort(results.begin(), results.end(), comp);

        // "C:\folder_a\..." < "C:\folder_z\...", so C:\folder_a\z_file.txt comes first
        CHECK(results[0].fullPath == R"(C:\folder_a\z_file.txt)");
        CHECK(results[1].fullPath == R"(C:\folder_z\a_file.txt)");
    }
}

TEST_CASE("CompareByColumn - unknown column index returns 0 (equal)") {
    constexpr int kUnknownColumn = 999;
    constexpr size_t kOffset = 8;

    SearchResult r1;
    r1.fullPath = R"(C:\path\a.txt)";
    r1.filename_offset = kOffset;

    SearchResult r2;
    r2.fullPath = R"(C:\path\z.txt)";
    r2.filename_offset = kOffset;

    CHECK(CompareByColumn(r1, r2, kUnknownColumn) == 0);
    CHECK(CompareByColumn(r2, r1, kUnknownColumn) == 0);
}

TEST_CASE("ResultsTable::FlushAggregatorFolderStats - null aggregator is no-op") {
    GuiState state;
    CHECK_EQ(ui::ResultsTable::FlushAggregatorFolderStats(state, nullptr), 0U);
}

namespace {

[[nodiscard]] SearchResult CreateTestPendingDirectory(std::string& path_storage) {
    path_storage = path_utils::JoinPath(path_utils::GetDefaultVolumeRootPath(), "folder");
    SearchResult dir{};
    dir.fileId = 1;
    dir.fullPath = path_storage;
    dir.isDirectory = true;
    dir.fileSize = kFileSizeNotLoaded;
    dir.folderFileCount = kFolderFileCountNotLoaded;
    return dir;
}

size_t FlushAggregatorAndPoll(GuiState& state, FolderSizeAggregator& aggregator, uint64_t file_id) {
    const size_t updated1 = ui::ResultsTable::FlushAggregatorFolderStats(state, &aggregator);
    for (int i = 0; i < 200; ++i) {
        if (aggregator.GetResult(file_id).has_value()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(aggregator.GetResult(file_id).has_value());
    const size_t updated2 = ui::ResultsTable::FlushAggregatorFolderStats(state, &aggregator);
    return updated1 + updated2;
}

// Shared folder+child index with an aggregator and a GuiState showing one
// pending directory sorted by size. Covers the FlushAggregatorFolderStats
// cases, which differ only in the flush call under test
// (Sonar: duplicate 16-line setup blocks).
struct FolderStatsFixture {
    FileIndex index;
    FolderSizeAggregator aggregator{index};
    GuiState state;
    // Backs the fullPath view stored in Results(): never read directly,
    // but must outlive the test body (same lifetime as the original local).
    std::string folder_path;
    FolderStatsFixture() {
        MarkCurrentThreadAsUI();
        index.Insert(ntfs_file_reference::NtfsFileReference(1), ntfs_file_reference::NtfsFileReference(0), "folder", true);
        index.Insert(ntfs_file_reference::NtfsFileReference(2), ntfs_file_reference::NtfsFileReference(1), "child.txt", false, {0, 0}, 1024);
        index.RecomputeAllPaths();
        state.search_pipeline.results_complete = true;
        state.result_pool_->Results() = {CreateTestPendingDirectory(folder_path)};
        state.async_sort_.last_sort_column = ResultColumn::Size;
    }
};

}  // namespace

TEST_CASE("ResultsTable::FlushAggregatorFolderStats - incomplete results still handoff display stats") {
    MarkCurrentThreadAsUI();
    FileIndex index;
    index.Insert(ntfs_file_reference::NtfsFileReference(1), ntfs_file_reference::NtfsFileReference(0), "folder", true);
    index.Insert(ntfs_file_reference::NtfsFileReference(2), ntfs_file_reference::NtfsFileReference(1), "child.txt", false, {0, 0}, 512);
    index.RecomputeAllPaths();

    FolderSizeAggregator aggregator(index);
    GuiState state;
    state.search_pipeline.results_complete = false;
    state.search_pipeline.search_active = true;

    std::string folder_path;
    state.result_pool_->Results() = {CreateTestPendingDirectory(folder_path)};
    state.async_sort_.last_sort_column = ResultColumn::Size;

    const size_t updated = FlushAggregatorAndPoll(state, aggregator, 1);
    CHECK_EQ(updated, 1U);
    CHECK_EQ(state.result_pool_->Results()[0].fileSize, 512U);
    CHECK_EQ(state.result_pool_->Results()[0].fileSizeDisplay, "512 B");
    CHECK_EQ(state.result_pool_->Results()[0].folderFileCount, 1U);
    // In-flight search: display stats update, but sort must not re-trigger.
    CHECK_FALSE(state.search_pipeline.results_updated);
}

TEST_CASE("ResultsTable::FlushAggregatorFolderStats - batches and writes folder stats") {
    FolderStatsFixture fixture;
    GuiState& state = fixture.state;
    FolderSizeAggregator& aggregator = fixture.aggregator;

    const size_t updated = FlushAggregatorAndPoll(state, aggregator, 1);
    CHECK_EQ(updated, 1U);
    CHECK_EQ(state.result_pool_->Results()[0].fileSize, 1024U);
    CHECK_EQ(state.result_pool_->Results()[0].fileSizeDisplay, "1.0 KB");
    CHECK_EQ(state.result_pool_->Results()[0].folderFileCount, 1U);
    CHECK_EQ(state.result_pool_->Results()[0].folderFileCountDisplay, "1");
    CHECK(state.search_pipeline.results_updated);

    // Idle third call: nothing pending, returns 0 immediately
    CHECK_EQ(ui::ResultsTable::FlushAggregatorFolderStats(state, &aggregator), 0U);
}

TEST_CASE("ResultsTable::FlushAggregatorFolderStats - idle steady state arms the fast-path") {
    FolderStatsFixture fixture;
    GuiState& state = fixture.state;
    FolderSizeAggregator& aggregator = fixture.aggregator;

    CHECK_EQ(FlushAggregatorAndPoll(state, aggregator, 1), 1U);
    // Drain to idle: all stats resolved, returns 0.
    CHECK_EQ(ui::ResultsTable::FlushAggregatorFolderStats(state, &aggregator), 0U);
    // Fast-path armed: version recorded, nothing pending.
    CHECK_EQ(state.search_pipeline.last_folder_stats_flush_version, state.GetResultsVersion());
    CHECK_FALSE(state.search_pipeline.folder_stats_flush_had_pending);
    // Steady-state call still returns 0 (via fast-path, no rescan).
    CHECK_EQ(ui::ResultsTable::FlushAggregatorFolderStats(state, &aggregator), 0U);
    // A results change re-arms the scan: fast-path must not fire on a stale version.
    state.BumpResultsVersion();
    CHECK_EQ(ui::ResultsTable::FlushAggregatorFolderStats(state, &aggregator), 0U);
    CHECK_EQ(state.search_pipeline.last_folder_stats_flush_version, state.GetResultsVersion());
}

TEST_CASE("ResultsTable::FlushAggregatorFolderStats - syncs active time filter results") {
    MarkCurrentThreadAsUI();
    FileIndex index;
    index.Insert(ntfs_file_reference::NtfsFileReference(1), ntfs_file_reference::NtfsFileReference(0), "folder", true);
    index.Insert(ntfs_file_reference::NtfsFileReference(2), ntfs_file_reference::NtfsFileReference(1), "child.txt", false, {0, 0}, 2048);
    index.RecomputeAllPaths();

    FolderSizeAggregator aggregator(index);
    GuiState state;
    state.search_pipeline.results_complete = true;

    std::string folder_path;
    SearchResult dir = CreateTestPendingDirectory(folder_path);
    state.result_pool_->Results() = {dir};
    state.searchCriteria.time_filter = TimeFilter::Today;
    state.filter_caches.time.valid = true;
    state.filter_caches.time.results = {dir};
    state.async_sort_.last_sort_column = ResultColumn::Filename;

    const size_t updated = FlushAggregatorAndPoll(state, aggregator, 1);
    CHECK_EQ(updated, 1U);
    CHECK_EQ(state.result_pool_->Results()[0].fileSize, 2048U);
    CHECK_EQ(state.filter_caches.time.results[0].fileSize, 2048U);
    CHECK_EQ(state.filter_caches.time.results[0].fileSizeDisplay, "2.0 KB");
    CHECK_EQ(state.filter_caches.time.results[0].folderFileCount, 1U);
    CHECK_EQ(state.filter_caches.time.results[0].folderFileCountDisplay, "1");
    // Since lastSortColumn is Name, search_pipeline.results_updated must NOT be set
    CHECK_FALSE(state.search_pipeline.results_updated);
}

namespace {

[[nodiscard]] SearchResult CreateDummySearchResult(uint64_t file_id, std::string_view path,
                                                    uint64_t size, uint32_t mod_time) {
    SearchResult r{};
    r.fileId = file_id;
    r.fullPath = path;
    r.fileSize = size;
    r.lastModificationTime.dwLowDateTime = mod_time;
    r.lastModificationTime.dwHighDateTime = 0;
    r.folderFileCount = 0;
    r.isDirectory = false;
    return r;
}

void WaitForWorkerResults(const SearchWorker& worker) {
    for (int i = 0; i < 200; ++i) {
        if (worker.HasNewResults() && !worker.IsBusy()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(worker.HasNewResults());
}

}  // namespace

TEST_CASE("search_controller_detail::AreSearchResultsEqual") {
    const SearchResult r1 = CreateDummySearchResult(100, "C:\\test\\file1.txt", 1024, 500);
    const SearchResult r2 = CreateDummySearchResult(101, "C:\\test\\file2.txt", 2048, 600);

    const std::vector<SearchResult> vec_a = {r1, r2};
    const std::vector<SearchResult> vec_b = {r1, r2};

    SUBCASE("Identical vectors return true") {
        CHECK(search_controller_detail::AreSearchResultsEqual(vec_a, vec_b));
    }

    SUBCASE("Different size vectors return false") {
        const std::vector<SearchResult> vec_c = {r1};
        CHECK_FALSE(search_controller_detail::AreSearchResultsEqual(vec_a, vec_c));
    }

    SUBCASE("Vector with modified element attributes returns false") {
        std::vector<SearchResult> vec_d = vec_a;
        vec_d[1].fileSize = 9999;
        CHECK_FALSE(search_controller_detail::AreSearchResultsEqual(vec_a, vec_d));
    }

    SUBCASE("Reordered vector returns false") {
        const std::vector<SearchResult> vec_e = {r2, r1};
        CHECK_FALSE(search_controller_detail::AreSearchResultsEqual(vec_a, vec_e));
    }
}

TEST_CASE("search_controller_detail::AreSearchResultSetsEqualUnordered") {
    const SearchResult r1 = CreateDummySearchResult(100, "C:\\test\\file1.txt", 1024, 500);
    const SearchResult r2 = CreateDummySearchResult(101, "C:\\test\\file2.txt", 2048, 600);

    SUBCASE("Same set in shuffled order returns true") {
        const std::vector<SearchResult> vec_a = {r1, r2};
        const std::vector<SearchResult> vec_b = {r2, r1};
        CHECK(search_controller_detail::AreSearchResultSetsEqualUnordered(vec_a, vec_b));
    }

    SUBCASE("Identical order returns true (agrees with ordered compare)") {
        const std::vector<SearchResult> vec_a = {r1, r2};
        const std::vector<SearchResult> vec_b = {r1, r2};
        CHECK(search_controller_detail::AreSearchResultSetsEqualUnordered(vec_a, vec_b));
        CHECK(search_controller_detail::AreSearchResultsEqual(vec_a, vec_b));
    }

    SUBCASE("Different size vectors return false") {
        const std::vector<SearchResult> vec_a = {r1, r2};
        const std::vector<SearchResult> vec_c = {r1};
        CHECK_FALSE(search_controller_detail::AreSearchResultSetsEqualUnordered(vec_a, vec_c));
    }

    SUBCASE("Swapped fileId returns false") {
        std::vector<SearchResult> vec_d = {r1, r2};
        vec_d[0].fileId = 999;
        CHECK_FALSE(search_controller_detail::AreSearchResultSetsEqualUnordered(vec_d, {r1, r2}));
    }

    SUBCASE("Size-only change returns false") {
        std::vector<SearchResult> vec_d = {r2, r1};
        vec_d[0].fileSize = 9999;
        CHECK_FALSE(search_controller_detail::AreSearchResultSetsEqualUnordered(vec_d, {r1, r2}));
    }

    SUBCASE("mtime-only change returns false") {
        std::vector<SearchResult> vec_d = {r2, r1};
        vec_d[1].lastModificationTime.dwLowDateTime = 777;
        CHECK_FALSE(search_controller_detail::AreSearchResultSetsEqualUnordered(vec_d, {r1, r2}));
    }

    SUBCASE("Both empty return true") {
        CHECK(search_controller_detail::AreSearchResultSetsEqualUnordered({}, {}));
    }
}

TEST_CASE("SearchController::PollResults double buffering handoff") {
    MarkCurrentThreadAsUI();
    FileIndex index;
    index.Insert(ntfs_file_reference::NtfsFileReference(10), ntfs_file_reference::NtfsFileReference(0), "alpha.txt", false, {100, 0}, 100);
    index.Insert(ntfs_file_reference::NtfsFileReference(20), ntfs_file_reference::NtfsFileReference(0), "beta.txt", false, {200, 0}, 200);
    index.RecomputeAllPaths();

    SearchWorker worker(index);
    SearchController controller;
    GuiState state;

    // Trigger a search on worker
    SearchParams params;
    params.filenameInput = "*.txt";
    worker.StartSearch(params);
    WaitForWorkerResults(worker);

    // First PollResults: populates result_pool_ with pre-sorted results (alpha.txt, beta.txt)
    state.async_sort_.last_sort_column = ResultColumn::Filename;
    state.async_sort_.last_sort_direction = ImGuiSortDirection_Ascending;
    constexpr bool kShowHierarchy = false;
    controller.PollResults(state, worker, nullptr, index, kShowHierarchy);

    CHECK(state.search_pipeline.results_updated);
    CHECK(state.search_pipeline.results_presorted_on_commit);
    CHECK_EQ(state.result_pool_->Results().size(), 2U);
    state.search_pipeline.results_updated = false;
    state.search_pipeline.results_presorted_on_commit = false;

    // Trigger same search again
    worker.StartSearch(params);
    WaitForWorkerResults(worker);

    // Second PollResults with identical result set:
    // Double buffering diff detects no change -> skips swap, search_pipeline.results_updated remains false!
    const uint64_t batch_before = state.result_pool_->BatchNumber();
    controller.PollResults(state, worker, nullptr, index, kShowHierarchy);

    CHECK_FALSE(state.search_pipeline.results_updated);
    CHECK_FALSE(state.search_pipeline.results_presorted_on_commit);
    CHECK_EQ(state.result_pool_->BatchNumber(), batch_before);
}

TEST_CASE("SearchController::PollResults quarantined commit keeps pool views valid") {
    // Regression: CommitNewSearchResults used to call Pool().shrink_to_fit()
    // after SearchResult.fullPath views were committed into it. Quarantine
    // slack (the merge reserves bytes for quarantined hits it then skips)
    // combined with a big-search → small-search transition made the shrink
    // reallocate: a formal use-after-free in ResultsTable::Render, masked in
    // practice only by allocator address reuse.
    MarkCurrentThreadAsUI();
    FileIndex index;
    for (int i = 0; i < 40; ++i) {
        index.InsertPath("/bigdir/file_" + std::to_string(i) + "_with_a_long_name.txt", false);
    }
    index.InsertPath("/bigdir/late_small.txt", false);

    SearchWorker worker(index);
    SearchController controller;
    GuiState state;
    state.async_sort_.last_sort_column = ResultColumn::Filename;
    state.async_sort_.last_sort_direction = ImGuiSortDirection_Ascending;
    constexpr bool kShowHierarchy = false;

    // Big commit: leaves a large pool capacity behind.
    SearchParams big_params;
    big_params.filenameInput = "*.txt";
    worker.StartSearch(big_params);
    WaitForWorkerResults(worker);
    controller.PollResults(state, worker, nullptr, index, kShowHierarchy);
    REQUIRE_EQ(state.result_pool_->Results().size(), 41U);

    // Arm quarantine AFTER the big commit: child CREATE before its parent →
    // bare-name placeholder tracked awaiting. The next merge reserves its
    // bytes but skips it, leaving capacity slack in the incoming pool.
    constexpr uint64_t kMissingParent = 0x00010000000000A0ULL;
    constexpr uint64_t kChild = 0x00010000000000B0ULL;
    index.Insert(ntfs_file_reference::NtfsFileReference(kChild), ntfs_file_reference::NtfsFileReference(kMissingParent), "late.txt", false);
    REQUIRE(index.GetAwaitingStats().count == 1);

    // Small commit: one visible hit ("late_small.txt") plus the quarantined
    // bare name ("late.txt"); new size << prev capacity fired the old shrink.
    SearchParams small_params;
    small_params.filenameInput = "late*";
    worker.StartSearch(small_params);
    WaitForWorkerResults(worker);
    controller.PollResults(state, worker, nullptr, index, kShowHierarchy);
    REQUIRE_EQ(state.result_pool_->Results().size(), 1U);

    // The incoming pool kept its quarantine slack (no shrink reallocated it),
    // so every committed view still lies inside the committed buffer.
    const auto& pool = state.result_pool_->Pool();
    CHECK(pool.capacity() > pool.size());
    const auto pool_start =
        reinterpret_cast<std::uintptr_t>(pool.data());  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) - test-only pointer range check
    const auto pool_end = pool_start + pool.size();
    for (const SearchResult& result : state.result_pool_->Results()) {
        const auto addr =
            reinterpret_cast<std::uintptr_t>(result.fullPath.data());  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) - test-only pointer range check
        CHECK(addr >= pool_start);
        CHECK(addr + result.fullPath.size() <= pool_end);
    }
    // Expected full path is platform-rooted: PathBuilder prefixes the default volume
    // root ("C:\" on Windows, "/" elsewhere), e.g. "C:\bigdir\late_small.txt".
    std::string expected_path(path_utils::GetDefaultVolumeRootPathView());
    expected_path += "bigdir";
    expected_path += path_utils::kPathSeparator;
    expected_path += "late_small.txt";
    CHECK(state.result_pool_->Results()[0].fullPath == expected_path);
}

TEST_CASE("SearchController::PollResults skip-swap across quarantine heal keeps views consistent") {
    // Finding #7 probe: the sets_equal early-return discards the fresh back
    // buffer without BumpResultsVersion / selection remap. If a quarantine
    // transition (awaiting placeholder -> healed full path) could ever appear
    // content-equal to the front buffer, the healed row would be stranded out
    // of the visible set while the version stays armed. This test pins the
    // contract across four generations:
    //   G1 commit [anchor]; G2 awaiting child quarantined -> sets equal ->
    //   SKIP (pool/version/batch untouched); G3 parent arrives, child heals ->
    //   set grows -> MUST commit (healed row visible, views inside new pool);
    //   G4 unchanged -> idempotent SKIP.
    MarkCurrentThreadAsUI();
    FileIndex index;
    index.Insert(ntfs_file_reference::NtfsFileReference(1), ntfs_file_reference::NtfsFileReference(0), "bigdir", true);
    index.Insert(ntfs_file_reference::NtfsFileReference(2), ntfs_file_reference::NtfsFileReference(1), "anchor.txt", false, {100, 0}, 100);
    index.RecomputeAllPaths();

    SearchWorker worker(index);
    SearchController controller;
    GuiState state;
    state.async_sort_.last_sort_column = ResultColumn::Filename;
    state.async_sort_.last_sort_direction = ImGuiSortDirection_Ascending;
    constexpr bool kShowHierarchy = false;

    SearchParams params;
    params.filenameInput = "*.txt";

    // G1: commit anchor-only set.
    worker.StartSearch(params);
    WaitForWorkerResults(worker);
    controller.PollResults(state, worker, nullptr, index, kShowHierarchy);
    REQUIRE_EQ(state.result_pool_->Results().size(), 1U);
    const char* const pool_g1 = state.result_pool_->Pool().data();
    const auto version_g1 = state.GetResultsVersion();
    const uint64_t batch_g1 = state.result_pool_->BatchNumber();

    // G2: awaiting child matches the query but is quarantined at merge, so the
    // back set equals the front set: skip-swap must fire and change nothing.
    constexpr uint64_t kMissingParent = 0x00010000000000C0ULL;
    constexpr uint64_t kChild = 0x00010000000000D0ULL;
    index.Insert(ntfs_file_reference::NtfsFileReference(kChild), ntfs_file_reference::NtfsFileReference(kMissingParent), "late.txt", false, {150, 0}, 150);
    REQUIRE(index.GetAwaitingStats().count == 1);
    worker.StartSearch(params);
    WaitForWorkerResults(worker);
    controller.PollResults(state, worker, nullptr, index, kShowHierarchy);
    REQUIRE_EQ(state.result_pool_->Results().size(), 1U);
    CHECK(state.result_pool_->Pool().data() == pool_g1);  // skip: no pool swap
    CHECK(state.GetResultsVersion() == version_g1);
    CHECK_EQ(state.result_pool_->BatchNumber(), batch_g1);

    // G3: parent arrival heals the child to a full path. The set grows: the
    // skip must NOT fire - healed row committed, views inside the new pool.
    index.Insert(ntfs_file_reference::NtfsFileReference(kMissingParent), ntfs_file_reference::NtfsFileReference(1), "LateDir", true);
    REQUIRE(index.GetAwaitingStats().count == 0);
    worker.StartSearch(params);
    WaitForWorkerResults(worker);
    controller.PollResults(state, worker, nullptr, index, kShowHierarchy);
    REQUIRE_EQ(state.result_pool_->Results().size(), 2U);
    CHECK(state.GetResultsVersion() != version_g1);   // commit bumped version
    CHECK(state.result_pool_->BatchNumber() != batch_g1);  // and batch number
    const auto& pool_g3 = state.result_pool_->Pool();
    const auto pool_start =
        reinterpret_cast<std::uintptr_t>(pool_g3.data());  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) - test-only pointer range check
    bool healed_visible = false;
    for (const SearchResult& result : state.result_pool_->Results()) {
        const auto addr =
            reinterpret_cast<std::uintptr_t>(result.fullPath.data());  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) - test-only pointer range check
        CHECK(addr >= pool_start);
        CHECK(addr + result.fullPath.size() <= pool_start + pool_g3.size());
        if (result.fileId == kChild) {
            healed_visible = result.fullPath.size() > std::string_view("late.txt").size() &&
                             result.fullPath.find("late.txt") != std::string_view::npos;
        }
    }
    CHECK(healed_visible);
    const auto version_g3 = state.GetResultsVersion();
    const uint64_t batch_g3 = state.result_pool_->BatchNumber();

    // G4: nothing changed -> idempotent skip; pool, version, batch stable and
    // every view still lies inside the committed buffer.
    const char* const pool_before_g4 = state.result_pool_->Pool().data();
    worker.StartSearch(params);
    WaitForWorkerResults(worker);
    controller.PollResults(state, worker, nullptr, index, kShowHierarchy);
    REQUIRE_EQ(state.result_pool_->Results().size(), 2U);
    CHECK(state.result_pool_->Pool().data() == pool_before_g4);
    CHECK(state.GetResultsVersion() == version_g3);
    CHECK_EQ(state.result_pool_->BatchNumber(), batch_g3);
    for (const SearchResult& result : state.result_pool_->Results()) {
        const auto addr =
            reinterpret_cast<std::uintptr_t>(result.fullPath.data());  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) - test-only pointer range check
        CHECK(addr >= pool_start);
        CHECK(addr + result.fullPath.size() <= pool_start + pool_g3.size());
    }
}

TEST_CASE("SearchController::PollResults skips swap when hierarchy sort matches front") {
    // Basename order (a then b) differs from full-path order (a/b then z/a).
    // Back-buffer must use show_hierarchy=true or skip-swap would falsely fail.
    MarkCurrentThreadAsUI();
    FileIndex index;
    index.Insert(ntfs_file_reference::NtfsFileReference(1), ntfs_file_reference::NtfsFileReference(0), "a", true);
    index.Insert(ntfs_file_reference::NtfsFileReference(2), ntfs_file_reference::NtfsFileReference(0), "z", true);
    index.Insert(ntfs_file_reference::NtfsFileReference(10), ntfs_file_reference::NtfsFileReference(1), "b.txt", false, {100, 0}, 100);
    index.Insert(ntfs_file_reference::NtfsFileReference(20), ntfs_file_reference::NtfsFileReference(2), "a.txt", false, {200, 0}, 200);
    index.RecomputeAllPaths();

    SearchWorker worker(index);
    SearchController controller;
    GuiState state;

    SearchParams params;
    params.filenameInput = "*.txt";
    worker.StartSearch(params);
    WaitForWorkerResults(worker);

    state.async_sort_.last_sort_column = ResultColumn::Filename;
    state.async_sort_.last_sort_direction = ImGuiSortDirection_Ascending;
    constexpr bool kShowHierarchy = true;
    controller.PollResults(state, worker, nullptr, index, kShowHierarchy);

    REQUIRE(state.result_pool_->Results().size() == 2U);
    // Hierarchy sort: full path → ".../a/b.txt" before ".../z/a.txt"
    CHECK(state.result_pool_->Results()[0].GetFilename() == "b");
    CHECK(state.result_pool_->Results()[1].GetFilename() == "a");
    state.search_pipeline.results_updated = false;

    worker.StartSearch(params);
    WaitForWorkerResults(worker);

    const uint64_t batch_before = state.result_pool_->BatchNumber();
    controller.PollResults(state, worker, nullptr, index, kShowHierarchy);

    CHECK_FALSE(state.search_pipeline.results_updated);
    CHECK_EQ(state.result_pool_->BatchNumber(), batch_before);
}

TEST_CASE("search_controller_detail::ReconcileComputedDirectoryAttributes") {
    SUBCASE("Copies size and folder file count from front") {
        SearchResult front_dir = CreateDummySearchResult(1, "C:\\test\\dir", kFileSizeNotLoaded, 0);
        front_dir.isDirectory = true;
        front_dir.fileSize = 4096;
        front_dir.fileSizeDisplay = "4.0 KB";
        front_dir.folderFileCount = 5;
        front_dir.folderFileCountDisplay = "5";
        front_dir.lastModificationTime = kFileTimeNotLoaded;

        SearchResult back_dir = CreateDummySearchResult(1, "C:\\test\\dir", kFileSizeNotLoaded, 0);
        back_dir.isDirectory = true;
        back_dir.fileSize = kFileSizeNotLoaded;
        back_dir.folderFileCount = kFolderFileCountNotLoaded;
        back_dir.lastModificationTime = kFileTimeNotLoaded;

        std::vector<SearchResult> front_results = {front_dir};
        std::vector<SearchResult> back_results = {back_dir};

        search_controller_detail::ReconcileComputedDirectoryAttributes(back_results, front_results);

        CHECK_EQ(back_results[0].fileSize, 4096U);
        CHECK_EQ(back_results[0].fileSizeDisplay, "4.0 KB");
        CHECK_EQ(back_results[0].folderFileCount, 5U);
        CHECK_EQ(back_results[0].folderFileCountDisplay, "5");
    }

    SUBCASE("Copies loaded lastModificationTime so AreSearchResultsEqual can skip-swap") {
        const FILETIME loaded_time{12345, 67890};

        SearchResult front_dir = CreateDummySearchResult(1, "C:\\test\\dir", kFileSizeNotLoaded, 0);
        front_dir.isDirectory = true;
        front_dir.fileSize = kFileSizeNotLoaded;
        front_dir.folderFileCount = kFolderFileCountNotLoaded;
        front_dir.lastModificationTime = loaded_time;
        front_dir.lastModificationDisplay = "2024-01-01 12:00";

        SearchResult back_dir = CreateDummySearchResult(1, "C:\\test\\dir", kFileSizeNotLoaded, 0);
        back_dir.isDirectory = true;
        back_dir.fileSize = kFileSizeNotLoaded;
        back_dir.folderFileCount = kFolderFileCountNotLoaded;
        back_dir.lastModificationTime = kFileTimeNotLoaded;
        back_dir.lastModificationDisplay.clear();

        std::vector<SearchResult> front_results = {front_dir};
        std::vector<SearchResult> back_results = {back_dir};

        CHECK_FALSE(search_controller_detail::AreSearchResultsEqual(front_results, back_results));

        search_controller_detail::ReconcileComputedDirectoryAttributes(back_results, front_results);

        CHECK_EQ(back_results[0].lastModificationTime.dwLowDateTime, loaded_time.dwLowDateTime);
        CHECK_EQ(back_results[0].lastModificationTime.dwHighDateTime, loaded_time.dwHighDateTime);
        CHECK_EQ(back_results[0].lastModificationDisplay, "2024-01-01 12:00");
        CHECK(search_controller_detail::AreSearchResultsEqual(front_results, back_results));
    }

    SUBCASE("Copies size, count, and mtime together when all loaded on front") {
        const FILETIME loaded_time{111, 222};

        SearchResult front_dir = CreateDummySearchResult(1, "C:\\test\\dir", kFileSizeNotLoaded, 0);
        front_dir.isDirectory = true;
        front_dir.fileSize = 8192;
        front_dir.fileSizeDisplay = "8.0 KB";
        front_dir.folderFileCount = 3;
        front_dir.folderFileCountDisplay = "3";
        front_dir.lastModificationTime = loaded_time;
        front_dir.lastModificationDisplay = "loaded";

        SearchResult back_dir = CreateDummySearchResult(1, "C:\\test\\dir", kFileSizeNotLoaded, 0);
        back_dir.isDirectory = true;
        back_dir.fileSize = kFileSizeNotLoaded;
        back_dir.folderFileCount = kFolderFileCountNotLoaded;
        back_dir.lastModificationTime = kFileTimeNotLoaded;

        std::vector<SearchResult> front_results = {front_dir};
        std::vector<SearchResult> back_results = {back_dir};

        search_controller_detail::ReconcileComputedDirectoryAttributes(back_results, front_results);

        CHECK_EQ(back_results[0].fileSize, 8192U);
        CHECK_EQ(back_results[0].folderFileCount, 3U);
        CHECK_EQ(back_results[0].lastModificationTime.dwLowDateTime, loaded_time.dwLowDateTime);
        CHECK_EQ(back_results[0].lastModificationDisplay, "loaded");
        CHECK(search_controller_detail::AreSearchResultsEqual(front_results, back_results));
    }

    SUBCASE("Bug 1: Copies lazy-loaded fileSize and lastModificationTime for regular files") {
        const FILETIME loaded_time{999, 888};

        SearchResult front_file = CreateDummySearchResult(10, "C:\\test\\file.txt", kFileSizeNotLoaded, 0);
        front_file.isDirectory = false;
        front_file.fileSize = 1048576;
        front_file.fileSizeDisplay = "1.0 MB";
        front_file.lastModificationTime = loaded_time;
        front_file.lastModificationDisplay = "2026-03-31 10:00";

        SearchResult back_file = CreateDummySearchResult(10, "C:\\test\\file.txt", kFileSizeNotLoaded, 0);
        back_file.isDirectory = false;
        back_file.fileSize = kFileSizeNotLoaded;
        back_file.lastModificationTime = kFileTimeNotLoaded;

        std::vector<SearchResult> front_results = {front_file};
        std::vector<SearchResult> back_results = {back_file};

        // Before reconciliation, front and back are not equal
        CHECK_FALSE(search_controller_detail::AreSearchResultsEqual(front_results, back_results));

        search_controller_detail::ReconcileComputedDirectoryAttributes(back_results, front_results);

        CHECK_EQ(back_results[0].fileSize, 1048576U);
        CHECK_EQ(back_results[0].fileSizeDisplay, "1.0 MB");
        CHECK_EQ(back_results[0].lastModificationTime.dwLowDateTime, loaded_time.dwLowDateTime);
        CHECK_EQ(back_results[0].lastModificationTime.dwHighDateTime, loaded_time.dwHighDateTime);
        CHECK_EQ(back_results[0].lastModificationDisplay, "2026-03-31 10:00");
        // After reconciliation, front and back match, allowing skip-swap optimization
        CHECK(search_controller_detail::AreSearchResultsEqual(front_results, back_results));
    }
}

TEST_CASE("SearchController::PollResults falls back to Filename sort during in-flight async sort (Bug 2)") {
    MarkCurrentThreadAsUI();
    FileIndex index;
    index.Insert(ntfs_file_reference::NtfsFileReference(10), ntfs_file_reference::NtfsFileReference(0), "beta.txt", false, {100, 0}, kFileSizeNotLoaded);
    index.Insert(ntfs_file_reference::NtfsFileReference(20), ntfs_file_reference::NtfsFileReference(0), "alpha.txt", false, {200, 0}, kFileSizeNotLoaded);
    index.RecomputeAllPaths();

    SearchWorker worker(index);
    SearchController controller;
    GuiState state;

    SearchParams params;
    params.filenameInput = "*.txt";
    worker.StartSearch(params);
    WaitForWorkerResults(worker);

    // Set UI sort to Size (which uses async sorting) and simulate an in-flight async sort
    state.async_sort_.last_sort_column = ResultColumn::Size;
    state.async_sort_.last_sort_direction = ImGuiSortDirection_Ascending;
    state.async_sort_.sort_ready_state_ = SortReadyState::Loading;

    controller.PollResults(state, worker, nullptr, index, false);

    REQUIRE_EQ(state.result_pool_->Results().size(), 2U);
    // Because an async sort was in-flight, PollResults fell back to Filename sort:
    // "alpha.txt" comes before "beta.txt"
    CHECK_EQ(state.result_pool_->Results()[0].GetFilename(), "alpha");
    CHECK_EQ(state.result_pool_->Results()[1].GetFilename(), "beta");
    // search_pipeline.results_presorted_on_commit must be false so HandleTableSorting won't skip re-sorting when attributes finish loading
    CHECK_FALSE(state.search_pipeline.results_presorted_on_commit);

    state.async_sort_.sort_ready_state_ = SortReadyState::Idle;
}
