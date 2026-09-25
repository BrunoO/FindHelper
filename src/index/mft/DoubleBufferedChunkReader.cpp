#include "index/mft/DoubleBufferedChunkReader.h"

#include <algorithm>
#include <chrono>

namespace mft {

namespace {

constexpr double kMicrosecondsPerSecond = 1e6;
constexpr double kBytesPerMegabyte = 1024.0 * 1024.0;
constexpr uint64_t kBytesPerMftRecord = 1024ULL;

}  // namespace

DoubleBufferedChunkReader::DoubleBufferedChunkReader(
    std::unique_ptr<IRawVolumeReader> volume_reader)
    : owned_volume_reader_(std::move(volume_reader)),
      volume_reader_(owned_volume_reader_.get()) {}

DoubleBufferedChunkReader::DoubleBufferedChunkReader(
    IRawVolumeReader* volume_reader)
    : volume_reader_(volume_reader) {}

#ifdef _WIN32
DoubleBufferedChunkReader::DoubleBufferedChunkReader(HANDLE volume)
    : owned_volume_reader_(std::make_unique<Win32VolumeReader>(volume)),
      volume_reader_(owned_volume_reader_.get()) {}
#endif  // _WIN32

DoubleBufferedChunkReader::~DoubleBufferedChunkReader() {
  StopStreaming();
}

bool DoubleBufferedChunkReader::StartStreaming(
    const std::vector<mft_seams::MftDiskExtent>& extents, size_t chunk_size) {
  if (volume_reader_ == nullptr || extents.empty() || chunk_size == 0) {
    return false;
  }

  if (is_streaming_) {
    StopStreaming();
  }

  const std::unique_lock lock(mutex_);
  extents_ = extents;
  chunk_size_ = chunk_size;

  slots_.at(0).buffer.Allocate(chunk_size_);
  slots_.at(1).buffer.Allocate(chunk_size_);
  if (!slots_.at(0).buffer.IsAllocated() || !slots_.at(1).buffer.IsAllocated()) {
    return false;
  }

  for (auto& slot : slots_) {
    slot.state = SlotState::Empty;
    slot.valid_bytes = 0;
    slot.stream_offset = 0;
    slot.first_record_number = 0;
    slot.is_final = false;
  }

  write_slot_idx_ = 0;
  read_slot_idx_ = 0;
  consumer_active_slot_ = -1;

  current_extent_idx_ = 0;
  current_extent_offset_ = 0;
  logical_stream_offset_ = 0;

  total_bytes_read_ = 0;
  total_chunks_read_ = 0;
  start_time_ = std::chrono::steady_clock::now();

  stop_requested_.store(false);
  eof_reached_ = false;
  is_streaming_ = true;

  reader_thread_ = std::thread(&DoubleBufferedChunkReader::ReaderThreadLoop, this);
  return true;
}

bool DoubleBufferedChunkReader::GetNextChunk(mft_seams::ChunkBuffer& out_chunk) {
  std::unique_lock lock(mutex_);
  if (!is_streaming_) {
    return false;
  }

  if (consumer_active_slot_ >= 0) {
    slots_.at(static_cast<size_t>(consumer_active_slot_)).state = SlotState::Empty;
    consumer_active_slot_ = -1;
    cv_reader_.notify_all();
  }

  cv_consumer_.wait(lock, [this] {
    return stop_requested_.load() ||
           slots_.at(read_slot_idx_).state == SlotState::Ready ||
           slots_.at(read_slot_idx_).state == SlotState::Error ||
           (eof_reached_ && slots_.at(read_slot_idx_).state != SlotState::Ready);
  });

  if (stop_requested_.load() || slots_.at(read_slot_idx_).state != SlotState::Ready) {
    return false;
  }

  auto& slot = slots_.at(read_slot_idx_);
  slot.state = SlotState::Consuming;
  consumer_active_slot_ = static_cast<int>(read_slot_idx_);

  out_chunk.data = slot.buffer.Data();
  out_chunk.size = slot.valid_bytes;
  out_chunk.stream_offset = slot.stream_offset;
  out_chunk.first_record_number = slot.first_record_number;
  out_chunk.is_final = slot.is_final;

  total_bytes_read_ += slot.valid_bytes;
  ++total_chunks_read_;

  read_slot_idx_ = (read_slot_idx_ + 1) % 2;
  return true;
}

void DoubleBufferedChunkReader::StopStreaming() {
  stop_requested_.store(true);
  cv_reader_.notify_all();
  cv_consumer_.notify_all();

  if (reader_thread_.joinable()) {
    reader_thread_.join();
  }

  const std::unique_lock lock(mutex_);
  is_streaming_ = false;
  consumer_active_slot_ = -1;
}

mft_seams::ReaderStats DoubleBufferedChunkReader::GetStats() const {
  const std::unique_lock lock(mutex_);
  mft_seams::ReaderStats stats{};
  stats.total_bytes_read = total_bytes_read_;
  stats.total_chunks_read = total_chunks_read_;

  const auto now = std::chrono::steady_clock::now();
  const auto elapsed_us =
      std::chrono::duration_cast<std::chrono::microseconds>(now - start_time_).count();
  stats.elapsed_seconds = static_cast<double>(elapsed_us) / kMicrosecondsPerSecond;
  if (stats.elapsed_seconds > 0.0) {
    stats.transfer_rate_mb_s =
        (static_cast<double>(total_bytes_read_) / kBytesPerMegabyte) / stats.elapsed_seconds;
  }
  return stats;
}

void DoubleBufferedChunkReader::ReaderThreadLoop() {
  while (!stop_requested_.load()) {
    size_t slot_idx = 0;
    {
      std::unique_lock lock(mutex_);
      cv_reader_.wait(lock, [this] {
        return stop_requested_.load() || slots_.at(write_slot_idx_).state == SlotState::Empty;
      });
      if (stop_requested_.load()) {
        break;
      }
      slot_idx = write_slot_idx_;
      slots_.at(slot_idx).state = SlotState::Filling;
    }

    bool eof = false;
    bool error = false;
    FillChunkSlot(slots_.at(slot_idx), eof, error);

    {
      const std::unique_lock lock(mutex_);
      if (error) {
        slots_.at(slot_idx).state = SlotState::Error;
        cv_consumer_.notify_all();
        break;
      }

      if (slots_.at(slot_idx).valid_bytes > 0) {
        slots_.at(slot_idx).state = SlotState::Ready;
        write_slot_idx_ = (write_slot_idx_ + 1) % 2;
      } else {
        slots_.at(slot_idx).state = SlotState::Empty;
      }

      if (eof) {
        eof_reached_ = true;
      }
      cv_consumer_.notify_all();
    }

    if (eof) {
      break;
    }
  }
}

void DoubleBufferedChunkReader::FillChunkSlot(ChunkSlot& slot, bool& out_eof,
                                             bool& out_error) {
  out_eof = false;
  out_error = false;

  size_t bytes_filled = 0;
  const uint64_t chunk_start_stream_offset = logical_stream_offset_;
  char* const dest = slot.buffer.Data();

  while (bytes_filled < chunk_size_ && current_extent_idx_ < extents_.size()) {
    if (stop_requested_.load()) {
      out_error = true;
      return;
    }

    const auto& extent = extents_.at(current_extent_idx_);
    if (current_extent_offset_ >= extent.byte_length) {
      ++current_extent_idx_;
      current_extent_offset_ = 0;
      continue;
    }

    const auto remaining_in_extent =
        static_cast<size_t>(extent.byte_length - current_extent_offset_);
    const size_t bytes_needed = chunk_size_ - bytes_filled;
    const size_t to_read = (std::min)(remaining_in_extent, bytes_needed);

    const uint64_t disk_offset = extent.byte_offset + current_extent_offset_;
    size_t bytes_read = 0;
    if (!volume_reader_->Read(disk_offset, dest + bytes_filled, to_read, bytes_read)) {
      out_error = true;
      return;
    }

    if (bytes_read == 0) {
      break;
    }

    bytes_filled += bytes_read;
    current_extent_offset_ += bytes_read;
    logical_stream_offset_ += bytes_read;

    if (current_extent_offset_ >= extent.byte_length) {
      ++current_extent_idx_;
      current_extent_offset_ = 0;
    }
  }

  slot.valid_bytes = bytes_filled;
  slot.stream_offset = chunk_start_stream_offset;
  slot.first_record_number = chunk_start_stream_offset / kBytesPerMftRecord;
  slot.is_final = (current_extent_idx_ >= extents_.size());

  if (slot.is_final || bytes_filled == 0) {
    out_eof = true;
  }
}

}  // namespace mft
