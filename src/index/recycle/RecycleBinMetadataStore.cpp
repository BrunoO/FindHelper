#include "index/recycle/RecycleBinMetadataStore.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace recycle_bin {

namespace {

constexpr std::size_t kMinRecycleNameLength = 3;
constexpr char kRecycleNameMark = '$';

[[nodiscard]] bool SameMetadata(const RecycleFileMetadata& lhs, const RecycleFileMetadata& rhs) {
  return lhs.is_directory == rhs.is_directory &&
         lhs.deletion_time_filetime == rhs.deletion_time_filetime &&
         lhs.original_size == rhs.original_size &&
         lhs.original_name == rhs.original_name &&
         lhs.original_path == rhs.original_path;
}

}  // namespace

std::optional<RecycleNameToken> TokenizeRecycleName(std::string_view filename) {
  if (filename.size() < kMinRecycleNameLength || filename.front() != kRecycleNameMark) {
    return std::nullopt;
  }
  const char tag = filename[1];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - size >= 3 checked above
  RecycleNameToken parsed;
  if (tag == 'r' || tag == 'R') {
    parsed.prefix = RecycleNamePrefix::kR;
  } else if (tag == 'i' || tag == 'I') {
    parsed.prefix = RecycleNamePrefix::kI;
  } else {
    return std::nullopt;
  }
  const std::string_view rest = filename.substr(2);
  const std::size_t dot = rest.find('.');
  parsed.token = rest.substr(0, dot);
  if (parsed.token.empty()) {
    return std::nullopt;
  }
  if (dot != std::string_view::npos) {
    parsed.extension = rest.substr(dot + 1);
  }
  return parsed;
}

RecycleBinMetadataStore::RecycleBinMetadataStore() = default;

void RecycleBinMetadataStore::IngestRFiles(const std::vector<RFileInfo>& infos) {
  if (infos.empty()) {
    return;
  }
  const std::scoped_lock lock{mutex_};
  SnapshotDelta delta;
  std::set<BatchRKey> handled_keys;
  for (const RFileInfo& info : infos) {
    const std::optional<RecycleNameToken> token = TokenizeRecycleName(info.name);
    if (!token || token->prefix != RecycleNamePrefix::kR) {
      continue;
    }
    if (const BatchRKey batch_key{info.r_frn, PendingKey{info.parent_frn, std::string{token->token}}};
        !handled_keys.insert(batch_key).second) {
      continue;
    }
    ProcessRLocked(info, token->token, delta);
  }
  CommitLocked(delta);
}

void RecycleBinMetadataStore::PublishIEntries(std::vector<IFileInfo> infos) {
  const std::scoped_lock lock{mutex_};
  SnapshotDelta delta;
  for (IFileInfo& info : infos) {
    const std::optional<RecycleNameToken> token = TokenizeRecycleName(info.name);
    if (!token || token->prefix != RecycleNamePrefix::kI) {
      continue;
    }
    ProcessILocked(info, token->token, delta);
  }
  CommitLocked(delta);
}

void RecycleBinMetadataStore::EvictMany(const std::vector<EvictOp>& ops) {
  if (ops.empty()) {
    return;
  }
  const std::scoped_lock lock{mutex_};
  IdSet r_ids;
  IdSet i_ids;
  r_ids.reserve(ops.size());
  i_ids.reserve(ops.size());
  for (const EvictOp& op : ops) {
    switch (op.kind) {
      case EvictKind::kByR:
      case EvictKind::kRenamedOut:
        r_ids.insert(op.frn);
        break;
      case EvictKind::kByI:
        i_ids.insert(op.frn);
        if (const auto paired = i_to_r_.find(op.frn); paired != i_to_r_.end()) {
          r_ids.insert(paired->second.r_frn);
        }
        break;
    }
  }
  SnapshotDelta delta;
  ErasePendingLocked(r_ids, i_ids);
  DropPublishedLocked(r_ids, i_ids, delta);
  CommitLocked(delta);
}

std::shared_ptr<const RecycleMetadataSnapshot> RecycleBinMetadataStore::Current() const {
  const std::scoped_lock lock{mutex_};
  return current_;
}

std::uint64_t RecycleBinMetadataStore::Version() const {
  const std::scoped_lock lock{mutex_};
  return current_->generation;
}

void RecycleBinMetadataStore::Reset() {
  const std::scoped_lock lock{mutex_};
  pending_.clear();
  i_to_r_.clear();
  r_to_i_.clear();
  if (!current_->by_file_id.empty()) {
    auto empty = std::make_shared<RecycleMetadataSnapshot>();
    empty->generation = current_->generation + 1;
    current_ = std::move(empty);
  }
}

