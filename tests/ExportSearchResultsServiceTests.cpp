/**
 * @file ExportSearchResultsServiceTests.cpp
 * @brief Unit tests for ExportSearchResultsService CSV escaping and ExportToCsv.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "doctest/doctest.h"
#include "index/FileIndex.h"
#include "search/ExportSearchResultsService.h"
#include "search/SearchTypes.h"

using search::ExportSearchResultsService;

namespace {

// Build a SearchResult with stable path storage and pre-filled display strings
// so EnsureDisplayStringsPopulated returns early without touching the index.
SearchResult MakeResult(const std::string& path_storage,
                        std::string_view size_display,
                        std::string_view time_display) {
  SearchResult r;
  r.fullPath = std::string_view(path_storage);
  r.fileSizeDisplay.assign(size_display);
  r.lastModificationDisplay.assign(time_display);
  // "C:/dir/file.txt" -> filename at 6, extension at 11
  const size_t last_slash = path_storage.find_last_of("/\\");
  r.filename_offset = (last_slash != std::string::npos) ? last_slash + 1U : 0U;
  const size_t last_dot = path_storage.find_last_of('.');
  const size_t name_start = r.filename_offset;
  r.extension_offset = (last_dot != std::string::npos && last_dot > name_start)
                           ? last_dot + 1U
                           : std::string_view::npos;
  r.isDirectory = false;
  return r;
}

std::string ReadFileContents(const std::string& path) {
  const std::ifstream f(path);
  std::ostringstream out;
  out << f.rdbuf();
  return out.str();
}

}  // namespace

TEST_CASE("ExportSearchResultsService::EscapeCsv basic cases") {
  CHECK(ExportSearchResultsService::EscapeCsv("") == "");
  CHECK(ExportSearchResultsService::EscapeCsv("plain") == "plain");
  CHECK(ExportSearchResultsService::EscapeCsv("with,comma") == "\"with,comma\"");
  CHECK(ExportSearchResultsService::EscapeCsv("with\nnewline") == "\"with\nnewline\"");
  CHECK(ExportSearchResultsService::EscapeCsv("with\"quote") == "\"with\"\"quote\"");
}

TEST_CASE("ExportSearchResultsService::EscapeCsv formula injection prevention") {
  CHECK(ExportSearchResultsService::EscapeCsv("=SUM(A1:A2)") == "\"'=SUM(A1:A2)\"");
  CHECK(ExportSearchResultsService::EscapeCsv("+CMD") == "\"'+CMD\"");
  CHECK(ExportSearchResultsService::EscapeCsv("-CMD") == "\"'-CMD\"");
  CHECK(ExportSearchResultsService::EscapeCsv("@user") == "\"'@user\"");
}

TEST_CASE("ExportSearchResultsService::ExportToCsv empty path returns false") {
  const FileIndex index(nullptr);
  const std::vector<SearchResult> results;
  CHECK_FALSE(ExportSearchResultsService::ExportToCsv(results, index, ""));
  CHECK_FALSE(ExportSearchResultsService::ExportToCsv(results, index, std::string_view()));
}

TEST_CASE("ExportSearchResultsService::ExportToCsv writes header and rows") {
  const std::string out_path = "export_test_csv_output.csv";
  const std::string path1 = "C:/path/file1.txt";
  const std::string path2 = "C:/path/file2.csv";
  const SearchResult r1 = MakeResult(path1, "1 KB", "2024-03-14 10:00");
  const SearchResult r2 = MakeResult(path2, "2 KB", "2024-03-14 11:00");
  const std::vector<SearchResult> results = {r1, r2};

  const FileIndex index(nullptr);
  CHECK(ExportSearchResultsService::ExportToCsv(results, index, out_path));

  const std::string contents = ReadFileContents(out_path);
  const std::string expected_header = "Filename,Size,Last Modified,Full Path,Extension\n";
  CHECK(contents.substr(0, expected_header.size()) == expected_header);

  // First data line: file1,1 KB,2024-03-14 10:00,C:/path/file1.txt,txt
  const size_t after_header = expected_header.size();
  CHECK(contents.find("file1.txt", after_header) != std::string::npos);
  CHECK(contents.find("1 KB", after_header) != std::string::npos);
  CHECK(contents.find("file2.csv", after_header) != std::string::npos);
  CHECK(contents.find("2 KB", after_header) != std::string::npos);

  // Remove temp file (best-effort)
  std::remove(out_path.c_str());
}

TEST_CASE("ExportSearchResultsService::ExportToCsv empty results writes only header") {
  const std::string out_path = "export_test_csv_empty.csv";
  const std::vector<SearchResult> results;

  const FileIndex index(nullptr);
  CHECK(ExportSearchResultsService::ExportToCsv(results, index, out_path));

  const std::string contents = ReadFileContents(out_path);
  CHECK(contents == "Filename,Size,Last Modified,Full Path,Extension\n");

  std::remove(out_path.c_str());
}
