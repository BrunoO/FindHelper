#include "index/InitialIndexPopulator.h"

#include "ctrack.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>
#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem
#include <winioctl.h>

#include "index/FileIndex.h"
#include "index/IndexBuildRun.h"
#include "index/LazyValue.h"
#include "index/NtfsFileReference.h"
#include "index/SystemPathFilter.h"
#include "index/mft/DoubleBufferedChunkReader.h"
#include "index/mft/MftExtentProvider.h"
#include "index/mft/RawMftRecordParser.h"
#include "index/mft/RawVolumeReader.h"
#include "index/mft/seams/MftTypes.h"
#include "usn/UsnIntegrityLatch.h"
#include "usn/UsnRecordUtils.h"
#include "usn/VolumeGateway.h"
#include "utils/FileAttributeConstants.h"
#include "utils/FileSystemUtils.h"
#include "utils/Logger.h"
#include "utils/StringUtils.h"

namespace {
// Upper-bound MFT record estimate for pre-sizing the index (issue #5).
// NTFS_VOLUME_DATA_BUFFER has no file count; MftValidDataLength /
// BytesPerFileRecordSegment counts all record slots including free ones,
// which is exactly the safe direction for reserve(). Returns 0 when the
// query fails or yields nothing useful (caller skips Reserve).
[[nodiscard]] size_t EstimateMftRecordCount(HANDLE volume_handle,
                                            volume_gateway::VolumeGateway& gateway) {
  NTFS_VOLUME_DATA_BUFFER volume_data = {};
  if (!gateway.GetVolumeData(volume_handle, volume_data)) {
    LOG_DEBUG_BUILD("FSCTL_GET_NTFS_VOLUME_DATA failed, skipping index pre-size");
    return 0;
  }
  if (volume_data.BytesPerFileRecordSegment == 0) {
    return 0;
  }
  // The MFT record parser assumes 512-byte sectors for the USA fixup. Log the
  // real geometry once so a 4Kn volume (systematic fixup/signature failures
  // below, all falling back to lazy load) is directly visible in the log.
  LOG_INFO_BUILD("NTFS geometry: BytesPerSector=" << volume_data.BytesPerSector
                 << " BytesPerCluster=" << volume_data.BytesPerCluster
                 << " BytesPerFileRecordSegment=" << volume_data.BytesPerFileRecordSegment);
  if (volume_data.BytesPerSector != 512) {
    LOG_WARNING_BUILD("Non-512-byte sectors detected; MFT USA fixup assumes 512B, "
                      "metadata reads may systematically fall back to lazy loading");
  }
  const ULONGLONG max_records =
      static_cast<ULONGLONG>(volume_data.MftValidDataLength.QuadPart) /
      volume_data.BytesPerFileRecordSegment;
  if (max_records == 0) {
    return 0;
  }
  // 15% headroom with integer math (avoids float->size_t narrowing).
  const ULONGLONG with_headroom = max_records * 115ULL / 100ULL;
  const ULONGLONG capped =
      with_headroom > static_cast<ULONGLONG>((std::numeric_limits<size_t>::max)())
          ? static_cast<ULONGLONG>((std::numeric_limits<size_t>::max)())
          : with_headroom;
  return static_cast<size_t>(capped);
}
} // namespace

