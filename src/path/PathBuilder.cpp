#include "path/PathBuilder.h"
#include "utils/Logger.h"
#include <array>
#include <cctype>
#include <sstream>

// Collect path components in reverse order (leaf to root).
// Names are read from name_cache; pointers into the map are stable (node-based).
int PathBuilder::CollectPathComponents(uint64_t parent_id,
                                       std::string_view name,
                                       const FileIndexStorage& storage,
                                       const hash_map_t<uint64_t, std::string>& name_cache,
                                       std::array<const std::string*, kMaxPathDepth>& components) {
  int component_count = 0;

  // Store the leaf name in a thread-local to give it a stable address.
  static thread_local std::string name_storage;
  name_storage.assign(name);
  components.at(static_cast<size_t>(component_count)) = &name_storage;
  ++component_count;
  uint64_t current_id = parent_id;

  // Walk up the parent chain, reading each parent's name from name_cache.
  while (component_count < kMaxPathDepth) {
    const FileEntry* entry = storage.GetEntry(current_id);
    if (entry == nullptr) {
      break;
    }
    const auto it = name_cache.find(current_id);
    if (it == name_cache.end()) {
      break;  // Name not in cache (entry inserted after ReleaseNameCache — shouldn't happen) NOSONAR(cpp:S924) - second break avoids name_cache.find when entry is null
    }
    components.at(static_cast<size_t>(component_count)) = &it->second;
    ++component_count;

    // Check for root directory (parent_id == current_id)
    if (current_id == entry->parentID) {
      return component_count;  // Root directory reached
    }
    current_id = entry->parentID;
  }

  return component_count;
}

// Build path string from collected components
std::string PathBuilder::BuildPathFromComponents(
    const std::array<const std::string*, kMaxPathDepth>& components,
    int component_count) {
#ifdef _WIN32
  // Calculate total size for single allocation
  std::string volume_root = path_utils::GetDefaultVolumeRootPath();
  int effective_component_count = component_count;

  // On Windows, treat a top-level "X:" component as the drive root instead of
  // prefixing with the default volume root. This prevents paths like "C:\\F:\\..."
  // when crawling subst or secondary drives and when directory roots are named "X:".
  if (component_count > 0) {
    const std::string* top_component =
      components.at(static_cast<size_t>(component_count - 1));
    const std::string& top_name = *top_component;
    if (top_name.size() == 2 && top_name[1] == ':' &&
        std::isalpha(static_cast<unsigned char>(top_name[0])) != 0) {
      volume_root.clear();
      volume_root.push_back(top_name[0]);
      volume_root.push_back(':');
      volume_root.push_back(path_utils::kPathSeparator);
      --effective_component_count;
    }
  }
#else   // _WIN32
  // Non-Windows: volume root is always the default; component count is unchanged.
  const std::string volume_root = path_utils::GetDefaultVolumeRootPath();
  const int effective_component_count = component_count;
#endif  // _WIN32

  size_t total_len = volume_root.length();
  if (effective_component_count > 0) {
    for (int i = 0; i < effective_component_count; ++i) {
      total_len += components.at(static_cast<size_t>(i))->length() + 1;  // component + separator
    }
    total_len -= 1; // No trailing separator at the end
  }

  std::string full_path;
  full_path.reserve(total_len);

  // Append volume root
  full_path.append(volume_root);

  // Append components in correct order (root to leaf)
  for (int i = effective_component_count; i > 0; --i) {
    if (i < effective_component_count) {
      full_path.push_back(path_utils::kPathSeparator);
    }
    full_path.append(*components.at(static_cast<size_t>(i - 1)));
  }

  return full_path;
}

// Build full path with depth limit checking and logging.
// Called exclusively by PathRecomputer::RecomputeAllPaths.
std::string PathBuilder::BuildFullPathWithLogging(uint64_t file_id,
                                                  uint64_t parent_id,
                                                  std::string_view name,
                                                  const FileIndexStorage& storage,
                                                  const hash_map_t<uint64_t, std::string>& name_cache) {
  std::array<const std::string*, kMaxPathDepth> components{};  // NOLINT(cppcoreguidelines-pro-type-member-init) - zero-initialized by {}

  const int component_count = CollectPathComponents(  // NOLINT(cppcoreguidelines-init-variables) - Initialized from CollectPathComponents()
      parent_id, name, storage, name_cache, components);

  if (component_count >= kMaxPathDepth) {
    std::ostringstream oss;
    oss << "Path depth limit (" << kMaxPathDepth << ") reached for file ID: 0x"
        << std::hex << file_id << std::dec << ", name: " << name
        << " (path may be incomplete)";
    LOG_WARNING(oss.str());
  }

  return BuildPathFromComponents(components, component_count);
}

