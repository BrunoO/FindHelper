#pragma once

/**
 * @file SystemPathFilter.h
 * @brief Shared NTFS system-file filtering for baseline journal enumeration and USN monitoring.
 */

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <unordered_map>

#include "index/NtfsFileReference.h"

namespace system_path_filter {

// NTFS system files and directories are named with this prefix: $MFT, $LogFile,
// $Bitmap, $Extend, $Recycle.Bin, etc. Shared by baseline journal enumeration and USN monitoring.
constexpr char kSystemFilePrefix = '$';

// True when filename starts with kSystemFilePrefix. Empty-safe: returns false for
// an empty name (callers may pass a name that failed to parse).
[[nodiscard]] bool IsSystemPrefixedName(std::string_view filename);

// True when filename is a pseudo/navigational directory ("." or "..").
[[nodiscard]] constexpr bool IsPseudoDirectoryName(std::string_view filename) noexcept {
  return filename == "." || filename == "..";
}

// True when a candidate entry must never become an IndexedFile: a pseudo-directory
// name (".", "..") or the volume root directory itself (MFT record 5,
// kRootDirectoryRecordNumber) carried by a directory entry.
[[nodiscard]] constexpr bool IsExcludedIndexEntry(std::string_view filename,
                                                  uint64_t file_reference,
                                                  bool is_directory) noexcept {
  return IsPseudoDirectoryName(filename) ||
         (is_directory && ntfs_file_reference::IsRootDirectoryRecord(file_reference));
}

// Strong type alias for a filtered directory's stripped record number.
using FilteredDirectory = ntfs_file_reference::MftRecordNumber;

// Ingestion outcome for one MFT/USN entry after recycle-aware classification.
enum class IngestionDisposition : std::uint8_t {
  IncludeNormal,        // Ordinary entry: ingest into index.
  IncludeRecycleBin,    // $Recycle.Bin / <SID> container or $R payload.
  SkipRecycleMetadata,  // $I sidecar: never enters FileIndex in any mode (INV-2).
  ExcludeSystem,        // System file / system directory child: excluded.
};

// Selects which recycle-bin behavior SystemPathFilterPolicy reports.
enum class IngestionMode : std::uint8_t {
  kExcludeRecycleContents,  // NOLINT(readability-identifier-naming) - Phase 1: recycle-subtree results except $I map to ExcludeSystem.
  kIncludeRecycleBin,       // NOLINT(readability-identifier-naming) - Phase 2: recycle-subtree results returned as classified.
};

// Live USN routing outcome for one record.
enum class LiveAction : std::uint8_t {
  kProcess,               // NOLINT(readability-identifier-naming) - Normal path: insert / rename / move / delete.
  kSkip,                  // NOLINT(readability-identifier-naming) - Excluded: evict descendants, publish delete.
  kSkipMetadata,          // NOLINT(readability-identifier-naming) - $I sidecar (INV-2): skip body plus debug count.
  kEvictAsRecycleDelete,  // NOLINT(readability-identifier-naming) - RENAME_NEW_NAME onto a $-shaped name: logical delete.
};

struct LiveDecision {
  LiveAction action = LiveAction::kProcess;
  bool tracker_miss = false;
};

// One policy instance per volume. Maintains container tracking for $Recycle.Bin roots,
// per-user <SID> folders, $R-folders, and filtered directories.
class SystemPathFilterPolicy {
public:
  [[nodiscard]] IngestionDisposition ClassifyEntry(std::string_view filename, uint64_t frn,
                                                   uint64_t parent_frn, bool is_directory,
                                                   IngestionMode mode);

  [[nodiscard]] LiveDecision ClassifyLiveEntry(std::string_view filename, uint64_t frn,
                                               uint64_t parent_frn, bool is_directory,
                                               bool is_rename_new_name, IngestionMode mode);

  void OnDirectoryDeleted(ntfs_file_reference::MftRecordNumber record);

  void ClearContainers();

  // Record a filtered directory so its descendants are pruned transitively.
  // Returns true if the directory was newly added, false if already tracked.
  bool MarkFilteredDir(FilteredDirectory dir);

  void ReserveContainers(std::size_t count);

  [[nodiscard]] std::size_t TrackedContainerCount() const noexcept {
    return containers_.size();
  }

  [[nodiscard]] bool IsTrackedRecycleRoot(ntfs_file_reference::MftRecordNumber dir) const noexcept {
    const auto it = containers_.find(dir.record_number);
    return it != containers_.end() && it->second.kind == ContainerKind::kRecycleRoot;
  }

  [[nodiscard]] bool IsTrackedSid(ntfs_file_reference::MftRecordNumber dir) const noexcept {
    const auto it = containers_.find(dir.record_number);
    return it != containers_.end() && it->second.kind == ContainerKind::kSid;
  }

  [[nodiscard]] bool IsTrackedRecycledFolder(ntfs_file_reference::MftRecordNumber dir) const noexcept {
    const auto it = containers_.find(dir.record_number);
    return it != containers_.end() && it->second.kind == ContainerKind::kRecycledFolder;
  }

  [[nodiscard]] bool IsTrackedSidOrRecycledFolder(ntfs_file_reference::MftRecordNumber dir) const noexcept {
    const auto it = containers_.find(dir.record_number);
    return it != containers_.end() && (it->second.kind == ContainerKind::kSid || it->second.kind == ContainerKind::kRecycledFolder);
  }

  [[nodiscard]] bool IsFilteredDirectory(ntfs_file_reference::MftRecordNumber dir) const noexcept {
    const auto it = containers_.find(dir.record_number);
    return it != containers_.end() && it->second.kind == ContainerKind::kFilteredDir;
  }

private:
  enum class ContainerKind : std::uint8_t {
    kRecycleRoot,     // NOLINT(readability-identifier-naming) - project convention: k prefix for enum constants
    kSid,             // NOLINT(readability-identifier-naming) - project convention: k prefix for enum constants
    kRecycledFolder,  // NOLINT(readability-identifier-naming) - project convention: k prefix for enum constants
    kFilteredDir,     // NOLINT(readability-identifier-naming) - project convention: k prefix for enum constants
  };

  struct ContainerNode {
    ContainerKind kind = ContainerKind::kFilteredDir;
    std::uint64_t parent = 0;
  };

  [[nodiscard]] IngestionDisposition ClassifyOutsideSubtree(ntfs_file_reference::MftRecordNumber record,
                                                            ntfs_file_reference::MftRecordNumber parent,
                                                            bool is_directory);

  void InsertContainer(ntfs_file_reference::MftRecordNumber record,
                       ntfs_file_reference::MftRecordNumber parent,
                       ContainerKind kind);

  [[nodiscard]] bool IsTrackedContainer(ntfs_file_reference::MftRecordNumber record) const noexcept;

  void EraseSubtree(ntfs_file_reference::MftRecordNumber record);

  std::unordered_map<std::uint64_t, ContainerNode> containers_;
};

}  // namespace system_path_filter
