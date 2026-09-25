#pragma once

#include <cstddef>
#include <vector>

#include "index/mft/seams/IChunkReader.h"

namespace mft_seams {

/**
 * @class FakeChunkReader
 * @brief Scripted fake implementation of IChunkReader for testing.
 */
class FakeChunkReader : public IChunkReader {
 public:
  FakeChunkReader() = default;

  explicit FakeChunkReader(std::vector<std::vector<char>> chunks)
      : chunks_(std::move(chunks)) {}

  void EnqueueChunk(std::vector<char> chunk) {
    chunks_.push_back(std::move(chunk));
  }

  void SetFailStart(bool fail) { fail_start_ = fail; }
  void SetFailNext(bool fail) { fail_next_ = fail; }

  [[nodiscard]] bool StartStreaming(const std::vector<MftDiskExtent>& /*extents*/,
                                    size_t /*chunk_size*/) override {
    if (fail_start_) {
      return false;
    }
    is_streaming_ = true;
    current_index_ = 0;
    stream_offset_ = 0;
    return true;
  }

  [[nodiscard]] bool GetNextChunk(ChunkBuffer& out_chunk) override {
    if (!is_streaming_ || fail_next_ || current_index_ >= chunks_.size()) {
      return false;
    }

    auto& chunk = chunks_.at(current_index_);
    out_chunk.data = chunk.data();
    out_chunk.size = chunk.size();
    out_chunk.stream_offset = stream_offset_;
    out_chunk.first_record_number = stream_offset_ / 1024U;  // 1024 B per record default
    out_chunk.is_final = (current_index_ + 1 == chunks_.size());

    stream_offset_ += chunk.size();
    stats_.total_bytes_read += chunk.size();
    ++stats_.total_chunks_read;
    ++current_index_;

    return true;
  }

  void StopStreaming() override {
    is_streaming_ = false;
  }

  [[nodiscard]] ReaderStats GetStats() const override {
    return stats_;
  }

 private:
  std::vector<std::vector<char>> chunks_;
  size_t current_index_ = 0;
  uint64_t stream_offset_ = 0;
  bool is_streaming_ = false;
  bool fail_start_ = false;
  bool fail_next_ = false;
  ReaderStats stats_{};
};

}  // namespace mft_seams
