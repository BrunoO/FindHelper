#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "doctest/doctest.h"
#include "index/recycle/RecycleBinIndexParser.h"

using recycle_bin_parser::ParseError;
using recycle_bin_parser::ParseRecycleBinIndex;
using recycle_bin_parser::ParseResult;

// The parser is documented as throwing no exceptions of its own; it must not be
// declared noexcept because std::string allocation may throw std::bad_alloc.
static_assert(!noexcept(ParseRecycleBinIndex(nullptr, 0)),
              "ParseRecycleBinIndex must not be noexcept (std::string may throw bad_alloc)");

namespace {

using Bytes = std::vector<std::uint8_t>;

// Layout constants mirrored from docs/docs/ntfs_recycle_bin_parser.md.
constexpr std::size_t kV1TotalSize = 24 + (260 * 2);
constexpr std::size_t kV2HeaderSize = 28;
constexpr std::uint32_t kV2MaxPathUnits = 32767;

// Appends an unsigned little-endian integer of byte_count bytes.
void AppendLe(Bytes& out, std::uint64_t value, std::size_t byte_count) {
  for (std::size_t i = 0; i < byte_count; ++i) {
    out.push_back(static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFU));
  }
}

// Appends UTF-16LE code units.
void AppendUnits(Bytes& out, const std::u16string& units) {
  for (const char16_t unit : units) {
    AppendLe(out, static_cast<std::uint64_t>(unit), 2);
  }
}

// Builds a version-1 record: 24-byte header + WCHAR[260] path field, zero-padded.
Bytes BuildV1(std::uint64_t version, std::uint64_t original_size, std::uint64_t filetime,
              const std::u16string& path) {
  Bytes out;
  out.reserve(kV1TotalSize);
  AppendLe(out, version, 8);
  AppendLe(out, original_size, 8);
  AppendLe(out, filetime, 8);
  AppendUnits(out, path);
  out.resize(kV1TotalSize, 0);
  return out;
}

// Builds a version-2 record with an explicit pathLength field (may be a lie on purpose).
Bytes BuildV2Raw(std::uint64_t original_size, std::uint64_t filetime, std::uint32_t path_length_field,
                 const std::u16string& units) {
  Bytes out;
  out.reserve(kV2HeaderSize + (units.size() * 2));
  AppendLe(out, 2, 8);
  AppendLe(out, original_size, 8);
  AppendLe(out, filetime, 8);
  AppendLe(out, path_length_field, 4);
  AppendUnits(out, units);
  return out;
}

// Returns path content followed by the terminating NUL unit.
std::u16string WithNul(const std::u16string& content) {
  return content + char16_t{0};
}

// Builds a well-formed version-2 record: pathLength = units including the NUL.
Bytes BuildV2(std::uint64_t original_size, std::uint64_t filetime, const std::u16string& content) {
  const std::u16string terminated = WithNul(content);
  return BuildV2Raw(original_size, filetime, static_cast<std::uint32_t>(terminated.size()), terminated);
}

ParseResult ParseBytes(const Bytes& bytes) {
  return ParseRecycleBinIndex(bytes.data(), bytes.size());
}

void RequireOk(const ParseResult& result) {
  REQUIRE(result.ok);
  REQUIRE(result.error == ParseError::kOk);
}

void RequireError(const ParseResult& result, ParseError expected) {
  REQUIRE_FALSE(result.ok);
  REQUIRE(result.error == expected);
}

}  // namespace

TEST_SUITE("RecycleBinIndexParser - version 1") {

  TEST_CASE("v1 valid record decoding") {
    SUBCASE("ASCII and non-ASCII surrogate decoding") {
      const Bytes ascii = BuildV1(1, 4096, 0x01D8A1B2C3D4E5F6ULL, u"C:\\dir\\a.txt");
      const ParseResult r1 = ParseBytes(ascii);
      RequireOk(r1);
      CHECK(r1.metadata.version == 1);
      CHECK(r1.metadata.original_path_utf8 == "C:\\dir\\a.txt");

      const std::u16string non_ascii = {u'A', char16_t{0xD83D}, char16_t{0xDE00}, char16_t{0xDC00},
                                        char16_t{0xD800}, u'B'};
      const ParseResult r2 = ParseBytes(BuildV1(1, 1, 1, non_ascii));
      RequireOk(r2);
      const std::string expected = std::string("A") + "\xF0\x9F\x98\x80" + "\xEF\xBF\xBD" + "\xEF\xBF\xBD" + "B";
      CHECK(r2.metadata.original_path_utf8 == expected);

      const ParseResult r3 = ParseBytes(BuildV1(1, 1, 1, std::u16string(259, u'A')));
      RequireOk(r3);
      CHECK(r3.metadata.original_path_utf8 == std::string(259, 'A'));
    }

    SUBCASE("Metadata and formatting pass-through") {
      constexpr std::uint64_t kSize = 0xFEDCBA9876543210ULL;
      constexpr std::uint64_t kFileTime = 0x01D8A1B2C3D4E5F6ULL;
      const ParseResult r = ParseBytes(BuildV1(1, kSize, kFileTime, u"C:\\a"));
      RequireOk(r);
      CHECK(r.metadata.original_size == kSize);
      CHECK(r.metadata.deletion_time_filetime == kFileTime);
      CHECK(r.metadata.original_path_utf8 == "C:\\a");
      CHECK(r.metadata.original_path_utf8.find('\0') == std::string::npos);
    }
  }

  TEST_CASE("v1 error handling") {
    Bytes trunc = BuildV1(1, 1, 1, u"C:\\a");
    trunc.pop_back();

    struct ErrorCase {
      Bytes bytes;
      ParseError expected;
    };
    const std::vector<ErrorCase> cases = {
        {std::move(trunc), ParseError::kTruncated},
        {BuildV1(1, 1, 1, std::u16string(260, u'A')), ParseError::kUnterminatedPath},
    };
    for (const auto& tc : cases) {
      RequireError(ParseBytes(tc.bytes), tc.expected);
    }
  }
}

