#include "index/InitialIndexPopulator.h"

#include <atomic>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>
#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem
#include <winioctl.h>

#include "index/FileIndex.h"
#include "index/LazyValue.h"
#include "usn/UsnRecordUtils.h"
#include "utils/FileAttributeConstants.h"
#include "utils/FileSystemUtils.h"
#include "utils/Logger.h"
#include "utils/StringUtils.h"

// Forward declaration for MftMetadataReader (needed in both #ifdef branches)
#ifdef ENABLE_MFT_METADATA_READING
#include "index/mft/MftMetadataReader.h"
#else
// Forward declaration for when MFT reading is disabled
class MftMetadataReader;
#endif  // ENABLE_MFT_METADATA_READING

// Constants for MFT enumeration
namespace {
constexpr int kBufferSize = 256 * 1024; // 256KB buffer: reduces FSCTL_ENUM_USN_DATA kernel transitions by ~4x
constexpr int kProgressUpdateInterval = 100000; // Update progress every N files

// Mask to extract the 48-bit file record number from a 64-bit
// file reference (upper 16 bits are the NTFS sequence number).
// FSCTL_GET_NTFS_FILE_RECORD expects only the record number.
constexpr uint64_t kFileRecordNumberMask = 0x0000FFFFFFFFFFFFULL;
} // namespace

// Groups parameters for ProcessUsnRecord and ProcessBufferRecords to satisfy cpp:S107 (max 7 params).
// Ownership: file_index and indexed_file_count are caller-owned (must outlive PopulateInitialIndex).
// When ENABLE_MFT_METADATA_READING, mft_reader is non-owning; it points to a stack object in
// PopulateInitialIndex and is used only on that thread for the duration of the call.
struct PopulationContext {
  FileIndex& file_index;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members) - context struct, ref by design
  std::atomic<size_t>* indexed_file_count = nullptr;  // Caller-owned; may be nullptr

  // Tracks file reference numbers of $-prefixed directories found during MFT
  // enumeration (e.g. $Recycle.Bin, $Extend). Their immediate children — most
  // notably per-user SID subdirectories inside $Recycle.Bin such as
  // "S-1-5-21-1234-5678-9012-567" — do NOT start with '$' and would otherwise
  // slip through the system-file filter, get inserted into the index, and end
  // up with orphaned placeholder paths (e.g. "C:/S-1-5-21-...") because their
  // parent was never indexed.  When a filtered directory child is itself a
  // directory, its ref num is also recorded here so that the pruning cascades
  // transitively to any deeper subtree rooted under a filtered parent.
  // Ownership: references a local variable in PopulateInitialIndex; must outlive the call.
  std::unordered_set<uint64_t>& filtered_dir_ref_nums;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members) - context struct, ref by design

#ifdef ENABLE_MFT_METADATA_READING
  MftMetadataReader* mft_reader = nullptr;  // Non-owning; points to PopulateInitialIndex stack object
  size_t& mft_success_count;
  size_t& mft_failure_count;
  size_t& mft_total_files;
#endif  // ENABLE_MFT_METADATA_READING
};

