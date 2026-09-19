#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <atomic>
#include <shared_mutex>
#include <vector>

#include "TestHelpers.h"
#include "crawler/IndexOperations.h"
#include "index/FileIndexStorage.h"
#include "index/NtfsFileReference.h"
#include "path/PathOperations.h"
#include "path/PathStorage.h"

// Test ids are synthetic small ints; wrap once per call so cases name the
// reference shape explicitly (see NtfsFileReference).
using ntfs_file_reference::NtfsFileReference;

TEST_CASE("IndexOperations::Insert creates file entry") {
  test_helpers::TestIndexOperationsFixture fixture;

  // Insert root directory first
  fixture.GetOperations().Insert(NtfsFileReference(1), NtfsFileReference(0), "root", true);

  // Insert a file
  fixture.GetOperations().Insert(NtfsFileReference(2), NtfsFileReference(1), "test.txt", false);

  // Verify file exists in storage
  const FileEntry* entry = fixture.GetStorage().GetEntry(2);
  CHECK(entry != nullptr);
  CHECK(entry->isDirectory == false);
  CHECK(entry->parentID.raw == 1);

  // Verify path was inserted
  const std::string path = fixture.GetPathOperations().GetPath(2);
  CHECK(path.find("test.txt") != std::string::npos);
}

TEST_CASE("IndexOperations::Insert creates directory entry") {
  test_helpers::TestIndexOperationsFixture fixture;

  // Insert root directory
  fixture.GetOperations().Insert(NtfsFileReference(1), NtfsFileReference(0), "root", true);

  // Insert a subdirectory
  fixture.GetOperations().Insert(NtfsFileReference(2), NtfsFileReference(1), "subdir", true);

  // Verify directory exists in storage
  const FileEntry* entry = fixture.GetStorage().GetEntry(2);
  CHECK(entry != nullptr);
  CHECK(entry->isDirectory == true);
  CHECK(entry->parentID.raw == 1);
}

TEST_CASE("IndexOperations::Remove removes file entry") {
  test_helpers::TestIndexOperationsFixture fixture;

  // Insert root directory
  fixture.GetOperations().Insert(NtfsFileReference(1), NtfsFileReference(0), "root", true);

  // Insert a file
  fixture.GetOperations().Insert(NtfsFileReference(2), NtfsFileReference(1), "test.txt", false);

  // Verify file exists
  CHECK(fixture.GetStorage().GetEntry(2) != nullptr);

  // Remove file
  fixture.GetOperations().Remove(NtfsFileReference(2));

  // Verify file is removed
  CHECK(fixture.GetStorage().GetEntry(2) == nullptr);

  // Verify path is marked as deleted
  CHECK(fixture.GetPathStorage().GetDeletedCount() > 0);
}

TEST_CASE("IndexOperations::Remove handles non-existent file") {
  test_helpers::TestIndexOperationsFixture fixture;

  // Try to remove non-existent file
  fixture.GetOperations().Remove(NtfsFileReference(999));

  // Should track that file was not in index
  CHECK(fixture.GetRemoveNotInIndexCount().load() == 1);
}

TEST_CASE("IndexOperations::Rename updates file name") {
  test_helpers::TestIndexOperationsFixture fixture;

  // Insert root directory
  fixture.GetOperations().Insert(NtfsFileReference(1), NtfsFileReference(0), "root", true);

  // Insert a file
  fixture.GetOperations().Insert(NtfsFileReference(2), NtfsFileReference(1), "oldname.txt", false);

  // Rename file
  const bool result = fixture.GetOperations().Rename(NtfsFileReference(2), "newname.txt");
  CHECK(result == true);

  // Verify name was updated
  const FileEntry* entry = fixture.GetStorage().GetEntry(2);
  CHECK(entry != nullptr);

  // Verify path was updated
  const std::string path = fixture.GetPathOperations().GetPath(2);
  CHECK(path.find("newname.txt") != std::string::npos);
}

TEST_CASE("IndexOperations::Rename returns false for non-existent file") {
  test_helpers::TestIndexOperationsFixture fixture;

  // Try to rename non-existent file
  const bool result = fixture.GetOperations().Rename(NtfsFileReference(999), "newname.txt");
  CHECK(result == false);
}

