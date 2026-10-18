#include "index/PathRecomputer.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <chrono>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "index/FileIndexStorage.h"
#include "index/NtfsFileReference.h"
#include "index/SystemPathFilter.h"
#include "path/PathStorage.h"
#include "path/PathUtils.h"
#include "utils/FileTimeTypes.h"
#include "utils/HashMapAliases.h"
#include "utils/Logger.h"
#include "utils/StringUtils.h"

namespace {

constexpr int kMaxPathDepth = 64;  // Maximum path depth (prevents infinite loops)

// Collect path components in reverse order (leaf to root).
// Names are read from name_cache as string_view into the arena (stable for arena lifetime).
int CollectPathComponents(uint64_t parent_id,
                          std::string_view name,
                          const FileIndexStorage& storage,
                          const NameArena& name_cache,
                          std::array<std::string_view, kMaxPathDepth>& components) {
  int component_count = 0;

  // Leaf name is already a string_view into the arena — stable for the arena's lifetime.
  // Pseudo-directory leaves (".", "..") contribute no component: the walk below
  // resolves record 5 to the volume root, which BuildPathFromComponents prefixes.
  if (!name.empty() && !system_path_filter::IsPseudoDirectoryName(name)) {
    components.at(static_cast<size_t>(component_count)) = name;
    ++component_count;
  }
  uint64_t current_id = parent_id;

  // Walk up the parent chain, reading each parent's name from the arena.
  while (component_count < kMaxPathDepth) {
    // Implicit NTFS volume root — do not resolve into a synthetic/"." stand-in;
    // BuildPathFromComponents will prefix the real volume root path.
    if (ntfs_file_reference::IsRootDirectoryRecord(current_id)) {
      break;
    }
    const auto [entry, resolved_id] = storage.ResolveEntryReference(current_id);
    if (entry == nullptr) {
      // Return early instead of a second nested break (cpp:S924).
      return component_count;
    }
    const std::string_view parent_name = name_cache.Find(resolved_id);
    if (parent_name.empty()) {
      // Name not in cache (entry inserted after ReleaseNameCache — shouldn't happen).
      // Empty names are never valid, so empty == not found.
      // Return early instead of using a second break to satisfy Sonar's limit on
      // nested break statements (S924) while preserving existing behaviour.
      return component_count;
    }
    if (!system_path_filter::IsPseudoDirectoryName(parent_name)) {
      components.at(static_cast<size_t>(component_count)) = parent_name;
      ++component_count;
    }

    // Check for root directory (parent_id == current_id)
    if (resolved_id == entry->parentID.raw) {
      return component_count;  // Root directory reached
    }
    current_id = entry->parentID.raw;
  }

  return component_count;
}

// Compute the final path length from components.
// Uses truncated component lengths so the result matches the stored string.
size_t ComputeLengthFromComponents(
    const std::array<std::string_view, kMaxPathDepth>& components, int component_count) {
#ifdef _WIN32
  size_t volume_root_len = path_utils::GetDefaultVolumeRootPathView().length();
  int effective_component_count = component_count;
  if (component_count > 0) {
    if (const std::string_view top_name = components.at(static_cast<size_t>(component_count - 1));
        top_name.size() == 2 && top_name[1] == ':' &&
        std::isalpha(static_cast<unsigned char>(top_name[0])) != 0) {
      volume_root_len = 3U;  // "X:" + separator
      --effective_component_count;
    }
  }
#else   // _WIN32
  const size_t volume_root_len = path_utils::GetDefaultVolumeRootPathView().length();
  const int effective_component_count = component_count;
#endif  // _WIN32

  size_t total_len = volume_root_len;
  if (effective_component_count > 0) {
    for (int i = 0; i < effective_component_count; ++i) {
      total_len += TruncateAtEmbeddedNull(components.at(static_cast<size_t>(i))).size() + 1U;
    }
    total_len -= 1U;  // No trailing separator at the end
  }
  return total_len;
}

// Build path string from collected components
std::string BuildPathFromComponents(
    const std::array<std::string_view, kMaxPathDepth>& components,
    int component_count) {
#ifdef _WIN32
  // Calculate total size for single allocation
  std::string drive_root_buffer;
  std::string_view volume_root = path_utils::GetDefaultVolumeRootPathView();
  int effective_component_count = component_count;

  // On Windows, treat a top-level "X:" component as the drive root instead of
  // prefixing with the default volume root. This prevents paths like "C:\\F:\\..."
  // when crawling subst or secondary drives and when directory roots are named "X:".
  if (component_count > 0) {
    if (const std::string_view top_name = components.at(static_cast<size_t>(component_count - 1));
        top_name.size() == 2 && top_name[1] == ':' &&
        std::isalpha(static_cast<unsigned char>(top_name[0])) != 0) {
      drive_root_buffer.clear();
      drive_root_buffer.push_back(top_name[0]);
      drive_root_buffer.push_back(':');
      drive_root_buffer.push_back(path_utils::kPathSeparator);
      volume_root = drive_root_buffer;
      --effective_component_count;
    }
  }
#else   // _WIN32
  // Non-Windows: volume root is always the default; component count is unchanged.
  const std::string_view volume_root = path_utils::GetDefaultVolumeRootPathView();
  const int effective_component_count = component_count;
#endif  // _WIN32

  const size_t total_len = ComputeLengthFromComponents(components, component_count);

  std::string full_path;
  full_path.reserve(total_len);

  // Append volume root
  full_path.append(volume_root);

  // Append components in correct order (root to leaf)
  for (int i = effective_component_count; i > 0; --i) {
    if (i < effective_component_count) {
      full_path.push_back(path_utils::kPathSeparator);
    }
    full_path.append(TruncateAtEmbeddedNull(components.at(static_cast<size_t>(i - 1))));
  }

  return full_path;
}

struct EntryRecord {
  uint64_t id = 0;             // NOLINT(readability-identifier-naming)
  uint64_t parent_id = 0;      // NOLINT(readability-identifier-naming)
  std::string_view leaf_name;  // NOLINT(readability-identifier-naming) - view into NameArena or the path-leaf fallback
  FileEntry* entry_ptr = nullptr;  // NOLINT(readability-identifier-naming) - direct pointer into frozen storage_
  bool is_directory = false;   // NOLINT(readability-identifier-naming)
  bool visited = false;        // NOLINT(readability-identifier-naming)
};

struct DfsFrame {
  // Points into `children` map values. The map is frozen after Step 1
  // (find-only during traversal), so these pointers stay valid for the DFS.
  const std::vector<size_t>* children = nullptr;
  size_t next_child = 0;
  size_t saved_path_len = 0;
};


}  // namespace

