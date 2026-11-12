#include "search/SearchPatternUtils.h"
#include "doctest/doctest.h"

TEST_CASE("PathPatternMatcher Integration") {
  SUBCASE("pp prefix is routed to PathPatternMatcher") {
    auto matcher =
        search_pattern_utils::CreatePathMatcher("pp:C:/Users/**/*.cpp", false);

    CHECK(matcher("C:/Users/Jules/project/main.cpp"));
    CHECK(matcher("C:/Users/Jules/another/path/to/file.cpp"));
    CHECK_FALSE(matcher("C:/Users/Jules/project/main.h"));
    CHECK_FALSE(matcher("D:/Another/Drive/file.cpp"));
  }

  SUBCASE("Case-sensitive matching with pp prefix") {
    auto matcher =
        search_pattern_utils::CreatePathMatcher("pp:C:/Users/**/*.CPP", true);

    CHECK(matcher("C:/Users/Jules/project/main.CPP"));
    CHECK_FALSE(matcher("C:/Users/Jules/project/main.cpp"));
  }

  SUBCASE("Empty pattern with pp prefix") {
    auto matcher = search_pattern_utils::CreatePathMatcher("pp:", false);
    CHECK(matcher("anything"));
  }

  SUBCASE("Wildcard and literal bracket path pattern features") {
    auto matcher = search_pattern_utils::CreatePathMatcher(
        R"(pp:C:/Users/J?les/project/????.cpp)", true);

    CHECK(matcher("C:/Users/Jules/project/main.cpp"));
    CHECK_FALSE(matcher("C:/Users/Jules/project/main.h"));
    CHECK_FALSE(matcher("C:/Users/Jules/project/toolong.cpp"));
    CHECK_FALSE(matcher("C:/Users/Kules/project/main.cpp"));

    // Brackets match literally
    auto lit_matcher = search_pattern_utils::CreatePathMatcher(
        R"(pp:C:/Users/J[a-z]les/project/*.cpp)", true);
    CHECK(lit_matcher("C:/Users/J[a-z]les/project/main.cpp"));
    CHECK_FALSE(lit_matcher("C:/Users/Jules/project/main.cpp"));
  }
}
