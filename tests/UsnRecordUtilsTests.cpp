#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "usn/UsnRecordUtils.h"

#ifdef _WIN32

#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include
#include <winioctl.h>

#include <cstdint>
#include <vector>

#include "UsnSyntheticRecordHelpers.h"

using usn_record_utils::AddWords;
using usn_record_utils::SizeOfUsn;
using usn_record_utils::SizeOfUsnRecordV2;
using usn_record_utils::ValidateAndParseUsnRecord;
using usn_test_helpers::AppendSyntheticRecord;

TEST_SUITE("UsnRecordUtils - Basic constexpr utilities") {

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

TEST_SUITE("UsnRecordUtils - ValidateAndParseUsnRecord") {

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
    // Extend FileNameLength so FileNameOffset + FileNameLength > RecordLength
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
    // Adjust RecordLength so that it equals FileNameOffset + FileNameLength exactly
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
