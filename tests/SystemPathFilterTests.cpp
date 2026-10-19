#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

#include "doctest/doctest.h"
#include "index/SystemPathFilter.h"

using ntfs_file_reference::MftRecordNumber;
using system_path_filter::IngestionDisposition;
using system_path_filter::IngestionMode;
using system_path_filter::IsSystemPrefixedName;
using system_path_filter::LiveAction;
using system_path_filter::SystemPathFilterPolicy;

namespace {

constexpr std::uint64_t kVolumeRootRecord = 5U;   // ntfs_file_reference::kRootDirectoryRecordNumber
constexpr std::uint64_t kBinRecord = 33U;         // $Recycle.Bin
constexpr std::uint64_t kSidRecord = 100U;        // S-1-5-21-... under the bin
constexpr std::uint64_t kRFolderRecord = 200U;    // $RABCDEF directory under the SID
constexpr std::uint64_t kNestedRecord = 300U;     // plain directory inside $R-folder
constexpr std::uint64_t kSequenceBits = std::uint64_t{7} << 48;  // upper 16-bit sequence number
constexpr std::uint64_t kDesktopRecord = 600U;    // unrelated user directory (not tracked)

constexpr IngestionMode kLiveMode = IngestionMode::kExcludeRecycleContents;
constexpr IngestionMode kIncMode = IngestionMode::kIncludeRecycleBin;

constexpr std::uint64_t kSubRecord = 210U;          // S: directory under the $R-folder (depth 2)
constexpr std::uint64_t kSubFileRecord = 220U;      // X: file under S
constexpr std::uint64_t kLeafDirRecord = 230U;      // T: directory under S (depth 3)
constexpr std::uint64_t kLeafFileRecord = 240U;     // F: file under T
constexpr std::uint64_t kOtherSidRecord = 101U;     // second <SID> under the bin
constexpr std::uint64_t kMetaDirRecord = 410U;      // $I-named directory under the $R-folder
constexpr std::uint64_t kPlainRecord = 950U;        // ordinary directory under the root
constexpr std::uint64_t kStashDirRecord = 951U;     // $-named directory under kPlainRecord

inline void CheckClassify(SystemPathFilterPolicy& policy, std::string_view filename,
                          std::uint64_t frn, std::uint64_t parent_frn, bool is_directory,
                          IngestionMode mode, IngestionDisposition expected) {
  CHECK(policy.ClassifyEntry(filename, frn, parent_frn, is_directory, mode) == expected);
}

inline void CheckLive(SystemPathFilterPolicy& policy, std::string_view filename,
                      std::uint64_t frn, std::uint64_t parent_frn, bool is_directory,
                      IngestionMode mode, LiveAction expected) {
  CHECK(policy.ClassifyLiveEntry(filename, frn, parent_frn, is_directory, false, mode).action == expected);
}

inline void CheckLiveRename(SystemPathFilterPolicy& policy, std::string_view filename,
                            std::uint64_t frn, std::uint64_t parent_frn, bool is_directory,
                            IngestionMode mode, LiveAction expected) {
  CHECK(policy.ClassifyLiveEntry(filename, frn, parent_frn, is_directory, true, mode).action == expected);
}

// Canonical volume layout: root 5 -> $Recycle.Bin 33 -> <SID> 100, all via classification.
void SeedRootAndSid(SystemPathFilterPolicy& policy, IngestionMode mode) {
  const IngestionDisposition expected = (mode == kIncMode) ? IngestionDisposition::IncludeRecycleBin
                                                          : IngestionDisposition::ExcludeSystem;
  REQUIRE(policy.ClassifyEntry("$Recycle.Bin", kBinRecord, kVolumeRootRecord, true, mode) == expected);
  REQUIRE(policy.ClassifyEntry("S-1-5-21-1234-5678-9012-567", kSidRecord, kBinRecord, true, mode) == expected);
}

void SeedRFolderWithSubfolder(SystemPathFilterPolicy& policy) {
  SeedRootAndSid(policy, kLiveMode);
  REQUIRE(policy.ClassifyLiveEntry("$RABCDEF", kRFolderRecord, kSidRecord, true, false, kLiveMode).action == LiveAction::kSkip);
  REQUIRE(policy.ClassifyLiveEntry("S", kSubRecord, kRFolderRecord, true, false, kLiveMode).action == LiveAction::kSkip);
  REQUIRE(policy.ClassifyLiveEntry("X", kSubFileRecord, kSubRecord, false, false, kLiveMode).action == LiveAction::kSkip);
}

}  // namespace

TEST_SUITE("SystemPathFilter - IsSystemPrefixedName") {
  TEST_CASE("IsSystemPrefixedName classification") {
    CHECK_FALSE(IsSystemPrefixedName(""));
    CHECK(IsSystemPrefixedName("$MFT"));
    CHECK(IsSystemPrefixedName("$LogFile"));
    CHECK(IsSystemPrefixedName("$Extend"));
    CHECK(IsSystemPrefixedName("$Recycle.Bin"));
    CHECK(IsSystemPrefixedName("$R1A2B3C.docx"));
    CHECK_FALSE(IsSystemPrefixedName("report.docx"));
    CHECK_FALSE(IsSystemPrefixedName("S-1-5-21-1234-5678-9012-567"));
    CHECK_FALSE(IsSystemPrefixedName("C:/Users/foo"));
    CHECK_FALSE(IsSystemPrefixedName("a$b"));
    CHECK_FALSE(IsSystemPrefixedName("my$Recycle.Bin"));
  }
}

