#include "index/recycle/RecycleBinEnricher.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "index/recycle/RecycleBinMetadataStore.h"
#include "path/PathUtils.h"
#include "utils/Logger.h"

#ifdef _WIN32
#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem

#include "utils/ScopedHandle.h"
#include "utils/StringUtils.h"
#endif  // _WIN32

namespace recycle_bin {

namespace {

// Record versions and layout constants for $I metadata files.
constexpr std::uint64_t kVersion1 = 1;
constexpr std::uint64_t kVersion2 = 2;
constexpr std::size_t kVersionFieldSize = 8;
constexpr std::size_t kSizeFieldOffset = 8;
constexpr std::size_t kFileTimeFieldOffset = 16;
constexpr std::size_t kV1PathOffset = 24;
constexpr std::size_t kV1PathUnits = 260;
constexpr std::size_t kV2PathLengthOffset = 24;
constexpr std::size_t kV2PathLengthFieldSize = 4;
constexpr std::size_t kV2HeaderSize = 28;
constexpr std::size_t kV2MaxPathUnits = 32767;
constexpr std::size_t kUnitBytes = 2;  // UTF-16 code unit size
constexpr std::size_t kU64Bytes = 8;
constexpr std::size_t kBitsPerByte = 8;

// Ranges and masks for UTF-16 -> UTF-8 transcoding (RFC 3629 / RFC 2781).
constexpr std::uint32_t kHighSurrogateFirst = 0xD800U;
constexpr std::uint32_t kHighSurrogateLast = 0xDBFFU;
constexpr std::uint32_t kLowSurrogateFirst = 0xDC00U;
constexpr std::uint32_t kLowSurrogateLast = 0xDFFFU;
constexpr std::uint32_t kSupplementaryBase = 0x10000U;
constexpr std::uint32_t kSurrogateDataBits = 10U;
constexpr std::uint32_t kSurrogateDataMask = 0x3FFU;
constexpr std::uint32_t kReplacementCodePoint = 0xFFFDU;

constexpr std::uint32_t kAsciiLimit = 0x80U;
constexpr std::uint32_t kTwoByteLimit = 0x800U;
constexpr std::uint32_t kThreeByteLimit = 0x10000U;
constexpr std::uint32_t kContinuationTag = 0x80U;
constexpr std::uint32_t kContinuationMask = 0x3FU;
constexpr std::uint32_t kContinuationBits = 6U;
constexpr std::uint32_t kTwoByteLeadTag = 0xC0U;
constexpr std::uint32_t kTwoByteLeadMask = 0x1FU;
constexpr std::uint32_t kThreeByteLeadTag = 0xE0U;
constexpr std::uint32_t kThreeByteLeadMask = 0x0FU;
constexpr std::uint32_t kFourByteLeadTag = 0xF0U;
constexpr std::uint32_t kFourByteLeadMask = 0x07U;
constexpr std::uint32_t kThreeByteShift = 12U;
constexpr std::uint32_t kFourByteShift = 18U;

std::uint64_t ReadLe(const std::uint8_t* bytes, std::size_t byte_count) noexcept {
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < byte_count; ++i) {
    value |= static_cast<std::uint64_t>(bytes[i]) << (i * kBitsPerByte);
  }
  return value;
}

std::uint32_t ReadUnit(const std::uint8_t* units, std::size_t unit_index) noexcept {
  return static_cast<std::uint32_t>(ReadLe(units + (unit_index * kUnitBytes), kUnitBytes));
}

bool IsHighSurrogate(std::uint32_t unit) noexcept {
  return unit >= kHighSurrogateFirst && unit <= kHighSurrogateLast;
}

bool IsLowSurrogate(std::uint32_t unit) noexcept {
  return unit >= kLowSurrogateFirst && unit <= kLowSurrogateLast;
}

void AppendUtf8(std::string& out, std::uint32_t code_point) {
  if (code_point < kAsciiLimit) {
    out.push_back(static_cast<char>(code_point));
  } else if (code_point < kTwoByteLimit) {
    out.push_back(static_cast<char>(kTwoByteLeadTag | ((code_point >> kContinuationBits) & kTwoByteLeadMask)));
    out.push_back(static_cast<char>(kContinuationTag | (code_point & kContinuationMask)));
  } else if (code_point < kThreeByteLimit) {
    out.push_back(static_cast<char>(kThreeByteLeadTag | ((code_point >> kThreeByteShift) & kThreeByteLeadMask)));
    out.push_back(static_cast<char>(kContinuationTag | ((code_point >> kContinuationBits) & kContinuationMask)));
    out.push_back(static_cast<char>(kContinuationTag | (code_point & kContinuationMask)));
  } else {
    out.push_back(static_cast<char>(kFourByteLeadTag | ((code_point >> kFourByteShift) & kFourByteLeadMask)));
    out.push_back(static_cast<char>(kContinuationTag | ((code_point >> kThreeByteShift) & kContinuationMask)));
    out.push_back(static_cast<char>(kContinuationTag | ((code_point >> kContinuationBits) & kContinuationMask)));
    out.push_back(static_cast<char>(kContinuationTag | (code_point & kContinuationMask)));
  }
}

std::uint32_t DecodeCodePoint(const std::uint8_t* units, std::size_t unit_count,
                              std::size_t unit_index, std::size_t& out_advance) noexcept {
  const std::uint32_t unit = ReadUnit(units, unit_index);
  out_advance = 1;
  if (IsLowSurrogate(unit)) {
    return kReplacementCodePoint;
  }
  if (!IsHighSurrogate(unit)) {
    return unit;
  }
  if (unit_index + 1 >= unit_count) {
    return kReplacementCodePoint;
  }
  const std::uint32_t next = ReadUnit(units, unit_index + 1);
  if (!IsLowSurrogate(next)) {
    return kReplacementCodePoint;
  }
  out_advance = 2;
  return kSupplementaryBase + ((unit - kHighSurrogateFirst) << kSurrogateDataBits) +
         ((next - kLowSurrogateFirst) & kSurrogateDataMask);
}

std::string TranscodeUnits(const std::uint8_t* units, std::size_t unit_count) {
  std::string out;
  out.reserve(unit_count * 3);
  std::size_t index = 0;
  while (index < unit_count) {
    if (ReadUnit(units, index) == 0) {
      break;
    }
    std::size_t advance = 1;
    const std::uint32_t code_point = DecodeCodePoint(units, unit_count, index, advance);
    AppendUtf8(out, code_point);
    index += advance;
  }
  return out;
}

bool HasNulUnit(const std::uint8_t* units, std::size_t unit_count) noexcept {
  for (std::size_t i = 0; i < unit_count; ++i) {
    if (ReadUnit(units, i) == 0) {
      return true;
    }
  }
  return false;
}

ParseResult MakeError(ParseError error) noexcept {
  return {false, error, {}};
}

ParseResult MakeSuccess(const std::uint8_t* data, std::uint64_t version,
                        const std::uint8_t* path, std::size_t unit_count) {
  return {true, ParseError::kOk,
          {version, ReadLe(data + kSizeFieldOffset, kU64Bytes),
           ReadLe(data + kFileTimeFieldOffset, kU64Bytes), TranscodeUnits(path, unit_count),},};
}

ParseResult ParseV1(const std::uint8_t* data, std::size_t size) {
  if (constexpr std::size_t kV1TotalSize = kV1PathOffset + (kV1PathUnits * kUnitBytes); size < kV1TotalSize) {
    return MakeError(ParseError::kTruncated);
  }
  const std::uint8_t* path = data + kV1PathOffset;
  if (!HasNulUnit(path, kV1PathUnits)) {
    return MakeError(ParseError::kUnterminatedPath);
  }
  return MakeSuccess(data, kVersion1, path, kV1PathUnits);
}

ParseResult ParseV2(const std::uint8_t* data, std::size_t size) {
  if (size < kV2HeaderSize) {
    return MakeError(ParseError::kTruncated);
  }
  const std::size_t remaining_units = (size - kV2HeaderSize) / kUnitBytes;
  const std::size_t max_units = remaining_units < kV2MaxPathUnits ? remaining_units : kV2MaxPathUnits;
  const auto path_length = static_cast<std::size_t>(
      ReadLe(data + kV2PathLengthOffset, kV2PathLengthFieldSize));
  if (path_length == 0 || path_length > max_units) {
    return MakeError(ParseError::kPathTooLong);
  }
  const std::uint8_t* path = data + kV2HeaderSize;
  if (ReadUnit(path, path_length - 1) != 0) {
    return MakeError(ParseError::kUnterminatedPath);
  }
  return MakeSuccess(data, kVersion2, path, path_length);
}

// Synthetic FRNs (POSIX, or when no FRN is known) set the top bit so they stay disjoint from the
// real NTFS references seen on Windows in practice (the top 16 bits there are a small sequence number).
constexpr std::uint64_t kSyntheticFrnBit = 1ULL << 63ULL;

// A v2 $I record is at most 28 + 2 * 32767 = 65562 bytes. The cap rejects anything far larger
// before we allocate for it.
constexpr std::uint64_t kMaxIndexFileBytes = 1ULL << 17ULL;

// Periodic drop logging (spec 3.5.7): one debug line per this many drops, never per drop.
constexpr std::uint64_t kDropLogInterval = 1000;

[[nodiscard]] bool IsIArtifactName(std::string_view name) {
  const std::optional<RecycleNameToken> token = TokenizeRecycleName(name);
  return token && token->prefix == RecycleNamePrefix::kI;
}

[[nodiscard]] std::uint64_t SyntheticIFrn(std::string_view path) {
  const std::uint64_t hashed = std::hash<std::string_view>{}(path);
  return kSyntheticFrnBit | (hashed & ~kSyntheticFrnBit);
}

// The handle FRN is authoritative when present (N2). A differing queued FRN means the file was
// replaced after it was queued, so the queued value is stale.
[[nodiscard]] std::uint64_t ResolveIFrn(const EnrichTask& task, std::uint64_t file_frn) {
  if (file_frn != 0) {
    if (task.i_frn != 0 && task.i_frn != file_frn) {
      LOG_DEBUG_BUILD("RecycleBinEnricher: FRN changed for " << task.absolute_path << " (queued " << task.i_frn
                                                             << ", on disk " << file_frn << ")");
    }
    return file_frn;
  }
  if (task.i_frn != 0) {
    return task.i_frn;
  }
  return SyntheticIFrn(task.absolute_path);
}

// Per-drain bookkeeping. Skips are counted here and reported as one debug line per drain, so a
// bin full of unreadable files cannot flood the log.
struct DrainStats {
  std::size_t skipped = 0;
};

// Reads and parses one $I file (skip-and-continue, spec 3.4; empty-bin race, spec 3.5.5). A failure
// is counted in stats and reported as false. No per-file log line is written.
[[nodiscard]] bool ReadAndParse(const EnrichTask& task, IFileInfo& out, DrainStats& stats) {
  RecycleFileBytes file;
  if (std::string error_kind; !ReadRecycleIndexFile(task.absolute_path, file, error_kind)) {
    ++stats.skipped;
    return false;
  }
  ParseResult parsed = ParseRecycleBinIndex(file.bytes.data(), file.bytes.size());
  if (!parsed.ok) {
    ++stats.skipped;
    return false;
  }
  out.i_frn = ResolveIFrn(task, file.file_frn);
  out.parent_frn = task.parent_frn;
  out.name = task.name;
  out.meta.original_path = std::move(parsed.metadata.original_path_utf8);
  out.meta.original_name = std::string(path_utils::GetFilename(out.meta.original_path));
  out.meta.original_size = parsed.metadata.original_size;
  out.meta.deletion_time_filetime = parsed.metadata.deletion_time_filetime;
  // The $R side decides is_directory inside the store, so the $I side leaves it false.
  out.meta.is_directory = false;
  return true;
}

#ifdef _WIN32
// Windows: SID paths and $I names are UTF-8 in this pipeline. u8path and u8string keep the bytes
// exact, instead of converting through the ANSI code page.
[[nodiscard]] std::filesystem::path SidDirectoryPath(std::string_view sid_path) {
  return std::filesystem::u8path(sid_path);
}

[[nodiscard]] std::string PathToUtf8(const std::filesystem::path& path) {
  return path.u8string();
}
#else
// POSIX: the narrow path is already the byte sequence we want, so no conversion is applied.
[[nodiscard]] std::filesystem::path SidDirectoryPath(std::string_view sid_path) {
  return {sid_path};
}

[[nodiscard]] std::string PathToUtf8(const std::filesystem::path& path) {
  return path.string();
}
#endif  // _WIN32

// Cold path (M8): lists the $I* children of one <SID> directory. Off-thread, on the worker only.
void ExpandSidMarker(std::uint64_t sid_frn, std::string_view sid_path, std::vector<EnrichTask>& out) {
  out.clear();
  std::error_code ec;
  const std::filesystem::directory_iterator end;
  const std::filesystem::path sid_dir = SidDirectoryPath(sid_path);
  // $I names are ASCII by construction ($Recycle.Bin\<SID>\$I<token>), so the name filter is exact.
  for (std::filesystem::directory_iterator it(sid_dir, ec); !ec && it != end; it.increment(ec)) {
    const std::filesystem::directory_entry& entry = *it;
    std::string name = PathToUtf8(entry.path().filename());
    if (!IsIArtifactName(name)) {
      continue;
    }
    if (std::error_code file_ec; !entry.is_regular_file(file_ec)) {
      continue;
    }
    out.push_back(EnrichTask{0, sid_frn, std::move(name), PathToUtf8(entry.path())});
  }
  if (ec) {
    LOG_DEBUG_BUILD("RecycleBinEnricher: cannot enumerate " << sid_path << " (" << ec.message() << ")");
  }
}

// Parses one task and stages it for the drain's publish. Invalid names and read/parse failures are skipped.
void StageTask(const EnrichTask& task, std::vector<IFileInfo>& infos, DrainStats& stats) {
  if (!IsIArtifactName(task.name)) {
    ++stats.skipped;
    return;
  }
  IFileInfo info;
  if (!ReadAndParse(task, info, stats)) {
    return;
  }
  infos.push_back(std::move(info));
}


void CountDrop(std::atomic<std::uint64_t>& dropped) {
  if (const std::uint64_t total = dropped.fetch_add(1) + 1; total % kDropLogInterval == 0) {
    LOG_DEBUG_BUILD("RecycleBinEnricher: " << total << " items dropped (queue full or stopped)");
  }
}

#ifdef _WIN32
[[nodiscard]] std::string_view OpenErrorKind(DWORD error) {
  if (error == ERROR_ACCESS_DENIED) {
    return "access_denied";
  }
  if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
    return "not_found";
  }
  return "open_failed";
}

