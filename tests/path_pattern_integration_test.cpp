#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
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

  SUBCASE("Advanced path pattern features") {
    auto matcher = search_pattern_utils::CreatePathMatcher(
        R"(pp:C:/Users/J[a-z]les/project/\w{4}.cpp)", true);

    CHECK(matcher("C:/Users/Jules/project/main.cpp"));
    CHECK_FALSE(matcher("C:/Users/Jules/project/main.h"));
    CHECK_FALSE(matcher("C:/Users/Jules/project/toolong.cpp"));
    CHECK_FALSE(matcher("C:/Users/Kules/project/main.cpp"));
  }
}
