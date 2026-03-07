#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "utils/SimpleRegex.h"
#include "doctest/doctest.h"
#include <string>
#include <string_view>
#include <vector>

TEST_SUITE("SimpleRegex") {

  struct MatchTestCase {
    std::string pattern;
    std::string text;
    bool expected;
  };

  TEST_CASE("RegExMatch") {
    std::vector<MatchTestCase> test_cases = {{"a", "a", true},
                                             {"test", "test", true},
                                             {"hello", "hello world", true},
                                             {"a", "b", false},
                                             {"xyz", "testing", false},
                                             {"test", "testing", true},
                                             {".", "a", true},
                                             {".", "1", true},
                                             {"a.c", "abc", true},
                                             {"a*", "", true},
                                             {"a*", "a", true},
                                             {"a*", "aa", true},
                                             {"a*b", "b", true},
                                             {"a*b", "ab", true},
                                             {".*", "hello world", true},
                                             {"^a", "abc", true},
                                             {"^a", "ba", false},
                                             {"a$", "ba", true},
                                             {"a$", "ab", false},
                                             {"^a$", "a", true},
                                             {"^a$", "ab", false},
                                             {"", "test", true},
                                             {"a.*b", "a123b", true},
                                             {"a.*b", "a", false},
                                             {"test", "this is a test", true}};

    for (const auto &tc : test_cases) {
      DOCTEST_SUBCASE((tc.pattern + " on " + tc.text).c_str()) {
        CHECK(simple_regex::RegExMatch(tc.pattern, tc.text) == tc.expected);
      }
    }
  }

  TEST_CASE("RegExMatchI") {
    std::vector<MatchTestCase> test_cases = {
        {"a", "A", true},         {"A", "a", true},
        {"test", "TEST", true},   {"TEST", "test", true},
        {"HeLlO", "hElLo", true}, {"a.c", "AbC", true},
        {"a*", "A", true},        {"^a", "A", true},
        {"a$", "bA", true},       {"test.*world", "TEST hello WORLD", true}};

    for (const auto &tc : test_cases) {
      DOCTEST_SUBCASE((tc.pattern + " on " + tc.text).c_str()) {
        CHECK(simple_regex::RegExMatchI(tc.pattern, tc.text) == tc.expected);
      }
    }
  }

  TEST_CASE("GlobMatch") {
    std::vector<MatchTestCase> test_cases = {{"test", "test", true},
                                             {"test", "testing", false},
                                             {"*", "", true},
                                             {"*", "test", true},
                                             {"test*", "testing", true},
                                             {"*test", "pretest", true},
                                             {"test*ing", "test123ing", true},
                                             {"?", "a", true},
                                             {"?", "ab", false},
                                             {"test?", "test1", true},
                                             {"*.txt", "file.txt", true},
                                             {"file.*", "file.doc", true},
                                             {"*/*", "path/file", true},
                                             {"", "", true},
                                             {"", "a", false}};

    for (const auto &tc : test_cases) {
      DOCTEST_SUBCASE((tc.pattern + " on " + tc.text).c_str()) {
        CHECK(simple_regex::GlobMatch(tc.pattern, tc.text) == tc.expected);
      }
    }
  }

  TEST_CASE("GlobMatchI") {
    std::vector<MatchTestCase> test_cases = {
        {"test", "TEST", true},      {"TEST", "test", true},
        {"test*", "TESTING", true},  {"*.txt", "FILE.TXT", true},
        {"*.TXT", "file.txt", true}, {"Test*", "testing", true}};

    for (const auto &tc : test_cases) {
      DOCTEST_SUBCASE((tc.pattern + " on " + tc.text).c_str()) {
        CHECK(simple_regex::GlobMatchI(tc.pattern, tc.text) == tc.expected);
      }
    }
  }

  TEST_CASE("CharEqualI") {
    CHECK(simple_regex::CharEqualI('a', 'A'));
    CHECK(simple_regex::CharEqualI('A', 'a'));
    CHECK(simple_regex::CharEqualI('0', '0'));
    CHECK_FALSE(simple_regex::CharEqualI('a', 'b'));
  }
}
