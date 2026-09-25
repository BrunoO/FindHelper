#include "index/IndexFromFilePopulator.h"

#include <fstream>
#include <string>
#include <vector>

#include "index/FileIndex.h"
#include "path/PathUtils.h"
#include "utils/Logger.h"

namespace {

// Tries to open file_path relative to executable directory and its parents.
// Returns true if opened (path_to_open set to resolved path), false otherwise.
bool TryResolveRelativeFromExecutable(std::string_view file_path,
                                      std::ifstream& input_file,
                                      std::string& path_to_open) {
  std::string base = path_utils::GetExecutableDirectory();
  if (!base.empty() && base.back() == path_utils::kPathSeparator) {
    base.pop_back();
  }
  int level = 0;
  for (; level < kIndexFromFileMaxResolutionLevels && !base.empty(); ++level) {  // NOSONAR(cpp:S886) - level must stay in scope for post-loop max-levels check
    const std::string candidate = path_utils::JoinPath(base, file_path);
    input_file.clear();
    input_file.open(candidate);
    if (input_file.is_open()) {
      path_to_open = candidate;
      LOG_INFO_BUILD("Resolved index file path (relative to executable): " << path_to_open);
      return true;
    }
    base = path_utils::GetParentDirectory(base);
  }
  if (level >= kIndexFromFileMaxResolutionLevels) {
    LOG_ERROR_BUILD("Relative path resolution hit max levels (" << kIndexFromFileMaxResolutionLevels
                    << "); file not found. Use an absolute path or place file within "
                    << kIndexFromFileMaxResolutionLevels << " levels above the executable.");
  }
  return false;
}

}  // namespace

// Populates the FileIndex from a text file containing one file path per line.
// Returns true on success, false on failure.
// Relative paths are tried from CWD first; if that fails, from the executable
// directory and its parents (so --index-from-file=tests/data/... works when
// launching the .app from Finder or from a different working directory).
bool populate_index_from_file(std::string_view file_path, FileIndex& file_index) {  // NOLINT(readability-identifier-naming) - snake_case for free function
  LOG_INFO_BUILD("Populating index from file: " << file_path);

  std::string path_to_open(file_path);
  std::ifstream input_file(path_to_open);
  if (!input_file.is_open() && !path_utils::IsPathAbsolute(file_path)) {
    (void)TryResolveRelativeFromExecutable(file_path, input_file, path_to_open);
  }
  if (!input_file.is_open()) {
    LOG_ERROR_BUILD("Failed to open file for reading: " << file_path);
    return false;
  }

  std::string line;
  size_t line_count = 0;
  while (std::getline(input_file, line)) {
    if (!line.empty()) {
      file_index.InsertPath(line);
      line_count++;
    }
  }

  if (input_file.bad()) {
    LOG_ERROR_BUILD("Error reading from file: " << path_to_open);
    return false;
  }

  LOG_INFO_BUILD("Successfully populated index with " << line_count << " paths from: " << path_to_open);
  return true;
}