TEST_SUITE("SystemPathFilter - Root recognition and exclusions") {
  TEST_CASE("valid root recognition across modes, casing, and sequence bits") {
    SystemPathFilterPolicy p2;
    CheckClassify(p2, "$Recycle.Bin", kBinRecord, kVolumeRootRecord, true, kIncMode, IngestionDisposition::IncludeRecycleBin);
    CHECK(p2.IsTrackedRecycleRoot(MftRecordNumber(kBinRecord)));
    CHECK(p2.TrackedContainerCount() == 1U);

    SystemPathFilterPolicy p1;
    CheckClassify(p1, "$Recycle.Bin", kBinRecord, kVolumeRootRecord, true, kLiveMode, IngestionDisposition::ExcludeSystem);
    CHECK(p1.IsTrackedRecycleRoot(MftRecordNumber(kBinRecord)));

    const std::array<const char*, 5> kPermutations = {"$Recycle.Bin", "$RECYCLE.BIN", "$recycle.bin", "$Recycle.bin", "$rEcYcLe.BiN"};
    for (const char* name : kPermutations) {
      SystemPathFilterPolicy policy;
      CheckClassify(policy, name, kBinRecord, kVolumeRootRecord, true, kIncMode, IngestionDisposition::IncludeRecycleBin);
      CHECK(policy.IsTrackedRecycleRoot(MftRecordNumber(kBinRecord)));
    }

    SystemPathFilterPolicy p_seq;
    CheckClassify(p_seq, "$RECYCLE.BIN", kBinRecord, kSequenceBits | kVolumeRootRecord, true, kIncMode, IngestionDisposition::IncludeRecycleBin);
    CHECK(p_seq.IsTrackedRecycleRoot(MftRecordNumber(kBinRecord)));
  }

  TEST_CASE("non-root, file, and self-referential root exclusions") {
    SystemPathFilterPolicy policy;
    CheckClassify(policy, "$Recycle.Bin", kBinRecord, kDesktopRecord, true, kIncMode, IngestionDisposition::IncludeNormal);
    CHECK_FALSE(policy.IsTrackedRecycleRoot(MftRecordNumber(kBinRecord)));
    CHECK(policy.TrackedContainerCount() == 0U);
    CHECK_FALSE(policy.IsFilteredDirectory(MftRecordNumber(kBinRecord)));
    CheckClassify(policy, "$Recycle.Bin", kBinRecord, kVolumeRootRecord, false, kIncMode, IngestionDisposition::IncludeNormal);
    CHECK(policy.TrackedContainerCount() == 0U);

    CheckClassify(policy, ".", kVolumeRootRecord, kVolumeRootRecord, true, kIncMode, IngestionDisposition::ExcludeSystem);
    CheckClassify(policy, "$Recycle.Bin", kSequenceBits | kVolumeRootRecord, kVolumeRootRecord, true, kIncMode, IngestionDisposition::ExcludeSystem);
    CHECK(policy.TrackedContainerCount() == 0U);
  }
}

