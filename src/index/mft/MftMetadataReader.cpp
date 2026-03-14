#include "index/mft/MftMetadataReader.h"

#include <atomic>
#include <cstddef>  // For std::byte
#include <cstring>
#include <iomanip>
#include <sstream>

#include "utils/FileAttributeConstants.h"
#include "utils/FileTimeTypes.h"
#include "utils/Logger.h"

// MFT record structure (based on WinDirStat implementation)
namespace {
// CRITICAL: Pack structures to match Windows MFT format exactly
// Without this, bitfields and structure alignment might differ between compilers
// This ensures structures match the on-disk format byte-for-byte
#pragma pack(push, 1)

// Attribute type codes
constexpr ULONG kAttributeStandardInformation = 0x10;
constexpr ULONG kAttributeFileName = 0x30;
constexpr ULONG kAttributeData = 0x80;
constexpr ULONG kAttributeEnd = 0xFFFFFFFF;

// FILE_RECORD structure - Windows MFT format, must match exactly
// NOLINTBEGIN(cppcoreguidelines-pro-type-member-init,hicpp-member-init,readability-identifier-naming)
struct FileRecord {
    ULONG Signature;
    USHORT UsaOffset;
    USHORT UsaCount;
    ULONGLONG Lsn;
    USHORT SequenceNumber;
    USHORT LinkCount;
    USHORT FirstAttributeOffset;
    USHORT Flags;
    ULONG FirstFreeByte;
    ULONG BytesAvailable;
    ULONGLONG BaseFileRecordNumber : 48;
    ULONGLONG BaseFileRecordSequence : 16;
    USHORT NextAttributeNumber;
    USHORT SegmentNumberHighPart;
    ULONG SegmentNumberLowPart;

    [[nodiscard]] bool IsValid() const noexcept {
        return Signature == 0x454C4946; // 'FILE'
    }

    [[nodiscard]] bool IsInUse() const noexcept {
        return (Flags & 0x0001) != 0;
    }
};

// ATTRIBUTE_RECORD structure - Windows MFT format, must match exactly
// NOLINTBEGIN(cppcoreguidelines-pro-type-member-init,hicpp-member-init,readability-identifier-naming)
struct AttributeRecord {
    ULONG TypeCode;
    ULONG RecordLength;
    UCHAR FormCode;
    UCHAR NameLength;
    USHORT NameOffset;
    USHORT Flags;
    USHORT Instance;

    union {
        struct {
            ULONG ValueLength;
            USHORT ValueOffset;
            UCHAR Reserved[2];  // NOLINT(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays) NOSONAR(cpp:S5945) - Windows MFT structure must match exact format
        } Resident;

        struct {
            LONGLONG LowestVcn;
            LONGLONG HighestVcn;
            USHORT DataRunOffset;
            USHORT CompressionSize;
            UCHAR Padding[4];  // NOLINT(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays) NOSONAR(cpp:S5945) - Windows MFT structure must match exact format
            ULONGLONG AllocatedLength;
            ULONGLONG FileSize;
            ULONGLONG ValidDataLength;
            ULONGLONG Compressed;
        } Nonresident;
    } Form;

    [[nodiscard]] bool IsNonResident() const noexcept {
        // Use std::byte for byte-oriented bit manipulation (SonarQube cpp:S5827)
        return (std::byte{FormCode} & std::byte{0x01}) != std::byte{0};
    }