TEST_SUITE("RecycleBinIndexParser - version 2") {

  TEST_CASE("v2 valid record decoding") {
    SUBCASE("Paths and surrogate handling") {
      const std::u16string content = u"\u00E9" + std::u16string(300, u'a');
      const ParseResult r1 = ParseBytes(BuildV2(1, 1, content));
      RequireOk(r1);
      CHECK(r1.metadata.version == 2);
      CHECK(r1.metadata.original_path_utf8 == std::string("\xC3\xA9") + std::string(300, 'a'));

      const ParseResult r2 =
          ParseBytes(BuildV2Raw(1, 1, kV2MaxPathUnits, WithNul(std::u16string(kV2MaxPathUnits - 1U, u'a'))));
      RequireOk(r2);
      CHECK(r2.metadata.original_path_utf8.size() == kV2MaxPathUnits - 1U);

      const std::u16string units_lone = {u'A', char16_t{0xD800}, char16_t{0x0000}};
      const ParseResult r3 = ParseBytes(BuildV2Raw(1, 1, 3, units_lone));
      RequireOk(r3);
      CHECK(r3.metadata.original_path_utf8 == std::string("A") + "\xEF\xBF\xBD");

      const std::u16string units_astral = {char16_t{0xD800}, char16_t{0xDC00}, char16_t{0x0000}};
      const ParseResult r4 = ParseBytes(BuildV2Raw(1, 1, 3, units_astral));
      RequireOk(r4);
      CHECK(r4.metadata.original_path_utf8 == "\xF0\x90\x80\x80");
    }

    SUBCASE("Metadata and formatting pass-through") {
      constexpr std::uint64_t kSize = 0x0123456789ABCDEFULL;
      constexpr std::uint64_t kFileTime = 0x01DA0F1E2D3C4B5AULL;
      const ParseResult r = ParseBytes(BuildV2(kSize, kFileTime, u"C:\\p"));
      RequireOk(r);
      CHECK(r.metadata.original_size == kSize);
      CHECK(r.metadata.deletion_time_filetime == kFileTime);
      CHECK(r.metadata.original_path_utf8 == "C:\\p");
      CHECK(r.metadata.original_path_utf8.find('\0') == std::string::npos);
    }
  }

  TEST_CASE("v2 error handling") {
    Bytes trunc = BuildV2(1, 1, u"C:\\a");
    trunc.resize(27);

    struct ErrorCase {
      Bytes bytes;
      ParseError expected;
    };
    const std::vector<ErrorCase> cases = {
        {std::move(trunc), ParseError::kTruncated},
        {BuildV2Raw(1, 1, 0, u"C:"), ParseError::kPathTooLong},
        {BuildV2Raw(1, 1, 100000, WithNul(u"AB")), ParseError::kPathTooLong},
        {BuildV2Raw(1, 1, kV2MaxPathUnits + 1U, std::u16string(kV2MaxPathUnits + 1U, u'A')), ParseError::kPathTooLong},
        {BuildV2Raw(1, 1, 3, u"ABC"), ParseError::kUnterminatedPath},
    };
    for (const auto& tc : cases) {
      RequireError(ParseBytes(tc.bytes), tc.expected);
    }
  }
}

TEST_SUITE("RecycleBinIndexParser - common rejects") {

  TEST_CASE("Common rejection cases") {
    struct RejectCase {
      Bytes bytes;
      ParseError expected;
    };
    const std::vector<RejectCase> cases = {
        {BuildV1(0, 1, 1, u"C:"), ParseError::kBadVersion},
        {BuildV1(3, 1, 1, u"C:"), ParseError::kBadVersion},
        {{}, ParseError::kTruncated},
        {Bytes(7, 0x01), ParseError::kTruncated},
    };
    for (const auto& tc : cases) {
      RequireError(ParseBytes(tc.bytes), tc.expected);
    }
    RequireError(ParseRecycleBinIndex(nullptr, 0), ParseError::kTruncated);
  }
}
