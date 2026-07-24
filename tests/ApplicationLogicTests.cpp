#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "core/ApplicationLogic.h"

TEST_CASE("ShouldEnablePeriodicRecrawl is false when USN monitor is primary with crawl folder") {
  CHECK_FALSE(application_logic::ShouldEnablePeriodicRecrawl(/*use_usn_monitor=*/true,
                                                              /*has_crawl_folder=*/true));
}

TEST_CASE("ShouldEnablePeriodicRecrawl is false when USN monitor is primary without crawl folder") {
  CHECK_FALSE(application_logic::ShouldEnablePeriodicRecrawl(/*use_usn_monitor=*/true,
                                                              /*has_crawl_folder=*/false));
}

TEST_CASE("ShouldEnablePeriodicRecrawl is true for folder crawl with crawl folder") {
  CHECK(application_logic::ShouldEnablePeriodicRecrawl(/*use_usn_monitor=*/false,
                                                       /*has_crawl_folder=*/true));
}

TEST_CASE("ShouldEnablePeriodicRecrawl is false for folder crawl without crawl folder") {
  CHECK_FALSE(application_logic::ShouldEnablePeriodicRecrawl(/*use_usn_monitor=*/false,
                                                              /*has_crawl_folder=*/false));
}