    [[nodiscard]] const AttributeRecord* Next() const noexcept {
        if (RecordLength == 0) {
            return nullptr;
        }
        return reinterpret_cast<const AttributeRecord*>( // NOSONAR(cpp:S3630,cpp:S6022) - Windows API requires reinterpret_cast and char* for MFT structure access
            reinterpret_cast<const char*>(this) + RecordLength); // NOSONAR(cpp:S6022) - char* required for Windows MFT structure access
    }
};
// NOLINTEND(cppcoreguidelines-pro-type-member-init,hicpp-member-init,readability-identifier-naming)

// STANDARD_INFORMATION structure (attribute 0x10)
struct StandardInformation {
    FILETIME CreationTime;
    FILETIME LastModificationTime;  // ← What we need
    FILETIME MftChangeTime;
    FILETIME LastAccessTime;
    ULONG FileAttributes;
};
// NOLINTEND(cppcoreguidelines-pro-type-member-init,hicpp-member-init,readability-identifier-naming)

// FILE_NAME structure (attribute 0x30) - contains file size as fallback, Windows MFT format, must match exactly
// NOLINTBEGIN(cppcoreguidelines-pro-type-member-init,hicpp-member-init,readability-identifier-naming)
struct FileName {
    ULONGLONG ParentDirectory : 48;
    ULONGLONG ParentSequence : 16;
    LONGLONG CreationTime;
    LONGLONG LastModificationTime;
    LONGLONG MftChangeTime;
    LONGLONG LastAccessTime;
    LONGLONG AllocatedLength;
    LONGLONG FileSize;  // ← Fallback if $DATA not available
    ULONG FileAttributes;
    USHORT PackedEaSize;
    USHORT Reserved;
    UCHAR FileNameLength;
    UCHAR Flags;
    // Note: Variable-length WCHAR FileName[] follows structure in actual MFT record
};
// NOLINTEND(cppcoreguidelines-pro-type-member-init,hicpp-member-init,readability-identifier-naming)

// NTFS_FILE_RECORD_OUTPUT_BUFFER structure (wrapper for MFT record) - Windows MFT format, must match exactly
// NOLINTBEGIN(cppcoreguidelines-pro-type-member-init,hicpp-member-init,readability-identifier-naming)
struct NtfsFileRecordOutputBuffer {
    ULONGLONG FileReferenceNumber;
    ULONG FileRecordLength;
    UCHAR FileRecordBuffer[1];  // NOLINT(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays) NOSONAR(cpp:S5945) - Windows MFT wrapper must match exact on-disk layout (flexible array)
};
// NOLINTEND(cppcoreguidelines-pro-type-member-init,hicpp-member-init,readability-identifier-naming)

// MFT record size: Standard is 1024 bytes, but can be up to 4096 bytes for files with many attributes
// Use 4096 bytes to handle most cases without needing dynamic resizing
constexpr ULONG kMftRecordSize = 4096;  // Increased from 1024 to handle larger MFT records
constexpr ULONG kBytesPerSector = 512;  // Standard NTFS sector size

// Restore default packing
#pragma pack(pop)
} // namespace

// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init,hicpp-member-init) - volume_handle_ initialized in initializer list
MftMetadataReader::MftMetadataReader(HANDLE volume_handle)
    : volume_handle_(volume_handle) {
    // Note: volume_handle validation is caller's responsibility
    // This class assumes a valid handle is passed (checked in InitialIndexPopulator)
}

void MftMetadataReader::LogParseStatistics() const {
    // Only log if we actually attempted to parse anything
    if (parse_stats_.wrapper_too_small == 0 &&
        parse_stats_.invalid_file_record_length == 0 &&
        parse_stats_.record_too_small_for_header == 0 &&
        parse_stats_.invalid_header_or_not_in_use == 0 &&
        parse_stats_.invalid_first_attribute_offset == 0 &&
        parse_stats_.parsed_successfully == 0) {
        return;
    }

    LOG_INFO_BUILD(
        "MFT parse statistics - "
        "wrapper_too_small=" << parse_stats_.wrapper_too_small
        << ", invalid_file_record_length=" << parse_stats_.invalid_file_record_length
        << ", record_too_small_for_header=" << parse_stats_.record_too_small_for_header
        << ", invalid_header_or_not_in_use=" << parse_stats_.invalid_header_or_not_in_use
        << ", invalid_first_attribute_offset=" << parse_stats_.invalid_first_attribute_offset
        << ", parsed_successfully=" << parse_stats_.parsed_successfully);
}

bool MftMetadataReader::TryGetMetadata(uint64_t file_ref_num,
                                       FILETIME* out_modification_time,
                                       uint64_t* out_file_size) {
    if (out_modification_time == nullptr || out_file_size == nullptr) {
        return false;
    }

    // Initialize outputs to sentinel values (fallback behavior)
    // Note: modification time defaults to kFileTimeNotLoaded,
    // size also defaults to kFileSizeNotLoaded (NOT 0) to ensure
    // lazy loading handles it if MFT parsing fails to find $DATA.
    *out_modification_time = kFileTimeNotLoaded;
    *out_file_size = kFileSizeNotLoaded;

    // Read MFT record
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays) - C-style array required for Windows API DeviceIoControl
    char mft_buffer[kMftRecordSize];  // NOSONAR(cpp:S5945) - C-style array required for Windows API DeviceIoControl; NOLINT above
    size_t bytes_returned = 0;

    if (!ReadMftRecord(file_ref_num, mft_buffer, kMftRecordSize, &bytes_returned)) {
        // MFT read failed - graceful fallback (return false, outputs already set to sentinels)
        return false;
    }

    // Parse MFT record
    if (!ParseMftRecord(mft_buffer, bytes_returned, out_modification_time, out_file_size)) {
        // Parsing failed - graceful fallback
        return false;
    }

    return true;
}