TEST_CASE("IndexOperations::Move updates parent directory") {
  test_helpers::TestIndexOperationsFixture fixture;

  // Insert root directory
  fixture.GetOperations().Insert(NtfsFileReference(1), NtfsFileReference(0), "root", true);

  // Insert two directories
  fixture.GetOperations().Insert(NtfsFileReference(2), NtfsFileReference(1), "dir1", true);
  fixture.GetOperations().Insert(NtfsFileReference(3), NtfsFileReference(1), "dir2", true);

  // Insert a file in dir1
  fixture.GetOperations().Insert(NtfsFileReference(4), NtfsFileReference(2), "file.txt", false);

  // Move file to dir2
  const bool result = fixture.GetOperations().Move(NtfsFileReference(4), NtfsFileReference(3));
  CHECK(result == true);

  // Verify parent was updated
  const FileEntry* entry = fixture.GetStorage().GetEntry(4);
  CHECK(entry != nullptr);
  CHECK(entry->parentID.raw == 3);

  // Verify path was updated
  const std::string path = fixture.GetPathOperations().GetPath(4);
  CHECK(path.find("dir2") != std::string::npos);
}

TEST_CASE("IndexOperations::Move returns false for non-existent file") {
  test_helpers::TestIndexOperationsFixture fixture;

  // Try to move non-existent file
  const bool result = fixture.GetOperations().Move(NtfsFileReference(999), NtfsFileReference(1));
  CHECK(result == false);
}

TEST_CASE("IndexOperations tracks and heals awaiting out-of-order children") {
  test_helpers::TestIndexOperationsFixture fixture;

  constexpr uint64_t kMissingParent = 0x00010000000000A0ULL;
  constexpr uint64_t kChild = 0x00010000000000B0ULL;

  fixture.GetOperations().Insert(NtfsFileReference(1), NtfsFileReference(0), "root", true);
  REQUIRE(fixture.GetOperations().AwaitingCount() == 0);
  REQUIRE(fixture.GetHealedAwaitingCount().load() == 0);

  // Child CREATE before parent: bare-name placeholder + awaiting tracking.
  fixture.GetOperations().Insert(NtfsFileReference(kChild), NtfsFileReference(kMissingParent), "late.txt", false);
  CHECK(fixture.GetOperations().AwaitingCount() == 1);
  CHECK(fixture.GetPathOperations().GetPath(kChild) == "late.txt");

  // Late parent INSERT heals the child in the same call (counted once).
  fixture.GetOperations().Insert(NtfsFileReference(kMissingParent), NtfsFileReference(1), "LateDir", true);
  CHECK(fixture.GetOperations().AwaitingCount() == 0);
  CHECK(fixture.GetHealedAwaitingCount().load() == 1);
  const std::string healed = fixture.GetPathOperations().GetPath(kChild);
  CHECK(healed.find("LateDir") != std::string::npos);
  CHECK(healed.find("late.txt") != std::string::npos);
  CHECK(fixture.GetOperations().CheckBareNameInvariant());
}

TEST_CASE("IndexOperations publishes ParentHealed with child and parent ids") {
  test_helpers::TestIndexOperationsFixture fixture;

  constexpr uint64_t kMissingParent = 0x00010000000000A0ULL;
  constexpr uint64_t kChild = 0x00010000000000B0ULL;

  fixture.GetOperations().Insert(NtfsFileReference(1), NtfsFileReference(0), "root", true);
  fixture.GetOperations().Insert(NtfsFileReference(kChild), NtfsFileReference(kMissingParent), "late.txt", false);
  REQUIRE(fixture.GetOperations().AwaitingCount() == 1);
  CHECK(fixture.ReceivedParentHealed().empty());

  fixture.GetOperations().Insert(NtfsFileReference(kMissingParent), NtfsFileReference(1), "LateDir", true);
  REQUIRE(fixture.ReceivedParentHealed().size() == 1U);
  CHECK(fixture.ReceivedParentHealed()[0].child_id == kChild);
  CHECK(fixture.ReceivedParentHealed()[0].parent_id == kMissingParent);
  // Counter subscribed through the same sink, so count and log agree.
  CHECK(fixture.GetHealedAwaitingCount().load() == 1);
}

