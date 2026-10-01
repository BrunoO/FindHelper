#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "search/FilenameTrigramExtractor.h"
#include <string_view>
#include <vector>

TEST_SUITE("FilenameTrigramExtractor") {

  TEST_CASE("PackTrigram - bit layout") {
    const filename_trigram::TrigramKey key = filename_trigram::PackTrigram('a', 'b', 'c');
    CHECK(((key >> 16) & 0xFF) == static_cast<unsigned char>('a'));
    CHECK(((key >> 8) & 0xFF) == static_cast<unsigned char>('b'));
    CHECK((key & 0xFF) == static_cast<unsigned char>('c'));
  }

  TEST_CASE("ExtractTrigrams - short strings return empty") {
    CHECK(filename_trigram::ExtractTrigrams("").empty());
    CHECK(filename_trigram::ExtractTrigrams("a").empty());
    CHECK(filename_trigram::ExtractTrigrams("ab").empty());
  }

  TEST_CASE("ExtractTrigrams - exact 3 characters") {
    const auto trigrams = filename_trigram::ExtractTrigrams("abc");
    REQUIRE(trigrams.size() == 1);
    CHECK(trigrams[0] == filename_trigram::PackTrigram('a', 'b', 'c'));
  }

  TEST_CASE("ExtractTrigrams - case normalization") {
    const auto lower = filename_trigram::ExtractTrigrams("test.txt");
    const auto upper = filename_trigram::ExtractTrigrams("TEST.TXT");
    const auto mixed = filename_trigram::ExtractTrigrams("TeSt.TxT");

    REQUIRE(lower.size() == 6);
    CHECK(lower == upper);
    CHECK(lower == mixed);
  }

  TEST_CASE("ExtractTrigrams - deduplication") {
    // "aaaaa" has substrings "aaa", "aaa", "aaa" -> must deduplicate to single "aaa"
    const auto trigrams = filename_trigram::ExtractTrigrams("aaaaa");
    REQUIRE(trigrams.size() == 1);
    CHECK(trigrams[0] == filename_trigram::PackTrigram('a', 'a', 'a'));
  }

  TEST_CASE("ExtractTrigrams - punctuation and extensions") {
    const auto trigrams = filename_trigram::ExtractTrigrams("c++_17.hpp");
    // "c++", "++_", "+_1", "_17", "17.", "7.h", ".hp", "hpp" -> 8 unique trigrams
    CHECK(trigrams.size() == 8);
  }

  TEST_CASE("ExtractTrigrams - buffer reuse preserves capacity") {
    std::vector<filename_trigram::TrigramKey> scratch;
    scratch.reserve(100);
    const size_t initial_cap = scratch.capacity();

    filename_trigram::ExtractTrigrams("test.txt", scratch);
    CHECK(scratch.size() == 6);
    CHECK(scratch.capacity() >= initial_cap);

    filename_trigram::ExtractTrigrams("ab", scratch);
    CHECK(scratch.empty());
    CHECK(scratch.capacity() >= initial_cap);
  }

}
