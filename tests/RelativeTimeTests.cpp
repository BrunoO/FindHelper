#include "doctest/doctest.h"
#include "utils/StringUtils.h"
#include <cstdint>

TEST_SUITE("StringUtils Relative Time") {
  TEST_CASE("FormatRelativeTime") {
    const std::int64_t now = 1000000000000; // Arbitrary base time

    CHECK(FormatRelativeTime(now, now) == "Just now");
    CHECK(FormatRelativeTime(now - 30000, now) == "Just now");
    CHECK(FormatRelativeTime(now - 60000, now) == "1m ago");
    CHECK(FormatRelativeTime(now - 300000, now) == "5m ago");
    CHECK(FormatRelativeTime(now - 3600000, now) == "1h ago");
    CHECK(FormatRelativeTime(now - 3600000 * 2, now) == "2h ago");
    CHECK(FormatRelativeTime(now - 3600000 * 24, now) == "Yesterday");
    CHECK(FormatRelativeTime(now - 3600000 * 24 * 2, now) == "2 days ago");
    CHECK(FormatRelativeTime(now - 3600000 * 24 * 7, now) == "7 days ago");
    
    CHECK(FormatRelativeTime(0, now) == "Never");
    CHECK(FormatRelativeTime(now + 1000, now) == "Future");
  }
}
