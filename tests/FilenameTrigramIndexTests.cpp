#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "path/PathStorage.h"
#include "search/FilenameTrigramExtractor.h"
#include "search/FilenameTrigramIndex.h"
#include <string_view>
#include <vector>

TEST_SUITE("FilenameTrigramIndex") {

  TEST_CASE("Empty index queries") {
    const filename_trigram::FilenameTrigramIndex index;
    CHECK(index.GetMemoryUsageBytes() == 0);

    // Empty trigram list returns nullopt
    CHECK(!index.QueryCandidates({}).has_value());

    // Query on non-existent trigram returns nullopt (short-circuit)
    const auto trigrams = filename_trigram::ExtractTrigrams("test");
    CHECK(!index.QueryCandidates(trigrams).has_value());
  }

  TEST_CASE("AddEntry and QueryCandidates") {
    filename_trigram::FilenameTrigramIndex index;
    index.AddEntry(10, "alpha_document.txt");
    index.AddEntry(20, "beta_document.txt");
    index.AddEntry(30, "other_file.cpp");

    CHECK(index.GetMemoryUsageBytes() > 0);

    // Query "document" -> both 10 and 20 match
    const auto doc_trigrams = filename_trigram::ExtractTrigrams("document");
    const auto doc_candidates = index.QueryCandidates(doc_trigrams);
    REQUIRE(doc_candidates.has_value());
    CHECK(doc_candidates->cardinality() == 2);
    CHECK(doc_candidates->contains(10));
    CHECK(doc_candidates->contains(20));
    CHECK(!doc_candidates->contains(30));

    // Query "alpha" -> only 10 matches
    const auto alpha_trigrams = filename_trigram::ExtractTrigrams("alpha");
    const auto alpha_candidates = index.QueryCandidates(alpha_trigrams);
    REQUIRE(alpha_candidates.has_value());
    CHECK(alpha_candidates->cardinality() == 1);
    CHECK(alpha_candidates->contains(10));

    // Query "gamma" -> 0 matches, short-circuit
    const auto gamma_trigrams = filename_trigram::ExtractTrigrams("gamma");
    CHECK(!index.QueryCandidates(gamma_trigrams).has_value());
  }

  TEST_CASE("RemoveEntry") {
    filename_trigram::FilenameTrigramIndex index;
    index.AddEntry(1, "my_report.pdf");
    index.AddEntry(2, "my_notes.txt");

    const auto my_trigrams = filename_trigram::ExtractTrigrams("my_");
    auto res1 = index.QueryCandidates(my_trigrams);
    REQUIRE(res1.has_value());
    CHECK(res1->cardinality() == 2);

    // Remove row 1
    index.RemoveEntry(1, "my_report.pdf");
    auto res2 = index.QueryCandidates(my_trigrams);
    REQUIRE(res2.has_value());
    CHECK(res2->cardinality() == 1);
    CHECK(res2->contains(2));
    CHECK(!res2->contains(1));
  }

  TEST_CASE("Clear resets index") {
    filename_trigram::FilenameTrigramIndex index;
    index.AddEntry(1, "sample.txt");
    const auto trigrams = filename_trigram::ExtractTrigrams("sample");
    CHECK(index.QueryCandidates(trigrams).has_value());

    index.Clear();
    CHECK(!index.QueryCandidates(trigrams).has_value());
    CHECK(index.GetMemoryUsageBytes() == 0);
  }

  TEST_CASE("Build from PathStorage SoAView") {
    PathStorage storage;
    (void)storage.InsertPath(1, "C:\\docs\\report2026.docx", false, std::nullopt);
    (void)storage.InsertPath(2, "C:\\docs\\notes.txt", false, std::nullopt);
    (void)storage.InsertPath(3, "C:\\code\\main.cpp", false, std::nullopt);

    // Mark row 2 as deleted
    (void)storage.RemovePathByIndex(1); // row 1 in 0-indexed SoA is "notes.txt"

    filename_trigram::FilenameTrigramIndex index;
    index.Build(storage.GetReadOnlyView(), storage.GetStorageSize());

    // "report" should match row 0
    const auto rep_trigrams = filename_trigram::ExtractTrigrams("report");
    const auto rep_res = index.QueryCandidates(rep_trigrams);
    REQUIRE(rep_res.has_value());
    CHECK(rep_res->cardinality() == 1);
    CHECK(rep_res->contains(0));

    // "notes" was deleted -> should return nullopt or empty
    const auto notes_trigrams = filename_trigram::ExtractTrigrams("notes");
    CHECK(!index.QueryCandidates(notes_trigrams).has_value());
  }

}
