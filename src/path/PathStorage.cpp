#include "path/PathStorage.h"

#include <cassert>
#include <cstring>

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
// All SoA array accesses use operator[] intentionally for performance.
// Indices originate from existing_index (caller), loop counters bounded by
// path_ids_.size(), or RebuildPathBuffer. Using .at() would add redundant
// checks in the hot search-iteration and insert paths.

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

size_t PathStorage::InsertPath(uint64_t id, const std::string& path,
                               bool isDirectory,  // NOLINT(readability-identifier-naming) - Public API parameter name
                               std::optional<size_t> existing_index) {
  // Precondition: id 0 is reserved; all real file/dir IDs must be non-zero.
  assert(id != 0 && "File ID 0 is reserved; callers must provide a non-zero ID");

  // Parse once; used in both the update and new-entry paths.
  size_t filename_start_offset = 0;
  size_t extension_start_offset = SIZE_MAX;
  ParsePathOffsets(path, filename_start_offset, extension_start_offset);

  // If caller provided an existing index (e.g. rename/UpdatePrefix), try in-place update.
  if (existing_index.has_value()) {
    const size_t idx = *existing_index;
    if (idx >= path_offsets_.size() || path_ids_[idx] != id) {
      goto append_new_entry;  // NOLINT(cppcoreguidelines-avoid-goto,hicpp-avoid-goto) - S134: single exit reduces nesting
    }
    const size_t old_offset = path_offsets_[idx];
    const size_t new_len = path.length();
    if (const size_t old_len = std::strlen(&path_storage_[old_offset]);  // NOSONAR(cpp:S1081) - Safe: path_storage_ entries are null-terminated (see AppendString)
        new_len > old_len) {
      if (is_deleted_[idx] == 0) {
        is_deleted_[idx] = 1;
        deleted_count_.fetch_add(1, std::memory_order_relaxed);
      }
      goto append_new_entry;  // NOLINT(cppcoreguidelines-avoid-goto,hicpp-avoid-goto) - S134: single exit reduces nesting
    }
    std::memcpy(&path_storage_[old_offset], path.c_str(), new_len + 1);
    filename_start_[idx] = filename_start_offset;
    extension_start_[idx] = extension_start_offset;
    is_directory_[idx] = isDirectory ? 1 : 0;
    if (is_deleted_[idx] != 0) {
      is_deleted_[idx] = 0;
      deleted_count_.fetch_sub(1, std::memory_order_relaxed);
    }
    AssertSoAInvariant("SoA arrays must remain synchronized after InsertPath");
    return idx;
  }

append_new_entry:
  // New entry: append a new slot to all SoA arrays.
  const size_t offset = AppendString(path);
  const size_t idx = path_offsets_.size();

  path_offsets_.push_back(offset);
  path_ids_.push_back(id);
  filename_start_.push_back(filename_start_offset);
  extension_start_.push_back(extension_start_offset);
  is_deleted_.push_back(0);                     // 0 = not deleted
  is_directory_.push_back(isDirectory ? 1 : 0);  // 0 = file, 1 = directory

  AssertSoAInvariant("SoA arrays must remain synchronized after InsertPath");
  return idx;
}

bool PathStorage::RemovePathByIndex(size_t index) {
  if (index >= path_offsets_.size()) {
    return false;
  }
  if (is_deleted_[index] != 0) {
    return false;
  }
  is_deleted_[index] = 1;
  deleted_count_.fetch_add(1, std::memory_order_relaxed);
  return true;
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

std::string_view PathStorage::GetPathByIndex(size_t index) const {
  if (index >= path_offsets_.size() || is_deleted_[index] != 0) {
    return {};
  }

  assert(index < path_offsets_.size());
  assert(is_deleted_[index] == 0);
  const char *path_start = &path_storage_[path_offsets_[index]];
  return {path_start};
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

