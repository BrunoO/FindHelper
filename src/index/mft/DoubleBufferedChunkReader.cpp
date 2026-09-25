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
  StopStreamingImpl();
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

  cursor_.extent_idx = 0;
  cursor_.extent_offset = 0;
  cursor_.stream_offset = 0;

  telemetry_.bytes_read = 0;
  telemetry_.chunks_read = 0;
  telemetry_.start_time = std::chrono::steady_clock::now();

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

  telemetry_.bytes_read += slot.valid_bytes;
  ++telemetry_.chunks_read;

  read_slot_idx_ = (read_slot_idx_ + 1) % 2;
  return true;
}

void DoubleBufferedChunkReader::StopStreaming() {
  StopStreamingImpl();
}

void DoubleBufferedChunkReader::StopStreamingImpl() {
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
  stats.total_bytes_read = telemetry_.bytes_read;
  stats.total_chunks_read = telemetry_.chunks_read;

  const auto now = std::chrono::steady_clock::now();
  const auto elapsed_us =
      std::chrono::duration_cast<std::chrono::microseconds>(now - telemetry_.start_time).count();
  stats.elapsed_seconds = static_cast<double>(elapsed_us) / kMicrosecondsPerSecond;
  if (stats.elapsed_seconds > 0.0) {
    stats.transfer_rate_mb_s =
        (static_cast<double>(telemetry_.bytes_read) / kBytesPerMegabyte) / stats.elapsed_seconds;
  }
  return stats;
}

void DoubleBufferedChunkReader::ReaderThreadLoop() {
  while (!stop_requested_.load()) {
    if (!FillNextReadySlot()) {
      break;
    }
  }
}

bool DoubleBufferedChunkReader::FillNextReadySlot() {
  size_t slot_idx = 0;
  {
    std::unique_lock lock(mutex_);
    cv_reader_.wait(lock, [this] {
      return stop_requested_.load() || slots_.at(write_slot_idx_).state == SlotState::Empty;
    });
    if (stop_requested_.load()) {
      return false;
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
      return false;
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

  return !eof;
}

void DoubleBufferedChunkReader::FillChunkSlot(ChunkSlot& slot, bool& out_eof,
                                             bool& out_error) {
  out_eof = false;
  out_error = false;

  size_t bytes_filled = 0;
  const uint64_t chunk_start_stream_offset = cursor_.stream_offset;
  char* const dest = slot.buffer.Data();

  while (bytes_filled < chunk_size_ && cursor_.extent_idx < extents_.size()) {
    if (stop_requested_.load()) {
      out_error = true;
      return;
    }

    const auto& extent = extents_.at(cursor_.extent_idx);
    if (cursor_.extent_offset >= extent.byte_length) {
      ++cursor_.extent_idx;
      cursor_.extent_offset = 0;
      continue;
    }

    const auto remaining_in_extent =
        static_cast<size_t>(extent.byte_length - cursor_.extent_offset);
    const size_t bytes_needed = chunk_size_ - bytes_filled;
    const size_t to_read = (std::min)(remaining_in_extent, bytes_needed);

    const uint64_t disk_offset = extent.byte_offset + cursor_.extent_offset;
    size_t bytes_read = 0;
    if (!volume_reader_->Read(disk_offset, dest + bytes_filled, to_read, bytes_read)) {
      out_error = true;
      return;
    }

    if (bytes_read == 0) {
      break;
    }

    bytes_filled += bytes_read;
    cursor_.extent_offset += bytes_read;
    cursor_.stream_offset += bytes_read;

    if (cursor_.extent_offset >= extent.byte_length) {
      ++cursor_.extent_idx;
      cursor_.extent_offset = 0;
    }
  }

  slot.valid_bytes = bytes_filled;
  slot.stream_offset = chunk_start_stream_offset;
  slot.first_record_number = chunk_start_stream_offset / kBytesPerMftRecord;
  slot.is_final = (cursor_.extent_idx >= extents_.size());

  if (slot.is_final || bytes_filled == 0) {
    out_eof = true;
  }
}

}  // namespace mft