PathRecomputer::PathRecomputer(FileIndexStorage& storage,
                               PathStorage& path_storage)
    : storage_(storage),
      path_storage_(path_storage) {}

std::string PathRecomputer::BuildFullPathWithLogging(
    uint64_t file_id, uint64_t parent_id, std::string_view name,
    const FileIndexStorage& storage, const NameArena& name_cache,
    bool log_depth_warning) {
  std::array<std::string_view, kMaxPathDepth> components{};  // NOLINT(cppcoreguidelines-pro-type-member-init) - value-initialized

  const int component_count = CollectPathComponents(
      parent_id, name, storage, name_cache, components);

  if (log_depth_warning && component_count >= kMaxPathDepth) {
    std::ostringstream oss;
    oss << "Path depth limit (" << kMaxPathDepth << ") reached for file ID: 0x"
        << std::hex << file_id << std::dec << ", name: " << name
        << " (path may be incomplete)";
    LOG_WARNING(oss.str());
  }

  std::string result = BuildPathFromComponents(components, component_count);
  // Postcondition: the volume root alone is already non-empty, so the result
  // must never be empty regardless of component_count.
  assert(!result.empty() && "Built path must not be empty");
  return result;
}

PathRecomputer::Stats PathRecomputer::RecomputeAllPaths() {  // NOLINT(readability-function-cognitive-complexity) NOSONAR(cpp:S3776) - Iterative DFS traversal requires state machine loops; splitting would require passing large state structs
  Stats stats;
  const size_t n = storage_.Size();
  const auto& name_cache = storage_.GetNameCache();

  // Eager-path inserts (folder crawl with populate_name_cache=false) have no
  // arena entry; their leaves come back from the already-materialized paths.
  // Snapshot them before Clear() drops PathStorage. Bulk-staged entries have
  // no paths yet and always come from the arena, so they never land here.
  // string_key lookup is by id (not path), so no per-entry allocation beyond
  // the fallback rows themselves — and only for cache-missing entries.
  flat_hash_map_t<uint64_t, std::string> path_leaf_fallback;
  constexpr auto kNoPathIndex = static_cast<size_t>(-1);
  for (auto& [id, entry] : storage_) {
    if (!name_cache.Find(id.raw).empty()) {
      continue;
    }
    if (entry.path_storage_index == kNoPathIndex ||
        entry.path_storage_index >= path_storage_.GetSize()) {
      continue;
    }
    // try_emplace (not emplace): the fallback string is only built when the key is new.
    // Pass the string_view straight through (no explicit std::string wrapper) - try_emplace
    // forwards it to the std::string constructor. GetFilename stays a pure lookup either way.
    path_leaf_fallback.try_emplace(
        id.raw,
        path_utils::GetFilename(path_storage_.GetPathByIndex(entry.path_storage_index)));
  }

  path_storage_.Clear();
  storage_.ClearDirectoryCache();

  // ── Step 1: Collect entries & build parent -> children adjacency ───────────
  std::vector<EntryRecord> records;
  std::vector<size_t> roots;
  flat_hash_map_t<uint64_t, std::vector<size_t>> children;

  const auto collect_start = std::chrono::steady_clock::now();
  {
    records.reserve(n);
    for (auto& [id, entry] : storage_) {
      // Arena first; eager-path entries fall back to the pre-Clear snapshot.
      std::string_view leaf = name_cache.Find(id.raw);
      if (leaf.empty()) {
        if (const auto fb_it = path_leaf_fallback.find(id.raw);
            fb_it != path_leaf_fallback.end()) {
          leaf = fb_it->second;
        }
      }
      records.push_back({id.raw, entry.parentID.raw, leaf, &entry, entry.isDirectory, false});
    }

    roots.reserve(128U);
    children.reserve(n / 8);  // NOLINT(readability-magic-numbers) - heuristic: ~1 dir per 8 entries

    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    for (size_t i = 0; i < records.size(); ++i) {
      const auto& rec = records[i];
      const bool is_self_root = (rec.parent_id == rec.id);
      if (const bool is_zero_parent = (rec.parent_id == 0);
          is_self_root || is_zero_parent || ntfs_file_reference::IsRootDirectoryRecord(rec.parent_id)) {
        roots.push_back(i);
        continue;
      }
      const auto [parent_entry, resolved_parent_id] =
          storage_.ResolveEntryReference(rec.parent_id);
      if (parent_entry == nullptr) {
        ++stats.unresolved_parent_entries;
        continue;
      }
      children[resolved_parent_id].push_back(i);
      ++stats.adjacency_edges;
    }
    // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

    // Sort each sibling list by entry id: storage_ iteration order (hence
    // insertion order above) is hash order, so without this the callback
    // firing order — and path_to_id layout — would vary run to run.
    for (auto& [parent_id, child_list] : children) {
      std::sort(child_list.begin(), child_list.end(),
                // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - indices built above, always < records.size()
                [&records](size_t a, size_t b) { return records[a].id < records[b].id; });
    }

    // Sort roots by ID for deterministic traversal order
    std::sort(roots.begin(), roots.end(), [&records](size_t a, size_t b) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
      return records[a].id < records[b].id;
    });
  }
  stats.collect_time_microseconds = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - collect_start)
          .count());
  stats.entries_collected = records.size();
  stats.roots = roots.size();
  stats.adjacency_buckets = children.size();

  // ── Step 2: Iterative DFS path assembly with backtrackable path buffer ─────
  size_t onedrive_reset_count = 0;
  std::string path_buffer;
  path_buffer.reserve(1024U);

  std::vector<DfsFrame> dfs_stack;
  dfs_stack.reserve(64U);

  // Failed loads must retry on the next recompute, not stick permanently.
  const auto clear_failed_sentinels = [&](size_t idx) {
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    if (records[idx].entry_ptr->fileSize.IsFailed()) {
      records[idx].entry_ptr->fileSize = LazyFileSize();
    }
    if (records[idx].entry_ptr->lastModificationTime.IsFailed()) {
      records[idx].entry_ptr->lastModificationTime = LazyFileTime(kFileTimeNotLoaded);
    }
    // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  };

  const auto process_entry = [&](size_t idx, std::string_view full_path,  // NOSONAR(cpp:S1188) - traversal lambdas share records/stats/buffers; extracting them would trade this for S107 parameter soup (see S3776 rationale on RecomputeAllPaths)
                                 std::optional<size_t> fn_start = std::nullopt,
                                 std::optional<size_t> ext_start = std::nullopt) {
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    const size_t path_idx = path_storage_.BulkAppendPath(
        records[idx].id, full_path, records[idx].is_directory, fn_start, ext_start);
    ++stats.path_entries;
    stats.path_bytes += static_cast<uint64_t>(full_path.size() + 1U);
    // Truncated view is empty iff the raw path is empty or starts with NUL
    // (BulkAppendPath truncates at the first embedded null). O(1), no scan.
    // Matches the rebuild's len==0 skip so the FileIndex assert holds exactly.
    const bool is_empty_path = full_path.empty() || full_path.front() == '\0';
    if (is_empty_path) {
      ++stats.empty_paths;
    }
    if (fn_start.has_value()) {
      ++stats.offset_fast_path_entries;
    }
    records[idx].entry_ptr->path_storage_index = path_idx;

    if (!is_empty_path) {
      clear_failed_sentinels(idx);
    }

    if (!records[idx].is_directory && records[idx].entry_ptr->IsOffline()) {
      records[idx].entry_ptr->fileSize = LazyFileSize(kFileSizeNotLoaded);
      records[idx].entry_ptr->lastModificationTime = LazyFileTime(kFileTimeNotLoaded);
      ++onedrive_reset_count;
    }
    // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  };

  // Extension offset from a truncated leaf: no extension for dotfiles,
  // trailing dots, or extensionless names (mirrors ParsePathOffsets).
  const auto extension_start_for = [](std::string_view leaf, size_t fn_start)
                                       -> std::optional<size_t> {
    if (const size_t last_dot = leaf.rfind('.');
        last_dot != std::string_view::npos && last_dot > 0 && last_dot < leaf.length() - 1) {
      return fn_start + last_dot + 1;
    }
    return std::nullopt;
  };

  // Pushes a frame for a directory's children; false when there is nothing
  // to descend into. Never holds a frame reference across the push.
  const auto try_descend_into = [&](size_t child_idx, size_t saved_len) {
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    if (!records[child_idx].is_directory) {
      return false;
    }
    const auto child_it = children.find(records[child_idx].id);
    if (child_it == children.end() || child_it->second.empty()) {
      return false;
    }
    dfs_stack.push_back({&child_it->second, 0, saved_len});
    stats.max_dfs_depth = (std::max)(stats.max_dfs_depth, dfs_stack.size());
    return true;
    // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  };

  // Visits one child: appends its leaf, fires the entry, then descends or
  // backtracks. The frame is always re-acquired by the caller afterwards.
  const auto visit_next_child = [&] {
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    DfsFrame* top = &dfs_stack.back();
    const size_t child_idx = (*top->children)[top->next_child++];
    if (records[child_idx].visited) {
      return;
    }
    records[child_idx].visited = true;

    const size_t saved_len = path_buffer.length();
    if (!path_buffer.empty() && path_buffer.back() != path_utils::kPathSeparator) {
      path_buffer.push_back(path_utils::kPathSeparator);
    }
    const size_t fn_start = path_buffer.length();
    const std::string_view leaf = TruncateAtEmbeddedNull(records[child_idx].leaf_name);
    path_buffer.append(leaf);

    process_entry(child_idx, path_buffer, fn_start, extension_start_for(leaf, fn_start));

    if (try_descend_into(child_idx, saved_len)) {
      return;
    }
    path_buffer.resize(saved_len);
    // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  };

  const auto run_dfs_from_current_buffer = [&](const std::vector<size_t>& initial_children) {
    dfs_stack.clear();
    dfs_stack.push_back({&initial_children, 0, 0});

    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    while (!dfs_stack.empty()) {
      if (const DfsFrame* top = &dfs_stack.back(); top->next_child >= top->children->size()) {
        path_buffer.resize(top->saved_path_len);
        dfs_stack.pop_back();
      } else {
        visit_next_child();
      }
    }
    // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  };

  const auto run_traversal = [&] {  // NOLINT(readability-function-cognitive-complexity) - Iterative DFS traversal requires state machine loops // NOSONAR(cpp:S1188) - driver over shared traversal state; splitting it would thread the whole DFS state through parameters (see S3776 rationale on RecomputeAllPaths)
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    for (const size_t root_idx : roots) {
      if (records[root_idx].visited) {
        continue;
      }
      records[root_idx].visited = true;

      path_buffer = BuildFullPathWithLogging(
          records[root_idx].id, records[root_idx].parent_id,
          records[root_idx].leaf_name, storage_, name_cache);
      if (path_buffer.empty()) {
        LOG_WARNING_BUILD("PathRecomputer::RecomputeAllPaths: empty path for root id "
                          << records[root_idx].id << "; its subtree resolves relatively");
      }

      process_entry(root_idx, path_buffer);

      if (const auto root_children_it = children.find(records[root_idx].id);
          root_children_it != children.end() && !root_children_it->second.empty()) {
        run_dfs_from_current_buffer(root_children_it->second);
      }
    }

    // ── Step 3: Fallback for any unreachable records (orphans / cycles) ──────
    // Collect-then-sort: records order is hash order, so iterating it directly
    // would make fallback order (and path_to_id layout) vary run to run.
    // The visited re-check inside skips entries a previous fallback's DFS run
    // already claimed, preserving single-processing.
    std::vector<size_t> fallback;
    for (size_t i = 0; i < records.size(); ++i) {
      if (!records[i].visited) {
        fallback.push_back(i);
      }
    }
    std::sort(fallback.begin(), fallback.end(),
              [&records](size_t a, size_t b) { return records[a].id < records[b].id; });
    for (const size_t i : fallback) {
      if (records[i].visited) {
        continue;
      }
      records[i].visited = true;
      ++stats.fallback_entries;

      path_buffer = BuildFullPathWithLogging(
          records[i].id, records[i].parent_id, records[i].leaf_name, storage_, name_cache);
      if (path_buffer.empty()) {
        LOG_WARNING_BUILD("PathRecomputer::RecomputeAllPaths: empty fallback path for id "
                          << records[i].id);
      }

      process_entry(i, path_buffer);

      if (records[i].is_directory) {
        if (const auto orphan_children_it = children.find(records[i].id);
            orphan_children_it != children.end() && !orphan_children_it->second.empty()) {
          run_dfs_from_current_buffer(orphan_children_it->second);
        }
      }
    }
    // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  };

  // Single pass with heuristic reserve: entry count is exactly n (every record
  // gets one path, including orphans via fallback). Bytes use ~128 B average
  // (telemetry: ~123 B); geometric growth covers variance at ~ms cost.
  static constexpr size_t kEstimatedAvgPathBytes = 128U;
  path_storage_.BeginBulkAppend(n, n * kEstimatedAvgPathBytes);

  const auto dfs_start = std::chrono::steady_clock::now();
  run_traversal();
  path_storage_.FinishBulkAppend();
  const PathStorage::Stats materialized_path_storage = path_storage_.GetStats();
  assert(materialized_path_storage.total_entries == n);
  assert(stats.path_entries == n);  // every record got exactly one fresh path

#ifndef NDEBUG
  // D2 safety net (AGENTS.md: SearchResult path-pool lifecycle +
  // .agents/rules/string-view-pool-remap-safety-cpp.md): the remap must be
  // EXHAUSTIVE. After this function returns, no storage entry may still hold
  // a path_storage_index from the previous generation - a stale index is
  // exactly how a view into a torn-down pool is born. (GetPathByIndex
  // bounds-asserts on use; this asserts on produce, so corruption cannot
  // escape undetected even if no read follows.) O(n) and debug-only.
  for (auto& [stale_check_id, stale_check_entry] : storage_) {
    assert(stale_check_entry.path_storage_index < path_storage_.GetSize());
    (void)stale_check_id;
  }
#endif  // NDEBUG
  stats.dfs_time_microseconds = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - dfs_start)
          .count());
  stats.visited_entries = stats.path_entries;

  if (onedrive_reset_count > 0) {
    LOG_INFO_BUILD("PathRecomputer::RecomputeAllPaths: Reset "
                   << onedrive_reset_count
                   << " OneDrive files to sentinel values for lazy loading");
  }

  // Release name cache: NameArena freed here.
  // records (holding string_view into the arena) goes out of scope immediately after.
  storage_.ReleaseNameCache();
  stats.path_storage = materialized_path_storage;
  return stats;
}
