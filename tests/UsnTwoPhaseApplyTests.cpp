// Windows-only regression test for the per-buffer two-phase USN apply:
// directory CREATE records apply before all other records in the same
// buffer, so same-buffer child-before-parent pairs join cleanly instead of
// parking bare-name placeholders that the heal pass must fix up.
//
// Also hosts the Phase-4 Recycle Bin end-to-end case (synthetic bin on disk ->
// $R/$I USN buffers -> enrichment -> eviction), which is the only test covering the
// store/enricher wiring inside UsnMonitor.
//
// Windows-only: needs the winioctl.h USN_RECORD_V2 layout and the real
// UsnMonitor (monitoring itself is never started; ProcessBufferForTest drives
// one synthetic buffer per call and the recycle case starts only the $I worker
// through the StartRecycleEnricherForTest seam). The CMake target is
// WIN32-guarded and this TU is never built on macOS/Linux; the body is
// additionally _WIN32-guarded so accidental compilation elsewhere degrades to
// an empty test binary.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "index/FileIndex.h"
#include "usn/UsnMonitor.h"
#include "usn/UsnReason.h"
#include "usn/UsnRecord.h"

#ifdef _WIN32

#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem
#include <winioctl.h>

#include "UsnSyntheticRecordHelpers.h"
#include "index/recycle/RecycleBinEnricher.h"
#include "index/recycle/RecycleBinMetadataStore.h"
#include "path/PathUtils.h"

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
// Delete-time close record for the recycle e2e below (same shape, FileDelete bit).
constexpr DWORD kCloseTimeDeleteReason =
    (usn_reason::UsnReason::FileDelete | usn_reason::UsnReason::Close).Mask();

// Raw journal buffer: leading next-USN slot, then records.
std::vector<char> MakeSyntheticBuffer() {
  return std::vector<char>(usn_record::SizeOfUsn(), 0);
}

// ---------------------------------------------------------------------------
// Recycle Bin end-to-end fixture (see the RecycleBinE2E case below)
// ---------------------------------------------------------------------------
// Container chain: $Recycle.Bin under the volume root (record 5), a <SID> folder
// under it, then the $R/$I artifact pair under the <SID>.
constexpr uint64_t kSynthRecycleBinDir = 0x00010000000000C0ULL;
constexpr uint64_t kSynthSidDir = 0x00010000000000C1ULL;
constexpr uint64_t kSynthRPayload = 0x00010000000000C2ULL;
constexpr wchar_t kRecycleBinRecordName[] = L"$Recycle.Bin";
constexpr wchar_t kSidRecordName[] = L"S-1-5-21-1234567890-1234567890-1234567890-1001";
constexpr wchar_t kRArtifactRecordName[] = L"$RE2E001.txt";
// The $I name must equal kIndexArtifactName below: the monitor queues the absolute
// path it builds from the USN record name, and the fixture writes the file at
// JoinPath(resolved <SID> path, kIndexArtifactName).
constexpr wchar_t kIArtifactRecordName[] = L"$IE2E001.txt";
constexpr char kIndexArtifactName[] = "$IE2E001.txt";

// $I v1 payload contents (the fixture writes a well-formed v1 record, like the
// RecycleBinEnricherTests fixtures: fixed 544-byte layout with a 260-unit path field).
constexpr char kE2eOriginalPath[] = "C:\\Users\\e2e\\report.txt";
constexpr char kE2eOriginalName[] = "report.txt";
constexpr uint64_t kE2eOriginalSize = 4096;
constexpr uint64_t kE2eDeletionFiletime = 133000000000000777ULL;
constexpr std::size_t kV1IndexRecordBytes = 544;

// Enrichment is asynchronous (the worker thread reads the $I file), so the case
// polls. Deadlines mirror RecycleBinEnricherTests.
constexpr auto kEnrichWaitLimit = std::chrono::seconds(10);
constexpr auto kEnrichPollInterval = std::chrono::milliseconds(5);

void AppendU64(std::vector<std::uint8_t>& out, std::uint64_t value) {
  for (int shift = 0; shift < 64; shift += 8) {
    out.push_back(static_cast<std::uint8_t>(value >> shift));
  }
}

// Appends ASCII text as UTF-16LE code units (the fixture paths are ASCII).
void AppendAsciiUtf16(std::vector<std::uint8_t>& out, std::string_view ascii) {
  for (const char c : ascii) {
    out.push_back(static_cast<std::uint8_t>(c));
    out.push_back(0);
  }
}

