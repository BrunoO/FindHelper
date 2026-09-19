#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstdint>

#include "doctest/doctest.h"
#include "index/NtfsFileReference.h"
#include "usn/UsnReason.h"
#include "usn/UsnRecord.h"

using ntfs_file_reference::NtfsFileReference;
using usn_reason::ReasonSet;
using usn_reason::UsnReason;
using usn_record::UsnRecord;

// Compile-time proof the predicates are usable in constant expressions.
static_assert(ReasonSet(0x00000100U).Includes(UsnReason::FileCreate),
              "FileCreate bit must be detected");
static_assert(!ReasonSet(0x80000000U).Intersects(usn_reason::kActionReasons),
              "close-only must carry no action");
static_assert(UsnRecord{NtfsFileReference(1U), NtfsFileReference(0U), "a.txt", 10,
                        ReasonSet(0x00000100U), true,}
                  .IsParentEstablishing(),
              "directory CREATE must establish parents");

TEST_SUITE("UsnReason - ReasonSet") {

  TEST_CASE("Includes detects single bits") {
    constexpr ReasonSet reasons(0x00000100U | 0x80000000U);
    CHECK(reasons.Includes(UsnReason::FileCreate));
    CHECK(reasons.Includes(UsnReason::Close));
    CHECK_FALSE(reasons.Includes(UsnReason::FileDelete));
    CHECK_FALSE(reasons.Includes(UsnReason::RenameNewName));
  }

  TEST_CASE("empty set includes nothing") {
    constexpr ReasonSet reasons(0U);
    CHECK_FALSE(reasons.Includes(UsnReason::FileCreate));
    CHECK_FALSE(reasons.Includes(UsnReason::Close));
    CHECK_FALSE(reasons.Intersects(usn_reason::kActionReasons));
  }

  TEST_CASE("bitwise combination builds sets") {
    constexpr ReasonSet combined =
        UsnReason::FileCreate | UsnReason::RenameNewName;
    CHECK(combined.Includes(UsnReason::FileCreate));
    CHECK(combined.Includes(UsnReason::RenameNewName));
    CHECK_FALSE(combined.Includes(UsnReason::Close));
    constexpr ReasonSet extended = combined | UsnReason::Close;
    CHECK(extended.Includes(UsnReason::Close));
    CHECK(extended.Mask() == (0x00000100U | 0x00002000U | 0x80000000U));
    // Symmetric operand order compiles too.
    constexpr ReasonSet reversed = UsnReason::Close | combined;
    CHECK(reversed == extended);
  }

  TEST_CASE("kInterestingReasons covers every consumed bit plus close") {
    for (const UsnReason reason :
         {UsnReason::FileCreate, UsnReason::FileDelete, UsnReason::RenameOldName,
          UsnReason::RenameNewName, UsnReason::DataExtend, UsnReason::DataTruncation,
          UsnReason::DataOverwrite, UsnReason::Close,}) {
      CHECK(usn_reason::kInterestingReasons.Includes(reason));
    }
  }

  TEST_CASE("kActionReasons is the interesting set without close") {
    for (const UsnReason reason :
         {UsnReason::FileCreate, UsnReason::FileDelete, UsnReason::RenameOldName,
          UsnReason::RenameNewName, UsnReason::DataExtend, UsnReason::DataTruncation,
          UsnReason::DataOverwrite,}) {
      CHECK(usn_reason::kActionReasons.Includes(reason));
    }
    CHECK_FALSE(usn_reason::kActionReasons.Includes(UsnReason::Close));
  }
}

TEST_SUITE("UsnRecord - specs") {

  TEST_CASE("IsParentEstablishing needs directory CREATE") {
    const UsnRecord dir_create{NtfsFileReference(2U), NtfsFileReference(1U), "d",
                               10, ReasonSet(0x00000100U), true,};
    CHECK(dir_create.IsParentEstablishing());
    const UsnRecord file_create{NtfsFileReference(2U), NtfsFileReference(1U), "f",
                                10, ReasonSet(0x00000100U), false,};
    CHECK_FALSE(file_create.IsParentEstablishing());
    const UsnRecord dir_delete{NtfsFileReference(2U), NtfsFileReference(1U), "d",
                               10, ReasonSet(0x00000200U), true,};
    CHECK_FALSE(dir_delete.IsParentEstablishing());
  }

  TEST_CASE("IsActionable skips close-only noise") {
    const UsnRecord close_only{NtfsFileReference(2U), NtfsFileReference(1U), "f",
                               10, ReasonSet(0x80000000U), false,};
    CHECK_FALSE(close_only.IsActionable());
    const UsnRecord create_close{NtfsFileReference(2U), NtfsFileReference(1U), "f",
                                 10, ReasonSet(0x00000100U | 0x80000000U), false,};
    CHECK(create_close.IsActionable());
    const UsnRecord data_only{NtfsFileReference(2U), NtfsFileReference(1U), "f",
                              10, ReasonSet(0x00000001U), false,};
    CHECK(data_only.IsActionable());
  }

  TEST_CASE("identity survives as strong references") {
    const UsnRecord record{NtfsFileReference(0x00010000000000B0ULL),
                           NtfsFileReference(0x00010000000000A0ULL), "late.txt", 99,
                           ReasonSet(0x00000100U), false,};
    CHECK(record.self.RecordNumber() == 0xB0ULL);
    CHECK(record.parent.RecordNumber() == 0xA0ULL);
    CHECK(record.name == "late.txt");
    CHECK(record.usn == 99);
  }

  TEST_CASE("free functions agree with the member specs") {
    // Production raw-record sites (partition, apply filter) call these: no
    // UsnRecord exists there yet (name conversion comes later).
    CHECK(usn_record::EstablishesParent(ReasonSet(0x00000100U), true));
    CHECK_FALSE(usn_record::EstablishesParent(ReasonSet(0x00000100U), false));
    CHECK_FALSE(usn_record::EstablishesParent(ReasonSet(0x00000200U), true));
    CHECK(usn_record::CarriesAction(ReasonSet(0x00000100U)));
    CHECK(usn_record::CarriesAction(ReasonSet(0x00000001U)));
    CHECK_FALSE(usn_record::CarriesAction(ReasonSet(0x80000000U)));
    CHECK_FALSE(usn_record::CarriesAction(ReasonSet(0U)));
  }
}
