/**
 * @file SearchResultSortTests.cpp
 * @brief Unit tests for search result sorting logic
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include <algorithm>
#include <string>
#include <vector>

#include "doctest/doctest.h"
#include "imgui.h"
#include "search/SearchResultUtils.h"
#include "search/SearchTypes.h"
#include "ui/ResultsTable.h"

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

    SUBCASE("Sort by Filename (Ascending)") {
        auto comp = CreateSearchResultComparator(ResultColumn::Filename, ImGuiSortDirection_Ascending);
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