bool MftMetadataReader::ReadMftRecord(uint64_t file_ref_num,
                                      char* buffer,  // NOLINT(readability-non-const-parameter) - Must be non-const for DeviceIoControl output
                                      size_t buffer_size,
                                      size_t* bytes_returned) {
    if (buffer == nullptr || bytes_returned == nullptr) {
        return false;
    }

    // NTFS_FILE_RECORD_INPUT_BUFFER structure for FSCTL_GET_NTFS_FILE_RECORD
    // This structure is not always defined in winioctl.h, so we define it here
    // NOLINTNEXTLINE(readability-identifier-naming,cppcoreguidelines-pro-type-member-init,hicpp-member-init) - Windows API; {} zero-inits FileReferenceNumber
    struct NTFS_FILE_RECORD_INPUT_BUFFER {
        ULONGLONG FileReferenceNumber;  // NOLINT(readability-identifier-naming) - Windows API member name
    };
    NTFS_FILE_RECORD_INPUT_BUFFER input_buffer = {};
    input_buffer.FileReferenceNumber = file_ref_num;

    // Validate volume handle before use (defensive programming)
    if (volume_handle_ == INVALID_HANDLE_VALUE || volume_handle_ == nullptr) {
        // Log this error only once to avoid spam
        static bool logged_handle_error = false;
        if (const bool should_log = !logged_handle_error; should_log) {  // Use init-statement pattern
            LOG_WARNING("MftMetadataReader: Invalid volume handle (INVALID_HANDLE_VALUE or nullptr)");
            logged_handle_error = true;
        }
        return false;
    }

    // FSCTL_GET_NTFS_FILE_RECORD is defined in winioctl.h (included via MftMetadataReader.h)
    // Available since Windows XP / Windows Server 2003
    DWORD bytes_returned_dword = 0;
    if (!DeviceIoControl(volume_handle_, FSCTL_GET_NTFS_FILE_RECORD,
                        &input_buffer, sizeof(input_buffer),
                        buffer, static_cast<DWORD>(buffer_size),  // NOSONAR(cpp:S1905) - Cast required: buffer_size is size_t, DeviceIoControl requires DWORD
                        &bytes_returned_dword, nullptr)) {
        DWORD err = GetLastError();

        // Handle ERROR_MORE_DATA (234): Buffer too small, need larger buffer
        // According to Microsoft docs, bytes_returned_dword contains the required size
        // ERROR_MORE_DATA is defined in winerror.h (included via windows.h)
        if (err == ERROR_MORE_DATA && bytes_returned_dword > 0 && bytes_returned_dword <= 65536) {
            // Required size is reasonable (max 64KB), but we can't resize static buffer
            // Log and return false - caller should use larger buffer or skip this record
            LOG_DEBUG_BUILD("FSCTL_GET_NTFS_FILE_RECORD requires " << bytes_returned_dword
                            << " bytes for file ref " << file_ref_num
                            << " (buffer size: " << buffer_size << ")");
            return false;
        }

        // Other errors: log with more detail for first few failures to diagnose issues
        // Use LOG_WARNING for first failure to ensure visibility, then LOG_DEBUG_BUILD for subsequent
        static std::atomic<size_t> error_count{0};
        constexpr size_t kMaxErrorLogCount = 5;  // Log first N errors
        if (size_t count = error_count.fetch_add(1, std::memory_order_relaxed); count < kMaxErrorLogCount) {  // Use init-statement pattern
            const bool volume_handle_valid = (volume_handle_ != INVALID_HANDLE_VALUE && volume_handle_ != nullptr);
            {
                std::ostringstream oss;
                oss << "FSCTL_GET_NTFS_FILE_RECORD failed for file ref " << file_ref_num
                    << ", error: " << err << " (0x" << std::hex << err << std::dec
                    << "), volume_handle valid: " << volume_handle_valid;
                LOG_WARNING(oss.str());
            }
        } else {
            LOG_DEBUG_BUILD("FSCTL_GET_NTFS_FILE_RECORD failed for file ref "
                            << file_ref_num << ", error: " << err);
        }
        return false;
    }

    // Validate returned size is reasonable
    if (bytes_returned_dword == 0 || bytes_returned_dword > buffer_size) {
        static std::atomic<size_t> size_error_count{0};
        if (size_t count = size_error_count.fetch_add(1, std::memory_order_relaxed); count < 3) {  // Use init-statement pattern
            {
                std::ostringstream oss;
                oss << "FSCTL_GET_NTFS_FILE_RECORD returned invalid size: "
                    << bytes_returned_dword << " (expected 1-" << buffer_size
                    << ") for file ref " << file_ref_num;
                LOG_WARNING(oss.str());
            }
        }
        return false;  // Invalid size returned
    }

    *bytes_returned = bytes_returned_dword;  // DWORD to size_t conversion (both unsigned, safe on Windows)

    // Log first successful read for confirmation
    static std::atomic<size_t> success_count{0};
    if (size_t count = success_count.fetch_add(1, std::memory_order_relaxed); count == 0) {  // Use init-statement pattern
        LOG_INFO_BUILD("FSCTL_GET_NTFS_FILE_RECORD first successful read: file ref "
                      << file_ref_num << ", bytes returned: " << bytes_returned_dword);
    }

    return true;
}

