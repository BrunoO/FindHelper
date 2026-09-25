#include "index/mft/UsnHandoffCoordinator.h"

#include <sstream>

namespace mft {

HandoffResult UsnHandoffCoordinator::VerifyIntegrity(
    const usn_journal::JournalCursor& pre_walk,
    const usn_journal::JournalCursor& post_walk) {
  HandoffResult result{};
  result.pre_walk_cursor = pre_walk;
  result.post_walk_cursor = post_walk;

  if (pre_walk.HasIdChanged(post_walk.journal_id)) {
    result.status = HandoffStatus::JournalIdChanged;
    std::ostringstream msg;
    msg << "Journal ID changed from " << pre_walk.journal_id << " to "
        << post_walk.journal_id << " during MFT walk";
    result.error_message = msg.str();
    return result;
  }

  if (pre_walk.IsWrappedBy(post_walk.lowest_valid_usn)) {
    result.status = HandoffStatus::JournalWrapped;
    std::ostringstream msg;
    msg << "Journal wrapped during MFT walk: snapshot USN " << pre_walk.next_usn
        << " is below LowestValidUsn " << post_walk.lowest_valid_usn;
    result.error_message = msg.str();
    return result;
  }

  if (post_walk.next_usn < pre_walk.next_usn) {
    result.status = HandoffStatus::JournalWrapped;
    std::ostringstream msg;
    msg << "Journal NextUsn decreased from " << pre_walk.next_usn << " to "
        << post_walk.next_usn;
    result.error_message = msg.str();
    return result;
  }

  result.status = HandoffStatus::Success;
  return result;
}

#ifdef _WIN32
bool UsnHandoffCoordinator::TakePreWalkSnapshot(
    volume_gateway::VolumeGateway& gateway,
    HANDLE volume,
    usn_journal::JournalCursor& out_cursor) {
  USN_JOURNAL_DATA_V0 journal_data{};
  if (!gateway.QueryJournal(volume, journal_data)) {
    return false;
  }

  out_cursor = {
      journal_data.UsnJournalID,
      journal_data.NextUsn,
      journal_data.LowestValidUsn,
  };
  return true;
}

HandoffResult UsnHandoffCoordinator::CompleteHandoff(
    volume_gateway::VolumeGateway& gateway,
    HANDLE volume,
    const usn_journal::JournalCursor& pre_walk) {
  USN_JOURNAL_DATA_V0 journal_data{};
  if (!gateway.QueryJournal(volume, journal_data)) {
    HandoffResult result{};
    result.status = HandoffStatus::QueryFailed;
    result.pre_walk_cursor = pre_walk;
    result.error_message = "FSCTL_QUERY_USN_JOURNAL failed after MFT walk";
    return result;
  }

  const usn_journal::JournalCursor post_walk{
      journal_data.UsnJournalID,
      journal_data.NextUsn,
      journal_data.LowestValidUsn,
  };

  return VerifyIntegrity(pre_walk, post_walk);
}
#endif  // _WIN32

}  // namespace mft