TEST_CASE("IndexOperations counts each healed placeholder once, not removals") {
  test_helpers::TestIndexOperationsFixture fixture;

  constexpr uint64_t kMissingParent = 0x00010000000000A0ULL;
  constexpr uint64_t kChild = 0x00010000000000B0ULL;
  constexpr uint64_t kGrandchild = 0x00010000000000C0ULL;
  constexpr uint64_t kOtherParent = 0x00010000000000D0ULL;
  constexpr uint64_t kOrphan = 0x00010000000000E0ULL;

  fixture.GetOperations().Insert(NtfsFileReference(1), NtfsFileReference(0), "root", true);

  // Grandchild before child: the child INSERT heals the grandchild (count 1)
  // while the child itself stays awaiting on the still-missing parent.
  fixture.GetOperations().Insert(NtfsFileReference(kGrandchild), NtfsFileReference(kChild), "deep.txt", false);
  fixture.GetOperations().Insert(NtfsFileReference(kChild), NtfsFileReference(kMissingParent), "MidDir", true);
  CHECK(fixture.GetOperations().AwaitingCount() == 1);
  CHECK(fixture.GetHealedAwaitingCount().load() == 1);

  // Late parent INSERT heals the child; the already-healed grandchild follows
  // via the scoped cascade without counting again.
  fixture.GetOperations().Insert(NtfsFileReference(kMissingParent), NtfsFileReference(1), "LateDir", true);
  CHECK(fixture.GetOperations().AwaitingCount() == 0);
  CHECK(fixture.GetHealedAwaitingCount().load() == 2);
  const std::string healed = fixture.GetPathOperations().GetPath(kGrandchild);
  CHECK(healed.find("LateDir") != std::string::npos);
  CHECK(healed.find("deep.txt") != std::string::npos);

  // Leaving the buckets via Remove is not a heal: no count.
  fixture.GetOperations().Insert(NtfsFileReference(kOrphan), NtfsFileReference(kOtherParent), "orphan.txt", false);
  REQUIRE(fixture.GetOperations().AwaitingCount() == 1);
  fixture.GetOperations().Remove(NtfsFileReference(kOrphan));
  CHECK(fixture.GetOperations().AwaitingCount() == 0);
  CHECK(fixture.GetHealedAwaitingCount().load() == 2);
  CHECK(fixture.GetOperations().CheckBareNameInvariant());
}

TEST_CASE("IndexOperations::Remove drops awaiting tracking") {
  test_helpers::TestIndexOperationsFixture fixture;

  constexpr uint64_t kMissingParent = 0x00010000000000A0ULL;
  constexpr uint64_t kChild = 0x00010000000000B0ULL;

  fixture.GetOperations().Insert(NtfsFileReference(kChild), NtfsFileReference(kMissingParent), "orphan.txt", false);
  REQUIRE(fixture.GetOperations().AwaitingCount() == 1);

  fixture.GetOperations().Remove(NtfsFileReference(kChild));
  CHECK(fixture.GetOperations().AwaitingCount() == 0);
  CHECK(fixture.GetOperations().CheckBareNameInvariant());
}

TEST_CASE("IndexOperations reports awaiting age in stats") {
  test_helpers::TestIndexOperationsFixture fixture;

  constexpr uint64_t kMissingParent = 0x00010000000000A0ULL;
  constexpr uint64_t kChild = 0x00010000000000B0ULL;

  fixture.GetOperations().Insert(NtfsFileReference(1), NtfsFileReference(0), "root", true);
  fixture.GetOperations().Insert(NtfsFileReference(kChild), NtfsFileReference(kMissingParent), "late.txt", false);

  const auto stats = fixture.GetOperations().GetAwaitingStats();
  CHECK(stats.count == 1);
  // Freshly tracked: age is milliseconds-small by construction (deterministic
  // upper bound, no sleeping in tests).
  CHECK(stats.oldest_age_ms <= 60000);
  CHECK(fixture.GetOperations().CheckBareNameInvariant());

  fixture.GetOperations().Insert(NtfsFileReference(kMissingParent), NtfsFileReference(1), "LateDir", true);
  const auto healed_stats = fixture.GetOperations().GetAwaitingStats();
  CHECK(healed_stats.count == 0);
  CHECK(healed_stats.oldest_age_ms == 0);
}

TEST_CASE("IndexOperations collects never-arriving by age") {
  test_helpers::TestIndexOperationsFixture fixture;

  constexpr uint64_t kMissingParent = 0x00010000000000A0ULL;
  constexpr uint64_t kChild = 0x00010000000000B0ULL;

  fixture.GetOperations().Insert(NtfsFileReference(kChild), NtfsFileReference(kMissingParent), "late.txt", false);

  // Zero threshold: everything awaiting qualifies (age >= 0 always holds).
  std::vector<IndexOperations::NeverArriving> stale;
  fixture.GetOperations().CollectNeverArriving(0, stale);
  REQUIRE(stale.size() == 1);
  CHECK(stale[0].id == kChild);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - size checked above

  // Far-future threshold: fresh entries are exempt.
  std::vector<IndexOperations::NeverArriving> fresh;
  fixture.GetOperations().CollectNeverArriving(3600000, fresh);
  CHECK(fresh.empty());
}

