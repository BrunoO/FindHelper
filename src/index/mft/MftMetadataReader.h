#pragma once

#include <cstddef>
#include <cstdint>
#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem
#include <winioctl.h>

#include "utils/FileTimeTypes.h"

// Isolated MFT metadata reader for Option 1 (Hybrid Approach)
// This module can be completely removed if MFT reading proves not beneficial
// See docs/research/2026-01-22_MFT_OPTION1_ISOLATED_IMPLEMENTATION.md for design details
//
// Purpose: Read file size and modification time directly from MFT records
// to eliminate lazy loading during initial population.
//
// Design: Completely isolated from existing code - can be deleted without
// affecting other functionality. Integration point is minimal (one conditional
// block in InitialIndexPopulator.cpp).

class MftMetadataReader {
public:
    // Construct with volume handle (must remain valid for lifetime of reader)
    explicit MftMetadataReader(HANDLE volume_handle);

    // Try to read metadata from MFT for a given file reference number
    // Returns true if successful, false otherwise (graceful fallback)
    // On failure, outputs are set to sentinel values (kFileTimeNotLoaded, 0)
    [[nodiscard]] bool TryGetMetadata(uint64_t file_ref_num,
                                       FILETIME* out_modification_time,
                                       uint64_t* out_file_size);

    // Expose parse statistics logging for diagnostics during initial index
    // population. This keeps detailed counters internal while allowing
    // callers (InitialIndexPopulator) to log a single summary line.
    void LogParseStatistics() const;

    // Lightweight parse diagnostics to understand why MFT parsing fails.
    // Used only during initial index population (single thread).
    // Made public to allow static helper functions to access it.
    struct MftParseStats {
        size_t wrapper_too_small = 0;
        size_t invalid_file_record_length = 0;
        size_t record_too_small_for_header = 0;
        size_t invalid_header_or_not_in_use = 0;
        size_t invalid_first_attribute_offset = 0;
        size_t parsed_successfully = 0;
    };

    // Helper: Get pointer to attribute at offset
    // Made public to allow static helper functions to access it.
    template<typename T>
    static T* GetAttributeAtOffset(const char* base, size_t offset) {
        return reinterpret_cast<T*>(const_cast<char*>(base + offset));  // NOLINT(cppcoreguidelines-pro-type-const-cast,cppcoreguidelines-pro-type-reinterpret-cast) NOSONAR(cpp:S3630) - MFT structure access via offset
    }

private:
    // NOLINTNEXTLINE(readability-identifier-naming) - project convention: snake_case_ for members
    HANDLE volume_handle_;

    // NOLINTNEXTLINE(readability-identifier-naming)
    MftParseStats parse_stats_{};

    // Read MFT record using FSCTL_GET_NTFS_FILE_RECORD
    // Returns true if successful, false otherwise
    [[nodiscard]] bool ReadMftRecord(uint64_t file_ref_num,
                                     char* buffer, size_t buffer_size,
                                     size_t* bytes_returned);

    // Parse MFT record to extract metadata
    // Handles fixup arrays, resident vs non-resident attributes, etc.
    // Returns true if successful, false otherwise
    [[nodiscard]] bool ParseMftRecord(const char* output_buffer, size_t output_buffer_size,
                                      FILETIME* out_modification_time,
                                      uint64_t* out_file_size);
};