constexpr DWORD kReadChunkBytes = 64U * 1024U;
#endif  // _WIN32

}  // namespace

#ifdef _WIN32

bool ReadRecycleIndexFile(std::string_view absolute_path, RecycleFileBytes& out, std::string& error_kind) {
  out.bytes.clear();
  out.file_frn = 0;
  const std::wstring wide_path = Utf8ToWide(absolute_path);
  // Share all three modes, so reading a $I never blocks the Recycle Bin or its owner.
  ScopedHandle handle(CreateFileW(wide_path.c_str(), GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr));
  if (handle.Get() == INVALID_HANDLE_VALUE) {
    error_kind = OpenErrorKind(GetLastError());
    return false;
  }
  BY_HANDLE_FILE_INFORMATION info{};
  if (!GetFileInformationByHandle(handle.Get(), &info)) {
    error_kind = "stat_failed";
    return false;
  }
  const std::uint64_t size = (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
  if (size > kMaxIndexFileBytes) {
    error_kind = "too_large";
    return false;
  }
  // Same composition as the NTFS 64-bit file reference (sequence in the top 16 bits). The index is
  // per volume, so the volume serial is deliberately not folded in (the USN FRN is volume-local too).
  out.file_frn = (static_cast<std::uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
  out.bytes.resize(static_cast<std::size_t>(size));
  std::size_t total = 0;
  while (total < out.bytes.size()) {
    const auto want = static_cast<DWORD>((std::min)(out.bytes.size() - total, static_cast<std::size_t>(kReadChunkBytes)));
    DWORD got = 0;
    if (!ReadFile(handle.Get(), out.bytes.data() + total, want, &got, nullptr) || got == 0) {
      error_kind = "read_failed";
      out.bytes.clear();
      out.file_frn = 0;
      return false;
    }
    total += got;
  }
  return true;
}

#else  // _WIN32

bool ReadRecycleIndexFile(std::string_view absolute_path, RecycleFileBytes& out, std::string& error_kind) {
  out.bytes.clear();
  out.file_frn = 0;  // POSIX trash needs no enrichment, so there is no NTFS FRN here.
  // $I paths are pure ASCII by construction ($Recycle.Bin\<SID>\$I<token>), so the narrow path is lossless.
  const std::string path(absolute_path);
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in.is_open()) {
    std::error_code ec;
    error_kind = std::filesystem::exists(path, ec) ? "access_denied" : "not_found";
    return false;
  }
  const std::streamoff size = in.tellg();
  if (size < 0) {
    error_kind = "stat_failed";
    return false;
  }
  if (static_cast<std::uint64_t>(size) > kMaxIndexFileBytes) {
    error_kind = "too_large";
    return false;
  }
  const auto count = static_cast<std::streamsize>(size);
  in.seekg(0, std::ios::beg);
  out.bytes.resize(static_cast<std::size_t>(size));
  in.read(reinterpret_cast<char*>(out.bytes.data()), count);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) - ifstream read requires a char buffer
  if (in.gcount() != count) {
    out.bytes.clear();
    error_kind = "read_failed";
    return false;
  }
  return true;
}

