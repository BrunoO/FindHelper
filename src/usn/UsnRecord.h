#pragma once

#include <cstdint>
#include <string_view>

#include "index/NtfsFileReference.h"
#include "usn/UsnReason.h"

namespace usn_record {

// Shared predicates, defined once here. Raw-record fast paths (phase-0
// partition, apply filter) run before name conversion, so no UsnRecord
// exists there yet — they call these directly. The member specs below
// delegate to the same functions; covered in UsnReasonTests.
[[nodiscard]] constexpr bool EstablishesParent(usn_reason::ReasonSet reasons,
                                              bool is_directory) noexcept {
  return reasons.Includes(usn_reason::UsnReason::FileCreate) && is_directory;
}

[[nodiscard]] constexpr bool CarriesAction(usn_reason::ReasonSet reasons) noexcept {
  return reasons.Intersects(usn_reason::kActionReasons);
}

// One parsed USN_RECORD_V2: identity, name, sequence number, reasons, and
// kind. Built once per record at the ProcessInterestingUsnRecord boundary
// from the validated raw record; downstream stages take this view instead
// of the raw pointer. Glossary term: UsnRecord
// (docs/design/USN_MFT_UBIQUITOUS_LANGUAGE.md).
//
// Lifetime: name is a view into the source USN buffer (or the converted
// thread-local text), valid only while that source is alive — same contract
// as the previous context struct. Journal timestamps are always zero, so no
// time field is carried (sizes/times lazy-load on demand).
//
// Aggregate initialization order is (self, parent, name, usn, reasons,
// is_directory).
struct UsnRecord {
  ntfs_file_reference::NtfsFileReference self;
  ntfs_file_reference::NtfsFileReference parent;
  std::string_view name;
  int64_t usn = 0;
  usn_reason::ReasonSet reasons{0U};
  bool is_directory = false;

  // Directory CREATE records establish the parent links that same-buffer
  // children join onto (phase-0 partition predicate).
  [[nodiscard]] constexpr bool IsParentEstablishing() const noexcept {
    return EstablishesParent(reasons, is_directory);
  }

  // Carries a create/delete/rename/data action (as opposed to close-only
  // noise, which the apply path skips).
  [[nodiscard]] constexpr bool IsActionable() const noexcept {
    return CarriesAction(reasons);
  }
};

}  // namespace usn_record
