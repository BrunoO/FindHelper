#pragma once

#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem
#include <winioctl.h>

/**
 * UsnRecordUtils - Type-safe utilities for USN record operations
 *
 * This header provides utility functions to avoid C4018 signed/unsigned
 * comparison warnings when working with USN records and Windows API types.
 *
 * PROBLEM:
 * - sizeof() returns size_t (64-bit on 64-bit systems)
 * - Windows API uses DWORD (32-bit unsigned)
 * - USN_RECORD_V2 uses WORD (16-bit) for FileNameOffset/FileNameLength
 * - Comparing these types directly triggers C4018 warnings
 *
 * SOLUTION:
 * - Use these utility functions to convert types consistently
 * - All comparisons use DWORD to match Windows API conventions
 * - Prevents warnings and makes code more maintainable
 *
 * USAGE:
 *   // Instead of: offset + sizeof(USN_RECORD_V2) > bytesReturned
 *   // Use:        offset + usn_record_utils::SizeOfUsnRecordV2() > bytesReturned
 *
 *   // Instead of: record->FileNameOffset + record->FileNameLength > record->RecordLength
 *   // Use:        usn_record_utils::AddWords(record->FileNameOffset, record->FileNameLength) > record->RecordLength
 */

namespace usn_record_utils {
  // Convert sizeof() result to DWORD to match Windows API types
  // This avoids C4018 warnings when comparing with DWORD values
  constexpr DWORD SizeOfUsnRecordV2() {
    return static_cast<DWORD>(sizeof(USN_RECORD_V2));  // NOSONAR(cpp:S1905) - Cast needed to match Windows API types and avoid C4018 warnings
  }

  constexpr DWORD SizeOfUsn() {
    return static_cast<DWORD>(sizeof(USN));  // NOSONAR(cpp:S1905) - Cast needed to match Windows API types and avoid C4018 warnings
  }

  // Convert WORD values to DWORD for safe arithmetic and comparisons
  // This avoids C4018 warnings when comparing WORD + WORD with DWORD
  constexpr DWORD ToDword(WORD value) {
    return static_cast<DWORD>(value);
  }

  // Safe addition of WORD values that returns DWORD
  constexpr DWORD AddWords(WORD a, WORD b) {
    return static_cast<DWORD>(a) + static_cast<DWORD>(b);
  }

  /**
   * @brief Validates and safely parses a USN record from a buffer
   *
   * This function performs comprehensive validation before allowing access to
   * the USN record structure:
   * 1. Validates buffer bounds for record header
   * 2. Validates RecordLength is non-zero and within bounds
   * 3. Validates MajorVersion and MinorVersion match USN_RECORD_V2
   * 4. Validates filename offset/length are within record bounds
   *
   * @param buffer Pointer to the buffer containing USN records
   * @param buffer_size Total size of the buffer in bytes
   * @param offset Current offset into the buffer (updated on success)
   * @param record_out Output parameter: pointer to validated record (nullptr if invalid)
   * @return true if record is valid and can be safely accessed, false otherwise
   */
  bool ValidateAndParseUsnRecord(const char* buffer, DWORD buffer_size,
                                 const DWORD& offset, PUSN_RECORD_V2& record_out);
}