// Named helper function to process a single USN record and insert into index
// Extracted from lambda to reduce complexity and make function reusable
static bool ProcessUsnRecord(PUSN_RECORD_V2 record, int& file_count,
                            PopulationContext& ctx) {
  // Extract filename (wide string, not null-terminated)
  std::wstring wfilename(
      reinterpret_cast<wchar_t *>(reinterpret_cast<std::byte *>(record) + // NOSONAR(cpp:S3630) - Windows API requires reinterpret_cast to access filename from USN_RECORD_V2
                                  record->FileNameOffset),
      record->FileNameLength / sizeof(wchar_t));

  // Convert to UTF-8 string
  auto filename = WideToUtf8(wfilename);

  // Skip empty filenames (shouldn't happen, but be defensive)
  if (filename.empty()) {
    LOG_WARNING("Encountered file with empty filename, skipping");
    return true; // Continue processing
  }

  // Skip NTFS system metadata files ($MFT, $MFTMirr, $LogFile, $Volume,
  // $AttrDef, $Bitmap, $Boot, $BadClus, $Secure, $UpCase, $Extend and its
  // children $ObjId/$Quota/$Reparse/$UsnJrnl, $Recycle.Bin artefacts, etc.).
  // This mirrors the filter applied during real-time USN monitoring in
  // HandleSystemFileFilter (UsnMonitor.cpp) so both paths stay in sync.
  // Without this, system files are visible in search results from startup
  // until their first USN event evicts them.
  if (filename[0] == '$') {
    // Track $-prefixed directories so we can skip their direct children below.
    // Only directories need to be recorded — files cannot be parents of other
    // MFT entries, so tracking them would be wasteful.
    if ((record->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
      ctx.filtered_dir_ref_nums.insert(record->FileReferenceNumber);
    }
    return true; // Continue processing
  }

  uint64_t file_ref_num = record->FileReferenceNumber;
  uint64_t parent_ref_num = record->ParentFileReferenceNumber;
  bool is_directory = false;
  if (record != nullptr && (record->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
    is_directory = true;
  }

  // Skip children of filtered $-prefixed directories.
  //
  // Background: FSCTL_ENUM_USN_DATA returns MFT records in file-reference-number
  // order, which is essentially allocation order.  NTFS system directories like
  // $Recycle.Bin (typically MFT record ~33) and $Extend are allocated very early
  // in the MFT, long before any user-created content.  Per-user SID subfolders
  // inside $Recycle.Bin (e.g. "S-1-5-21-1234-5678-9012-567", allocated on first
  // use by that account) always have much higher record numbers and are therefore
  // visited after their $-prefixed parent — meaning filtered_dir_ref_nums already
  // contains the parent's ref num by the time we reach them here.
  //
  // What would happen without this check: the SID folder name does not start with
  // '$', so it passes the system-file filter above.  Its parent ($Recycle.Bin) is
  // absent from the index (filtered), so RecomputeAllPaths cannot resolve a full
  // path for it and falls back to "C:/<SID-string>" — a nonsense top-level path
  // that pollutes the index.  The $R/$I children inside the SID folder ARE
  // filtered by the '$' prefix check, so only the SID folder itself is affected.
  //
  // If the SID folder were somehow visited before $Recycle.Bin (not observed in
  // practice), it would not be in filtered_dir_ref_nums yet and would slip through.
  // The consequence is a single orphaned directory entry with a wrong path — an
  // acceptable fallback given that SID strings are never entered as search queries.
  //
  // Directory children are also added to filtered_dir_ref_nums so that any
  // deeper nesting under a filtered parent is pruned transitively (cascading).
  // In practice $Recycle.Bin and $Extend are at most two levels deep, but the
  // propagation makes the logic correct for any depth.
  if (ctx.filtered_dir_ref_nums.count(parent_ref_num) != 0) {
    if (is_directory) {
      ctx.filtered_dir_ref_nums.insert(file_ref_num);  // propagate to grandchildren
    }
    return true; // Continue processing
  }

  // Modification time initialization:
  // - Directories: Set to {0,0} (no meaningful modification time tracked
  // for folders in this app)
  // - Files: Set to kFileTimeNotLoaded sentinel (lazy-load on first
  // access to avoid I/O during indexing) Note: USN record timestamps are
  // always zero, so we can't use them - must load from file system later
  FILETIME mod_time = is_directory ? FILETIME{0, 0} : kFileTimeNotLoaded;
  uint64_t file_size = kFileSizeNotLoaded;  // NOLINT(misc-const-correctness) - Passed by non-const pointer to MftMetadataReader::TryGetMetadata as output parameter
  bool mft_succeeded = false;  // NOLINT(misc-const-correctness) - Updated in ENABLE_MFT_METADATA_READING block when MFT succeeds

#ifdef ENABLE_MFT_METADATA_READING
  // Optional: Try to get metadata from MFT (only for files, not directories)
  // This eliminates lazy loading during initial population
  // PERFORMANCE: Reuse shared MftMetadataReader instance instead of creating new one per file
  if (!is_directory && ctx.mft_reader != nullptr) {
    ctx.mft_total_files++; // Count all files (not directories) that we attempt to read from MFT
    // FSCTL_GET_NTFS_FILE_RECORD expects only the 48-bit file record number.
    // Mask off the upper 16-bit NTFS sequence number before calling into MFT.
      const uint64_t mft_file_ref_num = file_ref_num & kFileRecordNumberMask;
      mft_succeeded = ctx.mft_reader->TryGetMetadata(mft_file_ref_num, &mod_time, &file_size);
      if (mft_succeeded) {
        ctx.mft_success_count++;
        // If succeeded: mod_time and file_size are now populated
      } else {
        ctx.mft_failure_count++;
        // If failed: mod_time remains kFileTimeNotLoaded, file_size remains 0
        // Note: OneDrive files and other cloud files often fail MFT reading
        // They will be handled via lazy loading (which uses IShellItem2 for cloud files)
        // OneDrive files are also reset to sentinel values during RecomputeAllPaths()
      }
  }
#endif  // ENABLE_MFT_METADATA_READING

  // Insert into the index. Pass file_size so the size is applied atomically
  // under the same lock acquisition as the insert (avoids a second unique_lock
  // acquire via UpdateFileSizeById). file_size is kFileSizeNotLoaded when MFT
  // reading is disabled or when MFT metadata reading failed/returned zero,
  // so Insert's size update is skipped in those cases.
  ctx.file_index.Insert(file_ref_num, parent_ref_num, filename, is_directory,
                        mod_time, file_size);
  file_count++;

  // Periodically update the indexed file count and log progress
  // so the UI can display live progress during initial enumeration.
  // PERFORMANCE FIX #3: Consolidated duplicate progress logging
  if ((file_count % kProgressUpdateInterval) == 0) { // NOSONAR(cpp:S134) - Nested if acceptable: simple progress update logic in loop
    if (ctx.indexed_file_count != nullptr) {
      ctx.indexed_file_count->store(static_cast<size_t>(file_count),
                                    std::memory_order_relaxed);
    }
    LOG_INFO_BUILD("Enumerated " << file_count << " files...");
  }

  return true; // Success
}

// Named helper function to process all USN records in a buffer
// Extracted from lambda to reduce complexity and make function reusable
static bool ProcessBufferRecords(const std::vector<char>& buf, DWORD bytes_ret,
                                 MFT_ENUM_DATA_V0& enum_data, int& total_files,
                                 PopulationContext& ctx) {
  if (bytes_ret < usn_record_utils::SizeOfUsn()) {
    return false; // Signal end of enumeration
  }

  // The first 8 bytes contain the next USN to continue from
  // Cast to const USN* first (since buf is const), then dereference
  USN next_usn = *reinterpret_cast<const USN *>(buf.data()); // NOSONAR(cpp:S3630) - Windows API requires reinterpret_cast to read USN from buffer

  // Parse the USN_RECORD_V2 structures
  // Use DWORD for offset to match the type of bytes_returned and avoid
  // signed/unsigned comparison warnings.
  DWORD offset = usn_record_utils::SizeOfUsn();
  while (offset < bytes_ret) {
    // Use centralized safe parsing function with comprehensive validation
    // (buffer bounds, RecordLength, MajorVersion/MinorVersion, filename bounds)
    PUSN_RECORD_V2 record = nullptr;
    if (!usn_record_utils::ValidateAndParseUsnRecord(
            buf.data(), bytes_ret, offset,
            record)) {
      // Invalid record - try to recover by advancing by minimum record size
      // This allows processing to continue even if some records are corrupted
      const DWORD min_record_length = usn_record_utils::SizeOfUsnRecordV2();
      offset += min_record_length;
      continue;
    }

    // Record is validated and safe to use
    if (!ProcessUsnRecord(record, total_files, ctx)) {
      return false; // Error processing record
    }

    // Safe to advance: we've validated record->RecordLength fits within
    // buffer
    offset += record->RecordLength;
  }

  // Update for next iteration
  enum_data.StartFileReferenceNumber = next_usn;
  return true; // Continue enumeration
}

// Populates the FileIndex with all existing files on the volume
// by enumerating the MFT using FSCTL_ENUM_USN_DATA.
// Returns true on success, false on failure.
// indexed_file_count: Optional pointer to atomic counter for progress updates
// (can be nullptr)
bool PopulateInitialIndex(HANDLE volume_handle, FileIndex &file_index, // NOSONAR(cpp:S3776) - Cognitive complexity: helper functions extracted
                          std::atomic<size_t>* indexed_file_count) {  // NOLINT(readability-non-const-parameter) - function stores into *indexed_file_count
  // CRITICAL FIX #2: Input validation
  if (volume_handle == INVALID_HANDLE_VALUE || volume_handle == nullptr) {
    LOG_ERROR("Invalid volume handle provided to PopulateInitialIndex");
    return false;
  }

  ScopedTimer total_timer("PopulateInitialIndex - Total");
  
#ifdef ENABLE_MFT_METADATA_READING
  // PERFORMANCE: Create MftMetadataReader once and reuse for all files
  // This avoids creating a new reader instance for each file, which is expensive
  MftMetadataReader mft_reader(volume_handle);
  MftMetadataReader* const mft_reader_ptr = &mft_reader;  // NOLINT(misc-const-correctness) - pointee used as non-const by MFT APIs
  
  // Validate volume handle is still valid before MFT reading
  if (volume_handle == INVALID_HANDLE_VALUE || volume_handle == nullptr) {
    LOG_ERROR("PopulateInitialIndex: Invalid volume handle when creating MftMetadataReader");
    return false;
  }
  
  // MFT statistics counters
  size_t mft_success_count = 0;
  size_t mft_failure_count = 0;
  size_t mft_total_files = 0;
  
  LOG_INFO_BUILD("MFT metadata reading enabled - will attempt to read file attributes from MFT");
#else
  MftMetadataReader* mft_reader_ptr = nullptr;  // NOLINT(misc-const-correctness) - type must match PopulationContext.mft_reader (non-const)
#endif  // ENABLE_MFT_METADATA_READING

  MFT_ENUM_DATA_V0 enum_data;
  enum_data.StartFileReferenceNumber = 0;
  enum_data.LowUsn = 0;
  // MAXLONGLONG is defined in winioctl.h (included above)
  // If not available, fallback to maximum USN value
#ifndef MAXLONGLONG
  // Fallback: maximum 64-bit signed integer value (max USN)
  static constexpr int64_t kMaxUsnFallback = 0x7FFFFFFFFFFFFFFFLL;
  enum_data.HighUsn = kMaxUsnFallback;
#else
  enum_data.HighUsn = MAXLONGLONG;
#endif  // MAXLONGLONG

  const int buffer_size = kBufferSize;
  std::vector<char> buffer(buffer_size);
  DWORD bytes_returned;

  int total_files = 0;  // NOLINT(misc-const-correctness) - total_files is modified in ProcessBufferRecords
  int iterations = 0;

  // Accumulates file reference numbers of $-prefixed directories (e.g. $Recycle.Bin,
  // $Extend) encountered during MFT enumeration so that ProcessUsnRecord can skip their
  // non-$-prefixed children (e.g. per-user SID subfolders inside $Recycle.Bin).
  // See the detailed comment in ProcessUsnRecord for the full rationale.
  // Reserve to avoid rehashes during enumeration (typically < 64 $-dir refs).
  std::unordered_set<uint64_t> filtered_dir_ref_nums;
  filtered_dir_ref_nums.reserve(64);

#ifdef ENABLE_MFT_METADATA_READING
  PopulationContext ctx{file_index, indexed_file_count, filtered_dir_ref_nums,  // NOLINT(misc-const-correctness) - modified by ProcessBufferRecords (ref members)
                        mft_reader_ptr, mft_success_count, mft_failure_count, mft_total_files};
#else
  PopulationContext ctx{file_index, indexed_file_count, filtered_dir_ref_nums};  // NOLINT(misc-const-correctness) - passed by non-const ref to ProcessBufferRecords
#endif  // ENABLE_MFT_METADATA_READING

  // Main enumeration loop - refactored to avoid nested breaks
  bool enumeration_complete = false;
  while (!enumeration_complete) {
    if (!DeviceIoControl(volume_handle, FSCTL_ENUM_USN_DATA, &enum_data,
                         sizeof(enum_data), buffer.data(), buffer_size,
                         &bytes_returned, nullptr)) {
      DWORD err = GetLastError();
      if (err == ERROR_HANDLE_EOF) {
        // Reached the end of enumeration
        LOG_INFO("Reached end of MFT enumeration");
        enumeration_complete = true;
      } else {
        // CODE QUALITY FIX #6: Use logging system consistently instead of
        // std::cerr
        LOG_ERROR_BUILD("FSCTL_ENUM_USN_DATA failed with error: " << err);
        return false;
      }
    } else if (!ProcessBufferRecords(buffer, bytes_returned, enum_data,
                                     total_files, ctx)) {
      enumeration_complete = true;
    } else {
      iterations++;
    }
  }

  LOG_INFO_BUILD("Index population completed - Total files: "
                 << total_files << ", Iterations: " << iterations);

#ifdef ENABLE_MFT_METADATA_READING
  // Log MFT statistics
  if (mft_total_files > 0) {
    double success_rate = (static_cast<double>(mft_success_count) / mft_total_files) * 100.0;
    LOG_INFO_BUILD("MFT Statistics: " << mft_success_count << " succeeded, " 
                  << mft_failure_count << " failed out of " << mft_total_files 
                  << " files (" << std::fixed << std::setprecision(2) << success_rate << "% success rate)");
  } else {
    LOG_INFO_BUILD("MFT Statistics: No files processed (all directories or MFT disabled)");
  }

  // Additional diagnostics: log internal MFT parse statistics to understand
  // why TryGetMetadata might be failing (read vs parse failures).
  if (mft_reader_ptr != nullptr) {
    mft_reader_ptr->LogParseStatistics();
  }
#endif  // ENABLE_MFT_METADATA_READING

  // RecomputeAllPaths() is intentionally NOT called here.
  // On the Windows USN path, UsnMonitor::RunInitialPopulationAndPrivileges
  // calls RecomputeAllPaths() before setting is_populating_index_ to false, so
  // paths are fully resolved before monitoring starts. On the crawler path the
  // builder calls it under the finalizing_population guard. Calling it here too
  // would duplicate the O(N) work.

  // Ensure the counter matches the final index size after all inserts.
  if (indexed_file_count != nullptr) {
    indexed_file_count->store(file_index.Size(), std::memory_order_relaxed);
  }

  return true;
}