static void ApplyMftFixup(char* record_buffer,  // NOLINT(readability-non-const-parameter) - Must be non-const to modify fixup values
                          size_t record_size,
                          size_t bytes_per_sector) {
    const auto* file_record = reinterpret_cast<const FileRecord*>(record_buffer);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) NOSONAR(cpp:S3630,cpp:S6022) - Windows MFT structure access

    if (file_record->UsaCount == 0 || file_record->UsaOffset == 0) {
        return;  // No fixup needed
    }

    // Validate fixup array offset is within buffer
    if (file_record->UsaOffset >= record_size) {
        return;  // Invalid offset
    }

    // Validate fixup array size (UsaCount words)
    if (size_t fixup_array_size = static_cast<size_t>(file_record->UsaCount) * sizeof(USHORT); // NOSONAR(cpp:S6004) - Variable used after if block for validation
        file_record->UsaOffset + fixup_array_size > record_size) {
        return;  // Fixup array extends beyond buffer
    }

    // Get fixup array (first word is the update sequence number)
    const auto* fixup_array = reinterpret_cast<const USHORT*>(record_buffer + file_record->UsaOffset);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) NOSONAR - Windows MFT structure access
    const USHORT update_sequence_number = fixup_array[0];

    // Apply fixup to each sector
    const size_t words_per_sector = bytes_per_sector / sizeof(USHORT);
    if (words_per_sector == 0) {
        return;  // Invalid sector size
    }

    auto* record_words = reinterpret_cast<USHORT*>(record_buffer);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) NOSONAR(cpp:S3630,cpp:S6022) - Windows MFT structure access

    // Validate UsaCount is reasonable (should be <= number of sectors in record)
    const size_t max_sectors = (record_size + bytes_per_sector - 1) / bytes_per_sector;
    const size_t max_count = (max_sectors < static_cast<size_t>(file_record->UsaCount)) ? max_sectors : static_cast<size_t>(file_record->UsaCount);

    for (size_t i = 1; i < max_count && i < static_cast<size_t>(file_record->UsaCount); ++i) {
        // Validate sector end is within buffer
        if (size_t sector_end_offset = (i * words_per_sector - 1) * sizeof(USHORT); // NOSONAR(cpp:S6004) - Variable used after if block for validation
            sector_end_offset >= record_size) {
            break;  // Sector end beyond buffer
        }

        // Last word of each sector should match the update sequence number

        USHORT* sector_end = record_words + (i * words_per_sector) - 1;
        if (*sector_end == update_sequence_number) {
            *sector_end = fixup_array[i];
        }
    }
}

// Helper function to validate and prepare MFT record buffer
// Reduces cognitive complexity of ParseMftRecord
struct MftRecordBuffer {
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays) - C-style array required for Windows API DeviceIoControl
    char buffer[kMftRecordSize];  // NOSONAR(cpp:S5945) - C-style array required for Windows API DeviceIoControl buffer operations
    size_t size;
    const FileRecord* file_record;
};

