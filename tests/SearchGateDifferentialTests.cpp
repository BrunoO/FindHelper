#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "index/FileIndex.h"
#include "index/IndexFromFilePopulator.h"
#include "tests/SearchGateDifferential.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

const std::vector<gate_differential::BatteryEntry>& GetTier1Battery() {
  static const std::vector<gate_differential::BatteryEntry> kBattery = {
      // 1. Plain CI substring: case-fold consistency
      {1, {"", "/dev", {}, false, false}, "plain CI substring", "/mnt/DEV/shm/x.txt", false},
      // 2. Plain CS substring
      {2, {"", "/dev", {}, false, true}, "plain CS substring", "/dev/null", false},
      // 3. Finding 1: lit** leaf unpinned (sound shape takes scan fallback, no FN)
      {3, {"", "pp:**/dev**", {}, false, false}, "path pp:**/dev** leaf unpinned", "/a/dev.deep/report.txt", false},
      // 4. Sound shape (leaf == **)
      {4, {"", "pp:**/include/**", {}, false, false}, "path pp:**/include/** leaf wildcard", "/x/include/a.h", false},
      // 5. Finding 2: glob-with-separator dir-component literal (sound shape takes scan)
      {5, {"", "src/*.cpp", {}, false, false}, "path glob with separator", "/w/src/x.cpp.d/report.txt", false},
      // 6. Sound suffix narrowing (*.cpp pins suffix)
      {6, {"", "*.cpp", {}, false, false}, "path suffix glob", "/any/depth/deep/file.cpp", true},
      // 7. Finding 3: + character
      {7, {"", "pp:/x/log+", {}, false, false}, "path leaf with + character", "/x/log+", false},
      // 8. Finding 4: escape prefilter (tested with bypass seam)
      {8, {"", R"(rs:\x41ndroid)", {}, false, false}, "regex with hex escape", "/usr/Android.txt", false},
      // 9. Extension postings x path verify
      {9, {"", "/etc", {"conf"}, false, false}, "path with extension filter", "/etc/svc/ntp.conf", true},
      // 10. Extension only search
      {10, {"", "", {"cpp"}, false, false}, "extension only query", "/a/b.cpp", true},
      // 11. Filename gate + path verify
      {11, {"tty", "/dev", {}, false, false}, "filename and path query", "/dev/ttyS0", true},
      // 12. folders_only post-gate
      {12, {"", "/dev", {}, true, false}, "folders_only path query", "/dev/nulls", false},
      // 13. Filename-side leaf rule (sound side)
      {13, {"pp:*dev**", "", {}, false, false}, "filename pp leaf pattern", "/x/y/devmon.exe", true},
      // 14. Windows-style pattern text
      {14, {"", R"(pp:C:\ab**)", {}, false, false}, "windows path pattern", R"(C:\ab\test.txt)", false},
      // 15. Shape #3 case-sensitive
      {15, {"", "pp:**/dev**", {}, false, true}, "path pp:**/dev** CS", "/a/dev.deep/report.txt", false},
  };
  return kBattery;
}

}  // namespace

TEST_SUITE("SearchGateDifferential") {

  TEST_CASE("Tier 1 - Mini fixture isolation battery") {
    FileIndex file_index;
    const bool loaded = populate_index_from_file("tests/data/differential_gate_fixture.txt", file_index);
    REQUIRE(loaded);
    file_index.RecomputeAllPaths();

    const bool ready = gate_differential::WaitForIndexesReady(file_index, 10000);
    REQUIRE(ready);

    const auto& battery = GetTier1Battery();
    for (const auto& entry : battery) {
      CAPTURE(entry.id);
      CAPTURE(entry.hazard_description);
      const bool check_ok = gate_differential::DifferentialCheck(file_index, entry, std::cerr);
      CHECK(check_ok);
    }
  }

  TEST_CASE("Tier 2 - Heavy std corpus battery (env opt-in)") {
    if (const char* const heavy_env = std::getenv("FINDHELPER_DIFFERENTIAL_HEAVY");
        heavy_env == nullptr || std::string_view(heavy_env) != "1") {
      MESSAGE("Tier 2 heavy battery skipped (set FINDHELPER_DIFFERENTIAL_HEAVY=1 to run)");
      return;
    }

    FileIndex file_index;
    if (const bool loaded = populate_index_from_file("tests/data/std-linux-filesystem.txt", file_index);
        !loaded) {
      MESSAGE("Skipping Tier 2: std-linux-filesystem.txt not found");
      return;
    }
    file_index.RecomputeAllPaths();

    const bool ready = gate_differential::WaitForIndexesReady(file_index, 30000);
    REQUIRE(ready);

    // Selected high-value battery entries for std dataset
    const std::vector<gate_differential::BatteryEntry> heavy_battery = {
        {1, {"", "/dev", {}, false, false}, "plain CI substring", "/dev/", false},
        {2, {"", "/dev", {}, false, true}, "plain CS substring", "/dev/", false},
        {3, {"", "pp:**/dev**", {}, false, false}, "path pp:**/dev**", "/dev/", false},
        {5, {"", "src/*.cpp", {}, false, false}, "path glob with separator", "", false},
        {9, {"", "/etc", {"conf"}, false, false}, "path with extension", "", true},
        {10, {"", "", {"cpp"}, false, false}, "extension only", "", true},
    };

    for (const auto& entry : heavy_battery) {
      CAPTURE(entry.id);
      CAPTURE(entry.hazard_description);
      const bool check_ok = gate_differential::DifferentialCheck(file_index, entry, std::cerr);
      CHECK(check_ok);
    }
  }

}