#endif  // _WIN32

ParseResult ParseRecycleBinIndex(const std::uint8_t* data, std::size_t size) {
  if (data == nullptr || size < kVersionFieldSize) {
    return MakeError(ParseError::kTruncated);
  }
  const std::uint64_t version = ReadLe(data, kVersionFieldSize);
  if (version == kVersion1) {
    return ParseV1(data, size);
  }
  if (version == kVersion2) {
    return ParseV2(data, size);
  }
  return MakeError(ParseError::kBadVersion);
}

RecycleBinEnricher::RecycleBinEnricher(RecycleBinMetadataStore* store, std::size_t max_queue)
    : store_(store), max_queue_((std::max)(max_queue, std::size_t{1})) {
  assert(store_ != nullptr && "RecycleBinEnricher requires a store");
}

RecycleBinEnricher::~RecycleBinEnricher() {
  Stop();
}

void RecycleBinEnricher::Start() {
  const std::scoped_lock lock{mutex_};
  if (worker_.joinable()) {
    return;
  }
  if (store_ == nullptr) {
    LOG_ERROR_BUILD("RecycleBinEnricher: null store, worker not started");
    return;
  }
  stop_ = false;
  try {
    worker_ = std::thread([this] { WorkerLoop(); });
  } catch (const std::system_error& error) {
    stop_ = true;
    LOG_ERROR_BUILD("RecycleBinEnricher: failed to start worker: " << error.what());
  }
}

