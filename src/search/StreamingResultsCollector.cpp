#include "search/StreamingResultsCollector.h"

#include <cassert>

// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init,hicpp-member-init) - mutex_, containers, atomics initialized in body or default
StreamingResultsCollector::StreamingResultsCollector(size_t batch_size, uint32_t notification_interval_ms)
    : batch_size_(batch_size),
      notification_interval_(notification_interval_ms) {
  last_flush_time_ = std::chrono::steady_clock::now();
  all_results_.reserve(batch_size * 2);
  current_batch_.reserve(batch_size);
}

void StreamingResultsCollector::AddResult(SearchResultData&& result) {
  assert(!search_complete_.load(std::memory_order_acquire));

  const std::scoped_lock lock(mutex_);

  all_results_.push_back(SearchResultData{result.id, result.fullPath, result.isDirectory});
  current_batch_.push_back(std::move(result));  // NOLINT(hicpp-move-const-arg,performance-move-const-arg) - S5500 requires move on rvalue; type is trivially copyable so no-op

  auto now = std::chrono::steady_clock::now();
  if (current_batch_.size() >= batch_size_ ||
      (now - last_flush_time_) >= notification_interval_) {
    FlushCurrentBatch();
  }
}

void StreamingResultsCollector::AddResult(const SearchResultData& result) {
  assert(!search_complete_.load(std::memory_order_acquire));

  const std::scoped_lock lock(mutex_);

  all_results_.push_back(SearchResultData{result.id, result.fullPath, result.isDirectory});
  current_batch_.push_back(result);

  auto now = std::chrono::steady_clock::now();
  if (current_batch_.size() >= batch_size_ ||
      (now - last_flush_time_) >= notification_interval_) {
    FlushCurrentBatch();
  }
}

void StreamingResultsCollector::AddResults(const std::vector<SearchResultData>& batch) {
  if (batch.empty()) { return; }
  assert(!search_complete_.load(std::memory_order_acquire));

  const std::scoped_lock lock(mutex_);
  all_results_.insert(all_results_.end(), batch.begin(), batch.end());
  current_batch_.insert(current_batch_.end(), batch.begin(), batch.end());

  const auto now = std::chrono::steady_clock::now();
  if (current_batch_.size() >= batch_size_ ||
      (now - last_flush_time_) >= notification_interval_) {
    FlushCurrentBatch();
  }
}

void StreamingResultsCollector::MarkSearchComplete() {
  const std::scoped_lock lock(mutex_);
  if (!current_batch_.empty()) {
    FlushCurrentBatch();
  }
  search_complete_.store(true, std::memory_order_release);
  assert(search_complete_.load(std::memory_order_acquire) && "postcondition");
}

void StreamingResultsCollector::SetError(std::string_view error_message) {
  const std::scoped_lock lock(mutex_);
  error_message_ = error_message;
  has_error_.store(true, std::memory_order_release);
  search_complete_.store(true, std::memory_order_release);
  assert(has_error_.load(std::memory_order_acquire) &&
         search_complete_.load(std::memory_order_acquire) && "SetError postcondition");
}

bool StreamingResultsCollector::HasNewBatch() const {
  return has_new_batch_.load(std::memory_order_acquire);
}

std::vector<SearchResultData> StreamingResultsCollector::GetAllPendingBatches() {
  const std::scoped_lock lock(mutex_);
  std::vector<SearchResultData> result = std::move(pending_batches_);
  pending_batches_.clear();
  has_new_batch_.store(false, std::memory_order_release);
  // Invariant: after drain, pending is empty and has_new_batch_ is false
  assert(pending_batches_.empty() && "pending must be clear after GetAllPendingBatches");
  return result;
}

std::vector<SearchResultData> StreamingResultsCollector::GetPendingBatchesUpTo(size_t max_results) {
  const std::scoped_lock lock(mutex_);
  if (pending_batches_.size() <= max_results) {
    std::vector<SearchResultData> result = std::move(pending_batches_);
    pending_batches_.clear();
    has_new_batch_.store(false, std::memory_order_release);
    return result;
  }
  std::vector<SearchResultData> result;
  result.reserve(max_results);
  result.insert(result.end(),
               std::make_move_iterator(pending_batches_.begin()),
               std::make_move_iterator(pending_batches_.begin() + static_cast<std::ptrdiff_t>(max_results)));
  pending_batches_.erase(pending_batches_.begin(),
                         pending_batches_.begin() + static_cast<std::ptrdiff_t>(max_results));
  has_new_batch_.store(true, std::memory_order_release);
  return result;
}

const std::vector<SearchResultData>& StreamingResultsCollector::GetAllResults() const {
  const std::scoped_lock lock(mutex_);
  return all_results_;
}

bool StreamingResultsCollector::IsSearchComplete() const {
  return search_complete_.load(std::memory_order_acquire);
}

bool StreamingResultsCollector::HasError() const {
  return has_error_.load(std::memory_order_acquire);
}

std::string_view StreamingResultsCollector::GetError() const {
  const std::scoped_lock lock(mutex_);
  return error_message_;
}

void StreamingResultsCollector::FlushCurrentBatch() {
  // Assumes mutex_ is already held
  if (current_batch_.empty()) {
    return;
  }

  // NOLINTNEXTLINE(bugprone-branch-clone) - then: move assign; else: insert range (different operations)
  if (pending_batches_.empty()) {
    pending_batches_ = std::move(current_batch_);
  } else {
    pending_batches_.insert(pending_batches_.end(),
                           std::make_move_iterator(current_batch_.begin()),
                           std::make_move_iterator(current_batch_.end()));
  }

  current_batch_.clear();
  current_batch_.reserve(batch_size_);
  last_flush_time_ = std::chrono::steady_clock::now();
  has_new_batch_.store(true, std::memory_order_release);
  assert(!pending_batches_.empty());
}
