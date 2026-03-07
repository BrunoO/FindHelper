/**
 * @file UsnRecordUtils.cpp
 * @brief Implementation of safe USN record parsing utilities
 *
 * This file provides safe parsing functions for USN_RECORD_V2 structures
 * with comprehensive validation to prevent buffer overreads and handle
 * malformed records gracefully.
 */

#include "usn/UsnRecordUtils.h"

#include "utils/Logger.h"

namespace usn_record_utils {

// Expected version for USN_RECORD_V2
constexpr WORD kExpectedMajorVersion = 2;
constexpr WORD kExpectedMinorVersion = 0;

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
                               const DWORD& offset, PUSN_RECORD_V2& record_out) {
  record_out = nullptr;

  // Validate we have enough bytes for the record header
  if (offset + SizeOfUsnRecordV2() > buffer_size) {
    LOG_WARNING_BUILD("Incomplete USN record at offset " << offset
                      << ", buffer size " << buffer_size);
    return false;
  }

  // Safe cast after bounds validation
  auto record =
      reinterpret_cast<PUSN_RECORD_V2>(const_cast<char*>(buffer) + offset);  // NOLINT(cppcoreguidelines-pro-type-const-cast) NOSONAR(cpp:S859) - required after bounds validation; buffer is API-const

  // Validate RecordLength is non-zero
  if (record->RecordLength == 0) {
    LOG_WARNING_BUILD("Invalid USN record at offset " << offset
                      << ": RecordLength is zero");
    return false;
  }

  // Validate RecordLength is at least the minimum size
  if (const DWORD min_record_length = SizeOfUsnRecordV2(); record->RecordLength < min_record_length) {
    LOG_WARNING_BUILD("Invalid USN record at offset " << offset
                      << ": RecordLength (" << record->RecordLength
                      << ") is less than minimum (" << min_record_length << ")");
    return false;
  }

  // Validate RecordLength doesn't exceed buffer bounds
  if (offset + record->RecordLength > buffer_size) {
    LOG_WARNING_BUILD("Invalid USN record at offset " << offset
                      << ": RecordLength (" << record->RecordLength
                      << ") exceeds buffer bounds (buffer size: " << buffer_size
                      << ")");
    return false;
  }

  // Validate MajorVersion and MinorVersion match USN_RECORD_V2
  if (record->MajorVersion != kExpectedMajorVersion) {
    LOG_WARNING_BUILD("Invalid USN record at offset " << offset
                      << ": MajorVersion (" << record->MajorVersion
                      << ") does not match expected version ("
                      << kExpectedMajorVersion << ")");
    return false;
  }

  if (record->MinorVersion != kExpectedMinorVersion) {
    LOG_WARNING_BUILD("Invalid USN record at offset " << offset
                      << ": MinorVersion (" << record->MinorVersion
                      << ") does not match expected version ("
                      << kExpectedMinorVersion << ")");
    return false;
  }

  // Validate filename offset and length are within record bounds
  if (AddWords(record->FileNameOffset, record->FileNameLength) >
      record->RecordLength) {
    LOG_WARNING_BUILD("Invalid USN record at offset " << offset
                      << ": filename extends beyond record (FileNameOffset: "
                      << record->FileNameOffset
                      << ", FileNameLength: " << record->FileNameLength
                      << ", RecordLength: " << record->RecordLength << ")");
    return false;
  }

  // All validations passed - record is safe to use
  record_out = record;
  return true;
}

} // namespace usn_record_utils
