#pragma once

/**
 * @file RecycleBinIndexParser.h
 * @brief Standalone, cross-platform parser for NTFS Recycle Bin $I metadata files.
 *
 * Pure C++17: no Win32 headers, no logging, no platform I/O. The parser's own
 * logic throws no exceptions; std::string allocation may throw std::bad_alloc.
 * Every byte is read through bounds-checked little-endian byte shifts, so the
 * parser is portable across x86 and ARM hosts and never reinterprets wchar_t.
 *
 * Authoritative layouts (docs/docs/ntfs_recycle_bin_parser.md, section 2):
 *
 * Version 1 (Windows 7 / 8 / 8.1) - fixed 544 bytes total
 *   Offset  Size  Field
 *   0x00    8     u64  version            (must be 1)
 *   0x08    8     u64  original file size (bytes)
 *   0x10    8     u64  deletion time      (Win32 FILETIME, raw)
 *   0x18    520   WCHAR[260] path         (UTF-16LE, NUL-terminated within 260 units)
 *
 * Version 2 (Windows 10 / 11) - 28-byte header + variable path
 *   Offset  Size      Field
 *   0x00    8         u64  version            (must be 2)
 *   0x08    8         u64  original file size (bytes)
 *   0x10    8         u64  deletion time      (Win32 FILETIME, raw)
 *   0x18    4         u32  pathLength         (WCHAR count INCLUDING the NUL)
 *   0x1C    2*len     WCHAR[pathLength] path  (UTF-16LE, last unit must be NUL)
 *   Validation: pathLength must be in [1, min(32767, (size - 28) / 2)].
 *
 * Pairing: a $I file named "$I<token>" pairs with the payload "$R<token>" in the
 * same $Recycle.Bin\<SID>\ directory. This module only parses the $I record;
 * matching the sibling $R file and enriching the record is Phase 4 work.
 */

#include <cstddef>
#include <cstdint>
#include <string>

namespace recycle_bin_parser {

/// Decoded fields of one $I metadata record.
struct RecycleBinMetadata {
  std::uint64_t version = 0;
  std::uint64_t original_size = 0;
  /// RAW Win32 FILETIME (100-ns ticks since 1601-01-01 UTC). Not converted.
  std::uint64_t deletion_time_filetime = 0;
  /// Original absolute path as UTF-8, without a trailing NUL.
  std::string original_path_utf8;
};

enum class ParseError : std::uint8_t {
  kOk,  // NOLINT(readability-identifier-naming) - k prefix is project convention for enum constants
  kTruncated,         ///< Null/empty input, or fewer bytes than the layout requires. // NOLINT(readability-identifier-naming) - k prefix is project convention for enum constants
  kBadVersion,        ///< Version field is neither 1 nor 2. // NOLINT(readability-identifier-naming) - k prefix is project convention for enum constants
  kPathTooLong,       ///< v2 pathLength is 0 or exceeds the bytes actually present. // NOLINT(readability-identifier-naming) - k prefix is project convention for enum constants
  kUnterminatedPath,  ///< v1: no NUL in the 260-unit field. v2: last unit is not NUL. // NOLINT(readability-identifier-naming) - k prefix is project convention for enum constants
};

struct ParseResult {
  bool ok = false;
  ParseError error = ParseError::kTruncated;
  RecycleBinMetadata metadata;
};

/**
 * Parses one $I metadata record from a raw byte buffer.
 *
 * Never reads outside [data, data + size). Lone UTF-16 surrogates decode to
 * U+FFFD. Surrogate pairs decode to their supplementary code point.
 *
 * @param data Pointer to the file bytes (may be null only if size == 0).
 * @param size Number of valid bytes at data.
 * @return ParseResult with ok == true and metadata on success, otherwise an error code.
 */
[[nodiscard]] ParseResult ParseRecycleBinIndex(const std::uint8_t* data, std::size_t size);

}  // namespace recycle_bin_parser