// Writes a valid v1 $I record (version, original size, deletion time, UTF-16 path).
// Creates the parent directory first so the enricher's read finds the file.
void WriteV1IndexFile(const std::filesystem::path& file) {
  std::error_code ec;
  std::filesystem::create_directories(file.parent_path(), ec);
  REQUIRE_FALSE(ec);

  std::vector<std::uint8_t> bytes;
  bytes.reserve(kV1IndexRecordBytes);
  AppendU64(bytes, 1);                       // Record version 1 (Windows 7/8/8.1 layout).
  AppendU64(bytes, kE2eOriginalSize);
  AppendU64(bytes, kE2eDeletionFiletime);
  AppendAsciiUtf16(bytes, kE2eOriginalPath);
  bytes.resize(kV1IndexRecordBytes, 0);

  std::ofstream out(file, std::ios::binary);
  REQUIRE(out.good());
  out.write(reinterpret_cast<const char*>(bytes.data()),  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) - binary blob write
            static_cast<std::streamsize>(bytes.size()));
  REQUIRE(out.good());
}

// Temporary directory removed on scope exit (same shape as the RecycleBinEnricherTests
// fixture; kept local because that TU's helpers are not shared through a header).
class TempDir {
 public:
  TempDir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() / ("usn_rb_e2e_" + std::to_string(stamp));
    std::error_code ec;
    std::filesystem::create_directories(path_, ec);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  TempDir(TempDir&&) = delete;
  TempDir& operator=(TempDir&&) = delete;

  [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

 private:
  std::filesystem::path path_;
};

// Stops the test-started enricher and restores the process-wide volume root even when
// a REQUIRE aborts the case (doctest unwinds locals), so a failing case cannot leak a
// worker thread or leave the root path retargeted for later cases.
class RecycleE2eGuard {
 public:
  RecycleE2eGuard(UsnMonitor& monitor, std::string saved_root)
      : monitor_(monitor), saved_root_(std::move(saved_root)) {}
  ~RecycleE2eGuard() {
    monitor_.StopRecycleEnricherForTest();
    path_utils::SetDefaultVolumeRootPath(saved_root_);
  }
  RecycleE2eGuard(const RecycleE2eGuard&) = delete;
  RecycleE2eGuard& operator=(const RecycleE2eGuard&) = delete;
  RecycleE2eGuard(RecycleE2eGuard&&) = delete;
  RecycleE2eGuard& operator=(RecycleE2eGuard&&) = delete;

 private:
  UsnMonitor& monitor_;
  std::string saved_root_;
};

// Polls until predicate holds or the deadline passes. Returns the final predicate value.
template <typename Predicate>
[[nodiscard]] bool WaitForEnrichment(Predicate predicate) {
  const auto deadline = std::chrono::steady_clock::now() + kEnrichWaitLimit;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(kEnrichPollInterval);
  }
  return predicate();
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
  buffer.resize(buffer.size() + usn_record::SizeOfUsnRecordV2(), 0);

  monitor.ProcessBufferForTest(buffer);

  // The valid prefix still applied; the corrupt tail latches integrity.
  CHECK(file_index.GetAwaitingStats().count == 1);
  CHECK(monitor.IsIndexIntegrityCompromised());
}

