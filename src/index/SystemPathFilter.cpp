#include "index/SystemPathFilter.h"

namespace system_path_filter {

bool IsSystemPrefixedName(std::string_view filename) {
  return !filename.empty() && filename.front() == kSystemFilePrefix;
}

void FilteredDirTracker::Clear() { filtered_dir_ref_nums_.clear(); }

void FilteredDirTracker::Reserve(std::size_t count) {
  filtered_dir_ref_nums_.reserve(count);
}

bool FilteredDirTracker::IsFilteredChild(FilteredDirectory parent) const {
  return filtered_dir_ref_nums_.count(parent.record_number) != 0;
}

void FilteredDirTracker::MarkFilteredDir(FilteredDirectory dir) {
  filtered_dir_ref_nums_.insert(dir.record_number);
}

void FilteredDirTracker::EraseOnDelete(FilteredDirectory dir) {
  filtered_dir_ref_nums_.erase(dir.record_number);
}

std::size_t FilteredDirTracker::Size() const {
  return filtered_dir_ref_nums_.size();
}

}  // namespace system_path_filter
