#pragma once

#include <cstdint>

namespace usn_journal {

// Reader position in one USN journal generation: which journal (identity),
// where to read next, and the oldest still-readable entry. Glossary term:
// JournalCursor (docs/design/USN_MFT_UBIQUITOUS_LANGUAGE.md).
//
// Value object: equality by value, Advance() returns a new cursor instead of
// mutating. Field types mirror the SDK layout without including <windows.h>
// so this header stays cross-platform and unit-testable: journal_id matches
// DWORDLONG (unsigned 64-bit), next_usn / lowest_valid_usn match USN
// (signed 64-bit, preserving wrap-comparison semantics).
//
// Aggregate initialization order is (journal_id, next_usn, lowest_valid_usn).
struct JournalCursor {
  uint64_t journal_id = 0;
  int64_t next_usn = 0;  // StartUsn for the next FSCTL_READ_USN_JOURNAL
  int64_t lowest_valid_usn = 0;

  // Position after consuming a buffer whose leading USN is new_next_usn.
  // Identity and validity range are unchanged (same journal generation).
  [[nodiscard]] constexpr JournalCursor Advance(int64_t new_next_usn) const noexcept {
    JournalCursor advanced = *this;
    advanced.next_usn = new_next_usn;
    return advanced;
  }

  // True when a fresh FSCTL_QUERY_USN_JOURNAL reports a different journal:
  // deleted/recreated mid-read, strictly worse than a wrap. Latched, never
  // resumed (restart is the only recovery).
  [[nodiscard]] constexpr bool HasIdChanged(uint64_t query_journal_id) const noexcept {
    return query_journal_id != journal_id;
  }

  // True when this position predates the journal's oldest readable entry:
  // events between next_usn and lowest_valid_usn were lost to a wrap.
  // Equal is NOT wrapped (next_usn itself is still readable).
  [[nodiscard]] constexpr bool IsWrappedBy(int64_t query_lowest_valid_usn) const noexcept {
    return next_usn < query_lowest_valid_usn;
  }

  [[nodiscard]] friend constexpr bool operator==(JournalCursor a, JournalCursor b) noexcept {
    return a.journal_id == b.journal_id && a.next_usn == b.next_usn &&
           a.lowest_valid_usn == b.lowest_valid_usn;
  }

  [[nodiscard]] friend constexpr bool operator!=(JournalCursor a, JournalCursor b) noexcept {
    return !(a == b);
  }
};

}  // namespace usn_journal
