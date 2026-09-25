#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace mft {

#pragma pack(push, 1)

// NTFS Attribute Type Codes
inline constexpr uint32_t kAttributeStandardInformation = 0x10U;
inline constexpr uint32_t kAttributeAttributeList = 0x20U;
inline constexpr uint32_t kAttributeFileName = 0x30U;
inline constexpr uint32_t kAttributeData = 0x80U;
inline constexpr uint32_t kAttributeEnd = 0xFFFFFFFFU;

// Record signature 'FILE' in little endian
inline constexpr uint32_t kFileRecordSignature = 0x454C4946U;

// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays)

/**
 * @struct NtfsFileRecordHeader
 * @brief On-disk header for NTFS MFT file records (FILE).
 */
struct NtfsFileRecordHeader {
  uint32_t signature;
  uint16_t usa_offset;
  uint16_t usa_count;
  uint64_t lsn;
  uint16_t sequence_number;
  uint16_t link_count;
  uint16_t first_attribute_offset;
  uint16_t flags;
  uint32_t first_free_byte;
  uint32_t bytes_available;
  uint64_t base_file_record_number : 48;
  uint64_t base_file_record_sequence : 16;
  uint16_t next_attribute_number;
  uint16_t segment_number_high_part;
  uint32_t segment_number_low_part;

  [[nodiscard]] bool IsValid() const noexcept {
    return signature == kFileRecordSignature;
  }

  [[nodiscard]] bool IsInUse() const noexcept {
    return (flags & 0x0001U) != 0;
  }

  [[nodiscard]] bool IsDirectory() const noexcept {
    return (flags & 0x0002U) != 0;
  }

  [[nodiscard]] uint64_t SegmentNumber() const noexcept {
    return (static_cast<uint64_t>(segment_number_high_part) << 32U) | segment_number_low_part;
  }
};

/**
 * @struct NtfsAttributeRecordHeader
 * @brief Common header for resident and non-resident MFT attributes.
 */
struct NtfsAttributeRecordHeader {
  uint32_t type_code;
  uint32_t record_length;
  uint8_t form_code;
  uint8_t name_length;
  uint16_t name_offset;
  uint16_t flags;
  uint16_t instance;

  union {
    struct {
      uint32_t value_length;
      uint16_t value_offset;
      std::array<uint8_t, 2> reserved;
    } resident;

    struct {
      int64_t lowest_vcn;
      int64_t highest_vcn;
      uint16_t data_run_offset;
      uint16_t compression_size;
      std::array<uint8_t, 4> padding;
      uint64_t allocated_length;
      uint64_t file_size;
      uint64_t valid_data_length;
      uint64_t compressed;
    } nonresident;
  } form;

  [[nodiscard]] bool IsNonResident() const noexcept {
    return form_code != 0;
  }
};

/**
 * @struct NtfsStandardInformation
 * @brief Attribute 0x10 ($STANDARD_INFORMATION) payload.
 */
struct NtfsStandardInformation {
  uint64_t creation_time;
  uint64_t last_modification_time;
  uint64_t mft_change_time;
  uint64_t last_access_time;
  uint32_t file_attributes;
};

/**
 * @struct NtfsFileNameAttribute
 * @brief Attribute 0x30 ($FILE_NAME) payload.
 */
struct NtfsFileNameAttribute {
  uint64_t parent_directory;
  uint64_t creation_time;
  uint64_t last_modification_time;
  uint64_t mft_change_time;
  uint64_t last_access_time;
  uint64_t allocated_size;
  uint64_t real_size;
  uint32_t flags;
  uint32_t ea_reparse;
  uint8_t file_name_length;
  uint8_t file_name_namespace;
  // Flexible array member: variable-length UTF-16 tail; indexed by length, never as a fixed array.
  char16_t file_name[1];  // NOSONAR(cpp:S5945) - NTFS on-disk flexible tail; std::string/std::array would break layout and length-indexed access
};

// NOLINTEND(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays)

#pragma pack(pop)

}  // namespace mft
