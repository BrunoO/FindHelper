#include "path/PathStorage.h"

#include <cassert>
#include <cstring>

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
// All SoA array accesses use operator[] intentionally for performance.
// Indices always originate from id_to_path_index_ (a validated lookup) or
// a size_t loop counter bounded by path_ids_.size(), so bounds are guaranteed
// by PathStorage's own invariants. Using .at() would add redundant checks
// in the hot search-iteration and insert paths.

PathStorage::PathStorage() {  // NOLINT(hicpp-use-equals-default,modernize-use-equals-default,cppcoreguidelines-pro-type-member-init,hicpp-member-init) - Cannot use = default; reserve() in body. Members default-initialized then reserve()d.
  // Initialize arrays with reasonable capacity
  path_storage_.reserve(kInitialPathStorageCapacity);
  path_offsets_.reserve(kInitialPathArrayCapacity);
  path_ids_.reserve(kInitialPathArrayCapacity);
  filename_start_.reserve(kInitialPathArrayCapacity);
  extension_start_.reserve(kInitialPathArrayCapacity);
  is_deleted_.reserve(kInitialPathArrayCapacity);
  is_directory_.reserve(kInitialPathArrayCapacity);
}

void PathStorage::InsertPath(uint64_t id, const std::string &path,
                              bool isDirectory) {  // NOLINT(readability-identifier-naming) - Public API parameter name
  // Precondition: id 0 is reserved; all real file/dir IDs must be non-zero.
  assert(id != 0 && "File ID 0 is reserved; callers must provide a non-zero ID");

  // Parse once; used in both the update and new-entry paths.
  size_t filename_start_offset = 0;
  size_t extension_start_offset = SIZE_MAX;
  ParsePathOffsets(path, filename_start_offset, extension_start_offset);

  // If the ID already has a slot and the new path fits in the existing
  // allocation, update it in-place rather than marking it deleted and
  // appending a new one. This keeps the SoA arrays compact, avoids growing
  // deleted_count_, and prevents unnecessary RebuildPathBuffer calls during a
  // bulk crawl (e.g. DirectoryResolver pre-inserts overwritten by real
  // entries). When the new path is longer than the previous allocation we
  // fall back to the "new entry" path below and treat the old slot as a
  // deleted tombstone so RebuildPathBuffer can reclaim its storage.
  if (auto path_iter = id_to_path_index_.find(id); path_iter != id_to_path_index_.end()) {
    const size_t idx = path_iter->second;
    const size_t old_offset = path_offsets_[idx];
    const size_t new_len = path.length();
    if (const size_t old_len = std::strlen(&path_storage_[old_offset]);  // NOSONAR(cpp:S1081) - Safe: path_storage_ entries are null-terminated (see AppendString)
        new_len <= old_len) {
      // New path fits in the existing slot: overwrite in-place (zero growth in path_storage_).
      // Critical for RecomputeAllPaths which re-inserts identical paths for every entry.
      std::memcpy(&path_storage_[old_offset], path.c_str(), new_len + 1);
      filename_start_[idx] = filename_start_offset;
      extension_start_[idx] = extension_start_offset;
      is_directory_[idx] = isDirectory ? 1 : 0;
      if (is_deleted_[idx] != 0) {
        is_deleted_[idx] = 0;
        deleted_count_.fetch_sub(1, std::memory_order_relaxed);
      }
      return;
    }

    // New path is longer than the existing allocation: mark the current slot
    // as a tombstone and fall through to the "new entry" path, which appends
    // a fresh slot to all SoA arrays. This preserves deleted_count_ semantics
    // and allows FileIndexMaintenance::RebuildPathBuffer() to reclaim the old
    // storage.
    if (is_deleted_[idx] == 0) {
      is_deleted_[idx] = 1;
      deleted_count_.fetch_add(1, std::memory_order_relaxed);
    }
    id_to_path_index_.erase(path_iter);
  }

  // New entry: append a new slot to all SoA arrays.
  const size_t offset = AppendString(path);
  const size_t idx = path_offsets_.size();

  path_offsets_.push_back(offset);
  path_ids_.push_back(id);
  filename_start_.push_back(filename_start_offset);
  extension_start_.push_back(extension_start_offset);
  is_deleted_.push_back(0);                     // 0 = not deleted
  is_directory_.push_back(isDirectory ? 1 : 0); // 0 = file, 1 = directory

  id_to_path_index_[id] = idx;

  // Postcondition: all SoA arrays must remain the same length after insertion.
  AssertSoAInvariant("SoA arrays must remain synchronized after InsertPath");
}