TEST_SUITE("SystemPathFilter - SID and payload classification") {
  TEST_CASE("SID container recognition and mode-dependent payload/metadata classification") {
    SystemPathFilterPolicy phase2;
    SystemPathFilterPolicy phase1;
    SeedRootAndSid(phase2, kIncMode);
    SeedRootAndSid(phase1, kLiveMode);

    CHECK(phase2.IsTrackedSid(MftRecordNumber(kSidRecord)));
    CHECK(phase2.TrackedContainerCount() == 2U);
    CHECK(phase1.IsTrackedSid(MftRecordNumber(kSidRecord)));

    CheckClassify(phase2, "S-1-5-18", 101U, kBinRecord, true, kIncMode, IngestionDisposition::IncludeRecycleBin);
    CheckClassify(phase1, "S-1-5-18", 101U, kBinRecord, true, kLiveMode, IngestionDisposition::ExcludeSystem);

    CheckClassify(phase2, "$R1A2B3C.docx", 301U, kSidRecord, false, kIncMode, IngestionDisposition::IncludeRecycleBin);
    CheckClassify(phase1, "$R1A2B3C.docx", 301U, kSidRecord, false, kLiveMode, IngestionDisposition::ExcludeSystem);
    CheckClassify(phase2, "$r1a2b3c.docx", 301U, kSidRecord, false, kIncMode, IngestionDisposition::IncludeRecycleBin);
    CheckClassify(phase2, "$R", 302U, kSidRecord, false, kIncMode, IngestionDisposition::IncludeNormal);
    CheckClassify(phase2, "$R1A2B3C.docx", 301U, kSequenceBits | kSidRecord, false, kIncMode, IngestionDisposition::IncludeRecycleBin);

    CheckClassify(phase2, "$I1A2B3C.docx", 401U, kSidRecord, false, kIncMode, IngestionDisposition::SkipRecycleMetadata);
    CheckClassify(phase2, "$i1a2b3c.docx", 402U, kSidRecord, false, kIncMode, IngestionDisposition::SkipRecycleMetadata);
    CheckClassify(phase1, "$I1A2B3C.docx", 401U, kSidRecord, false, kLiveMode, IngestionDisposition::SkipRecycleMetadata);

    CheckClassify(phase2, "desktop.ini", 501U, kSidRecord, false, kIncMode, IngestionDisposition::IncludeNormal);
    CheckClassify(phase1, "desktop.ini", 501U, kSidRecord, false, kLiveMode, IngestionDisposition::ExcludeSystem);
    CheckClassify(phase2, "notes.txt", 701U, kBinRecord, false, kIncMode, IngestionDisposition::IncludeNormal);
  }

  TEST_CASE("$R directory tracking and transitive cascading to any depth") {
    SystemPathFilterPolicy phase2;
    SeedRootAndSid(phase2, kIncMode);
    CheckClassify(phase2, "$RABCDEF", kRFolderRecord, kSidRecord, true, kIncMode, IngestionDisposition::IncludeRecycleBin);
    CHECK(phase2.IsTrackedRecycledFolder(MftRecordNumber(kRFolderRecord)));
    CheckClassify(phase2, "nested", kNestedRecord, kRFolderRecord, true, kIncMode, IngestionDisposition::IncludeRecycleBin);
    CHECK(phase2.IsTrackedRecycledFolder(MftRecordNumber(kNestedRecord)));
    CheckClassify(phase2, "deep.txt", 601U, kNestedRecord, false, kIncMode, IngestionDisposition::IncludeRecycleBin);

    SystemPathFilterPolicy phase1;
    SeedRootAndSid(phase1, kLiveMode);
    CheckClassify(phase1, "$RABCDEF", kRFolderRecord, kSidRecord, true, kLiveMode, IngestionDisposition::ExcludeSystem);
    CheckClassify(phase1, "nested", kNestedRecord, kRFolderRecord, true, kLiveMode, IngestionDisposition::ExcludeSystem);
    CHECK(phase1.IsTrackedRecycledFolder(MftRecordNumber(kNestedRecord)));
    CheckClassify(phase1, "deep.txt", 601U, kNestedRecord, false, kLiveMode, IngestionDisposition::ExcludeSystem);
  }
}

TEST_SUITE("SystemPathFilter - Non-artifact subtree content and cleanup") {
  TEST_CASE("non-artifact directory and $I handling inside recycle subtree") {
    SystemPathFilterPolicy phase2;
    SeedRootAndSid(phase2, kIncMode);
    CheckClassify(phase2, "Foo", 150U, kSidRecord, true, kIncMode, IngestionDisposition::IncludeNormal);
    CHECK(phase2.IsTrackedRecycledFolder(MftRecordNumber(150U)));
    CheckClassify(phase2, "a.txt", 151U, 150U, false, kIncMode, IngestionDisposition::IncludeRecycleBin);

    CheckClassify(phase2, "$Foo", 160U, kSidRecord, true, kIncMode, IngestionDisposition::IncludeNormal);
    CheckClassify(phase2, "a.txt", 161U, 160U, false, kIncMode, IngestionDisposition::IncludeRecycleBin);

    SystemPathFilterPolicy phase1;
    SeedRootAndSid(phase1, kLiveMode);
    CheckClassify(phase1, "Foo", 150U, kSidRecord, true, kLiveMode, IngestionDisposition::ExcludeSystem);
    CheckClassify(phase1, "a.txt", 151U, 150U, false, kLiveMode, IngestionDisposition::ExcludeSystem);
    CheckClassify(phase1, "$Foo", 160U, kSidRecord, true, kLiveMode, IngestionDisposition::ExcludeSystem);
    CheckClassify(phase1, "a.txt", 161U, 160U, false, kLiveMode, IngestionDisposition::ExcludeSystem);

    REQUIRE(phase2.ClassifyEntry("$RABCDEF", kRFolderRecord, kSidRecord, true, kIncMode) == IngestionDisposition::IncludeRecycleBin);
    CheckClassify(phase2, "$I1A2B3C.docx", 401U, kRFolderRecord, false, kIncMode, IngestionDisposition::SkipRecycleMetadata);
    CheckClassify(phase2, "$i1a2b3c.docx", 402U, kRFolderRecord, false, kLiveMode, IngestionDisposition::SkipRecycleMetadata);

    CheckClassify(phase2, "$I9Z8Y7", 410U, kRFolderRecord, true, kIncMode, IngestionDisposition::SkipRecycleMetadata);
    CHECK_FALSE(phase2.IsTrackedRecycledFolder(MftRecordNumber(410U)));
    CheckClassify(phase2, "payload.txt", 411U, 410U, false, kIncMode, IngestionDisposition::ExcludeSystem);
  }

  TEST_CASE("EraseOnDelete and OnDirectoryDeleted cleanup across trackers") {
    SystemPathFilterPolicy p_del;
    SeedRootAndSid(p_del, kIncMode);
    REQUIRE(p_del.ClassifyEntry("$I9Z8Y7", 410U, kSidRecord, true, kIncMode) == IngestionDisposition::SkipRecycleMetadata);
    REQUIRE(p_del.IsFilteredDirectory(MftRecordNumber(410U)));
    REQUIRE(p_del.ClassifyEntry("payload.txt", 411U, 410U, false, kIncMode) == IngestionDisposition::ExcludeSystem);
    REQUIRE(p_del.ClassifyEntry("$R1A2B3C.docx", 301U, kSidRecord, false, kIncMode) == IngestionDisposition::IncludeRecycleBin);

    p_del.OnDirectoryDeleted(MftRecordNumber(410U));
    p_del.OnDirectoryDeleted(MftRecordNumber(kSidRecord));

    CHECK_FALSE(p_del.IsFilteredDirectory(MftRecordNumber(410U)));
    CHECK_FALSE(p_del.IsTrackedSid(MftRecordNumber(kSidRecord)));
    CheckClassify(p_del, "$R1A2B3C.docx", 301U, kSidRecord, false, kIncMode, IngestionDisposition::IncludeNormal);
    CheckClassify(p_del, "desktop.ini", 501U, kSidRecord, false, kIncMode, IngestionDisposition::IncludeNormal);
    CheckClassify(p_del, "payload.txt", 411U, 410U, false, kIncMode, IngestionDisposition::IncludeNormal);
    CHECK(p_del.TrackedContainerCount() == 1U);
  }
}

