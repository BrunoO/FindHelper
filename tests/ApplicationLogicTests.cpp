#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "core/ApplicationLogic.h"
#include "core/Settings.h"

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

TEST_CASE("ShouldEnablePeriodicRecrawl evaluates auto_crawl_folder fallback correctly") {
  // When settings crawlFolder is empty but auto_crawl_folder is non-empty,
  // has_crawl_folder parameter passed to ShouldEnablePeriodicRecrawl is true.
  const bool has_crawl_folder_with_auto_fallback = true;
  CHECK(application_logic::ShouldEnablePeriodicRecrawl(/*use_usn_monitor=*/false,
                                                       has_crawl_folder_with_auto_fallback));
}

TEST_CASE("ShouldEnablePeriodicRecrawl is false when crawl folder is empty and no auto crawl folder") {
  CHECK_FALSE(application_logic::ShouldEnablePeriodicRecrawl(/*use_usn_monitor=*/false,
                                                              /*has_crawl_folder=*/false));
}

TEST_CASE("ShouldGateSearchOnUsnPopulation is true only for USN-primary sessions while populating") {
  CHECK(application_logic::ShouldGateSearchOnUsnPopulation(/*use_usn_monitor=*/true,
                                                           /*usn_populating=*/true));
  CHECK_FALSE(application_logic::ShouldGateSearchOnUsnPopulation(/*use_usn_monitor=*/true,
                                                                 /*usn_populating=*/false));
  CHECK_FALSE(application_logic::ShouldGateSearchOnUsnPopulation(/*use_usn_monitor=*/false,
                                                                 /*usn_populating=*/true));
  CHECK_FALSE(application_logic::ShouldGateSearchOnUsnPopulation(/*use_usn_monitor=*/false,
                                                                 /*usn_populating=*/false));
}
