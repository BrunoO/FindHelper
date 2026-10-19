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
static_assert(usn_reason::EstablishesParent(ReasonSet(0x00000100U), true),
              "directory CREATE must establish parents");
static_assert(!usn_reason::CarriesAction(ReasonSet(0x80000000U)),
              "close-only must not carry action");

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

  TEST_CASE("EstablishesParent and CarriesAction predicates") {
    CHECK(usn_reason::EstablishesParent(ReasonSet(0x00000100U), true));
    CHECK_FALSE(usn_reason::EstablishesParent(ReasonSet(0x00000100U), false));
    CHECK_FALSE(usn_reason::EstablishesParent(ReasonSet(0x00000200U), true));
    CHECK(usn_reason::CarriesAction(ReasonSet(0x00000100U)));
    CHECK(usn_reason::CarriesAction(ReasonSet(0x00000100U | 0x80000000U)));
    CHECK(usn_reason::CarriesAction(ReasonSet(0x00000001U)));
    CHECK_FALSE(usn_reason::CarriesAction(ReasonSet(0x80000000U)));
    CHECK_FALSE(usn_reason::CarriesAction(ReasonSet(0U)));
  }
}

TEST_SUITE("UsnRecord - value object") {

  TEST_CASE("identity survives as strong references") {
    const UsnRecord record{NtfsFileReference(0x00010000000000B0ULL),
                           NtfsFileReference(0x00010000000000A0ULL), "late.txt", 99,
                           ReasonSet(0x00000100U), false,};
    CHECK(record.self.RecordNumber() == 0xB0ULL);
    CHECK(record.parent.RecordNumber() == 0xA0ULL);
    CHECK(record.name == "late.txt");
    CHECK(record.usn == 99);
  }
}

#ifdef _WIN32

#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include
#include <winioctl.h>

#include <vector>

#include "UsnSyntheticRecordHelpers.h"

using usn_record::AddWords;
using usn_record::SizeOfUsn;
using usn_record::SizeOfUsnRecordV2;
using usn_record::ValidateAndParseUsnRecord;
using usn_test_helpers::AppendSyntheticRecord;

TEST_SUITE("UsnRecord - Basic constexpr utilities") {

  TEST_CASE("SizeOfUsnRecordV2 returns correct DWORD size") {
    constexpr DWORD expected = static_cast<DWORD>(sizeof(USN_RECORD_V2));
    CHECK(SizeOfUsnRecordV2() == expected);
  }

  TEST_CASE("SizeOfUsn returns correct DWORD size") {
    constexpr DWORD expected = static_cast<DWORD>(sizeof(USN));
    CHECK(SizeOfUsn() == expected);
  }

  TEST_CASE("AddWords performs safe WORD addition to DWORD") {
    constexpr WORD a = 0xFFFEU;
    constexpr WORD b = 0x0005U;
    constexpr DWORD sum = AddWords(a, b);
    CHECK(sum == 0x10003U);
  }
}