void RecycleBinMetadataStore::ProcessRLocked(const RFileInfo& info, std::string_view token,
                                             SnapshotDelta& delta) {
  PendingKey key{info.parent_frn, std::string{token}};
  if (current_->by_file_id.count(info.r_frn) > 0) {
    if (const auto owner = r_to_i_.find(info.r_frn); owner != r_to_i_.end()) {
      if (const auto paired = i_to_r_.find(owner->second);
          paired != i_to_r_.end() && paired->second.key == key) {
        return;
      }
      DropPublishedLocked(IdSet{info.r_frn}, IdSet{owner->second}, delta);
    } else {
      DropPublishedLocked(IdSet{info.r_frn}, IdSet{}, delta);
    }
  }
  if (const auto waiting = pending_.find(key); waiting != pending_.end()) {
    if (auto& opt_i = waiting->second.i; opt_i.has_value()) {
      PendingI partner = std::move(opt_i.value());
      pending_.erase(waiting);
      PairLocked(info.r_frn, info.is_directory, partner.i_frn, std::move(partner.meta), key, delta);
      return;
    }
  }
  pending_[std::move(key)].r = PendingR{info.r_frn, info.is_directory};
}

void RecycleBinMetadataStore::ProcessILocked(IFileInfo& info, std::string_view token, SnapshotDelta& delta) {
  if (const auto known = i_to_r_.find(info.i_frn); known != i_to_r_.end()) {
    RecycleFileMetadata meta = std::move(info.meta);
    meta.is_directory = known->second.is_directory;
    UpsertLocked(known->second.r_frn, std::move(meta), delta);
    return;
  }
  PendingKey key{info.parent_frn, std::string{token}};
  if (const auto waiting = pending_.find(key); waiting != pending_.end()) {
    if (const auto& opt_r = waiting->second.r; opt_r.has_value()) {
      const PendingR partner = opt_r.value();
      pending_.erase(waiting);
      PairLocked(partner.r_frn, partner.is_directory, info.i_frn, std::move(info.meta), key, delta);
      return;
    }
  }
  pending_[std::move(key)].i = PendingI{info.i_frn, std::move(info.meta)};
}

void RecycleBinMetadataStore::PairLocked(std::uint64_t r_frn, bool r_is_directory, std::uint64_t i_frn,
                                         RecycleFileMetadata meta, const PendingKey& key, SnapshotDelta& delta) {
  if (current_->by_file_id.count(r_frn) > 0) {
    DropPublishedLocked(IdSet{r_frn}, IdSet{}, delta);
  }
  if (const auto old = i_to_r_.find(i_frn); old != i_to_r_.end()) {
    DropPublishedLocked(IdSet{old->second.r_frn}, IdSet{i_frn}, delta);
  }
  meta.is_directory = r_is_directory;
  i_to_r_.insert_or_assign(i_frn, PairedRef{r_frn, r_is_directory, key});
  r_to_i_.insert_or_assign(r_frn, i_frn);
  UpsertLocked(r_frn, std::move(meta), delta);
}

void RecycleBinMetadataStore::UpsertLocked(std::uint64_t file_id, RecycleFileMetadata meta, SnapshotDelta& delta) const {
  if (const auto base = current_->by_file_id.find(file_id);
      base != current_->by_file_id.end() && SameMetadata(base->second, meta)) {
    delta.upserts.erase(file_id);
    delta.removals.erase(file_id);
    return;
  }
  delta.removals.erase(file_id);
  delta.upserts.insert_or_assign(file_id, std::move(meta));
}

void RecycleBinMetadataStore::DropPublishedLocked(const IdSet& r_ids, const IdSet& i_ids, SnapshotDelta& delta) {
  if (r_ids.empty() && i_ids.empty()) {
    return;
  }
  for (const std::uint64_t r_frn : r_ids) {
    if (const auto it = r_to_i_.find(r_frn); it != r_to_i_.end()) {
      i_to_r_.erase(it->second);
      r_to_i_.erase(it);
    }
    delta.upserts.erase(r_frn);
    if (current_->by_file_id.count(r_frn) > 0) {
      delta.removals.insert(r_frn);
    }
  }
  for (const std::uint64_t i_frn : i_ids) {
    if (const auto it = i_to_r_.find(i_frn); it != i_to_r_.end()) {
      r_to_i_.erase(it->second.r_frn);
      i_to_r_.erase(it);
    }
  }
}

void RecycleBinMetadataStore::ErasePendingLocked(const IdSet& r_ids, const IdSet& i_ids) {
  if (r_ids.empty() && i_ids.empty()) {
    return;
  }
  for (auto it = pending_.begin(); it != pending_.end();) {
    PendingPair& pair = it->second;
    if (pair.r && r_ids.count(pair.r->r_frn) > 0) {
      pair.r.reset();
    }
    if (pair.i && i_ids.count(pair.i->i_frn) > 0) {
      pair.i.reset();
    }
    if (!pair.r && !pair.i) {
      it = pending_.erase(it);
    } else {
      ++it;
    }
  }
}

void RecycleBinMetadataStore::CommitLocked(const SnapshotDelta& delta) {
  if (delta.upserts.empty() && delta.removals.empty()) {
    return;
  }
  const RecycleMetadataSnapshot& base = *current_;
  auto next = std::make_shared<RecycleMetadataSnapshot>();
  next->generation = base.generation + 1;
  next->by_file_id = base.by_file_id;
  for (const std::uint64_t file_id : delta.removals) {
    next->by_file_id.erase(file_id);
  }
  for (const auto& [file_id, meta] : delta.upserts) {
    next->by_file_id.insert_or_assign(file_id, meta);
  }
  current_ = std::move(next);
}

}  // namespace recycle_bin
