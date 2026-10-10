#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstdint>
#include <type_traits>
#include <unordered_map>

#include "doctest/doctest.h"
#include "index/NtfsFileReference.h"
#include "utils/HashMapAliases.h"

using ntfs_file_reference::MftRecordNumber;
using ntfs_file_reference::NtfsFileReference;

// Compile-time proof the VOs are usable in constant expressions.
static_assert(NtfsFileReference(0x00010000000000A0ULL).RecordNumber() == 0xA0ULL,
              "RecordNumber must strip the sequence bits");
static_assert(MftRecordNumber::FromFileReference(0x00010000000000A0ULL).record_number == 0xA0ULL,
              "FromFileReference must strip the sequence bits");
static_assert(MftRecordNumber(0xA0ULL) == MftRecordNumber(0xA0ULL),
              "record equality must hold");
static_assert(MftRecordNumber(0xA0ULL) != MftRecordNumber(0xB0ULL),
              "record inequality must hold");

TEST_SUITE("NtfsFileReference - full reference") {

  TEST_CASE("RecordNumber strips the 16-bit sequence") {
    constexpr NtfsFileReference ref(0x00010000000000A0ULL);
    CHECK(ref.RecordNumber() == 0xA0ULL);
  }

  TEST_CASE("SameRecordAs ignores sequence differences") {
    constexpr NtfsFileReference a(0x00010000000000A0ULL);
    constexpr NtfsFileReference b(0x00020000000000A0ULL);
    CHECK(a.SameRecordAs(b));
    CHECK_FALSE(a == b);
  }

  TEST_CASE("free functions delegate to the type") {
    CHECK(ntfs_file_reference::RecordNumber(0x00010000000000A0ULL) == 0xA0ULL);
    CHECK(ntfs_file_reference::SameRecordNumber(0x00010000000000A0ULL, 0x00020000000000A0ULL));
    CHECK(ntfs_file_reference::IsRootDirectoryRecord(5ULL));
  }

  TEST_CASE("works as a flat_hash_map_t key (production map alias)") {
    // Covers the hasher on the active configuration: std::hash by default,
    // boost::hash with FAST_LIBS_BOOST (Windows CI). Identity match: same
    // record but different sequence is a different key (contrast the
    // MftRecordNumber test above, where it collides by design).
    flat_hash_map_t<NtfsFileReference, uint64_t> by_id;
    by_id[NtfsFileReference(0x00010000000000A0ULL)] = 42U;
    const auto same = by_id.find(NtfsFileReference(0x00010000000000A0ULL));
    REQUIRE(same != by_id.end());
    CHECK(same->second == 42U);
    CHECK(by_id.find(NtfsFileReference(0x00020000000000A0ULL)) == by_id.end());
    CHECK(by_id.size() == 1U);
  }
}

TEST_SUITE("MftRecordNumber - stripped key") {

  TEST_CASE("FromFileReference strips; raw constructor keeps") {
    constexpr MftRecordNumber stripped = MftRecordNumber::FromFileReference(0x00FF0000000000A0ULL);
    CHECK(stripped.record_number == 0xA0ULL);
    constexpr MftRecordNumber already(0xA0ULL);
    CHECK(already == stripped);
  }

  TEST_CASE("works as an unordered-map key") {
    std::unordered_map<MftRecordNumber, uint64_t> by_record;
    by_record[MftRecordNumber::FromFileReference(0x00010000000000A0ULL)] = 42U;
    // Same record, different sequence: same bucket, no duplicate key.
    CHECK(by_record[MftRecordNumber::FromFileReference(0x00020000000000A0ULL)] == 42U);
    CHECK(by_record.size() == 1U);
  }

  TEST_CASE("works as a flat_hash_map_t key (production map alias)") {
    // Covers the hasher on the active configuration: std::hash by default,
    // boost::hash with FAST_LIBS_BOOST (Windows CI). A broken specialization
    // on either path fails here, not in production lookups.
    flat_hash_map_t<MftRecordNumber, uint64_t> by_record;
    by_record[MftRecordNumber::FromFileReference(0x00010000000000A0ULL)] = 42U;
    const auto it = by_record.find(MftRecordNumber::FromFileReference(0x00020000000000A0ULL));
    REQUIRE(it != by_record.end());
    CHECK(it->second == 42U);
  }

  TEST_CASE("is not constructible from uint64_t implicitly") {
    static_assert(!std::is_convertible_v<uint64_t, MftRecordNumber>,
                  "bare record numbers must not convert implicitly");
    static_assert(!std::is_convertible_v<uint64_t, NtfsFileReference>,
                  "bare FRNs must not convert implicitly");
  }
}