namespace index_build_run {

IndexBuildRun::IndexBuildRun(HANDLE volume_handle, volume_gateway::VolumeGateway& gateway,
                             FileIndex& file_index,
                             std::atomic<size_t>* indexed_file_count,
                             system_path_filter::FilteredDirTracker& filtered_dirs,
                             std::atomic<bool>* integrity_latch)
    : volume_handle_(volume_handle),
      volume_gateway_(gateway),
      file_index_(file_index),
      indexed_file_count_(indexed_file_count),
      filtered_dirs_(filtered_dirs),
      integrity_latch_(integrity_latch),
      last_progress_update_time_(std::chrono::steady_clock::now()) {
}

void IndexBuildRun::Reserve() {
  filtered_dirs_.Clear();
  filtered_dirs_.Reserve(64);
  // Pre-size index maps + name arena from the MFT upper bound so the build
  // loop does not pay geometric rehash pauses (issue #5). Skipped when the
  // volume query fails (estimate 0). Single-threaded here: no contention.
  if (const size_t estimated_entries = EstimateMftRecordCount(volume_handle_, volume_gateway_);
      estimated_entries > 0) {
    file_index_.ReserveForPopulation(estimated_entries);
    LOG_INFO_BUILD("Pre-sized index for ~" << estimated_entries << " MFT records");
  }
}

void IndexBuildRun::NoteStaged() {
  ++total_files_;
}

void IndexBuildRun::NoteIteration() {
  ++iterations_;
}

void IndexBuildRun::NoteFiltered() {
  ++filtered_count_;
}

void IndexBuildRun::CommitBatch(const std::vector<FileIndex::PopulationBatchEntry>& batch) {
  // Defer path indexing: this run always ends with RecomputePathsAndPruneOrphans
  // (UsnMonitor::RunInitialPopulationAndPrivileges, before is_populating clears
  // and search is ungated), which Clear()s PathStorage and indexes every path
  // exactly once. Per-insert joins + InsertPath here would be pure waste.
  // Search cannot observe the gap (gated on IsIndexBuilding()); replay-drain
  // mutators (delete/rename/move) tolerate pathless entries and the recompute
  // discards their intermediates.
  file_index_.InsertBatch(batch, /*defer_path_indexing=*/true);
}

void IndexBuildRun::NoteProgress() {
  if (const auto now = std::chrono::steady_clock::now();
      now - last_progress_update_time_ >= std::chrono::seconds(1)) {
    if (indexed_file_count_ != nullptr) {
      indexed_file_count_->store(static_cast<size_t>(total_files_));
    }
    LOG_INFO_BUILD("Enumerated " << total_files_ << " files...");
    last_progress_update_time_ = now;
  }
}

void IndexBuildRun::NoteCorruptRecord(DWORD offset) {
  integrity_hit_ = true;
  std::ostringstream reason;
  reason << "USN record corrupt at offset " << offset
         << " during initial journal enumeration; remaining buffer records "
            "skipped. Index may be incomplete/stale.";
  if (integrity_latch_ != nullptr) {
    usn_integrity_latch::LatchIntegrityCompromised(*integrity_latch_, reason.str());
  } else {
    LOG_ERROR_BUILD(reason.str());
  }
}

IndexBuildRun::Result IndexBuildRun::GetResult() const {
  return {total_files_, iterations_, filtered_count_, integrity_hit_};
}

void IndexBuildRun::LogOutcome() const {
  LOG_INFO_BUILD("Index population outcome: " << total_files_ << " entries indexed, "
                 << filtered_count_ << " filtered, in " << iterations_ << " chunk iterations");
}

void IndexBuildRun::FinalizeProgress() {
  // Ensure the counter matches the final index size after all inserts.
  if (indexed_file_count_ != nullptr) {
    indexed_file_count_->store(file_index_.Size());
  }
}

void IndexBuildRun::RecomputePathsAndPruneOrphans() {
  file_index_.RecomputeAllPaths();
}

bool IndexBuildRun::StageRecord(PUSN_RECORD_V2 record,
                               std::vector<FileIndex::PopulationBatchEntry>& out_batch) {
  CTRACK_DEV_NAME("IndexBuildRun::StageRecord");  // NOLINT(misc-const-correctness) - macro creates a non-const handler object
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
  // Shared policy: system_path_filter (index/SystemPathFilter.h), also used by
  // real-time USN monitoring in UsnMonitor.cpp so both paths stay in sync.
  // Without this, system files are visible in search results from startup
  // until their first USN event evicts them.
  if (system_path_filter::IsSystemPrefixedName(filename)) {
    // Track $-prefixed directories so we can skip their direct children below.
    // Only directories need to be recorded — files cannot be parents of other
    // MFT entries, so tracking them would be wasteful.
    if ((record->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
      filtered_dirs_.MarkFilteredDir(
          system_path_filter::FilteredDirectory::FromFileReference(record->FileReferenceNumber));
    }
    NoteFiltered();
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
  if (filtered_dirs_.IsFilteredChild(
          system_path_filter::FilteredDirectory::FromFileReference(parent_ref_num))) {
    if (is_directory) {
      filtered_dirs_.MarkFilteredDir(system_path_filter::FilteredDirectory::FromFileReference(
          file_ref_num));  // propagate to grandchildren
    }
    NoteFiltered();
    return true; // Continue processing
  }

  // Modification time initialization:
  // Both directories and files start as kFileTimeNotLoaded so the lazy loader
  // fetches the real timestamp on first access via GetFileAttributesExW.
  // FILETIME{0,0} must NOT be used — it is the Windows epoch (1601-01-01) and
  const FILETIME mod_time = kFileTimeNotLoaded;
  const uint64_t file_size = kFileSizeNotLoaded;

  // Stage into the batch. Copies the thread-local filename into an owned
  // string (the thread-local buffer is reused per record). The batch is
  // committed under one lock per enumeration buffer; file_size rides along so
  // the commit applies it atomically with the insert (no second lock).
  // kFileSizeNotLoaded / MFT failure mean "skip the update" (lazy loading will
  // handle them); zero-byte files have a valid size of 0.
  FileIndex::PopulationBatchEntry entry;
  entry.id = ntfs_file_reference::NtfsFileReference(file_ref_num);
  entry.parent_id = ntfs_file_reference::NtfsFileReference(parent_ref_num);
  entry.name = file_name::FileName(filename);
  entry.is_directory = is_directory;
  entry.modification_time = mod_time;
  entry.file_size = file_size;
  out_batch.push_back(std::move(entry));
  NoteStaged();

  // Periodically update the indexed file count and log progress
  // so the UI can display live progress during initial enumeration.
  NoteProgress();

  return true; // Success
}

bool IndexBuildRun::ProcessBuffer(const std::vector<char>& buf, DWORD bytes_ret,
                                  usn_journal_enum_position::UsnJournalEnumerationPosition& position) {
  CTRACK_DEV_NAME("IndexBuildRun::ProcessBuffer");  // NOLINT(misc-const-correctness) - macro creates a non-const handler object
  if (bytes_ret < usn_record_utils::SizeOfUsn()) {
    return false; // Signal end of enumeration
  }

  // The first 8 bytes contain the next USN to continue from
  // Cast to const USN* first (since buf is const), then dereference
  USN next_usn = *reinterpret_cast<const USN *>(buf.data()); // NOSONAR(cpp:S3630) - Windows API requires reinterpret_cast to read USN from buffer

  // Parse the USN_RECORD_V2 structures
  // Use DWORD for offset to match the type of bytes_returned and avoid
  // signed/unsigned comparison warnings.
  //
  // Records are staged into a batch (parsing/filtering/MFT reads need no lock)
  // and committed with one lock acquisition per buffer via InsertBatch.
  std::vector<FileIndex::PopulationBatchEntry> batch;
  batch.reserve(1024U);  // NOLINT(readability-magic-numbers) - heuristic: ~500-1000 records per 256KB buffer
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
      NoteCorruptRecord(offset);
      break;
    }

    // Record is validated and safe to use
    if (!StageRecord(record, batch)) {
      return false; // Error processing record
    }

    // Safe to advance: we've validated record->RecordLength fits within
    // buffer
    offset += record->RecordLength;
  }

  // Single lock acquisition for the whole buffer. Lock-wait vs. insert work is
  // split inside InsertBatch so profiles separate contention from hash cost.
  // MFT reads stay in the per-record span above (no lock held during I/O).
  if (!batch.empty()) {
    CommitBatch(batch);
  }

  // Update for next iteration
  position.Advance(next_usn);
  return true; // Continue enumeration
}

bool IndexBuildRun::Run(std::vector<char>& buffer,
                        usn_journal_enum_position::UsnJournalEnumerationPosition& position) {
  CTRACK_DEV_NAME("IndexBuildRun::Run");  // NOLINT(misc-const-correctness) - macro creates a non-const handler object
  const auto buffer_size = static_cast<int>(buffer.size());
  bool enumeration_complete = false;
  while (!enumeration_complete) {
    DWORD bytes_returned = 0;
    if (!volume_gateway_.EnumUsnJournal(volume_handle_, position.Data(), buffer.data(),
                                 buffer_size, bytes_returned)) {
      const DWORD err = GetLastError();
      if (err == ERROR_HANDLE_EOF) {
        LOG_INFO("Reached end of USN journal enumeration");
        enumeration_complete = true;
      } else {
        LOG_ERROR_BUILD("FSCTL_ENUM_USN_DATA failed with error: " << err);
        return false;
      }
    } else if (!ProcessBuffer(buffer, bytes_returned, position)) {
      enumeration_complete = true;
    } else {
      NoteIteration();
    }
  }
  return true;
}

bool IndexBuildRun::RunRawMft() {
  CTRACK_DEV_NAME("IndexBuildRun::RunRawMft");  // NOLINT(misc-const-correctness) - macro creates a non-const handler object
  if (volume_handle_ == INVALID_HANDLE_VALUE || volume_handle_ == nullptr) {
    LOG_ERROR_BUILD("Invalid volume handle provided for raw MFT streaming");
    return false;
  }

  mft::MftExtentProvider extent_provider(volume_handle_, volume_gateway_);
  std::vector<mft_seams::MftDiskExtent> extents;
  if (!extent_provider.GetMftExtents(extents)) {
    LOG_ERROR_BUILD("Failed to query MFT extents for raw indexing");
    return false;
  }

  mft_seams::VolumeGeometry geom{};
  if (!extent_provider.GetVolumeGeometry(geom)) {
    LOG_ERROR_BUILD("Failed to query volume geometry for raw indexing");
    return false;
  }

  auto volume_reader = std::make_unique<mft::Win32VolumeReader>(volume_handle_);
  mft::DoubleBufferedChunkReader chunk_reader(std::move(volume_reader));
  mft::RawMftRecordParser parser(geom.bytes_per_sector, geom.bytes_per_file_record);

  constexpr size_t kChunkSize = 4 * 1024 * 1024;  // 4 MB chunks (optimal for NVMe sequential DMA)
  if (!chunk_reader.StartStreaming(extents, kChunkSize)) {
    LOG_ERROR_BUILD("Failed to start raw MFT chunk streaming");
    return false;
  }

  mft_seams::ChunkBuffer chunk{};
  mft_seams::ParserStats stats{};
  while (chunk_reader.GetNextChunk(chunk)) {
    const mft_seams::ChunkSpan span{chunk.data, chunk.size, chunk.first_record_number};
    std::vector<FileIndex::PopulationBatchEntry> batch_entries;
    if (!parser.ParseChunk(span, batch_entries, stats)) {
      LOG_WARNING_BUILD("Chunk parse error encountered at record " << chunk.first_record_number);
    }

    if (!batch_entries.empty()) {
      std::vector<FileIndex::PopulationBatchEntry> filtered_batch;
      filtered_batch.reserve(batch_entries.size());
      for (auto& entry : batch_entries) {
        const std::string_view name_sv = entry.name.View();
        if (system_path_filter::IsSystemPrefixedName(name_sv)) {
          if (entry.is_directory) {
            filtered_dirs_.MarkFilteredDir(
                system_path_filter::FilteredDirectory::FromFileReference(entry.id.raw));
          }
          NoteFiltered();
          continue;
        }
        if (filtered_dirs_.IsFilteredChild(
                system_path_filter::FilteredDirectory::FromFileReference(entry.parent_id.raw))) {
          if (entry.is_directory) {
            filtered_dirs_.MarkFilteredDir(
                system_path_filter::FilteredDirectory::FromFileReference(entry.id.raw));
          }
          NoteFiltered();
          continue;
        }
        filtered_batch.push_back(std::move(entry));
        NoteStaged();
      }
      if (!filtered_batch.empty()) {
        CommitBatch(filtered_batch);
        NoteProgress();
      }
    }
    NoteIteration();
  }

  const auto reader_stats = chunk_reader.GetStats();
  LOG_INFO_BUILD("Raw MFT streaming complete: "
                 << reader_stats.bytes_read / (1024 * 1024) << " MB read in "
                 << std::fixed << std::setprecision(2) << reader_stats.elapsed_seconds << "s ("
                 << reader_stats.transfer_rate_mb_s << " MB/s). Total files: " << total_files_
                 << ", Filtered: " << filtered_count_
                 << ", Extensions: " << stats.extension_records
                 << ", Parse errors: " << stats.parse_errors);

  return true;
}

}  // namespace index_build_run

// Populates the FileIndex with all existing files on the volume
// using high-performance raw MFT chunk streaming.
// Returns true on success, false on failure.
bool PopulateInitialIndex(index_build_run::IndexBuildRun& run) {
  CTRACK_NAME("PopulateInitialIndex");  // NOLINT(misc-const-correctness) - macro creates a non-const handler object
  ScopedTimer total_timer("PopulateInitialIndex - Total");

  run.Reserve();
  if (!run.RunRawMft()) {
    return false;
  }

  const index_build_run::IndexBuildRun::Result result = run.GetResult();
  LOG_INFO_BUILD("Index population completed - Total files: "
                 << result.files << ", Iterations: " << result.iterations
                 << ", Filtered: " << result.filtered);

  run.LogOutcome();
  run.FinalizeProgress();

  return true;
}
