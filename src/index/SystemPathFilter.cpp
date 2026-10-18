#include "index/SystemPathFilter.h"

#include <vector>

#include "utils/StringUtils.h"

namespace system_path_filter {

namespace {

constexpr std::string_view kRecycleBinDirectoryName = "$Recycle.Bin";
constexpr std::string_view kRecycleRecordPrefix = "$R";
constexpr std::string_view kRecycleMetadataPrefix = "$I";
constexpr std::size_t kMinArtifactNameLength = 3;

[[nodiscard]] bool IsArtifactShaped(std::string_view filename, std::string_view prefix) noexcept {
  return filename.size() >= kMinArtifactNameLength && StartsWithIgnoreCaseAscii(filename, prefix);
}

[[nodiscard]] constexpr IngestionDisposition ApplyMode(IngestionDisposition disposition,
                                                       IngestionMode mode) noexcept {
  if (mode == IngestionMode::kIncludeRecycleBin ||
      disposition == IngestionDisposition::SkipRecycleMetadata) {
    return disposition;
  }
  return IngestionDisposition::ExcludeSystem;
}

}  // namespace

bool IsSystemPrefixedName(std::string_view filename) {
  return !filename.empty() && filename.front() == kSystemFilePrefix;
}

IngestionDisposition SystemPathFilterPolicy::ClassifyEntry(std::string_view filename, uint64_t frn,
                                                           uint64_t parent_frn, bool is_directory,
                                                           IngestionMode mode) {
  if (ntfs_file_reference::IsRootDirectoryRecord(frn)) {
    return IngestionDisposition::ExcludeSystem;
  }
  const auto record = ntfs_file_reference::MftRecordNumber::FromFileReference(frn);
  const auto parent = ntfs_file_reference::MftRecordNumber::FromFileReference(parent_frn);

  if (is_directory && ntfs_file_reference::IsRootDirectoryRecord(parent_frn) &&
      EqualsIgnoreCaseAscii(filename, kRecycleBinDirectoryName)) {
    InsertContainer(record, parent, ContainerKind::kRecycleRoot);
    return ApplyMode(IngestionDisposition::IncludeRecycleBin, mode);
  }
  if (IsTrackedRecycleRoot(parent) && !IsSystemPrefixedName(filename)) {
    if (is_directory) {
      InsertContainer(record, parent, ContainerKind::kSid);
      return ApplyMode(IngestionDisposition::IncludeRecycleBin, mode);
    }
    return ApplyMode(IngestionDisposition::IncludeNormal, mode);
  }
  if (IsArtifactShaped(filename, kRecycleMetadataPrefix) &&
      IsTrackedSidOrRecycledFolder(parent)) {
    if (is_directory) {
      InsertContainer(record, parent, ContainerKind::kFilteredDir);
    }
    return IngestionDisposition::SkipRecycleMetadata;
  }
  if (IsTrackedSid(parent)) {
    if (is_directory) {
      InsertContainer(record, parent, ContainerKind::kRecycledFolder);
    }
    const bool is_r = IsArtifactShaped(filename, kRecycleRecordPrefix);
    return ApplyMode(is_r ? IngestionDisposition::IncludeRecycleBin : IngestionDisposition::IncludeNormal, mode);
  }
  if (IsTrackedRecycledFolder(parent)) {
    if (is_directory) {
      InsertContainer(record, parent, ContainerKind::kRecycledFolder);
    }
    return ApplyMode(IngestionDisposition::IncludeRecycleBin, mode);
  }
  return ClassifyOutsideSubtree(record, parent, is_directory);
}

LiveDecision SystemPathFilterPolicy::ClassifyLiveEntry(std::string_view filename, uint64_t frn,
                                                       uint64_t parent_frn, bool is_directory,
                                                       bool is_rename_new_name, IngestionMode mode) {
  const bool is_artifact_shaped = (IsArtifactShaped(filename, kRecycleRecordPrefix) ||
                                   IsArtifactShaped(filename, kRecycleMetadataPrefix)) &&
                                  !EqualsIgnoreCaseAscii(filename, kRecycleBinDirectoryName);
  const bool tracker_miss =
      is_artifact_shaped &&
      !IsTrackedContainer(ntfs_file_reference::MftRecordNumber::FromFileReference(parent_frn));
  const IngestionDisposition disposition =
      ClassifyEntry(filename, frn, parent_frn, is_directory, mode);
  const bool evict_as_recycle_delete = is_rename_new_name && IsSystemPrefixedName(filename);
  if (disposition == IngestionDisposition::ExcludeSystem) {
    return {evict_as_recycle_delete ? LiveAction::kEvictAsRecycleDelete : LiveAction::kSkip,
            tracker_miss,};
  }
  if (disposition == IngestionDisposition::SkipRecycleMetadata) {
    return {evict_as_recycle_delete ? LiveAction::kEvictAsRecycleDelete : LiveAction::kSkipMetadata,
            tracker_miss,};
  }
  return {LiveAction::kProcess, tracker_miss};
}

void SystemPathFilterPolicy::OnDirectoryDeleted(ntfs_file_reference::MftRecordNumber record) {
  EraseSubtree(record);
}

bool SystemPathFilterPolicy::MarkFilteredDir(FilteredDirectory dir) {
  if (const auto it = containers_.find(dir.record_number); it != containers_.end()) {
    return false;
  }
  containers_.emplace(dir.record_number, ContainerNode{ContainerKind::kFilteredDir, 0});
  return true;
}

void SystemPathFilterPolicy::ClearContainers() {
  containers_.clear();
}

void SystemPathFilterPolicy::ReserveContainers(std::size_t count) {
  containers_.reserve(count);
}

void SystemPathFilterPolicy::InsertContainer(ntfs_file_reference::MftRecordNumber record,
                                             ntfs_file_reference::MftRecordNumber parent,
                                             ContainerKind kind) {
  if (const auto it = containers_.find(record.record_number); it != containers_.end()) {
    it->second.kind = kind;
    it->second.parent = parent.record_number;
    return;
  }
  containers_.emplace(record.record_number, ContainerNode{kind, parent.record_number});
}

bool SystemPathFilterPolicy::IsTrackedContainer(ntfs_file_reference::MftRecordNumber record) const noexcept {
  return containers_.count(record.record_number) != 0;
}

void SystemPathFilterPolicy::EraseSubtree(ntfs_file_reference::MftRecordNumber record) {
  const std::uint64_t root = record.record_number;
  std::vector<std::uint64_t> to_erase;
  for (const auto& [child, node] : containers_) {
    std::uint64_t curr = child;
    for (int depth = 0; depth < 32 && curr != 0 && curr != ntfs_file_reference::kRootDirectoryRecordNumber; ++depth) {
      if (curr == root) {
        to_erase.push_back(child);
        break;
      }
      const auto it = containers_.find(curr);
      curr = (it != containers_.end()) ? it->second.parent : 0;
    }
  }
  for (const std::uint64_t c : to_erase) {
    containers_.erase(c);
  }
}

IngestionDisposition SystemPathFilterPolicy::ClassifyOutsideSubtree(
    ntfs_file_reference::MftRecordNumber record, ntfs_file_reference::MftRecordNumber parent,
    bool is_directory) {
  if (IsFilteredDirectory(parent)) {
    if (is_directory) {
      InsertContainer(record, parent, ContainerKind::kFilteredDir);
    }
    return IngestionDisposition::ExcludeSystem;
  }

  if (is_directory && IsTrackedContainer(record)) {
    EraseSubtree(record);
  }
  return IngestionDisposition::IncludeNormal;
}

}  // namespace system_path_filter