TEST_SUITE("SystemPathFilter - Outside-subtree and isolation") {
  TEST_CASE("outside-subtree $-prefixed entries and volume isolation") {
    SystemPathFilterPolicy policy;
    SeedRootAndSid(policy, kIncMode);

    CheckClassify(policy, "$R1A2B3C.docx", 801U, kVolumeRootRecord, false, kIncMode, IngestionDisposition::IncludeNormal);
    CheckClassify(policy, "$RABCDEF", 802U, kVolumeRootRecord, true, kIncMode, IngestionDisposition::IncludeNormal);
    CHECK_FALSE(policy.IsTrackedRecycledFolder(MftRecordNumber(802U)));

    CheckClassify(policy, "$Report.docx", 900U, kDesktopRecord, false, kIncMode, IngestionDisposition::IncludeNormal);
    CheckClassify(policy, "$I1A2B3C.docx", 901U, kDesktopRecord, false, kIncMode, IngestionDisposition::IncludeNormal);

    CheckClassify(policy, "$Extend", 910U, kVolumeRootRecord, true, kIncMode, IngestionDisposition::IncludeNormal);
    CheckClassify(policy, "ObjId", 911U, 910U, false, kIncMode, IngestionDisposition::IncludeNormal);
    CheckClassify(policy, "report.docx", 912U, kVolumeRootRecord, false, kIncMode, IngestionDisposition::IncludeNormal);

    SystemPathFilterPolicy volume_c;
    SystemPathFilterPolicy volume_d;
    SeedRootAndSid(volume_c, kIncMode);
    CheckClassify(volume_d, "$R1A2B3C.docx", 301U, kSidRecord, false, kIncMode, IngestionDisposition::IncludeNormal);
    CHECK(volume_d.TrackedContainerCount() == 0U);
    CHECK(volume_c.TrackedContainerCount() == 2U);
    CheckClassify(volume_c, "$R1A2B3C.docx", 301U, kSidRecord, false, kIncMode, IngestionDisposition::IncludeRecycleBin);

    SystemPathFilterPolicy p1;
    SeedRootAndSid(p1, kLiveMode);
    CheckClassify(p1, "$R1A2B3C.docx", 301U, kSidRecord, false, kLiveMode, IngestionDisposition::ExcludeSystem);
    CheckClassify(p1, "notes.txt", 701U, kBinRecord, false, kLiveMode, IngestionDisposition::ExcludeSystem);
    CheckClassify(p1, "$I1A2B3C.docx", 401U, kSidRecord, false, kLiveMode, IngestionDisposition::SkipRecycleMetadata);
  }
}

