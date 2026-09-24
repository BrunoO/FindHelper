#pragma once

#include <cstdint>

namespace usn_reason {

// Why a USN record exists. Bit values match the SDK USN_REASON_* macros from
// winioctl.h (only the bits this application consumes are listed); Windows
// builds verify each value with static_assert in UsnMonitor.cpp, so SDK
// drift fails loudly instead of silently mis-filtering. Glossary term:
// UsnReason (docs/design/USN_MFT_UBIQUITOUS_LANGUAGE.md).
enum class UsnReason : uint32_t {
  DataOverwrite = 0x00000001U,
  DataExtend = 0x00000002U,
  DataTruncation = 0x00000004U,
  FileCreate = 0x00000100U,
  FileDelete = 0x00000200U,
  RenameOldName = 0x00001000U,
  RenameNewName = 0x00002000U,
  Close = 0x80000000U,
};

// A set of UsnReason bits parsed from a record's Reason mask (or combined
// from enumerators). Replaces raw DWORD masking at dispatch sites; the raw
// mask stays available via Mask() for the FSCTL ReasonMask field.
class ReasonSet {
 public:
  explicit constexpr ReasonSet(uint32_t mask) noexcept : mask_(mask) {}

  [[nodiscard]] constexpr bool Includes(UsnReason reason) const noexcept {
    return (mask_ & static_cast<uint32_t>(reason)) != 0U;
  }

  [[nodiscard]] constexpr bool Intersects(ReasonSet other) const noexcept {
    return (mask_ & other.mask_) != 0U;
  }

  [[nodiscard]] constexpr uint32_t Mask() const noexcept { return mask_; }

  [[nodiscard]] friend constexpr bool operator==(ReasonSet a, ReasonSet b) noexcept {
    return a.mask_ == b.mask_;
  }

  [[nodiscard]] friend constexpr bool operator!=(ReasonSet a, ReasonSet b) noexcept {
    return !(a == b);
  }

  // Bitwise-or as hidden friends (cpp:S2807): found by ADL through the
  // ReasonSet argument, no namespace-scope overloads needed. The
  // (UsnReason, UsnReason) seed stays at namespace scope below: with no
  // class argument ADL cannot see a hidden friend (verified with clang).
  [[nodiscard]] friend constexpr ReasonSet operator|(ReasonSet set,
                                                     UsnReason reason) noexcept {
    return ReasonSet(set.mask_ | static_cast<uint32_t>(reason));
  }

  [[nodiscard]] friend constexpr ReasonSet operator|(UsnReason reason,
                                                     ReasonSet set) noexcept {
    return set | reason;
  }

  [[nodiscard]] friend constexpr ReasonSet operator|(ReasonSet a,
                                                     ReasonSet b) noexcept {
    return ReasonSet(a.mask_ | b.mask_);
  }

 private:
  uint32_t mask_ = 0;
};

// Seed overload: two enumerators with no ReasonSet argument cannot use a
// hidden friend (ADL needs an associated class), so this one stays at
// namespace scope. Not flagged by cpp:S2807 (no class parameter to befriend).
[[nodiscard]] constexpr ReasonSet operator|(UsnReason a, UsnReason b) noexcept {  // NOSONAR(cpp:S2807) - Cannot be a hidden friend: with no class argument ADL cannot see it (clang rejects invalid operands); verified 2026-09-19
  return ReasonSet(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

// Kernel filter for FSCTL_READ_USN_JOURNAL. Must include Close when
// ReturnOnlyOnClose is nonzero (MSDN READ_USN_JOURNAL_DATA_V2); close-only
// records carry no action and are skipped via kActionReasons / IsActionable.
inline constexpr ReasonSet kInterestingReasons =
    UsnReason::FileCreate | UsnReason::FileDelete | UsnReason::RenameOldName |
    UsnReason::RenameNewName | UsnReason::DataExtend | UsnReason::DataTruncation |
    UsnReason::DataOverwrite | UsnReason::Close;

// Action bits only: the interesting set without Close.
inline constexpr ReasonSet kActionReasons =
    UsnReason::FileCreate | UsnReason::FileDelete | UsnReason::RenameOldName |
    UsnReason::RenameNewName | UsnReason::DataExtend | UsnReason::DataTruncation |
    UsnReason::DataOverwrite;

// Size-affecting bits: a record intersecting this set (without FileDelete,
// which Remove already handled) invalidates the cached file size.
inline constexpr ReasonSet kDataChangeReasons =
    UsnReason::DataExtend | UsnReason::DataTruncation | UsnReason::DataOverwrite;

}  // namespace usn_reason