void RecycleBinEnricher::Stop() {
  {
    const std::scoped_lock lock{mutex_};
    stop_ = true;
  }
  cv_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
  // The worker has exited, so the queue is quiet. Whatever never ran is counted as dropped.
  const std::scoped_lock lock{mutex_};
  const std::size_t remainder = queue_.size();
  queue_.clear();
  queued_paths_.clear();
  queue_depth_.store(0);
  if (remainder > 0) {
    dropped_.fetch_add(static_cast<std::uint64_t>(remainder));
  }
}

bool RecycleBinEnricher::EnqueueIFile(std::uint64_t i_frn, std::uint64_t parent_frn, std::string name,
                                      std::string absolute_path) {
  {
    const std::scoped_lock lock{mutex_};
    // Coalesce: an identical queued path already covers this report. queued_paths_ is empty after Stop().
    if (queued_paths_.count(absolute_path) > 0) {
      // Rare path, so a linear scan is fine. The newest known FRN wins. A 0 (unresolved) report never
      // overwrites a known value.
      if (i_frn != 0) {
        const auto found = std::find_if(queue_.begin(), queue_.end(), [&absolute_path](const EnrichTask& task) {
          return task.absolute_path == absolute_path;
        });
        if (found != queue_.end()) {
          found->i_frn = i_frn;
        }
      }
      return true;
    }
    if (!stop_ && queue_.size() < max_queue_) {
      queued_paths_.insert(absolute_path);
      queue_.emplace_back(EnrichTask{i_frn, parent_frn, std::move(name), std::move(absolute_path)});
      queue_depth_.store(queue_.size());
      cv_.notify_one();
      return true;
    }
  }
  CountDrop(dropped_);
  return false;
}

