// Windows-only regression test for the VolumeGateway seam (DDD #8): the
// baseline enumeration runs end-to-end against a scripted fake gateway with
// no volume attached. Covers EnumMft transcript delivery, cursor advance
// (MftEnumerationPosition), EOF termination, and the GetVolumeData reserve
// path. The CMake target is WIN32-guarded and this TU is never built on
// macOS/Linux; the body is additionally _WIN32-guarded so accidental
// compilation elsewhere degrades to an empty test binary.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

#include "index/FileIndex.h"
#include "index/IndexBuildRun.h"
#include "index/SystemPathFilter.h"
#include "usn/VolumeGateway.h"

#ifdef _WIN32

#include "UsnSyntheticRecordHelpers.h"

namespace {

using usn_test_helpers::AppendSyntheticRecord;
using usn_test_helpers::kSynthVolumeRoot;

// Synthetic FRNs (record numbers in the low 48 bits, sequence 1).
constexpr uint64_t kSynthFile = 0x00010000000000B0ULL;
// Leading next-USN carried by the scripted enumeration buffer.
constexpr USN kScriptedNextUsn = 4096;

// Scripted VolumeGateway: replays canned EnumMft buffers, then EOF with
// ::SetLastError so IndexBuildRun::Run takes its normal end-of-enumeration
// path. All other ioctls fail closed (unused by the run with metadata
// reading disabled) except GetVolumeData, which reports a canned geometry
// for the Reserve() sizing path.
class FakeVolumeGateway : public volume_gateway::VolumeGateway {
 public:
  explicit FakeVolumeGateway(std::vector<std::vector<char>> transcript)
      : transcript_(std::move(transcript)) {}

  [[nodiscard]] bool QueryJournal(HANDLE /*volume*/,
                                 USN_JOURNAL_DATA_V0& /*out_data*/) override {
    return false;
  }

  [[nodiscard]] bool ReadJournal(HANDLE /*volume*/,
                                READ_USN_JOURNAL_DATA_V0& /*params*/,
                                char* /*buffer*/,
                                int /*buffer_size*/,
                                DWORD& /*out_bytes*/) override {
    return false;
  }

  [[nodiscard]] bool EnumMft(HANDLE /*volume*/,
                            MFT_ENUM_DATA_V0& params,
                            char* buffer,
                            int buffer_size,
                            DWORD& out_bytes) override {
    ++enum_calls_;
    last_start_ = params.StartFileReferenceNumber;
    if (next_ < transcript_.size()) {
      const std::vector<char>& body = transcript_[next_++];
      if (static_cast<size_t>(buffer_size) < body.size()) {
        ::SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return false;
      }
      std::memcpy(buffer, body.data(), body.size());
      out_bytes = static_cast<DWORD>(body.size());
      return true;
    }
    ::SetLastError(ERROR_HANDLE_EOF);
    return false;
  }

  [[nodiscard]] bool GetFileRecord(HANDLE /*volume*/,
                                  uint64_t /*file_ref_num*/,
                                  char* /*buffer*/,
                                  DWORD /*buffer_size*/,
                                  DWORD& /*out_bytes*/) override {
    return false;
  }

  [[nodiscard]] bool GetVolumeData(HANDLE /*volume*/,
                                  NTFS_VOLUME_DATA_BUFFER& out_data) override {
    ++volume_data_calls_;
    out_data.BytesPerFileRecordSegment = 1024;
    out_data.MftValidDataLength.QuadPart = 1024 * 100;
    return true;
  }

  [[nodiscard]] int enum_calls() const { return enum_calls_; }
  [[nodiscard]] ULONGLONG last_start() const { return last_start_; }
  [[nodiscard]] int volume_data_calls() const { return volume_data_calls_; }

 private:
  std::vector<std::vector<char>> transcript_;
  size_t next_ = 0;
  int enum_calls_ = 0;
  ULONGLONG last_start_ = 0;
  int volume_data_calls_ = 0;
};

// One enumeration buffer: leading next-USN followed by a single file.
std::vector<char> ScriptedEnumBuffer() {
  std::vector<char> buffer;
  const USN next_usn = kScriptedNextUsn;
  const char* raw = reinterpret_cast<const char*>(&next_usn);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) - USN blob prefix of the DeviceIoControl buffer
  buffer.insert(buffer.end(), raw, raw + sizeof(next_usn));
  AppendSyntheticRecord(buffer, kSynthFile, kSynthVolumeRoot,
                        USN_REASON_FILE_CREATE | USN_REASON_CLOSE,
                        FILE_ATTRIBUTE_ARCHIVE, L"doc.txt");
  return buffer;
}

TEST_CASE("VolumeGateway: Run drains scripted buffers and advances the cursor") {
  FakeVolumeGateway gateway({ScriptedEnumBuffer()});
  FileIndex file_index;
  std::atomic indexed_file_count{size_t{0}};
  system_path_filter::FilteredDirTracker filtered_dirs;
  std::atomic integrity_latch{bool{false}};
  index_build_run::IndexBuildRun run(INVALID_HANDLE_VALUE, gateway, file_index,
                                     &indexed_file_count, filtered_dirs,
                                     &integrity_latch,
                                     /*enable_mft_metadata_reading=*/false);

  // Production order: Reserve() sizes the index through the gateway first.
  run.Reserve();
  CHECK(gateway.volume_data_calls() == 1);

  std::vector<char> buffer(256 * 1024);
  mft_enum_position::MftEnumerationPosition position =
      mft_enum_position::MftEnumerationPosition::Start();
  CHECK(run.Run(buffer, position));

  // One staged file, cursor advanced to the buffer's next USN, EOF observed.
  const index_build_run::IndexBuildRun::Result result = run.GetResult();
  CHECK(result.files == 1);
  CHECK(position.Data().StartFileReferenceNumber == kScriptedNextUsn);
  CHECK(gateway.enum_calls() == 2);
  CHECK(file_index.Size() == 1U);
  CHECK_FALSE(integrity_latch.load());
}

TEST_CASE("VolumeGateway: immediate EOF yields an empty run") {
  FakeVolumeGateway gateway({});
  FileIndex file_index;
  std::atomic indexed_file_count{size_t{0}};
  system_path_filter::FilteredDirTracker filtered_dirs;
  std::atomic integrity_latch{bool{false}};
  index_build_run::IndexBuildRun run(INVALID_HANDLE_VALUE, gateway, file_index,
                                     &indexed_file_count, filtered_dirs,
                                     &integrity_latch,
                                     /*enable_mft_metadata_reading=*/false);

  std::vector<char> buffer(256 * 1024);
  mft_enum_position::MftEnumerationPosition position =
      mft_enum_position::MftEnumerationPosition::Start();
  CHECK(run.Run(buffer, position));

  CHECK(run.GetResult().files == 0);
  CHECK(position.Data().StartFileReferenceNumber == 0U);
  CHECK(gateway.enum_calls() == 1);
  CHECK(file_index.Size() == 0U);
}

}  // namespace

#endif  // _WIN32
