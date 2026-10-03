#include "path/PathBuilder.h"
#include "index/NtfsFileReference.h"
#include "utils/Logger.h"
#include "utils/StringUtils.h"
#include <array>
#include <cassert>
#include <cctype>
#include <sstream>

// Collect path components in reverse order (leaf to root).
// Names are read from name_cache as string_view into the arena (stable for arena lifetime).
int PathBuilder::CollectPathComponents(uint64_t parent_id,
                                       std::string_view name,
                                       const FileIndexStorage& storage,
                                       const NameArena& name_cache,
                                       std::array<std::string_view, kMaxPathDepth>& components) {
  int component_count = 0;

  // Leaf name is already a string_view into the arena — stable for the arena's lifetime.
  components.at(static_cast<size_t>(component_count)) = name;
  ++component_count;
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
      assert(component_count >= 1 && "Path must have at least the leaf name component");
      return component_count;
    }
    const std::string_view parent_name = name_cache.Find(resolved_id);
    if (parent_name.empty()) {
      // Name not in cache (entry inserted after ReleaseNameCache — shouldn't happen).
      // Empty names are never valid, so empty == not found.
      // Return early instead of using a second break to satisfy Sonar's limit on
      // nested break statements (S924) while preserving existing behaviour.
      assert(component_count >= 1 && "Path must have at least the leaf name component");
      return component_count;
    }
    components.at(static_cast<size_t>(component_count)) = parent_name;
    ++component_count;

    // Check for root directory (parent_id == current_id)
    if (resolved_id == entry->parentID.raw) {
      // Postcondition: the leaf name was stored before entering the loop.
      assert(component_count >= 1 && "Path must have at least the leaf name component");
      return component_count;  // Root directory reached
    }
    current_id = entry->parentID.raw;
  }

  // Postcondition: the leaf name was always stored at the top of this function.
  assert(component_count >= 1 && "Path must have at least the leaf name component");
  return component_count;
}


// Compute the final path length (and trailing-separator flag) from components.
// Uses truncated component lengths so the result matches the stored string.
size_t PathBuilder::ComputeLengthFromComponents(
    const std::array<std::string_view, kMaxPathDepth>& components, int component_count,
    bool* out_ends_with_separator) {
#ifdef _WIN32
  size_t volume_root_len = path_utils::GetDefaultVolumeRootPathView().length();
  int effective_component_count = component_count;
  if (component_count > 0) {
    const std::string_view top_name = components.at(static_cast<size_t>(component_count - 1));
    if (top_name.size() == 2 && top_name[1] == ':' &&
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

  if (out_ends_with_separator != nullptr) {
    if (effective_component_count == 0) {
      *out_ends_with_separator = true;
    } else {
      const std::string_view leaf =
          TruncateAtEmbeddedNull(components.at(0U));
      if (leaf.empty()) {
        *out_ends_with_separator = true;
      } else {
        const char last = leaf.back();
        *out_ends_with_separator = (last == '/' || last == '\\');
      }
    }
  }
  return total_len;
}

// Build path string from collected components
std::string PathBuilder::BuildPathFromComponents(
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
    const std::string_view top_name = components.at(static_cast<size_t>(component_count - 1));
    if (top_name.size() == 2 && top_name[1] == ':' &&
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

  const size_t total_len =
      ComputeLengthFromComponents(components, component_count, nullptr);

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

// Build full path with depth limit checking and logging.
// Called exclusively by PathRecomputer::RecomputeAllPaths.
std::string PathBuilder::BuildFullPathWithLogging(uint64_t file_id,
                                                  uint64_t parent_id,
                                                  std::string_view name,
                                                  const FileIndexStorage& storage,
                                                  const NameArena& name_cache,
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

// Compute the full path length without allocating the path string.
// Shares component collection and length math with the building path above.
size_t PathBuilder::ComputeFullPathLength(uint64_t parent_id, std::string_view name,
                                          const FileIndexStorage& storage,
                                          const NameArena& name_cache,
                                          bool* out_ends_with_separator) {
  std::array<std::string_view, kMaxPathDepth> components{};  // NOLINT(cppcoreguidelines-pro-type-member-init) - value-initialized
  const int component_count =
      CollectPathComponents(parent_id, name, storage, name_cache, components);
  return ComputeLengthFromComponents(components, component_count, out_ends_with_separator);
}
