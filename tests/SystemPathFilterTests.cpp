#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstdint>
#include <type_traits>

#include "doctest/doctest.h"
#include "index/SystemPathFilter.h"

using system_path_filter::FilteredDirectory;
using system_path_filter::FilteredDirTracker;
using system_path_filter::IsSystemPrefixedName;

// The safety property of the strong type: raw FRNs never convert implicitly,
// so a full 64-bit reference cannot slip into the record-number tracker.
static_assert(!std::is_convertible_v<std::uint64_t, FilteredDirectory>,
              "FilteredDirectory must require explicit construction");
static_assert(std::is_trivially_copyable_v<FilteredDirectory>,
              "FilteredDirectory must stay a trivially copyable value type");

TEST_SUITE("SystemPathFilter - IsSystemPrefixedName") {

  TEST_CASE("empty name is not system-prefixed") {
    CHECK_FALSE(IsSystemPrefixedName(""));
  }

  TEST_CASE("dollar-prefixed NTFS metadata names are system-prefixed") {
    CHECK(IsSystemPrefixedName("$MFT"));
    CHECK(IsSystemPrefixedName("$LogFile"));
    CHECK(IsSystemPrefixedName("$Extend"));
    CHECK(IsSystemPrefixedName("$Recycle.Bin"));
    CHECK(IsSystemPrefixedName("$R1A2B3C.docx"));
  }

  TEST_CASE("regular names are not system-prefixed") {
    CHECK_FALSE(IsSystemPrefixedName("report.docx"));
    CHECK_FALSE(IsSystemPrefixedName("S-1-5-21-1234-5678-9012-567"));
    CHECK_FALSE(IsSystemPrefixedName("C:/Users/foo"));
  }

  TEST_CASE("only the first character is significant") {
    // A '$' later in the name must not trigger the prefix predicate.
    CHECK_FALSE(IsSystemPrefixedName("a$b"));
    CHECK_FALSE(IsSystemPrefixedName("my$Recycle.Bin"));
  }
}

TEST_SUITE("SystemPathFilter - FilteredDirTracker") {

  TEST_CASE("starts empty") {
    FilteredDirTracker tracker;
    CHECK(tracker.Size() == 0U);
    CHECK_FALSE(tracker.IsFilteredChild(FilteredDirectory(42U)));
  }

  TEST_CASE("marked directories are reported as filtered children") {
    FilteredDirTracker tracker;
    tracker.MarkFilteredDir(FilteredDirectory(33U));  // e.g. $Recycle.Bin
    CHECK(tracker.Size() == 1U);
    CHECK(tracker.IsFilteredChild(FilteredDirectory(33U)));
    CHECK_FALSE(tracker.IsFilteredChild(FilteredDirectory(34U)));
  }

  TEST_CASE("marking the same directory twice does not grow the tracker") {
    FilteredDirTracker tracker;
    tracker.MarkFilteredDir(FilteredDirectory(33U));
    tracker.MarkFilteredDir(FilteredDirectory(33U));
    CHECK(tracker.Size() == 1U);
  }

  TEST_CASE("EraseOnDelete removes a tracked directory") {
    FilteredDirTracker tracker;
    tracker.MarkFilteredDir(FilteredDirectory(33U));
    tracker.MarkFilteredDir(FilteredDirectory(34U));
    tracker.EraseOnDelete(FilteredDirectory(33U));
    CHECK_FALSE(tracker.IsFilteredChild(FilteredDirectory(33U)));
    CHECK(tracker.IsFilteredChild(FilteredDirectory(34U)));
    CHECK(tracker.Size() == 1U);
  }

  TEST_CASE("Clear removes all tracked directories") {
    FilteredDirTracker tracker;
    tracker.MarkFilteredDir(FilteredDirectory(33U));
    tracker.MarkFilteredDir(FilteredDirectory(34U));
    tracker.Clear();
    CHECK(tracker.Size() == 0U);
    CHECK_FALSE(tracker.IsFilteredChild(FilteredDirectory(33U)));
    CHECK_FALSE(tracker.IsFilteredChild(FilteredDirectory(34U)));
  }

  TEST_CASE("Reserve keeps the tracker empty and functional") {
    FilteredDirTracker tracker;
    tracker.Reserve(64U);
    CHECK(tracker.Size() == 0U);
    tracker.MarkFilteredDir(FilteredDirectory(33U));
    CHECK(tracker.IsFilteredChild(FilteredDirectory(33U)));
  }

  TEST_CASE("trackers are independent value types") {
    FilteredDirTracker first;
    FilteredDirTracker second;
    first.MarkFilteredDir(FilteredDirectory(33U));
    CHECK(first.IsFilteredChild(FilteredDirectory(33U)));
    CHECK_FALSE(second.IsFilteredChild(FilteredDirectory(33U)));
  }
}
