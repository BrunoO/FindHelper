#pragma once

/**
 * @file RecycleBinMetadataStore.h
 * @brief Pairs NTFS Recycle Bin $I/$R artifacts and publishes immutable metadata snapshots.
 *
 * Spec section 5 Phase 4 item 1 (INV-6). Pure C++17: no Win32, no logging, no platform I/O.
 *
 * Pairing (spec section 3.5.1): a $I<token>.<ext> sibling and a $R<token>.<ext> sibling under
 * the same <SID> parent form one deleted item. Producers report both sides keyed by
 * (parent_frn, token) in any order; the store pairs whichever side arrives second.
 * Matching rules: the I/R tag is ASCII case-insensitive (see TokenizeRecycleName), but the token
 * is compared case-sensitively. This assumes NTFS writes byte-identical tokens on both sides of a
 * pair, which holds for every Windows Recycle Bin layout we parse.
 *
 * Publication model (copy-on-write): every visible state is a RecycleMetadataSnapshot behind
 * shared_ptr<const>. A call that changes visible state builds one new snapshot and publishes it
 * with generation + 1. Calls that change nothing publish nothing, so Version() only moves on a
 * real change. Readers keep the snapshot they obtained via Current() for as long as they hold it.
 *
 * Cost note: a publishing call copies the published maps once (O(N) in published pairs). Batch
 * producers should prefer IngestRFiles / PublishIEntries / EvictMany over one call per item.
 *
 * Thread safety: every public method takes the store mutex. Current() hands out a shared_ptr
 * copy, so readers never race with publishers.
 */

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace recycle_bin {

/// Metadata of one deleted item, keyed by its $R fileId inside a snapshot.
struct RecycleFileMetadata {
  std::string original_name;
  std::string original_path;
  /// RAW Win32 FILETIME, copied from the $I record.
  std::uint64_t deletion_time_filetime = 0;
  std::uint64_t original_size = 0;
  /// Taken from the $R payload (authoritative), not from the $I record.
  bool is_directory = false;
};

/// Immutable once published. Obtain through RecycleBinMetadataStore::Current().
struct RecycleMetadataSnapshot {
  std::uint64_t generation = 0;
  std::unordered_map<std::uint64_t, RecycleFileMetadata> by_file_id;
};

enum class RecycleNamePrefix : std::uint8_t {
  kI,  // NOLINT(readability-identifier-naming) - k prefix is project convention for enum constants
  kR,  // NOLINT(readability-identifier-naming) - k prefix is project convention for enum constants
};

/// Result of TokenizeRecycleName. token and extension are views into the tokenized filename,
/// so the caller must keep that string alive while using them.
struct RecycleNameToken {
  RecycleNamePrefix prefix = RecycleNamePrefix::kI;
  std::string_view token;
  std::string_view extension;
};

/**
 * Splits a Recycle Bin artifact name ("$I<token>[.<ext>]" or "$R<token>[.<ext>]").
 *
 * The prefix tag is ASCII case-insensitive. The token is everything between the tag and the
 * first '.', so it never contains a dot. The extension is everything after that dot, possibly
 * empty and possibly dotted ("$R1A2B3C.tar.gz" gives "tar.gz"). A name is rejected when it is
 * shorter than 3 characters, does not start with '$I' or '$R', or has an empty token.
 *
 * Note: "$Recycle.Bin" matches the shape (token "ecycle"). Callers must pre-filter bin
 * containers. SystemPathFilter already does.
 *
 * @param filename Bare file name (no directory part).
 * @return Tokenized parts, or std::nullopt when the name is not a Recycle Bin artifact.
 */
[[nodiscard]] std::optional<RecycleNameToken> TokenizeRecycleName(std::string_view filename);

/// $R-side report from a producer (cold index or USN).
struct RFileInfo {
  std::uint64_t r_frn = 0;
  std::uint64_t parent_frn = 0;
  std::string name;
  bool is_directory = false;
};

/// $I-side report from a producer. meta.is_directory is ignored (the $R side decides).
struct IFileInfo {
  std::uint64_t i_frn = 0;
  std::uint64_t parent_frn = 0;
  std::string name;
  RecycleFileMetadata meta;
};

enum class EvictKind : std::uint8_t {
  kByR,         // NOLINT(readability-identifier-naming) - k prefix is project convention for enum constants
  kByI,         // NOLINT(readability-identifier-naming) - k prefix is project convention for enum constants
  kRenamedOut,  // NOLINT(readability-identifier-naming) - k prefix is project convention for enum constants
};

/// One eviction request for EvictMany. frn is an $R fileId for kByR and kRenamedOut, and an $I FRN for kByI.
struct EvictOp {
  EvictKind kind = EvictKind::kByR;
  std::uint64_t frn = 0;
};

class RecycleBinMetadataStore {
 public:
  RecycleBinMetadataStore();
  RecycleBinMetadataStore(const RecycleBinMetadataStore&) = delete;
  RecycleBinMetadataStore& operator=(const RecycleBinMetadataStore&) = delete;
  RecycleBinMetadataStore(RecycleBinMetadataStore&&) = delete;
  RecycleBinMetadataStore& operator=(RecycleBinMetadataStore&&) = delete;
  ~RecycleBinMetadataStore() = default;

  /// Records a batch of $R sides. Each pairs with a waiting $I or pends. Publishes at most once.
  void IngestRFiles(const std::vector<RFileInfo>& infos);

  /// Records a batch of $I sides. Each pairs with a waiting $R or pends. Publishes at most once.
  void PublishIEntries(std::vector<IFileInfo> infos);

  /// Applies all eviction ops as one batch. Publishes at most once.
  void EvictMany(const std::vector<EvictOp>& ops);

  /// Latest published snapshot. Never null.
  [[nodiscard]] std::shared_ptr<const RecycleMetadataSnapshot> Current() const;
  /// Generation of the latest published snapshot. Moves only on a real change.
  [[nodiscard]] std::uint64_t Version() const;

  /// Clears pending halves, the pairing index, and published state.
  void Reset();

 private:
  struct PendingR {
    std::uint64_t r_frn = 0;
    bool is_directory = false;
  };
  struct PendingI {
    std::uint64_t i_frn = 0;
    RecycleFileMetadata meta;
  };
  struct PendingPair {
    std::optional<PendingR> r;
    std::optional<PendingI> i;
  };
  // (parent_frn, token) pairing key.
  using PendingKey = std::pair<std::uint64_t, std::string>;
  using IdSet = std::unordered_set<std::uint64_t>;
  // Batch-local dedup identity for IngestRFiles: one $R fileId under one pairing key.
  using BatchRKey = std::pair<std::uint64_t, PendingKey>;
  // Pairing index for published items: $I FRN -> its $R fileId, payload kind, and pairing key.
  struct PairedRef {
    std::uint64_t r_frn = 0;
    bool is_directory = false;
    PendingKey key;
  };
  // Changes accumulated during one locked call, committed as a single publish.
  struct SnapshotDelta {
    std::unordered_map<std::uint64_t, RecycleFileMetadata> upserts;
    std::unordered_set<std::uint64_t> removals;
  };

  void ProcessRLocked(const RFileInfo& info, std::string_view token, SnapshotDelta& delta);
  void ProcessILocked(IFileInfo& info, std::string_view token, SnapshotDelta& delta);
  void PairLocked(std::uint64_t r_frn, bool r_is_directory, std::uint64_t i_frn, RecycleFileMetadata meta,
                  const PendingKey& key, SnapshotDelta& delta);
  void UpsertLocked(std::uint64_t file_id, RecycleFileMetadata meta, SnapshotDelta& delta);
  void DropPublishedLocked(const IdSet& r_ids, const IdSet& i_ids, SnapshotDelta& delta);
  void ErasePendingLocked(const IdSet& r_ids, const IdSet& i_ids);
  void CommitLocked(const SnapshotDelta& delta);

  mutable std::mutex mutex_;
  std::shared_ptr<const RecycleMetadataSnapshot> current_;
  std::map<PendingKey, PendingPair> pending_;
  std::unordered_map<std::uint64_t, PairedRef> i_to_r_;
  std::unordered_map<std::uint64_t, std::uint64_t> r_to_i_;
};

}  // namespace recycle_bin
