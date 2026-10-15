#pragma once

// Central integrity latch for USN index monitoring (R7).
//
// All index-integrity-compromised stores go through this header so the site
// count is auditable (grep LatchIntegrityCompromised / MarkIntegrityCompromised)
// and every latch emits a log. Store-first ordering: concurrent
// IsIndexIntegrityCompromised() readers observe the compromised state even if
// logging stalls. Logging uses LOG_ERROR_BUILD (not LOG_WARNING_BUILD, which is
// compiled out in Release/NDEBUG builds) so the message is emitted in all
// build types.
//
// Throttled paths (e.g. queue-drop bursts): call MarkIntegrityCompromised() on
// every event for an immediate latch, and LatchIntegrityCompromised() only on
// the log interval. Latch re-stores idempotently, so the combination keeps an
// immediate latch with spam-free logs.
//
// NOTE: rename-failure callers hold the FileIndex unique_lock; logging under
// that lock preserves pre-existing behavior (not introduced here).

#include <atomic>
#include <string_view>

#include "utils/Logger.h"

namespace usn_integrity_latch {

// Store-only latch for throttled paths; the interval log goes via
// LatchIntegrityCompromised() below.
inline void MarkIntegrityCompromised(std::atomic<bool> &latch) {
  latch.store(true);
}

// Store-first latch that always emits an ERROR log with the caller-built reason.
// The reason must be fully formatted by the caller; throttling stays at callers.
inline void LatchIntegrityCompromised(std::atomic<bool> &latch,
                                      std::string_view reason) {
  latch.store(true);
  LOG_ERROR_BUILD(reason);
}

}  // namespace usn_integrity_latch
