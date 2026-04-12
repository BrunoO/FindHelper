#pragma once

#include <cstdint>

/**
 * @file FileAttributeConstants.h
 * @brief Sentinel values for file attributes (size, etc.)
 */

// Sentinel values for file size
constexpr uint64_t kFileSizeNotLoaded = UINT64_MAX;
constexpr uint64_t kFileSizeFailed = UINT64_MAX - 1;

// Sentinel value for folder file count (used by SearchResult::folderFileCount)
constexpr uint64_t kFolderFileCountNotLoaded = UINT64_MAX - 2;