TEST_SUITE("UsnRecord - ValidateAndParseUsnRecord") {

  TEST_CASE("Valid single USN record parses successfully") {
    std::vector<char> buffer;
    AppendSyntheticRecord(buffer, 100, 5, USN_REASON_FILE_CREATE,
                          FILE_ATTRIBUTE_NORMAL, L"test.txt");

    PUSN_RECORD_V2 record_out = nullptr;
    const DWORD offset = 0;
    const bool valid = ValidateAndParseUsnRecord(
        buffer.data(), static_cast<DWORD>(buffer.size()), offset, record_out);

    CHECK(valid);
    REQUIRE(record_out != nullptr);
    CHECK(record_out->FileReferenceNumber == 100);
    CHECK(record_out->ParentFileReferenceNumber == 5);
    CHECK(record_out->MajorVersion == 2);
    CHECK(record_out->MinorVersion == 0);
  }

  TEST_CASE("Sequential valid USN records parse at offsets") {
    std::vector<char> buffer;
    AppendSyntheticRecord(buffer, 101, 5, USN_REASON_FILE_CREATE,
                          FILE_ATTRIBUTE_NORMAL, L"first.txt");
    AppendSyntheticRecord(buffer, 102, 5, USN_REASON_FILE_DELETE,
                          FILE_ATTRIBUTE_NORMAL, L"second.txt");

    PUSN_RECORD_V2 record1 = nullptr;
    DWORD offset = 0;
    bool valid1 = ValidateAndParseUsnRecord(
        buffer.data(), static_cast<DWORD>(buffer.size()), offset, record1);

    CHECK(valid1);
    REQUIRE(record1 != nullptr);
    CHECK(record1->FileReferenceNumber == 101);

    offset += record1->RecordLength;
    PUSN_RECORD_V2 record2 = nullptr;
    bool valid2 = ValidateAndParseUsnRecord(
        buffer.data(), static_cast<DWORD>(buffer.size()), offset, record2);

    CHECK(valid2);
    REQUIRE(record2 != nullptr);
    CHECK(record2->FileReferenceNumber == 102);
  }

  TEST_CASE("Buffer too small for header fails validation") {
    std::vector<char> buffer(SizeOfUsnRecordV2() - 1, 0);

    PUSN_RECORD_V2 record_out = reinterpret_cast<PUSN_RECORD_V2>(1234);
    const DWORD offset = 0;
    const bool valid = ValidateAndParseUsnRecord(
        buffer.data(), static_cast<DWORD>(buffer.size()), offset, record_out);

    CHECK_FALSE(valid);
    CHECK(record_out == nullptr);
  }

  TEST_CASE("Offset beyond buffer fails validation") {
    std::vector<char> buffer(SizeOfUsnRecordV2() * 2, 0);

    PUSN_RECORD_V2 record_out = nullptr;
    const DWORD offset = static_cast<DWORD>(buffer.size());
    const bool valid = ValidateAndParseUsnRecord(
        buffer.data(), static_cast<DWORD>(buffer.size()), offset, record_out);

    CHECK_FALSE(valid);
    CHECK(record_out == nullptr);
  }

  TEST_CASE("Zero RecordLength fails validation") {
    std::vector<char> buffer(SizeOfUsnRecordV2(), 0);
    auto* raw_rec = reinterpret_cast<PUSN_RECORD_V2>(buffer.data());
    raw_rec->RecordLength = 0;
    raw_rec->MajorVersion = 2;
    raw_rec->MinorVersion = 0;

    PUSN_RECORD_V2 record_out = nullptr;
    const DWORD offset = 0;
    const bool valid = ValidateAndParseUsnRecord(
        buffer.data(), static_cast<DWORD>(buffer.size()), offset, record_out);

    CHECK_FALSE(valid);
    CHECK(record_out == nullptr);
  }

  TEST_CASE("RecordLength smaller than minimum header size fails validation") {
    std::vector<char> buffer(SizeOfUsnRecordV2(), 0);
    auto* raw_rec = reinterpret_cast<PUSN_RECORD_V2>(buffer.data());
    raw_rec->RecordLength = SizeOfUsnRecordV2() - 1;
    raw_rec->MajorVersion = 2;
    raw_rec->MinorVersion = 0;

    PUSN_RECORD_V2 record_out = nullptr;
    const DWORD offset = 0;
    const bool valid = ValidateAndParseUsnRecord(
        buffer.data(), static_cast<DWORD>(buffer.size()), offset, record_out);

    CHECK_FALSE(valid);
    CHECK(record_out == nullptr);
  }

  TEST_CASE("RecordLength exceeding buffer bounds fails validation") {
    std::vector<char> buffer(SizeOfUsnRecordV2(), 0);
    auto* raw_rec = reinterpret_cast<PUSN_RECORD_V2>(buffer.data());
    raw_rec->RecordLength = static_cast<DWORD>(buffer.size()) + 10;
    raw_rec->MajorVersion = 2;
    raw_rec->MinorVersion = 0;

    PUSN_RECORD_V2 record_out = nullptr;
    const DWORD offset = 0;
    const bool valid = ValidateAndParseUsnRecord(
        buffer.data(), static_cast<DWORD>(buffer.size()), offset, record_out);

    CHECK_FALSE(valid);
    CHECK(record_out == nullptr);
  }

  TEST_CASE("Invalid MajorVersion fails validation") {
    std::vector<char> buffer;
    AppendSyntheticRecord(buffer, 100, 5, USN_REASON_FILE_CREATE,
                          FILE_ATTRIBUTE_NORMAL, L"test.txt");
    auto* raw_rec = reinterpret_cast<PUSN_RECORD_V2>(buffer.data());
    raw_rec->MajorVersion = 3;  // Expected 2

    PUSN_RECORD_V2 record_out = nullptr;
    const DWORD offset = 0;
    const bool valid = ValidateAndParseUsnRecord(
        buffer.data(), static_cast<DWORD>(buffer.size()), offset, record_out);

    CHECK_FALSE(valid);
    CHECK(record_out == nullptr);
  }

  TEST_CASE("Invalid MinorVersion fails validation") {
    std::vector<char> buffer;
    AppendSyntheticRecord(buffer, 100, 5, USN_REASON_FILE_CREATE,
                          FILE_ATTRIBUTE_NORMAL, L"test.txt");
    auto* raw_rec = reinterpret_cast<PUSN_RECORD_V2>(buffer.data());
    raw_rec->MinorVersion = 1;  // Expected 0

    PUSN_RECORD_V2 record_out = nullptr;
    const DWORD offset = 0;
    const bool valid = ValidateAndParseUsnRecord(
        buffer.data(), static_cast<DWORD>(buffer.size()), offset, record_out);

    CHECK_FALSE(valid);
    CHECK(record_out == nullptr);
  }

  TEST_CASE("Filename extending beyond RecordLength fails validation") {
    std::vector<char> buffer;
    AppendSyntheticRecord(buffer, 100, 5, USN_REASON_FILE_CREATE,
                          FILE_ATTRIBUTE_NORMAL, L"test.txt");
    auto* raw_rec = reinterpret_cast<PUSN_RECORD_V2>(buffer.data());
    raw_rec->FileNameLength = static_cast<WORD>(raw_rec->RecordLength);

    PUSN_RECORD_V2 record_out = nullptr;
    const DWORD offset = 0;
    const bool valid = ValidateAndParseUsnRecord(
        buffer.data(), static_cast<DWORD>(buffer.size()), offset, record_out);

    CHECK_FALSE(valid);
    CHECK(record_out == nullptr);
  }

  TEST_CASE("Exact filename boundary (FileNameOffset + FileNameLength == RecordLength) is valid") {
    std::vector<char> buffer;
    AppendSyntheticRecord(buffer, 100, 5, USN_REASON_FILE_CREATE,
                          FILE_ATTRIBUTE_NORMAL, L"exact");
    auto* raw_rec = reinterpret_cast<PUSN_RECORD_V2>(buffer.data());
    raw_rec->RecordLength = AddWords(raw_rec->FileNameOffset, raw_rec->FileNameLength);

    PUSN_RECORD_V2 record_out = nullptr;
    const DWORD offset = 0;
    const bool valid = ValidateAndParseUsnRecord(
        buffer.data(), static_cast<DWORD>(buffer.size()), offset, record_out);

    CHECK(valid);
    REQUIRE(record_out != nullptr);
    CHECK(record_out->RecordLength == AddWords(raw_rec->FileNameOffset, raw_rec->FileNameLength));
  }
}

#endif  // _WIN32