// Inlined for performance (called once per MFT record during initial index population)
static inline bool ValidateAndPrepareMftRecord(const char* output_buffer,
                                               size_t output_buffer_size,
                                               MftMetadataReader::MftParseStats& parse_stats,
                                               MftRecordBuffer& out_record) {
    // Buffer from DeviceIoControl contains NTFS_FILE_RECORD_OUTPUT_BUFFER wrapper
    if (output_buffer_size < sizeof(NtfsFileRecordOutputBuffer)) {
        ++parse_stats.wrapper_too_small;
        return false;
    }

    const auto* output = reinterpret_cast<const NtfsFileRecordOutputBuffer*>(output_buffer);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) NOSONAR(cpp:S3630,cpp:S6022) - Windows MFT structure access

    // Validate FileRecordLength
    if (output->FileRecordLength == 0 ||
        output->FileRecordLength > output_buffer_size - offsetof(NtfsFileRecordOutputBuffer, FileRecordBuffer)) {
        ++parse_stats.invalid_file_record_length;
        return false;
    }

    // Get pointer to actual MFT record and its size
    const auto* mft_record_ptr = reinterpret_cast<const char*>(output->FileRecordBuffer);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) NOSONAR(cpp:S3630,cpp:S6022) - Windows MFT structure access
    const size_t mft_record_size = output->FileRecordLength;

    if (mft_record_size < sizeof(FileRecord)) {
        ++parse_stats.record_too_small_for_header;
        return false;
    }

    if (mft_record_size > kMftRecordSize) {
        return false; // MFT record too large for our buffer
    }

    // Copy record to mutable buffer
    std::memcpy(out_record.buffer, mft_record_ptr, mft_record_size);
    out_record.size = mft_record_size;

    // PERFORMANCE: Check if fixup is needed before applying
    const auto* file_record_check = reinterpret_cast<const FileRecord*>(out_record.buffer);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) NOSONAR(cpp:S3630,cpp:S6022) - Windows MFT structure access
    // Apply fixup array only if needed (required for NTFS integrity when present)
    if (bool needs_fixup = (file_record_check->UsaCount > 0 && file_record_check->UsaOffset > 0); needs_fixup) {
        ApplyMftFixup(out_record.buffer, mft_record_size, kBytesPerSector);
    }

    // Validate record
    out_record.file_record = reinterpret_cast<const FileRecord*>(out_record.buffer);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) NOSONAR(cpp:S3630,cpp:S6022) - Windows MFT structure access
    if (!out_record.file_record->IsValid() || !out_record.file_record->IsInUse()) {
        ++parse_stats.invalid_header_or_not_in_use;
        return false;
    }

    // Validate FirstAttributeOffset is within buffer
    if (out_record.file_record->FirstAttributeOffset >= mft_record_size ||
        out_record.file_record->FirstAttributeOffset < sizeof(FileRecord)) {
        ++parse_stats.invalid_first_attribute_offset;
        return false;  // Invalid attribute offset
    }

    return true;
}

// Helper function to parse $STANDARD_INFORMATION attribute
// Reduces cognitive complexity of ParseMftRecord
// Inlined for performance (called in loop for each attribute during MFT parsing)
static inline bool ParseStandardInformationAttribute(const AttributeRecord* attr,
                                                       const char* record_end,
                                                       FILETIME* out_modification_time) {
    if (attr->TypeCode != kAttributeStandardInformation ||
        attr->IsNonResident() ||
        attr->Form.Resident.ValueOffset + sizeof(StandardInformation) > attr->RecordLength) {
        return false;
    }

    if (const char* attr_end = reinterpret_cast<const char*>(attr) + attr->Form.Resident.ValueOffset + sizeof(StandardInformation);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) NOSONAR - Windows MFT structure access
        attr_end > record_end) {
        return false;
    }

    const StandardInformation* standard_info = MftMetadataReader::GetAttributeAtOffset<StandardInformation>(
        reinterpret_cast<const char*>(attr), attr->Form.Resident.ValueOffset);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) NOSONAR(cpp:S3630,cpp:S6022) - Windows MFT structure access
    *out_modification_time = standard_info->LastModificationTime;
    return true;
}

// Helper function to parse $DATA attribute
// Reduces cognitive complexity of ParseMftRecord
// Inlined for performance (called in loop for each attribute during MFT parsing)
static inline bool ParseDataAttribute(const AttributeRecord* attr,
                                       uint64_t* out_file_size) {
    // Only process default data stream (NameLength == 0)
    if (attr->TypeCode != kAttributeData || attr->NameLength != 0) {
        return false;
    }

    // Only process default data stream (NameLength == 0)
    if (attr->IsNonResident() && attr->Form.Nonresident.LowestVcn == 0) {
        // Non-resident: use FileSize from header
        *out_file_size = static_cast<uint64_t>(attr->Form.Nonresident.FileSize);
        return true;
    }

    if (!attr->IsNonResident()) {
        // Resident: file size is ValueLength
        *out_file_size = static_cast<uint64_t>(attr->Form.Resident.ValueLength);  // NOSONAR(cpp:S1905) - Cast required: ValueLength is ULONG, out_file_size is uint64_t*
        return true;
    }

    return false;
}