TEST_SUITE("SystemPathFilter - LiveDecision routing") {
  TEST_CASE("recycle routing, evictions, skip metadata, and mode switching") {
    SystemPathFilterPolicy policy;
    SeedRootAndSid(policy, kLiveMode);

    CheckLiveRename(policy, "$R1A2B3C.docx", 301U, kSidRecord, false, kLiveMode, LiveAction::kEvictAsRecycleDelete);
    CheckLiveRename(policy, "$RABCDEF", kRFolderRecord, kSidRecord, true, kLiveMode, LiveAction::kEvictAsRecycleDelete);
    CHECK(policy.IsTrackedRecycledFolder(MftRecordNumber(kRFolderRecord)));

    CheckLiveRename(policy, "moved.txt", 702U, kSidRecord, false, kLiveMode, LiveAction::kSkip);
    CheckLive(policy, "$I1A2B3C.docx", 401U, kSidRecord, false, kLiveMode, LiveAction::kSkipMetadata);
    CheckLiveRename(policy, "$I1A2B3C.docx", 401U, kSidRecord, false, kLiveMode, LiveAction::kEvictAsRecycleDelete);

    SystemPathFilterPolicy p_inc;
    SeedRootAndSid(p_inc, kIncMode);
    CheckLiveRename(p_inc, "$R1A2B3C.docx", 301U, kSidRecord, false, kIncMode, LiveAction::kProcess);
  }

  TEST_CASE("normal operations, filtered directory deletion, journal-order priming, and stale/rename recovery") {
    SystemPathFilterPolicy policy;
    CheckLive(policy, "report.docx", 701U, kVolumeRootRecord, false, kLiveMode, LiveAction::kProcess);
    CheckLiveRename(policy, "report2.docx", 701U, kVolumeRootRecord, false, kLiveMode, LiveAction::kProcess);

    SeedRootAndSid(policy, kLiveMode);
    CheckLive(policy, "$I9Z8Y7", 410U, kSidRecord, true, kLiveMode, LiveAction::kSkipMetadata);
    CheckLive(policy, "child.txt", 411U, 410U, false, kLiveMode, LiveAction::kSkip);
    CheckLiveRename(policy, "child2.txt", 412U, 410U, false, kLiveMode, LiveAction::kSkip);

    policy.OnDirectoryDeleted(MftRecordNumber(410U));
    CheckLive(policy, "child.txt", 411U, 410U, false, kLiveMode, LiveAction::kProcess);

    // Journal-order priming
    SystemPathFilterPolicy in_order;
    SeedRootAndSid(in_order, kLiveMode);
    CheckLive(in_order, "$RABCDEF", kRFolderRecord, kSidRecord, true, kLiveMode, LiveAction::kSkip);
    CheckLive(in_order, "notes.txt", 701U, kRFolderRecord, false, kLiveMode, LiveAction::kSkip);

    SystemPathFilterPolicy unprimed;
    SeedRootAndSid(unprimed, kLiveMode);
    CheckLive(unprimed, "notes.txt", 701U, kRFolderRecord, false, kLiveMode, LiveAction::kProcess);

    SystemPathFilterPolicy primed;
    SeedRootAndSid(primed, kLiveMode);
    static_cast<void>(primed.ClassifyEntry("$RABCDEF", kRFolderRecord, kSidRecord, true, kLiveMode));
    static_cast<void>(primed.ClassifyEntry("notes.txt", 701U, kRFolderRecord, false, kLiveMode));
    CheckLive(primed, "notes.txt", 701U, kRFolderRecord, false, kLiveMode, LiveAction::kSkip);
    CheckLive(primed, "$RABCDEF", kRFolderRecord, kSidRecord, true, kLiveMode, LiveAction::kSkip);
    CHECK(primed.TrackedContainerCount() == in_order.TrackedContainerCount());

    // Restored $R-folder drops stale membership
    SystemPathFilterPolicy p_rest;
    SeedRootAndSid(p_rest, kLiveMode);
    REQUIRE(p_rest.ClassifyLiveEntry("$RABCDEF", kRFolderRecord, kSidRecord, true, false, kLiveMode).action == LiveAction::kSkip);
    REQUIRE(p_rest.IsTrackedRecycledFolder(MftRecordNumber(kRFolderRecord)));
    CheckLive(p_rest, "RestoredDir", kRFolderRecord, kDesktopRecord, true, kLiveMode, LiveAction::kProcess);
    CHECK_FALSE(p_rest.IsTrackedRecycledFolder(MftRecordNumber(kRFolderRecord)));
    CheckLive(p_rest, "notes.txt", 701U, kRFolderRecord, false, kLiveMode, LiveAction::kProcess);

    // $-named directory renamed out and back
    SystemPathFilterPolicy p_rename;
    SeedRootAndSid(p_rename, kLiveMode);
    REQUIRE(p_rename.ClassifyLiveEntry("$I9Z8Y7", 410U, kSidRecord, true, true, kLiveMode).action == LiveAction::kEvictAsRecycleDelete);
    REQUIRE(p_rename.IsFilteredDirectory(MftRecordNumber(410U)));
    REQUIRE(p_rename.ClassifyLiveEntry("ObjId", 411U, 410U, false, false, kLiveMode).action == LiveAction::kSkip);
    CheckLiveRename(p_rename, "Foo", 410U, kDesktopRecord, true, kLiveMode, LiveAction::kProcess);
    CHECK_FALSE(p_rename.IsFilteredDirectory(MftRecordNumber(410U)));
    CheckLive(p_rename, "ObjId", 411U, 410U, false, kLiveMode, LiveAction::kProcess);

    // Soft-delete rename of a directory to a $R-name under a SID marks it for children
    SystemPathFilterPolicy p_soft;
    SeedRootAndSid(p_soft, kLiveMode);
    CheckLiveRename(p_soft, "$RABCDEF", kRFolderRecord, kSidRecord, true, kLiveMode, LiveAction::kEvictAsRecycleDelete);
    CHECK(p_soft.IsTrackedRecycledFolder(MftRecordNumber(kRFolderRecord)));
    CheckLive(p_soft, "notes.txt", 701U, kRFolderRecord, false, kLiveMode, LiveAction::kSkip);
    CheckLive(p_soft, "$R1A2B3C.docx", 702U, kRFolderRecord, false, kLiveMode, LiveAction::kSkip);
  }
}

