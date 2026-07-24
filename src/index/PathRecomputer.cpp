#include "index/PathRecomputer.h"

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

#include "index/FileIndexStorage.h"
#include "index/LazyValue.h"
#include "index/NtfsFileReference.h"
#include "path/PathBuilder.h"
#include "path/PathOperations.h"
#include "path/PathStorage.h"
#include "path/PathUtils.h"
#include "utils/FileSystemUtils.h"
#include "utils/FileTimeTypes.h"
#include "utils/HashMapAliases.h"
#include "utils/Logger.h"
#include "utils/StringUtils.h"

namespace {

// Sentinel depth for entries unreachable from any root (orphans, cycles).
// These sort last and use the legacy BuildFullPathWithLogging fallback.
constexpr int kOrphanDepth = std::numeric_limits<int>::max();

struct EntryRecord {
  uint64_t id = 0;  // NOLINT(readability-identifier-naming) - plain BFS record fields
  uint64_t parent_id = 0;  // NOLINT(readability-identifier-naming) - plain BFS record fields
  std::string_view leaf_name;  // NOLINT(readability-identifier-naming) - view into NameArena until ReleaseNameCache()
  bool is_directory = false;  // NOLINT(readability-identifier-naming) - plain BFS record fields
  int depth = kOrphanDepth;  // NOLINT(readability-identifier-naming) - plain BFS record fields
};

// Build full path from parent path already in PathStorage (memo). Falls back when parent path is empty.
[[nodiscard]] std::string BuildPathFromMemoizedParent(uint64_t id, uint64_t parent_id,
                                                      std::string_view leaf_name,
                                                      std::string_view parent_path,
                                                      const FileIndexStorage& storage,
                                                      const NameArena& name_cache) {
  if (parent_path.empty()) {
    LOG_WARNING_BUILD("PathRecomputer: empty parent path for id "
                      << id << " (parent " << parent_id << "), using fallback");
    return PathBuilder::BuildFullPathWithLogging(id, parent_id, leaf_name, storage,
                                                 name_cache);
  }
  std::string full_path;
  full_path.reserve(parent_path.size() + leaf_name.size() + 2);
  full_path.assign(parent_path);
  if (full_path.back() != path_utils::kPathSeparator) {
    full_path += path_utils::kPathSeparator;
  }
  full_path.append(TruncateAtEmbeddedNull(leaf_name));
  return full_path;
}

}  // namespace

PathRecomputer::PathRecomputer(FileIndexStorage& storage,
                               PathStorage& path_storage,
                               PathOperations& path_operations)
    : storage_(storage),
      path_storage_(path_storage),
      path_operations_(path_operations) {}