bool RecycleBinEnricher::EnqueueSidMarker(std::uint64_t sid_frn, std::string sid_path) {
  {
    const std::scoped_lock lock{mutex_};
    if (!stop_ && queue_.size() < max_queue_) {
      queue_.emplace_back(EnrichTask{sid_frn, 0, "", std::move(sid_path)});
      queue_depth_.store(queue_.size());
      cv_.notify_one();
      return true;
    }
  }
  CountDrop(dropped_);
  return false;
}

bool RecycleBinEnricher::CancelByPath(std::string_view absolute_path) {
  const std::scoped_lock lock{mutex_};
  const std::string key(absolute_path);
  if (queued_paths_.erase(key) == 0) {
    return false;
  }
  if (const auto it = std::find_if(queue_.begin(), queue_.end(), [&key](const EnrichTask& task) {
        return task.absolute_path == key;
      });
      it != queue_.end()) {
    queue_.erase(it);
    queue_depth_.store(queue_.size());
    return true;
  }
  return false;
}

std::uint64_t RecycleBinEnricher::DropCount() const noexcept {
  return dropped_.load();
}

std::size_t RecycleBinEnricher::QueueDepth() const noexcept {
  return queue_depth_.load();
}

bool RecycleBinEnricher::DequeueBatch(std::vector<EnrichTask>& batch) {
  // unique_lock, not scoped_lock: the condition variable needs to unlock while it waits.
  std::unique_lock lock{mutex_};
  cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
  if (stop_) {
    // The remainder stays queued. Stop() counts it as dropped after the join.
    return false;
  }
  while (!queue_.empty() && batch.size() < kEnrichDrainBatch) {
    EnrichTask task = std::move(queue_.front());
    queue_.pop_front();
    if (!task.name.empty()) {
      queued_paths_.erase(task.absolute_path);
    }
    batch.push_back(std::move(task));
  }
  queue_depth_.store(queue_.size());
  return true;
}

