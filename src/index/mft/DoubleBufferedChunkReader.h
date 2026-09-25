#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "index/mft/AlignedBuffer.h"
#include "index/mft/RawVolumeReader.h"
#include "index/mft/seams/IChunkReader.h"
#include "index/mft/seams/MftTypes.h"

#ifdef _WIN32
#include <windows.h>
#endif  // _WIN32

namespace mft {

/**
 * @class DoubleBufferedChunkReader
 * @brief High-throughput unbuffered chunk reader implementing seam 2.
 *
 * Employs double-buffering (two sector-aligned buffers) and an asynchronous background
 * worker thread to prefetch raw MFT chunks from disk concurrently with CPU parsing.
 */
class DoubleBufferedChunkReader : public mft_seams::IChunkReader {
 public:
  explicit DoubleBufferedChunkReader(std::unique_ptr<IRawVolumeReader> volume_reader);
  explicit DoubleBufferedChunkReader(IRawVolumeReader* volume_reader);

#ifdef _WIN32
  explicit DoubleBufferedChunkReader(HANDLE volume);
#endif  // _WIN32

  ~DoubleBufferedChunkReader() override;

  DoubleBufferedChunkReader(const DoubleBufferedChunkReader&) = delete;
  DoubleBufferedChunkReader& operator=(const DoubleBufferedChunkReader&) = delete;

  [[nodiscard]] bool StartStreaming(const std::vector<mft_seams::MftDiskExtent>& extents,
                                    size_t chunk_size) override;

  [[nodiscard]] bool GetNextChunk(mft_seams::ChunkBuffer& out_chunk) override;

  void StopStreaming() override;

  [[nodiscard]] mft_seams::ReaderStats GetStats() const override;

 private:
  enum class SlotState : uint8_t {
    Empty,
    Filling,
    Ready,
    Consuming,
    Error,
  };

  struct ChunkSlot {
    AlignedBuffer buffer;
    size_t valid_bytes = 0;
    uint64_t stream_offset = 0;
    uint64_t first_record_number = 0;
    bool is_final = false;
    SlotState state = SlotState::Empty;
  };

  void ReaderThreadLoop();
  void FillChunkSlot(ChunkSlot& slot, bool& out_eof, bool& out_error);

  std::unique_ptr<IRawVolumeReader> owned_volume_reader_;
  IRawVolumeReader* volume_reader_ = nullptr;

  std::vector<mft_seams::MftDiskExtent> extents_;
  size_t chunk_size_ = mft_seams::kDefaultChunkSizeBytes;

  mutable std::mutex mutex_;
  std::condition_variable cv_reader_;
  std::condition_variable cv_consumer_;

  std::thread reader_thread_;
  std::atomic<bool> stop_requested_{false};
  bool is_streaming_ = false;
  bool eof_reached_ = false;

  std::array<ChunkSlot, 2> slots_{};
  size_t write_slot_idx_ = 0;
  size_t read_slot_idx_ = 0;
  int consumer_active_slot_ = -1;

  // Streaming cursor (accessed only by background reader thread)
  size_t current_extent_idx_ = 0;
  uint64_t current_extent_offset_ = 0;
  uint64_t logical_stream_offset_ = 0;

  // Telemetry (protected under mutex_)
  uint64_t total_bytes_read_ = 0;
  uint64_t total_chunks_read_ = 0;
  std::chrono::steady_clock::time_point start_time_;
};

}  // namespace mft