TEST_SUITE("SystemPathFilter - Reverse index descendant sweep and hygiene") {
  TEST_CASE("descendant sweep: restoring filtered directories and $R-folders sweeps all depths without affecting untracked") {
    SystemPathFilterPolicy p_depth2;
    SeedRootAndSid(p_depth2, kLiveMode);
    REQUIRE(p_depth2.ClassifyLiveEntry("$I9Z8Y7", 910U, kSidRecord, true, false, kLiveMode).action == LiveAction::kSkipMetadata);
    REQUIRE(p_depth2.ClassifyLiveEntry("Sub", 912U, 910U, true, false, kLiveMode).action == LiveAction::kSkip);
    REQUIRE(p_depth2.ClassifyLiveEntry("ObjId", 913U, 912U, false, false, kLiveMode).action == LiveAction::kSkip);

    CheckLiveRename(p_depth2, "Foo", 910U, kDesktopRecord, true, kLiveMode, LiveAction::kProcess);
    CHECK_FALSE(p_depth2.IsFilteredDirectory(MftRecordNumber(912U)));
    CheckLive(p_depth2, "ObjId", 913U, 912U, false, kLiveMode, LiveAction::kProcess);
    CHECK(p_depth2.TrackedContainerCount() == 2U);

    SystemPathFilterPolicy policy;
    SeedRFolderWithSubfolder(policy);
    REQUIRE(policy.IsTrackedRecycledFolder(MftRecordNumber(kSubRecord)));
    REQUIRE(policy.ClassifyLiveEntry("T", kLeafDirRecord, kSubRecord, true, false, kLiveMode).action == LiveAction::kSkip);
    REQUIRE(policy.ClassifyLiveEntry("F", kLeafFileRecord, kLeafDirRecord, false, false, kLiveMode).action == LiveAction::kSkip);

    CheckLiveRename(policy, "RestoredDir", kRFolderRecord, kDesktopRecord, true, kLiveMode, LiveAction::kProcess);
    CHECK_FALSE(policy.IsTrackedRecycledFolder(MftRecordNumber(kSubRecord)));
    CHECK_FALSE(policy.IsTrackedRecycledFolder(MftRecordNumber(kLeafDirRecord)));
    CheckLive(policy, "X2", 221U, kSubRecord, false, kLiveMode, LiveAction::kProcess);
    CheckLive(policy, "F2", 241U, kLeafDirRecord, false, kLiveMode, LiveAction::kProcess);
    CHECK(policy.TrackedContainerCount() == 2U);

    SystemPathFilterPolicy p_untracked;
    REQUIRE(p_untracked.ClassifyLiveEntry("Plain", kPlainRecord, kVolumeRootRecord, true, false, kLiveMode).action == LiveAction::kProcess);
    REQUIRE(p_untracked.ClassifyLiveEntry("$Stash", kStashDirRecord, kPlainRecord, true, false, kLiveMode).action == LiveAction::kProcess);

    CheckLiveRename(p_untracked, "Plain2", kPlainRecord, kVolumeRootRecord, true, kLiveMode, LiveAction::kProcess);
    CHECK_FALSE(p_untracked.IsFilteredDirectory(MftRecordNumber(kStashDirRecord)));
    CheckLive(p_untracked, "a.txt", 952U, kStashDirRecord, false, kLiveMode, LiveAction::kProcess);
  }

  TEST_CASE("record reuse, re-parenting, and ClearContainers hygiene") {
    SystemPathFilterPolicy policy;
    SeedRFolderWithSubfolder(policy);
    REQUIRE(policy.ClassifyLiveEntry("T", kLeafDirRecord, kSubRecord, true, false, kLiveMode).action == LiveAction::kSkip);

    policy.OnDirectoryDeleted(MftRecordNumber(kRFolderRecord));
    CHECK(policy.TrackedContainerCount() == 2U);
    CheckLive(policy, "X2", 221U, kSubRecord, false, kLiveMode, LiveAction::kProcess);
    CheckLive(policy, "F", kLeafFileRecord, kLeafDirRecord, false, kLiveMode, LiveAction::kProcess);

    CheckLive(policy, "Fresh", kRFolderRecord, kDesktopRecord, true, kLiveMode, LiveAction::kProcess);
    CheckLive(policy, "$RNEW", kSubRecord, kSidRecord, true, kLiveMode, LiveAction::kSkip);
    policy.OnDirectoryDeleted(MftRecordNumber(kRFolderRecord));
    CHECK(policy.IsTrackedRecycledFolder(MftRecordNumber(kSubRecord)));
    CheckLive(policy, "Y", 231U, kSubRecord, false, kLiveMode, LiveAction::kSkip);

    SystemPathFilterPolicy p_reparent;
    SeedRFolderWithSubfolder(p_reparent);
    REQUIRE(p_reparent.ClassifyLiveEntry("S-1-5-18", kOtherSidRecord, kBinRecord, true, false, kLiveMode).action == LiveAction::kSkip);
    CheckLiveRename(p_reparent, "$RABCDEF", kRFolderRecord, kOtherSidRecord, true, kLiveMode, LiveAction::kEvictAsRecycleDelete);
    p_reparent.OnDirectoryDeleted(MftRecordNumber(kSidRecord));
    CHECK(p_reparent.IsTrackedRecycledFolder(MftRecordNumber(kRFolderRecord)));
    CheckLive(p_reparent, "X", kSubFileRecord, kSubRecord, false, kLiveMode, LiveAction::kSkip);

    SystemPathFilterPolicy p_clear;
    SeedRFolderWithSubfolder(p_clear);
    REQUIRE(p_clear.ClassifyLiveEntry("$I9Z8Y7", kMetaDirRecord, kRFolderRecord, true, false, kLiveMode).action == LiveAction::kSkipMetadata);
    CHECK(p_clear.TrackedContainerCount() == 5U);

    p_clear.ClearContainers();
    CHECK(p_clear.TrackedContainerCount() == 0U);
    CheckLive(p_clear, "X", kSubFileRecord, kSubRecord, false, kLiveMode, LiveAction::kProcess);

    p_clear.ReserveContainers(64U);
    CHECK(p_clear.TrackedContainerCount() == 0U);
    SeedRootAndSid(p_clear, kLiveMode);
    CHECK(p_clear.TrackedContainerCount() == 2U);
  }
}

