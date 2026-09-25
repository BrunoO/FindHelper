#include "index/mft/RawMftRecordParser.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>

#include "index/FileName.h"
#include "index/NtfsFileReference.h"

// NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-type-union-access,readability-magic-numbers,cppcoreguidelines-pro-bounds-array-to-pointer-decay)

namespace mft {

namespace {

struct FoundFileName {
  uint64_t parent_frn = 0;
  std::string name;
  uint8_t file_namespace = 0;
  uint64_t fn_size = 0;
};

struct ParsedRecordAttributes {
  FILETIME modification_time = kFileTimeNotLoaded;
  uint64_t data_file_size = kFileSizeNotLoaded;
  bool has_data_attr = false;
  std::vector<FoundFileName> found_names;
};

// Decodes one code point at index; returns {code_point, units consumed}.
// A valid surrogate pair consumes 2 units, everything else 1 (lone surrogates
// pass through and encode as 3-byte sequences, matching the prior behavior).
inline std::pair<uint32_t, size_t> DecodeOneCodePoint(const char16_t* chars, size_t length,
                                                      size_t index) {
  const uint32_t code_point = chars[index];
  if (code_point >= 0xD800U && code_point <= 0xDBFFU && index + 1 < length) {
    const uint32_t next = chars[index + 1];
    if (next >= 0xDC00U && next <= 0xDFFFU) {
      return {(((code_point - 0xD800U) << 10U) | (next - 0xDC00U)) + 0x10000U, 2};
    }
  }
  return {code_point, 1};
}

inline void AppendUtf8CodePoint(std::string& out, uint32_t code_point) {
  if (code_point < 0x80U) {
    out.push_back(static_cast<char>(code_point));
  } else if (code_point < 0x800U) {
    out.push_back(static_cast<char>(0xC0U | ((code_point >> 6U) & 0x1FU)));
    out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
  } else if (code_point < 0x10000U) {
    out.push_back(static_cast<char>(0xE0U | ((code_point >> 12U) & 0x0FU)));
    out.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
  } else {
    out.push_back(static_cast<char>(0xF0U | ((code_point >> 18U) & 0x07U)));
    out.push_back(static_cast<char>(0x80U | ((code_point >> 12U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
  }
}

inline void ParseStandardInformationAttribute(
    const NtfsAttributeRecordHeader* attr,
    const char* attr_ptr,
    FILETIME& out_time) {
  if (attr->type_code == kAttributeStandardInformation && !attr->IsNonResident() &&
      attr->form.resident.value_offset + sizeof(NtfsStandardInformation) <= attr->record_length) {
    const auto* std_info = reinterpret_cast<const NtfsStandardInformation*>(  // NOSONAR(cpp:S3630) - on-disk NTFS layout; value bounds validated above
        attr_ptr + attr->form.resident.value_offset);
    out_time.dwLowDateTime = static_cast<uint32_t>(std_info->last_modification_time & 0xFFFFFFFFU);
    out_time.dwHighDateTime = static_cast<uint32_t>(std_info->last_modification_time >> 32U);
  }
}

inline void ParseFileNameAttribute(
    const NtfsAttributeRecordHeader* attr,
    const char* attr_ptr,
    std::vector<FoundFileName>& out_names) {
  if (attr->type_code != kAttributeFileName || attr->IsNonResident() ||
      attr->form.resident.value_offset + sizeof(NtfsFileNameAttribute) > attr->record_length) {
    return;
  }

  const auto* fn_attr = reinterpret_cast<const NtfsFileNameAttribute*>(  // NOSONAR(cpp:S3630) - on-disk NTFS layout; value bounds validated above
      attr_ptr + attr->form.resident.value_offset);
  if (const size_t required_bytes = sizeof(NtfsFileNameAttribute) +
                                   (static_cast<size_t>(fn_attr->file_name_length) > 1
                                        ? (static_cast<size_t>(fn_attr->file_name_length) - 1) * sizeof(char16_t)
                                        : 0);
      attr->form.resident.value_offset + required_bytes <= attr->record_length) {
    FoundFileName fn;
    fn.parent_frn = fn_attr->parent_directory;
    fn.file_namespace = fn_attr->file_name_namespace;
    fn.fn_size = fn_attr->real_size;
    fn.name = RawMftRecordParser::TranscodeUtf16ToUtf8(fn_attr->file_name, fn_attr->file_name_length);
    out_names.push_back(std::move(fn));
  }
}

inline void ParseDataAttribute(
    const NtfsAttributeRecordHeader* attr,
    uint64_t& out_size,
    bool& out_has_data) {
  if (attr->type_code == kAttributeData && attr->name_length == 0) {
    if (attr->IsNonResident()) {
      if (attr->form.nonresident.lowest_vcn == 0) {
        out_size = attr->form.nonresident.file_size;
        out_has_data = true;
      }
    } else {
      out_size = attr->form.resident.value_length;
      out_has_data = true;
    }
  }
}

inline void ParseAttributes(
    const char* record_ptr,
    const NtfsFileRecordHeader* header,
    size_t bytes_per_record,
    ParsedRecordAttributes& out_attr) {
  const char* record_end = record_ptr + bytes_per_record;
  const char* attr_ptr = record_ptr + header->first_attribute_offset;

  while (attr_ptr + sizeof(NtfsAttributeRecordHeader) <= record_end) {
    const auto* attr = reinterpret_cast<const NtfsAttributeRecordHeader*>(attr_ptr);  // NOSONAR(cpp:S3630) - on-disk NTFS layout; loop bounds validated
    if (attr->type_code == kAttributeEnd || attr->record_length == 0 ||
        attr_ptr + attr->record_length > record_end) {
      break;
    }

    ParseStandardInformationAttribute(attr, attr_ptr, out_attr.modification_time);
    ParseFileNameAttribute(attr, attr_ptr, out_attr.found_names);
    ParseDataAttribute(attr, out_attr.data_file_size, out_attr.has_data_attr);

    attr_ptr += attr->record_length;
  }
}

inline void EmitEntriesForNames(
    const ParsedRecordAttributes& parsed,
    const NtfsFileRecordHeader* header,
    uint64_t record_num,
    std::vector<FileIndex::PopulationBatchEntry>& out_entries,
    mft_seams::ParserStats& out_stats) {
  std::vector<FoundFileName> primary_names;
  std::vector<FoundFileName> dos_names;
  for (const auto& fn : parsed.found_names) {
    if (fn.file_namespace == 2) {
      dos_names.push_back(fn);
    } else {
      primary_names.push_back(fn);
    }
  }

  const std::vector<FoundFileName>& names_to_emit = primary_names.empty() ? dos_names : primary_names;
  const uint64_t frn = (static_cast<uint64_t>(header->sequence_number) << 48U) |
                       (record_num & ntfs_file_reference::kFileRecordNumberMask);
  const bool is_dir = header->IsDirectory();

  for (const auto& fn : names_to_emit) {
    FileIndex::PopulationBatchEntry entry;
    entry.id = ntfs_file_reference::NtfsFileReference(frn);
    entry.parent_id = ntfs_file_reference::NtfsFileReference(fn.parent_frn);
    entry.name = file_name::FileName(fn.name);
    entry.is_directory = is_dir;
    entry.modification_time = parsed.modification_time;
    if (is_dir) {
      entry.file_size = 0;
    } else if (parsed.has_data_attr) {
      entry.file_size = parsed.data_file_size;
    } else {
      entry.file_size = fn.fn_size;
    }

    out_entries.push_back(std::move(entry));
    ++out_stats.records_parsed;
  }

  ++out_stats.base_records;
}

}  // namespace

RawMftRecordParser::RawMftRecordParser(uint32_t bytes_per_sector, uint32_t bytes_per_record)
    : bytes_per_sector_(bytes_per_sector), bytes_per_record_(bytes_per_record) {}

bool RawMftRecordParser::ApplyIdempotentUsaFixup(
    char* record_buffer,
    size_t record_size,
    size_t bytes_per_sector) {
  if (record_buffer == nullptr || record_size < sizeof(NtfsFileRecordHeader) || bytes_per_sector == 0) {
    return false;
  }

  const auto* file_record = reinterpret_cast<const NtfsFileRecordHeader*>(record_buffer);  // NOSONAR(cpp:S3630) - on-disk NTFS layout; size validated above
  if (!file_record->IsValid()) {
    return false;
  }

  if (file_record->usa_count <= 1 || file_record->usa_offset == 0) {
    return true;
  }

  if (file_record->usa_offset >= record_size) {
    return false;
  }

  if (const size_t fixup_array_bytes = static_cast<size_t>(file_record->usa_count) * sizeof(uint16_t);
      file_record->usa_offset + fixup_array_bytes > record_size) {
    return false;
  }

  const auto* fixup_array = reinterpret_cast<const uint16_t*>(record_buffer + file_record->usa_offset);  // NOSONAR(cpp:S3630) - USA offset bounds validated above
  const uint16_t update_sequence_number = fixup_array[0];

  const size_t words_per_sector = bytes_per_sector / sizeof(uint16_t);
  if (words_per_sector == 0) {
    return false;
  }

  auto* record_words = reinterpret_cast<uint16_t*>(record_buffer);  // NOSONAR(cpp:S3630) - in-place USA fixup writes sector trailer words by design
  const size_t max_sectors = record_size / bytes_per_sector;
  const size_t sectors_to_check = (std::min)(max_sectors, static_cast<size_t>(file_record->usa_count - 1));

  for (size_t i = 1; i <= sectors_to_check; ++i) {
    const size_t sector_end_idx = (i * words_per_sector) - 1;
    if (sector_end_idx * sizeof(uint16_t) >= record_size) {
      break;
    }

    uint16_t* sector_end = &record_words[sector_end_idx];
    if (*sector_end == update_sequence_number) {
      *sector_end = fixup_array[i];
    } else if (*sector_end != fixup_array[i]) {
      return false;
    }
  }

  return true;
}

std::string RawMftRecordParser::TranscodeUtf16ToUtf8(
    const char16_t* utf16_chars,
    size_t length) {
  if (utf16_chars == nullptr || length == 0) {
    return {};
  }

  bool is_all_ascii = true;
  for (size_t i = 0; i < length; ++i) {
    if (utf16_chars[i] >= 0x80U) {
      is_all_ascii = false;
      break;
    }
  }

  if (is_all_ascii) {
    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i) {
      result.push_back(static_cast<char>(utf16_chars[i]));
    }
    return result;
  }

  std::string result;
  result.reserve(length * 3 / 2);
  size_t i = 0;
  while (i < length) {
    const auto [code_point, advance] = DecodeOneCodePoint(utf16_chars, length, i);
    AppendUtf8CodePoint(result, code_point);
    i += advance;
  }

  return result;
}

void RawMftRecordParser::ParseSingleRecord(
    const mft_seams::ChunkSpan& chunk_span,
    size_t record_idx,
    std::vector<FileIndex::PopulationBatchEntry>& out_entries,
    mft_seams::ParserStats& out_stats) {
  char* record_ptr = chunk_span.data + (record_idx * bytes_per_record_);
  const auto* header = reinterpret_cast<const NtfsFileRecordHeader*>(record_ptr);  // NOSONAR(cpp:S3630) - on-disk NTFS layout; chunk bounds validated by caller

  if (header->signature == 0) {
    ++out_stats.records_filtered;
    return;
  }

  if (!header->IsValid()) {
    ++out_stats.parse_errors;
    return;
  }

  if (!header->IsInUse()) {
    ++out_stats.records_filtered;
    return;
  }

  if (!ApplyIdempotentUsaFixup(record_ptr, bytes_per_record_, bytes_per_sector_)) {
    ++out_stats.fixup_failures;
    return;
  }

  const uint64_t record_num = (header->SegmentNumber() != 0)
                                  ? header->SegmentNumber()
                                  : (chunk_span.first_record_number + record_idx);

  if (header->base_file_record_number != 0) {
    ++out_stats.extension_records;
    extension_records_.push_back({
        header->base_file_record_number,
        record_num,
        std::vector<char>(record_ptr, record_ptr + bytes_per_record_),
    });
    return;
  }

  if (header->first_attribute_offset < sizeof(NtfsFileRecordHeader) ||
      header->first_attribute_offset >= bytes_per_record_) {
    ++out_stats.parse_errors;
    return;
  }

  ParsedRecordAttributes parsed_attr;
  ParseAttributes(record_ptr, header, bytes_per_record_, parsed_attr);

  if (parsed_attr.found_names.empty()) {
    ++out_stats.parse_errors;
    return;
  }

  EmitEntriesForNames(parsed_attr, header, record_num, out_entries, out_stats);
}

bool RawMftRecordParser::ParseChunk(
    const mft_seams::ChunkSpan& chunk_span,
    std::vector<FileIndex::PopulationBatchEntry>& out_entries,
    mft_seams::ParserStats& out_stats) {
  if (chunk_span.data == nullptr || chunk_span.size < bytes_per_record_ || bytes_per_record_ == 0) {
    return false;
  }

  const size_t total_records = chunk_span.size / bytes_per_record_;

  for (size_t record_idx = 0; record_idx < total_records; ++record_idx) {
    ParseSingleRecord(chunk_span, record_idx, out_entries, out_stats);
  }

  return true;
}

}  // namespace mft

// NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-type-union-access,readability-magic-numbers,cppcoreguidelines-pro-bounds-array-to-pointer-decay)
