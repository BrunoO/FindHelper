#include "index/InitialIndexPopulator.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>
#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem
#include <winioctl.h>

#include "index/FileIndex.h"
#include "index/LazyValue.h"
#include "index/NtfsFileReference.h"
#include "usn/UsnRecordUtils.h"
#include "utils/FileAttributeConstants.h"
#include "utils/FileSystemUtils.h"
#include "utils/Logger.h"
#include "utils/StringUtils.h"

#include "index/mft/MftMetadataReader.h"

// Constants for MFT enumeration
namespace {
constexpr int kBufferSize = 256 * 1024; // 256KB buffer: reduces FSCTL_ENUM_USN_DATA kernel transitions by ~4x
} // namespace

// Groups parameters for ProcessUsnRecord and ProcessBufferRecords to satisfy cpp:S107 (max 7 params).
// Ownership: file_index and indexed_file_count are caller-owned (must outlive PopulateInitialIndex).
// When mft_reader is non-null, it points to storage in PopulateInitialIndex (same thread, call duration).
struct PopulationContext {
  FileIndex& file_index;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members,readability-identifier-naming) - context struct, ref by design
  std::atomic<size_t>* indexed_file_count = nullptr;  // NOLINT(readability-identifier-naming) - Caller-owned; may be nullptr

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
  std::unordered_set<uint64_t>& filtered_dir_ref_nums;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members,readability-identifier-naming) - context struct, ref by design
  std::chrono::steady_clock::time_point last_progress_update_time;  // NOLINT(readability-identifier-naming)

  MftMetadataReader* mft_reader = nullptr;  // NOLINT(readability-identifier-naming) - Non-owning; null when MFT metadata reading is disabled
  size_t* mft_success_count = nullptr;  // NOLINT(readability-identifier-naming)
  size_t* mft_failure_count = nullptr;  // NOLINT(readability-identifier-naming)
  size_t* mft_total_files = nullptr;  // NOLINT(readability-identifier-naming)
  // Set when a corrupt USN record is seen; caller (UsnMonitor) latches UI warning.
  std::atomic<bool>* index_integrity_compromised = nullptr;  // NOLINT(readability-identifier-naming)
};