TEST_SUITE("SystemPathFilter - Edge classifications and recovery sequences") {
  TEST_CASE("non-directory children of root, double-inversion fix-up, and untracked deletes") {
    SystemPathFilterPolicy phase2;
    REQUIRE(phase2.ClassifyEntry("$Recycle.Bin", kBinRecord, kVolumeRootRecord, true, kIncMode) == IngestionDisposition::IncludeRecycleBin);
    CheckClassify(phase2, "notes.txt", 701U, kBinRecord, false, kIncMode, IngestionDisposition::IncludeNormal);
    CHECK(phase2.TrackedContainerCount() == 1U);

    SystemPathFilterPolicy phase1;
    REQUIRE(phase1.ClassifyEntry("$Recycle.Bin", kBinRecord, kVolumeRootRecord, true, kLiveMode) == IngestionDisposition::ExcludeSystem);
    CheckClassify(phase1, "notes.txt", 701U, kBinRecord, false, kLiveMode, IngestionDisposition::ExcludeSystem);
    CHECK(phase1.TrackedContainerCount() == 1U);

    SystemPathFilterPolicy policy;
    CheckClassify(policy, "$RABCDEF", kRFolderRecord, kSidRecord, true, kIncMode, IngestionDisposition::IncludeNormal);

    SeedRootAndSid(policy, kIncMode);
    REQUIRE(policy.ClassifyEntry("$RABCDEF", kRFolderRecord, kSidRecord, true, kIncMode) == IngestionDisposition::IncludeRecycleBin);
    CHECK(policy.IsTrackedRecycledFolder(MftRecordNumber(kRFolderRecord)));
    CHECK(policy.TrackedContainerCount() == 3U);

    CheckClassify(policy, "notes.txt", 701U, kRFolderRecord, false, kIncMode, IngestionDisposition::IncludeRecycleBin);
    CheckClassify(policy, "notes.txt", 702U, kRFolderRecord, false, kLiveMode, IngestionDisposition::ExcludeSystem);

    const std::size_t tracked_before = policy.TrackedContainerCount();

    policy.OnDirectoryDeleted(MftRecordNumber(kPlainRecord));
    CHECK(policy.TrackedContainerCount() == tracked_before);

    CHECK(policy.IsTrackedSid(MftRecordNumber(kSidRecord)));
    CheckClassify(policy, "$R1A2B3C.docx", 301U, kSidRecord, false, kIncMode, IngestionDisposition::IncludeRecycleBin);

    SystemPathFilterPolicy empty_policy;
    empty_policy.OnDirectoryDeleted(MftRecordNumber(kBinRecord));
    CHECK(empty_policy.TrackedContainerCount() == 0U);
  }
}

TEST_SUITE("SystemPathFilter - Classification throughput (informational)") {
  TEST_CASE("100k mixed ClassifyEntry calls: average ns/item is reported, not asserted") {
    struct Sample {
      std::string_view prefix;
      uint64_t parent;
      IngestionDisposition expected;
    };
    constexpr std::array<Sample, 4> kSamples = {{
        {"$Ra", kSidRecord, IngestionDisposition::IncludeRecycleBin},
        {"$Ia", kSidRecord, IngestionDisposition::SkipRecycleMetadata},
        {"notes", kVolumeRootRecord, IngestionDisposition::IncludeNormal},
        {"$Extend", kVolumeRootRecord, IngestionDisposition::IncludeNormal},
    }};
    constexpr std::size_t kThroughputItems = 100000U;
    constexpr std::size_t kSampleCount = kSamples.size();

    SystemPathFilterPolicy policy;
    policy.ReserveContainers(64U);
    SeedRootAndSid(policy, kIncMode);

    std::string filename;
    filename.reserve(32U);
    std::size_t mismatches = 0U;

    const auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < kThroughputItems; ++i) {
      const Sample& sample = kSamples[i % kSampleCount];
      filename.clear();
      filename.append(sample.prefix);
      filename.push_back(static_cast<char>('a' + (i % 26U)));
      if (const IngestionDisposition got = policy.ClassifyEntry(filename, 1000U + i, sample.parent, false, kIncMode);
          got != sample.expected) {
        ++mismatches;
      }
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const auto total_ns = static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
    const std::int64_t avg_ns = total_ns / static_cast<std::int64_t>(kThroughputItems);
    std::fprintf(stderr, "ClassifyEntry throughput (informational): %zu items, avg %lld ns/item\n",
                 kThroughputItems, static_cast<long long>(avg_ns));
    CHECK(mismatches == 0U);
    CHECK(policy.TrackedContainerCount() == 2U);
  }
}

