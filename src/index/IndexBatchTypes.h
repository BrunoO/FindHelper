#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "index/FileName.h"
#include "index/NtfsFileReference.h"
#include "utils/FileAttributeConstants.h"
#include "utils/FileTimeTypes.h"

// Batch entry types for bulk index population, extracted from FileIndex.
// FileIndex re-exports these as nested aliases (FileIndex::PopulationBatchEntry,
// FileIndex::CrawlBatchEntry) so existing qualified names keep working; headers
// that only name the entries or hold FileIndex references (FolderCrawler,
// IndexBuildRun, MFT seams) include this light header instead of the full
// FileIndex facade (47 transitive dependents).
namespace index_batch {

// Owned batch entry for bulk population (e.g. baseline journal enumeration).
// snake_case fields per project convention; name is owned because the
// thread-local UTF-8 view it comes from is reused per record. The name is
// a FileName (bare-name intent, explicit construction) rather than a raw
// string, so batch producers spell the conversion and full paths stand
// out at the call site (V5).
struct PopulationBatchEntry {
  ntfs_file_reference::NtfsFileReference id{0};
  ntfs_file_reference::NtfsFileReference parent_id{0};
  file_name::FileName name;
  bool is_directory = false;
  FILETIME modification_time = kFileTimeNotLoaded;
  uint64_t file_size = kFileSizeNotLoaded;
  uint32_t file_attributes = 0;
};

// Insert a file entry from a full path.
// Each element carries the path, directory flag, raw NTFS attribute
// bitmask from enumeration (FindFirstFile; 0 where unavailable), and
// optional precomputed FNV-1a path hash (0 = compute inside InsertPaths).
struct CrawlBatchEntry {
  std::string path;
  bool is_directory = false;
  uint32_t file_attributes = 0;
  size_t path_hash = 0;
};

}  // namespace index_batch