bool PathStorage::RemovePath(uint64_t id) {
  auto path_iter = id_to_path_index_.find(id);
  if (path_iter == id_to_path_index_.end()) {
    return false;
  }

  if (const size_t idx = path_iter->second; is_deleted_[idx] == 0) {
    is_deleted_[idx] = 1;
    deleted_count_.fetch_add(1, std::memory_order_relaxed);
    return true;
  }
  // Already deleted
  return false;
}

void PathStorage::UpdatePath(uint64_t id, const std::string &newPath, bool isDirectory) {  // NOLINT(readability-identifier-naming) - Public API parameter names (id, newPath, isDirectory)
  // Remove old entry and insert new one
  // Note: We intentionally ignore RemovePath's return value - it doesn't matter if the old path existed
  (void)RemovePath(id);
  InsertPath(id, newPath, isDirectory);
}

void PathStorage::UpdatePrefix(std::string_view oldPrefix,  // NOLINT(readability-identifier-naming) - Public API parameter name
                                std::string_view newPrefix) {  // NOLINT(readability-identifier-naming) - Public API parameter name
  // Collect updates first to avoid invalidating pointers/iterators due to
  // reallocation
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init,hicpp-member-init) - path default-initialized then assigned in push_back
  struct UpdateInfo {
    uint64_t file_id = 0;
    std::string path;
    bool is_directory = false;
  };
  std::vector<UpdateInfo> updates_full{};
  updates_full.reserve(100);  // NOLINT(readability-magic-numbers) - Heuristic capacity

  const size_t count = path_ids_.size();
  const size_t old_len = oldPrefix.length();

  for (size_t i = 0; i < count; ++i) {
    if (is_deleted_[i] != 0) {
      continue;
    }

    const char *path = &path_storage_[path_offsets_[i]];
    if (const size_t path_len = std::strlen(path); path_len < old_len) {  // NOSONAR(cpp:S1081) - Safe: path_storage_ entries are null-terminated (see AppendString)
      continue;
    }
    if (std::string_view(path, old_len) == oldPrefix) {
      std::string new_path_str(newPrefix);
      new_path_str.append(path + old_len);

      updates_full.push_back(
          {path_ids_[i], std::move(new_path_str), (is_directory_[i] == 1)});
    }
  }

  // Apply updates
  for (const auto &update : updates_full) {
    InsertPath(update.file_id, update.path, update.is_directory);
  }
}

PathStorage::SoAView PathStorage::GetReadOnlyView() const {
  SoAView view;
  if (path_ids_.empty()) {
    return view; // Return empty view
  }

  // Invariant: all SoA arrays must be the same length before we hand out raw
  // pointers.  A size mismatch indicates a bug in InsertPath / RebuildPathBuffer.
  AssertSoAInvariant("SoA arrays must be synchronized before producing a view");

  view.path_storage = path_storage_.data();
  view.path_offsets = path_offsets_.data();
  view.path_ids = path_ids_.data();
  view.filename_start = filename_start_.data();
  view.extension_start = extension_start_.data();
  view.is_deleted = is_deleted_.data();
  view.is_directory = is_directory_.data();
  view.size = path_ids_.size();

  return view;
}

std::string PathStorage::GetPath(uint64_t id) const {
  auto path_iter = id_to_path_index_.find(id);
  if (path_iter == id_to_path_index_.end()) {
    return "";
  }

  const size_t idx = path_iter->second;
  assert(idx < path_offsets_.size());
  assert(path_offsets_[idx] < path_storage_.size());
  if (is_deleted_[idx] != 0) {
    return "";
  }

  // Direct pointer access to contiguous buffer
  return {&path_storage_[path_offsets_[idx]]};
}