void PathRecomputer::RecomputeAllPaths() {  // NOLINT(readability-function-cognitive-complexity) NOSONAR(cpp:S3776) - BFS + topological sort require nested loops; splitting would require passing large state structs
  const ScopedTimer timer("PathRecomputer::RecomputeAllPaths");
  path_storage_.Clear();
  storage_.ClearDirectoryCache();

  const auto& name_cache = storage_.GetNameCache();
  const size_t n = storage_.Size();

  // ── Step 1: Collect all entries into a flat vector ─────────────────────────
  // leaf_name is a string_view into NameArena; stable until ReleaseNameCache().
  std::vector<EntryRecord> records;
  records.reserve(n);
  for (const auto& [id, entry] : storage_) {
    records.push_back({id, entry.parentID, name_cache.Find(id), entry.isDirectory, kOrphanDepth});
  }

  // ── Step 2: Build parent→children adjacency; identify and seed roots ─────────
  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  // All records[] accesses use loop-bounded indices (i < records.size()) or
  // indices sourced from the children adjacency list (seeded from the same loop).
  //
  // Root detection: only self-parented entries and parent_id == 0 are true roots.
  // Missing parents are orphans (kOrphanDepth) — never promoted to C:\<name>.
  flat_hash_map_t<uint64_t, std::vector<size_t>> children;
  children.reserve(n / 8);  // NOLINT(readability-magic-numbers) — heuristic: ~1 dir per 8 entries
  // Vector-based BFS queue: one contiguous allocation, sequential access — avoids std::deque chunks.
  std::vector<size_t> bfs_queue;
  bfs_queue.reserve(n);  // each entry enqueued at most once
  size_t bfs_front = 0;

  for (size_t i = 0; i < records.size(); ++i) {
    const auto& rec = records[i];
    const bool is_self_root = (rec.parent_id == rec.id);
    if (const bool is_zero_parent = (rec.parent_id == 0); is_self_root || is_zero_parent) {
      records[i].depth = 0;
      bfs_queue.push_back(i);
      continue;
    }
    // Implicit NTFS volume root (MFT record 5) is usually absent from the index.
    // Do not Resolve into a synthetic/"." stand-in — leave at kOrphanDepth so
    // PathBuilder prefixes the real volume root (C:\file1.txt, not C:\.\file1.txt).
    if (ntfs_file_reference::IsRootDirectoryRecord(rec.parent_id)) {
      continue;
    }
    const auto [parent_entry, resolved_parent_id] =
        storage_.ResolveEntryReference(rec.parent_id);
    if (parent_entry == nullptr) {
      continue;
    }
    children[resolved_parent_id].push_back(i);
  }

  // ── Step 4: BFS to assign topological depth ────────────────────────────────
  // Each node is visited at most once; cycles remain at kOrphanDepth.
  while (bfs_front < bfs_queue.size()) {
    const size_t cur_idx = bfs_queue[bfs_front];
    ++bfs_front;
    const int child_depth = records[cur_idx].depth + 1;
    const auto it = children.find(records[cur_idx].id);
    if (it == children.end()) {
      continue;
    }
    for (const size_t child_idx : it->second) {
      if (records[child_idx].depth != kOrphanDepth) {
        continue;
      }
      records[child_idx].depth = child_depth;
      bfs_queue.push_back(child_idx);
    }
  }
  // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

  // ── Step 5: Sort by (depth, id) — parents strictly before children ─────────
  std::sort(records.begin(), records.end(), [](const EntryRecord& a, const EntryRecord& b) {
    if (a.depth != b.depth) {
      return a.depth < b.depth;
    }
    return a.id < b.id;
  });

  // ── Step 6: Single pass — build full paths using PathStorage as memo ───────
  // For depth > 0: parent is already in PathStorage (topological order guarantees it).
  // IMPORTANT: copy parent path string before calling InsertPath — PathStorage's
  // internal vector<char> may reallocate on append, invalidating any string_view.
  std::string full_path;
  full_path.reserve(256);
  size_t onedrive_reset_count = 0;

  for (const auto& rec : records) {
    if (const bool use_memo = (rec.depth > 0) && (rec.depth != kOrphanDepth); use_memo) {
      const auto [parent_entry, resolved_parent_id] = storage_.ResolveEntryReference(rec.parent_id);
      (void)parent_entry;
      full_path = BuildPathFromMemoizedParent(
          rec.id, rec.parent_id, rec.leaf_name,
          path_operations_.GetPathView(resolved_parent_id), storage_, name_cache);
    } else {
      // depth == 0 (true roots) or kOrphanDepth (broken parent chain, cycles):
      // roots, volume prefix, and Windows "X:" components.
      full_path = PathBuilder::BuildFullPathWithLogging(
          rec.id, rec.parent_id, rec.leaf_name, storage_, name_cache);
    }

    path_operations_.InsertPath(rec.id, full_path, rec.is_directory);

    if (!rec.is_directory && IsOneDriveFile(full_path)) {
      storage_.UpdateFileSize(rec.id, kFileSizeNotLoaded);
      storage_.UpdateModificationTime(rec.id, kFileTimeNotLoaded);
      ++onedrive_reset_count;
    }
  }

  if (onedrive_reset_count > 0) {
    LOG_INFO_BUILD("PathRecomputer::RecomputeAllPaths: Reset "
                   << onedrive_reset_count
                   << " OneDrive files to sentinel values for lazy loading");
  }

  // Release name cache: NameArena freed here.
  // records (holding string_view into the arena) goes out of scope immediately after.
  storage_.ReleaseNameCache();
}