namespace {

void ProcessEnrichBatchTask(const EnrichTask& task, std::vector<IFileInfo>& infos,
                            DrainStats& stats, std::vector<EnrichTask>& expanded) {
  if (!task.name.empty()) {
    StageTask(task, infos, stats);
    return;
  }
  ExpandSidMarker(task.i_frn, task.absolute_path, expanded);
  for (const EnrichTask& item : expanded) {
    StageTask(item, infos, stats);
  }
  expanded.clear();
}

}  // namespace

void RecycleBinEnricher::WorkerLoop() {
  std::vector<EnrichTask> batch;
  std::vector<IFileInfo> infos;
  std::vector<EnrichTask> expanded;
  batch.reserve(kEnrichDrainBatch);
  infos.reserve(kEnrichDrainBatch);
  while (DequeueBatch(batch)) {
    infos.clear();
    DrainStats stats;
    for (const EnrichTask& task : batch) {
      ProcessEnrichBatchTask(task, infos, stats, expanded);
    }
    batch.clear();
    if (!infos.empty()) {
      store_->PublishIEntries(std::move(infos));
    }
    if (stats.skipped > 0) {
      LOG_DEBUG_BUILD("RecycleBinEnricher: drain skipped " << stats.skipped << " $I file(s)");
    }
  }
}

}  // namespace recycle_bin
