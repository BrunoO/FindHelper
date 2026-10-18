#include "index/recycle/RecycleBinIndexParser.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace recycle_bin_parser {

namespace {

// Record versions.
constexpr std::uint64_t kVersion1 = 1;
constexpr std::uint64_t kVersion2 = 2;

// Field offsets shared by both layouts.
constexpr std::size_t kVersionFieldSize = 8;
constexpr std::size_t kSizeFieldOffset = 8;
constexpr std::size_t kFileTimeFieldOffset = 16;

// v1 layout.
constexpr std::size_t kV1PathOffset = 24;
constexpr std::size_t kV1PathUnits = 260;

// v2 layout.
constexpr std::size_t kV2PathLengthOffset = 24;
constexpr std::size_t kV2PathLengthFieldSize = 4;
constexpr std::size_t kV2HeaderSize = 28;
constexpr std::size_t kV2MaxPathUnits = 32767;

constexpr std::size_t kUnitBytes = 2;      // UTF-16 code unit size
constexpr std::size_t kU64Bytes = 8;
constexpr std::size_t kBitsPerByte = 8;

// Ranges and masks for UTF-16 -> UTF-8 transcoding (RFC 3629 / RFC 2781).
constexpr std::uint32_t kHighSurrogateFirst = 0xD800U;
constexpr std::uint32_t kHighSurrogateLast = 0xDBFFU;
constexpr std::uint32_t kLowSurrogateFirst = 0xDC00U;
constexpr std::uint32_t kLowSurrogateLast = 0xDFFFU;
constexpr std::uint32_t kSupplementaryBase = 0x10000U;
constexpr std::uint32_t kSurrogateDataBits = 10U;
constexpr std::uint32_t kSurrogateDataMask = 0x3FFU;
constexpr std::uint32_t kReplacementCodePoint = 0xFFFDU;

constexpr std::uint32_t kAsciiLimit = 0x80U;
constexpr std::uint32_t kTwoByteLimit = 0x800U;
constexpr std::uint32_t kThreeByteLimit = 0x10000U;
constexpr std::uint32_t kContinuationTag = 0x80U;
constexpr std::uint32_t kContinuationMask = 0x3FU;
constexpr std::uint32_t kContinuationBits = 6U;
constexpr std::uint32_t kTwoByteLeadTag = 0xC0U;
constexpr std::uint32_t kTwoByteLeadMask = 0x1FU;
constexpr std::uint32_t kThreeByteLeadTag = 0xE0U;
constexpr std::uint32_t kThreeByteLeadMask = 0x0FU;
constexpr std::uint32_t kFourByteLeadTag = 0xF0U;
constexpr std::uint32_t kFourByteLeadMask = 0x07U;
constexpr std::uint32_t kThreeByteShift = 12U;
constexpr std::uint32_t kFourByteShift = 18U;

// Reads an unsigned little-endian integer of byte_count bytes (1..8).
// Byte shifts only: no alignment or host-endianness assumptions.
std::uint64_t ReadLe(const std::uint8_t* bytes, std::size_t byte_count) noexcept {
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < byte_count; ++i) {
    value |= static_cast<std::uint64_t>(bytes[i]) << (i * kBitsPerByte);
  }
  return value;
}

// Returns the UTF-16LE code unit at unit_index within a buffer of unit_count units.
// Caller guarantees unit_index < unit_count, which bounds every access.
std::uint32_t ReadUnit(const std::uint8_t* units, std::size_t unit_index) noexcept {
  return static_cast<std::uint32_t>(ReadLe(units + (unit_index * kUnitBytes), kUnitBytes));
}

bool IsHighSurrogate(std::uint32_t unit) noexcept {
  return unit >= kHighSurrogateFirst && unit <= kHighSurrogateLast;
}

bool IsLowSurrogate(std::uint32_t unit) noexcept {
  return unit >= kLowSurrogateFirst && unit <= kLowSurrogateLast;
}

// Appends one Unicode scalar value as 1-4 UTF-8 bytes.
void AppendUtf8(std::string& out, std::uint32_t code_point) {
  if (code_point < kAsciiLimit) {
    out.push_back(static_cast<char>(code_point));
  } else if (code_point < kTwoByteLimit) {
    out.push_back(static_cast<char>(kTwoByteLeadTag | ((code_point >> kContinuationBits) & kTwoByteLeadMask)));
    out.push_back(static_cast<char>(kContinuationTag | (code_point & kContinuationMask)));
  } else if (code_point < kThreeByteLimit) {
    out.push_back(static_cast<char>(kThreeByteLeadTag | ((code_point >> kThreeByteShift) & kThreeByteLeadMask)));
    out.push_back(static_cast<char>(kContinuationTag | ((code_point >> kContinuationBits) & kContinuationMask)));
    out.push_back(static_cast<char>(kContinuationTag | (code_point & kContinuationMask)));
  } else {
    out.push_back(static_cast<char>(kFourByteLeadTag | ((code_point >> kFourByteShift) & kFourByteLeadMask)));
    out.push_back(static_cast<char>(kContinuationTag | ((code_point >> kThreeByteShift) & kContinuationMask)));
    out.push_back(static_cast<char>(kContinuationTag | ((code_point >> kContinuationBits) & kContinuationMask)));
    out.push_back(static_cast<char>(kContinuationTag | (code_point & kContinuationMask)));
  }
}