TEST_CASE("IndexOperations carries journal position on awaiting entries") {
  test_helpers::TestIndexOperationsFixture fixture;

  constexpr uint64_t kMissingParent = 0x00010000000000A0ULL;
  constexpr uint64_t kChild = 0x00010000000000B0ULL;
  constexpr int64_t kUsn = 98765;

  IndexOperations::InsertOptions options{};
  options.usn_at_track = kUsn;
  fixture.GetOperations().Insert(NtfsFileReference(kChild), NtfsFileReference(kMissingParent), "late.txt", false,
                                 {UINT32_MAX, UINT32_MAX}, options);

  std::vector<IndexOperations::NeverArriving> stale;
  fixture.GetOperations().CollectNeverArriving(0, stale);
  REQUIRE(stale.size() == 1);
  CHECK(stale[0].id == kChild);
  CHECK(stale[0].usn_at_track == kUsn);

  // Default options: unknown journal position stays 0.
  constexpr uint64_t kOtherChild = 0x00010000000000C0ULL;
  fixture.GetOperations().Insert(NtfsFileReference(kOtherChild), NtfsFileReference(kMissingParent), "plain.txt", false);
  std::vector<IndexOperations::NeverArriving> all;
  fixture.GetOperations().CollectNeverArriving(0, all);
  REQUIRE(all.size() == 2);
}

TEST_CASE("IndexOperations collects journal-gap evictions in USN order") {
  test_helpers::TestIndexOperationsFixture fixture;

  constexpr uint64_t kMissingParent = 0x00010000000000A0ULL;
  constexpr uint64_t kOldChild = 0x00010000000000B0ULL;
  constexpr uint64_t kFreshChild = 0x00010000000000C0ULL;
  constexpr int64_t kGap = 1024 * 1024;
  constexpr int64_t kCurrentUsn = 2000000;
  // Old entry: gap exceeds the threshold. Fresh entry: 100 USNs under it.
  constexpr int64_t kOldUsn = kCurrentUsn - kGap;
  constexpr int64_t kFreshUsn = kCurrentUsn - kGap + 100;

  IndexOperations::InsertOptions old_options{};
  old_options.usn_at_track = kOldUsn;
  fixture.GetOperations().Insert(NtfsFileReference(kOldChild), NtfsFileReference(kMissingParent), "old.txt", false,
                                 {UINT32_MAX, UINT32_MAX}, old_options);
  IndexOperations::InsertOptions fresh_options{};
  fresh_options.usn_at_track = kFreshUsn;
  fixture.GetOperations().Insert(NtfsFileReference(kFreshChild), NtfsFileReference(kMissingParent), "fresh.txt", false,
                                 {UINT32_MAX, UINT32_MAX}, fresh_options);
  // Unknown position: wall-clock only, never a gap eviction.
  constexpr uint64_t kUnknownChild = 0x00010000000000D0ULL;
  fixture.GetOperations().Insert(NtfsFileReference(kUnknownChild), NtfsFileReference(kMissingParent), "plain.txt", false);

  // Only the old entry qualifies; fresh (under threshold) and unknown stay.
  std::vector<IndexOperations::NeverArriving> stale;
  fixture.GetOperations().CollectNeverArrivingByJournalGap(kCurrentUsn, kGap, stale);
  REQUIRE(stale.size() == 1);
  CHECK(stale[0].id == kOldChild);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - size checked above
  CHECK(stale[0].usn_at_track == kOldUsn);
}

TEST_CASE("IndexOperations evicts descendants of a never-inserted parent record") {
  test_helpers::TestIndexOperationsFixture fixture;

  constexpr uint64_t kFilteredParent = 0x0001000000000300ULL;
  constexpr uint64_t kChildDir = 0x0001000000000310ULL;
  constexpr uint64_t kGrandChild = 0x0001000000000320ULL;

  fixture.GetOperations().Insert(NtfsFileReference(1), NtfsFileReference(0), "root", true);
  fixture.GetOperations().Insert(NtfsFileReference(kChildDir), NtfsFileReference(kFilteredParent), "EarlyDir", true);
  fixture.GetOperations().Insert(NtfsFileReference(kGrandChild), NtfsFileReference(kChildDir), "early.txt", false);
  REQUIRE(fixture.GetOperations().AwaitingCount() > 0);

  // Caller collects then removes (mirrors FileIndex::Evict...Locked, which
  // additionally unlinks path_to_id_).
  std::vector<uint64_t> to_evict;
  fixture.GetOperations().CollectDescendantIds(
      ntfs_file_reference::MftRecordNumber(
          ntfs_file_reference::RecordNumber(kFilteredParent)),
      to_evict);
  REQUIRE(to_evict.size() == 2);
  for (const uint64_t id : to_evict) {
    fixture.GetOperations().Remove(NtfsFileReference(id));
  }
  CHECK(fixture.GetStorage().GetEntry(kChildDir) == nullptr);
  CHECK(fixture.GetStorage().GetEntry(kGrandChild) == nullptr);
  CHECK(fixture.GetOperations().AwaitingCount() == 0);
  CHECK(fixture.GetOperations().CheckBareNameInvariant());
}