// Named helper function to process a single USN record and insert into index
// Extracted from lambda to reduce complexity and make function reusable
static bool ProcessUsnRecord(PUSN_RECORD_V2 record, int& file_count,
                            PopulationContext& ctx) {
  // Extract filename (wide string, not null-terminated) and convert via thread-local buffer.
  const wchar_t* const wfilename =
      reinterpret_cast<wchar_t*>(reinterpret_cast<std::byte*>(record) +  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) NOSONAR(cpp:S3630) - USN_RECORD_V2 variable-offset filename; bounds checked by ValidateAndParseUsnRecord
                                   record->FileNameOffset);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
  const size_t wchar_len = record->FileNameLength / sizeof(wchar_t);
  const std::string_view filename = WideToUtf8ThreadLocal(wfilename, wchar_len);

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
  // Both directories and files start as kFileTimeNotLoaded so the lazy loader
  // fetches the real timestamp on first access via GetFileAttributesExW.
  // FILETIME{0,0} must NOT be used — it is the Windows epoch (1601-01-01) and
  // is indistinguishable from a real (very old) timestamp; IsNotLoaded() would
  // return false, suppressing lazy loading and displaying the epoch in the UI.
  FILETIME mod_time = kFileTimeNotLoaded;
  uint64_t file_size = kFileSizeNotLoaded;  // NOLINT(misc-const-correctness) - Passed by non-const pointer to MftMetadataReader::TryGetMetadata as output parameter
  bool mft_succeeded = false;  // NOLINT(misc-const-correctness) - Set when MFT TryGetMetadata runs

  // Optional: read metadata from MFT (files only). Skipped when ctx.mft_reader is null.
  if (!is_directory && ctx.mft_reader != nullptr) {
    ++(*ctx.mft_total_files);
    // FSCTL_GET_NTFS_FILE_RECORD expects only the 48-bit file record number.
    const uint64_t mft_file_ref_num = ntfs_file_reference::RecordNumber(file_ref_num);
    mft_succeeded = ctx.mft_reader->TryGetMetadata(mft_file_ref_num, &mod_time, &file_size);
    if (mft_succeeded) {
      ++(*ctx.mft_success_count);
    } else {
      ++(*ctx.mft_failure_count);
      // OneDrive / cloud files often fail; lazy loading handles them later.
    }
  }

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
  if (const auto now = std::chrono::steady_clock::now();
      now - ctx.last_progress_update_time >= std::chrono::seconds(1)) {
    if (ctx.indexed_file_count != nullptr) {
      ctx.indexed_file_count->store(static_cast<size_t>(file_count));
    }
    LOG_INFO_BUILD("Enumerated " << file_count << " files...");
    ctx.last_progress_update_time = now;
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
      // Conservative: same as UsnMonitor::ProcessOneBuffer. Do NOT skip ahead by
      // sizeof(USN_RECORD_V2) — that can land mid-record, pass a false MajorVersion=2
      // header, and Insert phantom FRNs that USN replay cannot clean up.
      // Drop the rest of this buffer; continue enumeration from the buffer's next USN.
      if (ctx.index_integrity_compromised != nullptr) {
        ctx.index_integrity_compromised->store(true);
      }
      LOG_ERROR_BUILD("USN record corrupt at offset " << offset
                      << " during initial MFT enumeration; remaining buffer records "
                         "skipped. Index may be incomplete/stale.");
      break;
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

// Runs FSCTL_ENUM_USN_DATA until EOF or ProcessBufferRecords stops. Returns false on ioctl failure.
[[nodiscard]] static bool RunMftEnumerationLoop(HANDLE volume_handle, std::vector<char>& buffer,
                                                int buffer_size, MFT_ENUM_DATA_V0& enum_data,
                                                int& total_files, int& iterations,
                                                PopulationContext& ctx) {
  bool enumeration_complete = false;
  while (!enumeration_complete) {
    DWORD bytes_returned = 0;
    if (!DeviceIoControl(volume_handle, FSCTL_ENUM_USN_DATA, &enum_data, sizeof(enum_data),
                         buffer.data(), buffer_size, &bytes_returned, nullptr)) {
      const DWORD err = GetLastError();
      if (err == ERROR_HANDLE_EOF) {
        LOG_INFO("Reached end of MFT enumeration");
        enumeration_complete = true;
      } else {
        LOG_ERROR_BUILD("FSCTL_ENUM_USN_DATA failed with error: " << err);
        return false;
      }
    } else if (!ProcessBufferRecords(buffer, bytes_returned, enum_data, total_files, ctx)) {
      enumeration_complete = true;
    } else {
      ++iterations;
    }
  }
  return true;
}

static void LogMftPopulationOutcome(bool enable_mft_metadata_reading, size_t mft_total_files,
                                    size_t mft_success_count, size_t mft_failure_count,
                                    const MftMetadataReader* mft_reader_ptr) {
  if (!enable_mft_metadata_reading) {
    return;
  }
  if (mft_total_files > 0) {
    const double success_rate =
        (static_cast<double>(mft_success_count) / static_cast<double>(mft_total_files)) * 100.0;
    LOG_INFO_BUILD("MFT Statistics: " << mft_success_count << " succeeded, "
                                       << mft_failure_count << " failed out of " << mft_total_files
                                       << " files (" << std::fixed << std::setprecision(2)
                                       << success_rate << "% success rate)");
  } else {
    LOG_INFO_BUILD("MFT Statistics: No files processed (all directories)");
  }
  if (mft_reader_ptr != nullptr) {
    mft_reader_ptr->LogParseStatistics();
  }
}

// Populates the FileIndex with all existing files on the volume
// by enumerating the MFT using FSCTL_ENUM_USN_DATA.
// Returns true on success, false on failure.
// indexed_file_count: Optional pointer to atomic counter for progress updates
// (can be nullptr)
bool PopulateInitialIndex(HANDLE volume_handle, FileIndex &file_index,
                          std::atomic<size_t>* indexed_file_count,  // NOLINT(readability-non-const-parameter) - function stores into *indexed_file_count
                          bool enable_mft_metadata_reading,
                          std::atomic<bool>* index_integrity_compromised) {
  // CRITICAL FIX #2: Input validation
  if (volume_handle == INVALID_HANDLE_VALUE || volume_handle == nullptr) {
    LOG_ERROR("Invalid volume handle provided to PopulateInitialIndex");
    return false;
  }

  ScopedTimer total_timer("PopulateInitialIndex - Total");

  std::optional<MftMetadataReader> mft_reader_storage;
  size_t mft_success_count = 0;
  size_t mft_failure_count = 0;
  size_t mft_total_files = 0;
  MftMetadataReader* mft_reader_ptr = nullptr;
  size_t* mft_success_ptr = nullptr;
  size_t* mft_failure_ptr = nullptr;
  size_t* mft_total_ptr = nullptr;

  if (enable_mft_metadata_reading) {
    mft_reader_storage.emplace(volume_handle);
    mft_reader_ptr = &*mft_reader_storage;
    mft_success_ptr = &mft_success_count;
    mft_failure_ptr = &mft_failure_count;
    mft_total_ptr = &mft_total_files;
    LOG_INFO_BUILD("MFT metadata reading enabled - will attempt to read file attributes from MFT");
  } else {
    LOG_INFO_BUILD("MFT metadata reading disabled (lazy load for size/mod time)");
  }

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

  int total_files = 0;  // NOLINT(misc-const-correctness) - total_files is modified in ProcessBufferRecords
  int iterations = 0;

  // Accumulates file reference numbers of $-prefixed directories (e.g. $Recycle.Bin,
  // $Extend) encountered during MFT enumeration so that ProcessUsnRecord can skip their
  // non-$-prefixed children (e.g. per-user SID subfolders inside $Recycle.Bin).
  // See the detailed comment in ProcessUsnRecord for the full rationale.
  // Reserve to avoid rehashes during enumeration (typically < 64 $-dir refs).
  std::unordered_set<uint64_t> filtered_dir_ref_nums;
  filtered_dir_ref_nums.reserve(64);

  if (PopulationContext ctx{file_index,
                            indexed_file_count,
                            filtered_dir_ref_nums,
                            std::chrono::steady_clock::now(),
                            mft_reader_ptr,
                            mft_success_ptr,
                            mft_failure_ptr,
                            mft_total_ptr,
                            index_integrity_compromised};
      !RunMftEnumerationLoop(volume_handle, buffer, buffer_size, enum_data, total_files, iterations,
                             ctx)) {
    return false;
  }

  LOG_INFO_BUILD("Index population completed - Total files: "
                 << total_files << ", Iterations: " << iterations);

  LogMftPopulationOutcome(enable_mft_metadata_reading, mft_total_files, mft_success_count,
                          mft_failure_count, mft_reader_ptr);

  // RecomputeAllPaths() is intentionally NOT called here.
  // On the Windows USN path, UsnMonitor::RunInitialPopulationAndPrivileges
  // calls RecomputeAllPaths() before setting is_populating_index_ to false, so
  // paths are fully resolved before monitoring starts. On the crawler path the
  // builder calls it under the finalizing_population guard. Calling it here too
  // would duplicate the O(N) work.

  // Ensure the counter matches the final index size after all inserts.
  if (indexed_file_count != nullptr) {
    indexed_file_count->store(file_index.Size());
  }

  return true;
}