// Helper function to parse $FILE_NAME attribute
// Reduces cognitive complexity of ParseMftRecord
// Inlined for performance (called in loop for each attribute during MFT parsing)
static inline bool ParseFileNameAttribute(const AttributeRecord* attr,
                                           const char* record_end,
                                           uint64_t* out_file_size) {
    if (attr->TypeCode != kAttributeFileName ||
        attr->IsNonResident() ||
        attr->Form.Resident.ValueOffset + sizeof(FileName) > attr->RecordLength) {
        return false;
    }

    if (const char* attr_end = reinterpret_cast<const char*>(attr) + attr->Form.Resident.ValueOffset + sizeof(FileName);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) NOSONAR - Windows MFT structure access
        attr_end > record_end) {
        return false;
    }

    // Only use long filename records (not short names)
    if (const FileName* file_name = MftMetadataReader::GetAttributeAtOffset<FileName>(
            reinterpret_cast<const char*>(attr), attr->Form.Resident.ValueOffset);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) NOSONAR(cpp:S3630,cpp:S6022) - Windows MFT structure access
        file_name->Flags != 0x02) {  // Long filename (Flags != 0x02 for short name)
        *out_file_size = static_cast<uint64_t>(file_name->FileSize);
        return true;
    }

    return false;
}

bool MftMetadataReader::ParseMftRecord(const char* output_buffer, size_t output_buffer_size,
                                       FILETIME* out_modification_time,
                                       uint64_t* out_file_size) {
    // Validate and prepare MFT record buffer (extracted to reduce complexity)
    MftRecordBuffer record;  // NOLINT(misc-const-correctness) - passed by non-const ref to ValidateAndPrepareMftRecord
    if (!ValidateAndPrepareMftRecord(output_buffer, output_buffer_size, parse_stats_, record)) {
        return false;
    }

    // Initialize outputs
    bool found_modification_time = false;
    bool found_file_size = false;

    // Parse attributes (extracted to helper functions to reduce complexity)
    const auto* attr = reinterpret_cast<const AttributeRecord*>(  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) NOSONAR - Windows MFT structure access
        record.buffer + record.file_record->FirstAttributeOffset);
    const char* record_end = record.buffer + record.size;

    while (reinterpret_cast<const char*>(attr) < record_end &&  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) NOSONAR - Windows MFT structure access
           attr->TypeCode != kAttributeEnd) {
        // Validate attribute is within buffer
        if (reinterpret_cast<const char*>(attr) + sizeof(AttributeRecord) > record_end) {  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) NOSONAR - Windows MFT structure access
            break;  // Attribute header extends beyond buffer
        }

        if (attr->RecordLength == 0) {
            break;  // Invalid record
        }

        // Validate attribute fits within buffer
        if (reinterpret_cast<const char*>(attr) + attr->RecordLength > record_end) {  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) NOSONAR - Windows MFT structure access
            break;  // Attribute extends beyond buffer
        }

        // Check for $STANDARD_INFORMATION (0x10) - contains modification time
        if (!found_modification_time && ParseStandardInformationAttribute(attr, record_end, out_modification_time)) {
            found_modification_time = true;
        }

        // Check for $DATA (0x80) - contains file size (preferred)
        if (!found_file_size && ParseDataAttribute(attr, out_file_size)) {
            found_file_size = true;
        }

        // Check for $FILE_NAME (0x30) - contains file size as fallback
        if (!found_file_size && ParseFileNameAttribute(attr, record_end, out_file_size)) {
            found_file_size = true;
        }

        // PERFORMANCE: Early exit if we found both values we need
        if (found_modification_time && found_file_size) {
            break;  // No need to parse remaining attributes
        }

        // Move to next attribute
        attr = attr->Next();
        if (attr == nullptr) {
            break;
        }
    }

    // Return true if we found at least one usable attribute.
    const bool found = found_modification_time || found_file_size;
    if (found) {
        ++parse_stats_.parsed_successfully;
    }
    return found;
}