std::string_view PathStorage::GetPathView(uint64_t id) const {
  auto path_iter = id_to_path_index_.find(id);
  if (path_iter == id_to_path_index_.end()) {
    return {};
  }

  const size_t idx = path_iter->second;
  if (idx >= path_offsets_.size() || is_deleted_[idx] != 0) {
    return {};
  }

  assert(idx < path_offsets_.size());
  assert(is_deleted_[idx] == 0);
  // Zero-copy access to contiguous buffer
  const char *path_start = &path_storage_[path_offsets_[idx]];
  return {path_start};
}

std::string_view PathStorage::GetPathByIndex(size_t index) const {
  if (index >= path_offsets_.size() || is_deleted_[index] != 0) {
    return {};
  }

  assert(index < path_offsets_.size());
  assert(is_deleted_[index] == 0);
  const char *path_start = &path_storage_[path_offsets_[index]];
  return {path_start};
}

size_t PathStorage::GetIndexForId(uint64_t file_id) const {
  auto path_iter = id_to_path_index_.find(file_id);
  if (path_iter == id_to_path_index_.end()) {
    return SIZE_MAX;
  }
  return path_iter->second;
}

void PathStorage::RebuildPathBuffer() {
  rebuild_count_.fetch_add(1, std::memory_order_relaxed);

  // Collect all non-deleted entries
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init,hicpp-member-init) - path default-initialized then assigned in push_back
  struct Entry {
    uint64_t file_id = 0;
    std::string path;
    size_t filename_start = 0;
    size_t extension_start = 0;
    bool is_directory = false;
  };
  std::vector<Entry> entries{};
  entries.reserve(path_ids_.size() - deleted_count_.load(std::memory_order_relaxed));

  // Collect non-deleted entries
  for (size_t i = 0; i < path_ids_.size(); ++i) {
    if (is_deleted_[i] == 0) {
      const char *path = &path_storage_[path_offsets_[i]];
      entries.push_back({path_ids_[i], std::string(path), filename_start_[i],
                         extension_start_[i], (is_directory_[i] == 1)});
    }
  }

  ClearAll();

  // Reserve to avoid reallocations during append (each resize can trigger memcpy)
  size_t total_path_bytes = 0;
  for (const auto &entry : entries) {
    total_path_bytes += entry.path.length() + 1;  // +1 for null terminator
  }
  path_storage_.reserve(total_path_bytes);
  path_offsets_.reserve(entries.size());
  path_ids_.reserve(entries.size());
  filename_start_.reserve(entries.size());
  extension_start_.reserve(entries.size());
  is_deleted_.reserve(entries.size());
  is_directory_.reserve(entries.size());

  // Rebuild from collected entries
  for (const auto &entry : entries) {
    const size_t offset = AppendString(entry.path);
    const size_t idx = path_offsets_.size();

    path_offsets_.push_back(offset);
    path_ids_.push_back(entry.file_id);
    filename_start_.push_back(entry.filename_start);
    extension_start_.push_back(entry.extension_start);
    is_deleted_.push_back(0);
    is_directory_.push_back(entry.is_directory ? 1 : 0);

    id_to_path_index_[entry.file_id] = idx;
  }

  LOG_INFO_BUILD("PathStorage::RebuildPathBuffer: Rebuilt " << entries.size()
                                                             << " entries");
}

void PathStorage::ClearAll() {
  // Clear all arrays and release memory
  path_storage_.clear();
  path_storage_.shrink_to_fit();
  path_offsets_.clear();
  path_offsets_.shrink_to_fit();
  path_ids_.clear();
  path_ids_.shrink_to_fit();
  filename_start_.clear();
  filename_start_.shrink_to_fit();
  extension_start_.clear();
  extension_start_.shrink_to_fit();
  is_deleted_.clear();
  is_deleted_.shrink_to_fit();
  is_directory_.clear();
  is_directory_.shrink_to_fit();
  id_to_path_index_.clear();
  id_to_path_index_.rehash(0); // For hash_map, rehash(0) releases memory

  // Reset deleted count
  deleted_count_.store(0, std::memory_order_relaxed);
}

void PathStorage::Clear() {
  ClearAll();
  rebuild_count_.store(0, std::memory_order_relaxed);
}