// Decodes the code point starting at unit_index. Sets out_advance to the number of
// units consumed (2 for a valid surrogate pair, otherwise 1). Lone surrogates
// yield U+FFFD. Requires unit_index < unit_count.
std::uint32_t DecodeCodePoint(const std::uint8_t* units, std::size_t unit_count,
                              std::size_t unit_index, std::size_t& out_advance) noexcept {
  const std::uint32_t unit = ReadUnit(units, unit_index);
  out_advance = 1;
  if (IsLowSurrogate(unit)) {
    return kReplacementCodePoint;
  }
  if (!IsHighSurrogate(unit)) {
    return unit;
  }
  if (unit_index + 1 >= unit_count) {
    return kReplacementCodePoint;
  }
  const std::uint32_t next = ReadUnit(units, unit_index + 1);
  if (!IsLowSurrogate(next)) {
    return kReplacementCodePoint;
  }
  out_advance = 2;
  return kSupplementaryBase + ((unit - kHighSurrogateFirst) << kSurrogateDataBits) +
         ((next - kLowSurrogateFirst) & kSurrogateDataMask);
}

// Transcodes UTF-16LE units [0, unit_count) to UTF-8, stopping at the first NUL.
// The returned string never contains the terminating NUL.
std::string TranscodeUnits(const std::uint8_t* units, std::size_t unit_count) {
  std::string out;
  // Worst-case UTF-8 bytes per UTF-16 unit is 3 (BMP code unit). A surrogate pair
  // is 2 units -> 4 bytes, so 3 per unit is an upper bound. unit_count <= 32767
  // (v2) or 260 (v1), so the product cannot overflow std::size_t.
  out.reserve(unit_count * 3);
  std::size_t index = 0;
  while (index < unit_count) {
    if (ReadUnit(units, index) == 0) {
      break;
    }
    std::size_t advance = 1;
    const std::uint32_t code_point = DecodeCodePoint(units, unit_count, index, advance);
    AppendUtf8(out, code_point);
    index += advance;
  }
  return out;
}

// True if any of the first unit_count units is NUL.
bool HasNulUnit(const std::uint8_t* units, std::size_t unit_count) noexcept {
  for (std::size_t i = 0; i < unit_count; ++i) {
    if (ReadUnit(units, i) == 0) {
      return true;
    }
  }
  return false;
}

ParseResult MakeError(ParseError error) noexcept {
  return {false, error, {}};
}

// Fills a successful result. Caller has already validated that data holds the
// fixed header and that path spans unit_count valid units.
ParseResult MakeSuccess(const std::uint8_t* data, std::uint64_t version,
                        const std::uint8_t* path, std::size_t unit_count) {
  return {true, ParseError::kOk,
          {version, ReadLe(data + kSizeFieldOffset, kU64Bytes),
           ReadLe(data + kFileTimeFieldOffset, kU64Bytes), TranscodeUnits(path, unit_count),},};
}

ParseResult ParseV1(const std::uint8_t* data, std::size_t size) {
  constexpr std::size_t kV1TotalSize = kV1PathOffset + (kV1PathUnits * kUnitBytes);
  if (size < kV1TotalSize) {
    return MakeError(ParseError::kTruncated);
  }
  const std::uint8_t* path = data + kV1PathOffset;
  if (!HasNulUnit(path, kV1PathUnits)) {
    return MakeError(ParseError::kUnterminatedPath);
  }
  return MakeSuccess(data, kVersion1, path, kV1PathUnits);
}

ParseResult ParseV2(const std::uint8_t* data, std::size_t size) {
  if (size < kV2HeaderSize) {
    return MakeError(ParseError::kTruncated);
  }

  // Cap the declared length by both the format maximum and the bytes actually present.
  const std::size_t remaining_units = (size - kV2HeaderSize) / kUnitBytes;
  const std::size_t max_units = remaining_units < kV2MaxPathUnits ? remaining_units : kV2MaxPathUnits;
  const auto path_length = static_cast<std::size_t>(
      ReadLe(data + kV2PathLengthOffset, kV2PathLengthFieldSize));
  if (path_length == 0 || path_length > max_units) {
    return MakeError(ParseError::kPathTooLong);
  }

  const std::uint8_t* path = data + kV2HeaderSize;
  if (ReadUnit(path, path_length - 1) != 0) {
    return MakeError(ParseError::kUnterminatedPath);
  }
  return MakeSuccess(data, kVersion2, path, path_length);
}

}  // namespace

ParseResult ParseRecycleBinIndex(const std::uint8_t* data, std::size_t size) {
  if (data == nullptr || size < kVersionFieldSize) {
    return MakeError(ParseError::kTruncated);
  }

  const std::uint64_t version = ReadLe(data, kVersionFieldSize);
  if (version == kVersion1) {
    return ParseV1(data, size);
  }
  if (version == kVersion2) {
    return ParseV2(data, size);
  }
  return MakeError(ParseError::kBadVersion);
}

}  // namespace recycle_bin_parser
