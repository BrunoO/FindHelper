#pragma once

#include <cstdint>
#include <string>

#include "usn/JournalCursor.h"

#ifdef _WIN32
#include <windows.h>
#include "usn/VolumeGateway.h"
#endif  // _WIN32

namespace mft {

/**
 * @enum HandoffStatus
 * @brief Outcomes of verifying journal continuity across an MFT walk.
 */
enum class HandoffStatus : uint8_t {
  Success,
  JournalIdChanged,
  JournalWrapped,
  QueryFailed,
};

/**
 * @struct HandoffResult
 * @brief Structured verdict and telemetry from the USN handoff verification.
 */
struct HandoffResult {
  HandoffStatus status = HandoffStatus::Success;
  usn_journal::JournalCursor pre_walk_cursor{};
  usn_journal::JournalCursor post_walk_cursor{};
  std::string error_message;

  [[nodiscard]] bool IsSuccess() const noexcept {
    return status == HandoffStatus::Success;
  }
};

/**
 * @class UsnHandoffCoordinator
 * @brief Coordinates the pre-walk snapshot and post-walk verification of USN journal state.
 *
 * Guarantees that all file system modifications occurring during an MFT bulk walk
 * are reliably detected and replayed forward, failing closed if journal continuity
 * was broken.
 */
class UsnHandoffCoordinator {
 public:
  /**
   * Pure evaluation of journal continuity between a pre-walk snapshot and post-walk state.
   * Cross-platform and deterministic.
   */
  [[nodiscard]] static HandoffResult VerifyIntegrity(
      const usn_journal::JournalCursor& pre_walk,
      const usn_journal::JournalCursor& post_walk);

#ifdef _WIN32
  /**
   * Takes a pre-walk snapshot of the active USN journal.
   * @param gateway VolumeGateway interface for DeviceIoControl queries.
   * @param volume Open volume handle.
   * @param out_cursor Output cursor populated with current journal identity and NextUsn.
   * @return true on success, false on kernel query failure.
   */
  [[nodiscard]] static bool TakePreWalkSnapshot(
      volume_gateway::VolumeGateway& gateway,
      HANDLE volume,
      usn_journal::JournalCursor& out_cursor);

  /**
   * Re-queries the journal after the walk and evaluates integrity.
   * @param gateway VolumeGateway interface for DeviceIoControl queries.
   * @param volume Open volume handle.
   * @param pre_walk Pre-walk snapshot cursor.
   * @return HandoffResult containing verification verdict and target replay cursor.
   */
  [[nodiscard]] static HandoffResult CompleteHandoff(
      volume_gateway::VolumeGateway& gateway,
      HANDLE volume,
      const usn_journal::JournalCursor& pre_walk);
#endif  // _WIN32
};

}  // namespace mft