PathStorage::Stats PathStorage::GetStats() const {
  Stats stats;
  stats.total_entries = path_ids_.size();
  stats.deleted_entries = deleted_count_.load(std::memory_order_relaxed);
  stats.path_storage_bytes = path_storage_.size();
  stats.rebuild_count = rebuild_count_.load(std::memory_order_relaxed);
  return stats;
}

size_t PathStorage::AppendString(const std::string &str) {
  const size_t offset = path_storage_.size();
  const size_t len = str.length() + 1; // Include null terminator

  path_storage_.resize(path_storage_.size() + len);
  // Use safe string copy (Windows: strcpy_s, others: memcpy + manual null)
#ifdef _WIN32
  strcpy_s(&path_storage_[offset], len, str.c_str());
#else
  std::memcpy(&path_storage_[offset], str.c_str(), len - 1);
  path_storage_[offset + len - 1] = '\0';
#endif  // _WIN32

  return offset;
}

void PathStorage::AssertSoAInvariant([[maybe_unused]] const char* context) const {
  assert(path_offsets_.size() == path_ids_.size() &&
         path_ids_.size() == filename_start_.size() &&
         filename_start_.size() == extension_start_.size() &&
         extension_start_.size() == is_deleted_.size() &&
         is_deleted_.size() == is_directory_.size() &&
         context);
}

void PathStorage::ParsePathOffsets(std::string_view path,
                                    size_t &filename_start,
                                    size_t &extension_start) const {
  // Pre-parse path: Find where filename starts (after last slash/backslash)
  // This pre-parsing eliminates parsing overhead in the hot search loop
  filename_start = 0;
  if (const size_t last_slash = path.find_last_of("\\/"); last_slash != std::string_view::npos) {
    filename_start = last_slash + 1;
  }

  // Pre-parse extension: Find where extension starts (after last dot in filename)
  extension_start = SIZE_MAX; // Sentinel: no extension
  if (filename_start < path.length()) {
    // Find last dot in filename portion
    // OPTIMIZATION: Use find_last_of with offset to avoid unnecessary string allocation
    const size_t last_dot = path.find_last_of('.', path.length() - 1);
    // Ensure the dot is within the filename portion (not in directory path) and is
    // not the leading dot of a dotfile (e.g. ".gitignore" has no extension).
    if (last_dot != std::string_view::npos && last_dot > filename_start &&
        last_dot < path.length() - 1) {
      // Extension exists and is non-empty (not "file.")
      extension_start = last_dot + 1;
    }
  }
}

// Helper to create empty PathComponentsView (used for error cases)
static PathStorage::PathComponentsView MakeEmptyPathComponentsView() {
  return {};
}

PathStorage::PathComponentsView PathStorage::GetPathComponents(uint64_t id) const {
  const size_t idx = GetIndexForId(id);
  if (idx == SIZE_MAX) {
    // Not found - return empty views
    return MakeEmptyPathComponentsView();
  }
  return GetPathComponentsByIndex(idx);
}

PathStorage::PathComponentsView PathStorage::GetPathComponentsByIndex(size_t index) const {
  if (index >= path_offsets_.size() || is_deleted_[index] != 0) {
    // Invalid index or deleted - return empty views
    return MakeEmptyPathComponentsView();
  }

  assert(index < path_offsets_.size());
  assert(is_deleted_[index] == 0);
  PathComponentsView result{};  // Initialize all members with defaults

  const char *path = &path_storage_[path_offsets_[index]];
  const size_t filename_offset = filename_start_[index];
  const size_t extension_offset = extension_start_[index];

  result.full_path = std::string_view(path);
  result.filename = std::string_view(path + filename_offset);
  result.directory_path =
      std::string_view(path, (filename_offset > 0) ? (filename_offset - 1) : 0);

  if (extension_offset != SIZE_MAX) {
    const char *ext_start = path + extension_offset;
    const size_t ext_len = strlen(ext_start);  // NOSONAR(cpp:S1081) - Safe: path_storage_ is guaranteed null-terminated (see AppendString)
    result.extension = std::string_view(ext_start, ext_len);
    result.has_extension = true;
  } else {
    result.extension = {};
    result.has_extension = false;
  }

  return result;
}

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