TEST_SUITE("SystemPathFilter - Phase 2 tracker-miss diagnostic (spec §3.6)") {
  TEST_CASE("tracker-miss diagnostic: root-level and gaps set flag; tracked parents, non-artifacts, and filtered children do not") {
    SystemPathFilterPolicy root_policy;
    const system_path_filter::LiveDecision root_level = root_policy.ClassifyLiveEntry(
        "$R1A2B3C.docx", 301U, kVolumeRootRecord, false, false, kIncMode);
    CHECK(root_level.tracker_miss);
    CHECK(root_level.action == LiveAction::kProcess);

    SystemPathFilterPolicy gap_policy;
    const system_path_filter::LiveDecision gap = gap_policy.ClassifyLiveEntry(
        "$R1A2B3C.docx", 301U, kSidRecord, false, false, kIncMode);
    CHECK(gap.tracker_miss);
    CHECK(gap.action == LiveAction::kProcess);

    SystemPathFilterPolicy policy;
    SeedRootAndSid(policy, kIncMode);
    CHECK_FALSE(policy.ClassifyLiveEntry("$R1A2B3C.docx", 301U, kSidRecord, false, false, kIncMode).tracker_miss);
    REQUIRE(policy.ClassifyLiveEntry("$RABCDEF", kRFolderRecord, kSidRecord, true, false, kIncMode).action == LiveAction::kProcess);
    CHECK_FALSE(policy.ClassifyLiveEntry("$R9Z8Y7X.txt", 302U, kRFolderRecord, false, false, kIncMode).tracker_miss);

    CHECK_FALSE(policy.ClassifyLiveEntry("$MFT", 800U, kVolumeRootRecord, false, false, kIncMode).tracker_miss);
    CHECK_FALSE(policy.ClassifyLiveEntry("$R", 801U, kVolumeRootRecord, false, false, kIncMode).tracker_miss);
    CHECK_FALSE(policy.ClassifyLiveEntry("report.docx", 802U, 9999U, false, false, kIncMode).tracker_miss);

    REQUIRE(policy.ClassifyLiveEntry("$I9Z8Y7", 410U, kSidRecord, true, false, kIncMode).action == LiveAction::kSkipMetadata);
    CHECK_FALSE(policy.ClassifyLiveEntry("$R1A2B3C.docx", 301U, 410U, false, false, kIncMode).tracker_miss);
  }
}

TEST_SUITE("SystemPathFilter - relaxed system files ingestion policy") {
  TEST_CASE("relaxed ingestion: $-names are IncludeNormal, Record 5 is ExcludeSystem, and $I under SID is SkipRecycleMetadata") {
    SystemPathFilterPolicy policy;
    constexpr uint64_t kDevDirRecord = 1200U;

    CheckClassify(policy, "$report.text.tst", 1201U, kDevDirRecord, false, kIncMode, IngestionDisposition::IncludeNormal);
    CheckClassify(policy, "$MFT", 0U, kVolumeRootRecord, false, kIncMode, IngestionDisposition::IncludeNormal);
    CheckClassify(policy, "$LogFile", 2U, kVolumeRootRecord, false, kIncMode, IngestionDisposition::IncludeNormal);
    CheckClassify(policy, "$Extend", 910U, kVolumeRootRecord, true, kIncMode, IngestionDisposition::IncludeNormal);

    CheckClassify(policy, ".", kVolumeRootRecord, kVolumeRootRecord, true, kIncMode, IngestionDisposition::ExcludeSystem);
    CHECK(system_path_filter::IsExcludedIndexEntry(".", kVolumeRootRecord, true));

    SeedRootAndSid(policy, kIncMode);
    CheckClassify(policy, "$I1A2B3C.docx", 1301U, kSidRecord, false, kIncMode, IngestionDisposition::SkipRecycleMetadata);
  }

  TEST_CASE("MarkFilteredDir reports true on first insertion and false when already tracked") {
    SystemPathFilterPolicy policy;
    CHECK(policy.MarkFilteredDir(system_path_filter::FilteredDirectory(33U)));
    CHECK_FALSE(policy.MarkFilteredDir(system_path_filter::FilteredDirectory(33U)));
    CHECK(policy.TrackedContainerCount() == 1U);
  }
}
