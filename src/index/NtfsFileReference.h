#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>

#ifdef FAST_LIBS_BOOST
#include <boost/container_hash/hash.hpp>
#endif  // FAST_LIBS_BOOST

namespace ntfs_file_reference {

// Lower 48 bits of an NTFS 64-bit file reference (upper 16 bits = sequence number).
constexpr uint64_t kFileRecordNumberMask = 0x0000FFFFFFFFFFFFULL;

// MFT record 5 is the NTFS volume root directory ("."). USN enumeration often skips it
// (empty name or never emitted), but every indexed file ultimately parents to this FRN.
constexpr uint64_t kRootDirectoryRecordNumber = 5ULL;

// Strong type for a 64-bit NTFS file reference: upper 16 bits = sequence
// number, lower 48 bits = MFT record number. See the glossary
// (docs/design/USN_MFT_UBIQUITOUS_LANGUAGE.md, "NtfsFileReference").
//
// Step 1 of the migration: this type exists alongside the free functions
// below, which delegate to it, so all current uint64_t call sites compile
// untouched. Later steps migrate fields and map keys to this type. There is
// deliberately no implicit conversion to/from uint64_t and no comparison
// against uint64_t, so mixing a full FRN with a stripped record number
// becomes a compile error once call sites convert.
struct NtfsFileReference {
  uint64_t raw = 0;

  explicit constexpr NtfsFileReference(uint64_t raw_value) noexcept : raw(raw_value) {
  }

  // Default: null reference (raw 0), e.g. the synthetic root parent.
  // Required so FileEntry stays default-constructible for map operator[].
  constexpr NtfsFileReference() noexcept = default;

  [[nodiscard]] constexpr uint64_t RecordNumber() const noexcept {
    return raw & kFileRecordNumberMask;
  }

  // True when two FRNs refer to the same MFT record (sequence may differ).
  [[nodiscard]] constexpr bool SameRecordAs(NtfsFileReference other) const noexcept {
    return RecordNumber() == other.RecordNumber();
  }

  [[nodiscard]] constexpr bool IsRoot() const noexcept {
    return RecordNumber() == kRootDirectoryRecordNumber;
  }

  [[nodiscard]] friend constexpr bool operator==(NtfsFileReference a, NtfsFileReference b) noexcept {
    return a.raw == b.raw;
  }

  [[nodiscard]] friend constexpr bool operator!=(NtfsFileReference a, NtfsFileReference b) noexcept {
    return !(a == b);
  }
};

[[nodiscard]] constexpr uint64_t RecordNumber(uint64_t file_reference) noexcept {
  return NtfsFileReference(file_reference).RecordNumber();
}

// Strong type for a 48-bit MFT record number (sequence already stripped).
// Glossary term: MftRecordNumber
// (docs/design/USN_MFT_UBIQUITOUS_LANGUAGE.md). Map keys that match by record
// (record_number_to_id_, awaiting_parent_) use this instead of bare uint64_t
// so a full 64-bit FRN cannot flow into a stripped-key lookup without an
// explicit strip at the boundary (FromFileReference / RecordNumber()).
// There is deliberately no implicit conversion from uint64_t and no
// comparison against uint64_t or NtfsFileReference.
struct MftRecordNumber {
  uint64_t record_number = 0;

  // Debug guard: the raw value must already be stripped (upper 16 bits
  // clear). A full FRN here means the caller skipped FromFileReference /
  // RecordNumber() and the key would silently miss. Masking inside is
  // rejected on purpose: it would hide the missing strip instead of
  // surfacing it. Compiled out in Release (NDEBUG), like all asserts.
  explicit constexpr MftRecordNumber(uint64_t record_num) noexcept
      : record_number((assert((record_num & ~kFileRecordNumberMask) == 0 &&
                              "MftRecordNumber needs a stripped record; use FromFileReference"),
                       record_num)) {
  }

  // Strip a full 64-bit FRN (sequence + record) down to its record number.
  // Prefer this over the raw constructor whenever the input may carry a
  // sequence number; the raw constructor is for already-stripped values only.
  [[nodiscard]] static constexpr MftRecordNumber FromFileReference(
      uint64_t file_reference) noexcept {
    return MftRecordNumber(RecordNumber(file_reference));
  }

  [[nodiscard]] friend constexpr bool operator==(MftRecordNumber a, MftRecordNumber b) noexcept {
    return a.record_number == b.record_number;
  }

  [[nodiscard]] friend constexpr bool operator!=(MftRecordNumber a, MftRecordNumber b) noexcept {
    return !(a == b);
  }
};

// True when two FRNs refer to the same MFT record (sequence number may differ).
// Used for USN rename vs move: ParentFileReferenceNumber often carries a stale
// sequence while the indexed parentID holds the canonical FRN.
[[nodiscard]] constexpr bool SameRecordNumber(uint64_t a, uint64_t b) noexcept {
  return NtfsFileReference(a).SameRecordAs(NtfsFileReference(b));
}

[[nodiscard]] constexpr bool IsRootDirectoryRecord(uint64_t file_reference) noexcept {
  return NtfsFileReference(file_reference).IsRoot();
}

}  // namespace ntfs_file_reference

// Hash support so MftRecordNumber works as an unordered-map key under both
// map configurations (std::unordered_map by default, boost::unordered_flat_map
// with FAST_LIBS_BOOST, which hashes via boost::hash instead of std::hash).
// Specializing hash templates for a user-defined type is the documented
// extension point of both libraries.
template <>
struct std::hash<ntfs_file_reference::MftRecordNumber> {
  [[nodiscard]] std::size_t operator()(
      ntfs_file_reference::MftRecordNumber key) const noexcept {
    return std::hash<uint64_t>{}(key.record_number);
  }
};

// Same for NtfsFileReference (identity match on the full raw value, sequence
// included): id_to_entry_ is keyed by it so exact-FRN lookups need no
// unpacking at the map boundary.
template <>
struct std::hash<ntfs_file_reference::NtfsFileReference> {
  [[nodiscard]] std::size_t operator()(
      ntfs_file_reference::NtfsFileReference key) const noexcept {
    return std::hash<uint64_t>{}(key.raw);
  }
};

#ifdef FAST_LIBS_BOOST
namespace boost {
template <>
struct hash<ntfs_file_reference::MftRecordNumber> {
  [[nodiscard]] std::size_t operator()(
      ntfs_file_reference::MftRecordNumber key) const noexcept {
    return boost::hash<uint64_t>{}(key.record_number);
  }
};
template <>
struct hash<ntfs_file_reference::NtfsFileReference> {
  [[nodiscard]] std::size_t operator()(
      ntfs_file_reference::NtfsFileReference key) const noexcept {
    return boost::hash<uint64_t>{}(key.raw);
  }
};
}  // namespace boost
#endif  // FAST_LIBS_BOOST
