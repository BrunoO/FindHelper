// Windows-only regression test for the per-buffer two-phase USN apply:
// directory CREATE records apply before all other records in the same
// buffer, so same-buffer child-before-parent pairs join cleanly instead of
// parking bare-name placeholders that the heal pass must fix up.
//
// Windows-only: needs the winioctl.h USN_RECORD_V2 layout and the real
// UsnMonitor (never started; ProcessBufferForTest drives one synthetic
// buffer). The CMake target is WIN32-guarded and this TU is never built on
// macOS/Linux; the body is additionally _WIN32-guarded so accidental
// compilation elsewhere degrades to an empty test binary.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

#include "index/FileIndex.h"
#include "usn/UsnMonitor.h"
#include "usn/UsnReason.h"
#include "usn/UsnRecordUtils.h"

#ifdef _WIN32

#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem
#include <winioctl.h>

#include "UsnSyntheticRecordHelpers.h"

namespace {

using usn_test_helpers::AppendSyntheticRecord;
using usn_test_helpers::kSynthVolumeRoot;

// Synthetic FRNs (record numbers in the low 48 bits, sequence 1), mirroring
// the constant style in IndexOperationsTests.
constexpr uint64_t kSynthParentDir = 0x00010000000000A0ULL;
constexpr uint64_t kSynthChildFile = 0x00010000000000B0ULL;
// Close-time CREATE under ReturnOnlyOnClose: action bit + CLOSE. Built from
// UsnReason enumerators (DDD #5) so SDK drift fails at the static_asserts in
// UsnMonitor.cpp instead of silently forking the mask here.
constexpr DWORD kCloseTimeCreateReason =
    (usn_reason::UsnReason::FileCreate | usn_reason::UsnReason::Close).Mask();

// Raw journal buffer: leading next-USN slot, then records.
std::vector<char> MakeSyntheticBuffer() {
  return std::vector<char>(usn_record_utils::SizeOfUsn(), 0);
}

}  // namespace

TEST_CASE("TwoPhaseApply: same-buffer child-before-parent joins cleanly") {
  FileIndex file_index;
  UsnMonitor monitor(file_index);

  // Journal order reproduces the ReturnOnlyOnClose inversion inside one
  // buffer: the child CLOSE arrives before the parent directory CLOSE.
  auto buffer = MakeSyntheticBuffer();
  AppendSyntheticRecord(buffer, kSynthChildFile, kSynthParentDir,
                        kCloseTimeCreateReason, FILE_ATTRIBUTE_ARCHIVE,
                        L"late.txt");
  AppendSyntheticRecord(buffer, kSynthParentDir, kSynthVolumeRoot,
                        kCloseTimeCreateReason, FILE_ATTRIBUTE_DIRECTORY,
                        L"LateDir");

  monitor.ProcessBufferForTest(buffer);

  // Clean insert: full path, nothing awaiting, and crucially no heal — the
  // healed counter distinguishes "joined directly" from "placeholder+heal".
  const std::string path =
      file_index.GetPathAccessor().GetPathCopy(kSynthChildFile);
  CHECK(path.find("LateDir") != std::string::npos);
  CHECK(path.find("late.txt") != std::string::npos);
  CHECK(file_index.GetAwaitingStats().count == 0);
  CHECK(file_index.GetHealedAwaitingTotal() == 0);
  CHECK(monitor.GetMetricsSnapshot().files_created == 2);
}

TEST_CASE("TwoPhaseApply: lone child still awaits parent (harness check)") {
  FileIndex file_index;
  UsnMonitor monitor(file_index);

  auto buffer = MakeSyntheticBuffer();
  AppendSyntheticRecord(buffer, kSynthChildFile, kSynthParentDir,
                        kCloseTimeCreateReason, FILE_ATTRIBUTE_ARCHIVE,
                        L"orphan.txt");

  monitor.ProcessBufferForTest(buffer);

  // Proves the assertions above can fail: without the parent, the child
  // parks a bare placeholder and waits.
  CHECK(file_index.GetPathAccessor().GetPathCopy(kSynthChildFile) ==
        "orphan.txt");
  CHECK(file_index.GetAwaitingStats().count == 1);
  CHECK(file_index.GetHealedAwaitingTotal() == 0);
}

TEST_CASE("TwoPhaseApply: corrupt tail latches integrity") {
  FileIndex file_index;
  UsnMonitor monitor(file_index);
  REQUIRE(!monitor.IsIndexIntegrityCompromised());

  auto buffer = MakeSyntheticBuffer();
  AppendSyntheticRecord(buffer, kSynthChildFile, kSynthParentDir,
                        kCloseTimeCreateReason, FILE_ATTRIBUTE_ARCHIVE,
                        L"orphan.txt");
  // Corrupt tail: full header room but zero RecordLength.
  buffer.resize(buffer.size() + usn_record_utils::SizeOfUsnRecordV2(), 0);

  monitor.ProcessBufferForTest(buffer);

  // The valid prefix still applied; the corrupt tail latches integrity.
  CHECK(file_index.GetAwaitingStats().count == 1);
  CHECK(monitor.IsIndexIntegrityCompromised());
}

#endif  // _WIN32
