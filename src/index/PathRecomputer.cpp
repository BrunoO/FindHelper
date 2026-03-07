#include "index/PathRecomputer.h"

#include <string>

#include "index/FileIndexStorage.h"
#include "index/LazyValue.h"
#include "path/PathBuilder.h"
#include "path/PathOperations.h"
#include "path/PathStorage.h"
#include "utils/FileSystemUtils.h"
#include "utils/FileTimeTypes.h"
#include "utils/Logger.h"

PathRecomputer::PathRecomputer(FileIndexStorage& storage,
                               PathStorage& path_storage,
                               PathOperations& path_operations)
    : storage_(storage),
      path_storage_(path_storage),
      path_operations_(path_operations) {}

void PathRecomputer::RecomputeAllPaths() {
  path_storage_.Clear();
  storage_.ClearDirectoryCache();

  const auto& name_cache = storage_.GetNameCache();
  std::string full_path;
  full_path.reserve(256);

  static const std::string kEmptyName;
  size_t onedrive_reset_count = 0;
  for (const auto& [id, entry] : storage_) {
    const auto name_it = name_cache.find(id);
    const std::string_view leaf_name = (name_it != name_cache.end()) ? std::string_view{name_it->second} : std::string_view{kEmptyName};
    full_path = PathBuilder::BuildFullPathWithLogging(
        id, entry.parentID, leaf_name, storage_, name_cache);

    path_operations_.InsertPath(id, full_path, entry.isDirectory);

    if (!entry.isDirectory && IsOneDriveFile(full_path)) {
      storage_.UpdateFileSize(id, kFileSizeNotLoaded);
      storage_.UpdateModificationTime(id, kFileTimeNotLoaded);
      onedrive_reset_count++;
    }
  }

  if (onedrive_reset_count > 0) {
    LOG_INFO_BUILD("PathRecomputer::RecomputeAllPaths: Reset "
                   << onedrive_reset_count
                   << " OneDrive files to sentinel values for lazy loading");
  }

  // Release name cache now that PathStorage is fully built.
  // From this point forward, names are derived from PathStorage paths.
  storage_.ReleaseNameCache();
}
