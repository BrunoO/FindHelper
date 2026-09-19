#pragma once

/**
 * @file SystemPathFilter.h
 * @brief Shared NTFS system-file filtering for MFT enumeration and USN monitoring.
 *
 * Previously this predicate and the filtered-directory cascade were duplicated in
 * InitialIndexPopulator.cpp (initial MFT enumeration) and UsnMonitor.cpp (real-time
 * USN monitoring), with an explicit "keep both paths in sync" comment. This header
 * is the single source of truth for both paths.
 */

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <unordered_set>

#include "index/NtfsFileReference.h"

namespace system_path_filter {

// NTFS system files and directories are named with this prefix: $MFT, $LogFile,
// $Bitmap, $Extend, $Recycle.Bin, etc. Shared by MFT enumeration and USN monitoring.
constexpr char kSystemFilePrefix = '$';

// True when filename starts with kSystemFilePrefix. Empty-safe: returns false for
// an empty name (callers may pass a name that failed to parse).
[[nodiscard]] bool IsSystemPrefixedName(std::string_view filename);

// A $-prefixed NTFS system directory excluded from the index, identified by
// its 48-bit MFT record number (upper sequence bits stripped). Glossary term:
// FilteredDirectory (docs/design/USN_MFT_UBIQUITOUS_LANGUAGE.md). The strong
// type keeps full 64-bit FRNs and stripped record numbers apart at the
// tracker boundary: callers strip with ntfs_file_reference::RecordNumber()
// and wrap explicitly. There is deliberately no implicit conversion from
// uint64_t and no comparison against uint64_t.
struct FilteredDirectory {
  uint64_t record_number = 0;

  explicit constexpr FilteredDirectory(uint64_t record_num) noexcept : record_number(record_num) {
  }

  // Build from a full 64-bit FRN, stripping the upper sequence bits.
  // Prefer this over the raw constructor whenever the input may carry a
  // sequence number (e.g. USN FileReferenceNumber); the raw constructor is
  // for already-stripped 48-bit record numbers only.
  [[nodiscard]] static constexpr FilteredDirectory FromFileReference(uint64_t file_reference) noexcept {
    return FilteredDirectory(ntfs_file_reference::RecordNumber(file_reference));
  }

  [[nodiscard]] friend constexpr bool operator==(FilteredDirectory a, FilteredDirectory b) noexcept {
    return a.record_number == b.record_number;
  }

  [[nodiscard]] friend constexpr bool operator!=(FilteredDirectory a, FilteredDirectory b) noexcept {
    return !(a == b);
  }
};

// Tracks file reference numbers of filtered directories (e.g. $Recycle.Bin, $Extend).
// Their immediate children — most notably per-user SID subdirectories inside
// $Recycle.Bin such as "S-1-5-21-1234-5678-9012-567" — do NOT start with '$' and would
// otherwise slip through the system-file filter. When a filtered directory child is
// itself a directory, its record number is also recorded so pruning cascades
// transitively to any deeper subtree rooted under a filtered parent.
//
// Ownership: this is a plain value type. InitialIndexPopulator populates a local
// instance (or one supplied through its out-parameter) and UsnMonitor keeps a member
// that is seeded from initial population and maintained during real-time monitoring.
class FilteredDirTracker {
public:
  // Remove all tracked directory record numbers.
  void Clear();

  // Reserve capacity for at least count record numbers (avoids rehashes during
  // initial enumeration, where typically < 64 filtered directories are seen).
  void Reserve(std::size_t count);

  // True when parent identifies a filtered directory, i.e. the entry is a
  // descendant that must be pruned. Takes the stripped 48-bit record number
  // (see FilteredDirectory); passing a full FRN with sequence bits misses.
  [[nodiscard]] bool IsFilteredChild(FilteredDirectory parent) const;

  // Record a filtered directory so its descendants are pruned transitively.
  void MarkFilteredDir(FilteredDirectory dir);

  // Stop tracking a record number (called when a filtered directory is deleted).
  void EraseOnDelete(FilteredDirectory dir);

  // Number of tracked filtered directories (diagnostics/tests).
  [[nodiscard]] std::size_t Size() const;

private:
  std::unordered_set<std::uint64_t> filtered_dir_ref_nums_;
};

}  // namespace system_path_filter