// Phase-4 end-to-end: a synthetic Recycle Bin pair travels the whole pipeline —
// USN $R/$I producers -> store pairing -> off-thread $I read + parse (a real v1 file
// on disk) -> published snapshot -> eviction on the $I delete. This is the only test
// that exercises the store/enricher wiring inside UsnMonitor (the Tier-1 store and
// enricher suites drive them directly), so it is also the regression guard for the
// per-buffer batching, the test seams and the link closure of this target.
TEST_CASE("RecycleBinE2E: $R/$I pair is enriched from disk and evicted by the $I delete") {
  // The monitor resolves MFT-record-5 children against path_utils' configured volume
  // root (IndexOperations: "live USN CREATE gets a full path"), so retargeting the root
  // at a temp dir puts the whole synthetic bin on disk under test control.
  TempDir temp_dir;
  const std::string saved_root(path_utils::GetDefaultVolumeRootPath());
  path_utils::SetDefaultVolumeRootPath(temp_dir.Path().u8string());
  FileIndex file_index;
  UsnMonitor monitor(file_index);
  RecycleE2eGuard guard(monitor, saved_root);
  // Never starts monitoring threads; the enricher's production lifecycle lives on the
  // reader/stop path, which this test does not run.
  monitor.StartRecycleEnricherForTest();

  auto buffer = MakeSyntheticBuffer();
  AppendSyntheticRecord(buffer, kSynthRecycleBinDir, kSynthVolumeRoot, kCloseTimeCreateReason,
                        FILE_ATTRIBUTE_DIRECTORY, kRecycleBinRecordName);
  monitor.ProcessBufferForTest(buffer);

  buffer = MakeSyntheticBuffer();
  AppendSyntheticRecord(buffer, kSynthSidDir, kSynthRecycleBinDir, kCloseTimeCreateReason,
                        FILE_ATTRIBUTE_DIRECTORY, kSidRecordName);
  monitor.ProcessBufferForTest(buffer);

  // Read the <SID> path back from the index: that is exactly the directory the monitor
  // will join the $I name onto when it queues the enrichment task.
  const std::string sid_path = file_index.GetPathAccessor().GetPathCopy(kSynthSidDir);
  REQUIRE(!sid_path.empty());
  const std::filesystem::path index_file = std::filesystem::u8path(sid_path) / kIndexArtifactName;
  WriteV1IndexFile(index_file);

  // The enricher takes the $I's FRN from its read handle (N2) and publishes that value,
  // so the synthetic USN records must carry the same reference for the delete-time
  // kByI eviction to resolve the published pair. In production the USN
  // FileReferenceNumber IS the file's FRN; here the fixture reads it off the file.
  recycle_bin::RecycleFileBytes probe;
  std::string probe_error_kind;
  REQUIRE(recycle_bin::ReadRecycleIndexFile(index_file.u8string(), probe, probe_error_kind));
  REQUIRE(probe.file_frn != 0);

  // $R create first: it has no $I partner yet, so the store pends it and publishes nothing.
  buffer = MakeSyntheticBuffer();
  AppendSyntheticRecord(buffer, kSynthRPayload, kSynthSidDir, kCloseTimeCreateReason,
                        FILE_ATTRIBUTE_ARCHIVE, kRArtifactRecordName);
  monitor.ProcessBufferForTest(buffer);
  CHECK(monitor.TestMetadataStore().Current()->by_file_id.empty());

  // $I create: the monitor queues an off-thread read of index_file; the worker parses it
  // and publishes the pair.
  buffer = MakeSyntheticBuffer();
  AppendSyntheticRecord(buffer, probe.file_frn, kSynthSidDir, kCloseTimeCreateReason,
                        FILE_ATTRIBUTE_ARCHIVE, kIArtifactRecordName);
  monitor.ProcessBufferForTest(buffer);
  // Loud failure instead of a silent timeout: a task that never reached a running worker
  // (enricher not started, or queue full) is counted as a drop by the enricher.
  CHECK_MESSAGE(monitor.TestRecycleDropCount() == 0,
                "the queued $I task was dropped - is the enricher running?");

  const bool published = WaitForEnrichment([&monitor] {
    return monitor.TestMetadataStore().Current()->by_file_id.count(kSynthRPayload) == 1;
  });
  REQUIRE_MESSAGE(published, "recycle pair never published (drops="
                                 << monitor.TestRecycleDropCount()
                                 << ", queue depth=" << monitor.TestRecycleQueueDepth() << ")");
  CHECK(monitor.TestRecycleQueueDepth() == 0);

  const auto snapshot = monitor.TestMetadataStore().Current();
  REQUIRE(snapshot->by_file_id.count(kSynthRPayload) == 1);
  const recycle_bin::RecycleFileMetadata& row = snapshot->by_file_id.at(kSynthRPayload);
  CHECK(row.original_name == kE2eOriginalName);
  CHECK(row.original_path == kE2eOriginalPath);
  CHECK(row.original_size == kE2eOriginalSize);
  CHECK(row.deletion_time_filetime == kE2eDeletionFiletime);
  CHECK_FALSE(row.is_directory);

  // $I delete (Empty Bin / post-restore cleanup): evicts the pair by the $I FRN. The
  // flush is synchronous, so the eviction is visible when ProcessBufferForTest returns.
  buffer = MakeSyntheticBuffer();
  AppendSyntheticRecord(buffer, probe.file_frn, kSynthSidDir, kCloseTimeDeleteReason,
                        FILE_ATTRIBUTE_ARCHIVE, kIArtifactRecordName);
  monitor.ProcessBufferForTest(buffer);
  CHECK(monitor.TestMetadataStore().Current()->by_file_id.empty());
}

#endif  // _WIN32
