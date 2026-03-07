#pragma once

#include <string>

// Forward declaration
class FileIndex;

/** Maximum parent directory levels when resolving a relative --index-from-file path from the executable (e.g. FindHelper.app/Contents/MacOS up to project root). */
constexpr int kIndexFromFileMaxResolutionLevels = 8;

// Populates the FileIndex from a text file containing one file path per line.
// Returns true on success, false on failure.
bool populate_index_from_file(std::string_view file_path, FileIndex& file_index);  // NOLINT(readability-identifier-naming) - snake_case for free function in index namespace
