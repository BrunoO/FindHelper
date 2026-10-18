#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "doctest/doctest.h"
#include "index/recycle/RecycleBinEnricher.h"
#include "index/recycle/RecycleBinMetadataStore.h"

using recycle_bin::ReadRecycleIndexFile;
using recycle_bin::RecycleBinEnricher;
using recycle_bin::RecycleBinMetadataStore;
using recycle_bin::RecycleFileBytes;
using recycle_bin::RFileInfo;

namespace {

constexpr std::uint64_t kParent = 5;
constexpr std::uint64_t kV1FileTime = 133000000000000001ULL;
constexpr std::uint64_t kV2FileTime = 133000000000000000ULL;
constexpr std::size_t kV1RecordBytes = 544;
constexpr std::size_t kOversizeBytes = 200000;
constexpr auto kWaitLimit = std::chrono::seconds(10);
constexpr auto kPollInterval = std::chrono::milliseconds(5);

class TempDir {
 public:
  TempDir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() / ("rbe_tests_" + std::to_string(stamp));
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

void AppendU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) {
    out.push_back(static_cast<std::uint8_t>(value >> shift));
  }
}

void AppendU64(std::vector<std::uint8_t>& out, std::uint64_t value) {
  for (int shift = 0; shift < 64; shift += 8) {
    out.push_back(static_cast<std::uint8_t>(value >> shift));
  }
}

void AppendAsciiUtf16(std::vector<std::uint8_t>& out, std::string_view ascii) {
  for (const char c : ascii) {
    out.push_back(static_cast<std::uint8_t>(c));
    out.push_back(0);
  }
}

std::vector<std::uint8_t> MakeV2Record(std::string_view original_path, std::uint64_t size, std::uint64_t filetime) {
  std::vector<std::uint8_t> bytes;
  AppendU64(bytes, 2);
  AppendU64(bytes, size);
  AppendU64(bytes, filetime);
  AppendU32(bytes, static_cast<std::uint32_t>(original_path.size() + 1));
  AppendAsciiUtf16(bytes, original_path);
  bytes.push_back(0);
  bytes.push_back(0);
  return bytes;
}

std::vector<std::uint8_t> MakeV1Record(std::string_view original_path, std::uint64_t size, std::uint64_t filetime) {
  std::vector<std::uint8_t> bytes;
  AppendU64(bytes, 1);
  AppendU64(bytes, size);
  AppendU64(bytes, filetime);
  AppendAsciiUtf16(bytes, original_path);
  bytes.resize(kV1RecordBytes, 0);
  return bytes;
}

void WriteBytes(const std::filesystem::path& file, const std::vector<std::uint8_t>& bytes) {
  std::ofstream out(file, std::ios::binary);
  out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) - fixture test buffer
}

RFileInfo MakeR(std::uint64_t r_frn, std::string name, bool is_directory = false) {
  RFileInfo info;
  info.r_frn = r_frn;
  info.parent_frn = kParent;
  info.name = std::move(name);
  info.is_directory = is_directory;
  return info;
}

template <typename Predicate>
[[nodiscard]] bool WaitFor(Predicate predicate) {
  const auto deadline = std::chrono::steady_clock::now() + kWaitLimit;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(kPollInterval);
  }
  return predicate();
}

struct EnricherHarness {
  TempDir dir;
  RecycleBinMetadataStore store;
  RecycleBinEnricher enricher{&store};

  EnricherHarness() { enricher.Start(); }
  ~EnricherHarness() { enricher.Stop(); }
  EnricherHarness(const EnricherHarness&) = delete;
  EnricherHarness& operator=(const EnricherHarness&) = delete;
  EnricherHarness(EnricherHarness&&) = delete;
  EnricherHarness& operator=(EnricherHarness&&) = delete;

  template <typename Predicate>
  [[nodiscard]] bool Wait(Predicate predicate) {
    return WaitFor(std::move(predicate));
  }
};

}  // namespace

TEST_CASE("Worker enriches v1 and v2 $I records into published snapshot rows") {
  EnricherHarness h;
  const std::filesystem::path v2_file = h.dir.Path() / "$IAAA001.txt";
  const std::filesystem::path v1_file = h.dir.Path() / "$IBBB002.bin";
  WriteBytes(v2_file, MakeV2Record("C:\\Users\\alice\\notes.txt", 1234, kV2FileTime));
  WriteBytes(v1_file, MakeV1Record("D:\\Data\\blob.bin", 77, kV1FileTime));

  h.store.IngestRFile(MakeR(901, "$RAAA001.txt"));
  h.store.IngestRFile(MakeR(902, "$RBBB002.bin", true));

  CHECK(h.enricher.EnqueueIFile(0, kParent, "$IAAA001.txt", v2_file.string()));
  CHECK(h.enricher.EnqueueIFile(0, kParent, "$IBBB002.bin", v1_file.string()));
  REQUIRE(h.Wait([&h] { return h.store.Current()->by_file_id.size() == 2; }));

  const auto snapshot = h.store.Current();
  const auto& first = snapshot->by_file_id.at(901);
  CHECK(first.original_name == "notes.txt");
  CHECK(first.original_path == "C:\\Users\\alice\\notes.txt");
  CHECK(first.original_size == 1234);
  CHECK(first.deletion_time_filetime == kV2FileTime);
  CHECK_FALSE(first.is_directory);

  const auto& second = snapshot->by_file_id.at(902);
  CHECK(second.original_name == "blob.bin");
  CHECK(second.original_path == "D:\\Data\\blob.bin");
  CHECK(second.original_size == 77);
  CHECK(second.deletion_time_filetime == kV1FileTime);
  CHECK(second.is_directory);  // The $R payload decides is_directory, not the $I side.
}

#ifndef _WIN32
TEST_CASE("Queued I FRN is used for pairing when no handle FRN exists (POSIX)") {
  EnricherHarness h;
  const std::filesystem::path file = h.dir.Path() / "$IFRN001.txt";
  WriteBytes(file, MakeV2Record("C:\\frn.txt", 1, kV2FileTime));

  h.store.IngestRFile(MakeR(931, "$RFRN001.txt"));
  CHECK(h.enricher.EnqueueIFile(777, kParent, "$IFRN001.txt", file.string()));
  REQUIRE(h.Wait([&h] { return h.store.Current()->by_file_id.count(931) == 1; }));

  h.store.EvictByIFrn(777);
  CHECK(h.store.Current()->by_file_id.count(931) == 0);
}
#endif  // _WIN32

TEST_CASE("Missing, corrupt, or empty $I records are skipped while valid work publishes") {
  EnricherHarness h;
  const std::filesystem::path good = h.dir.Path() / "$IGOOD01.txt";
  WriteBytes(good, MakeV2Record("C:\\good.txt", 10, kV2FileTime));
  h.store.IngestRFile(MakeR(900, "$RGOOD01.txt"));

  std::vector<std::uint8_t> corrupt;
  AppendU64(corrupt, 9);

  struct TestCase {
    std::string name;
    std::uint64_t r_frn;
    std::string r_name;
    std::vector<std::uint8_t> bytes;
    bool create_file;
  };
  const std::vector<TestCase> cases = {
      {"$IMISS01.txt", 910, "$RMISS01.txt", {}, false},
      {"$ICORR01.txt", 920, "$RCORR01.txt", corrupt, true},
      {"$IEMPT01.txt", 921, "$REMPT01.txt", {}, true},
  };

  for (const auto& tc : cases) {
    h.store.IngestRFile(MakeR(tc.r_frn, tc.r_name));
    const std::filesystem::path path = h.dir.Path() / tc.name;
    if (tc.create_file) {
      WriteBytes(path, tc.bytes);
    }
    CHECK(h.enricher.EnqueueIFile(0, kParent, tc.name, path.string()));
  }
  CHECK(h.enricher.EnqueueIFile(0, kParent, "$IGOOD01.txt", good.string()));

  REQUIRE(h.Wait([&h] { return h.store.Current()->by_file_id.count(900) == 1; }));
  for (const auto& tc : cases) {
    CHECK(h.store.Current()->by_file_id.count(tc.r_frn) == 0);
  }
  CHECK(h.store.Current()->by_file_id.size() == 1);
  CHECK(h.enricher.DropCount() == 0);
}

TEST_CASE("Bounded queue drops the newest item and counts every drop") {
  RecycleBinMetadataStore store;
  RecycleBinEnricher enricher(&store, 4);  // Not started, so nothing drains.

  CHECK(enricher.EnqueueIFile(0, kParent, "$IA000001.txt", "/p/a"));
  CHECK(enricher.EnqueueIFile(0, kParent, "$IB000002.txt", "/p/b"));
  CHECK(enricher.EnqueueIFile(0, kParent, "$IC000003.txt", "/p/c"));
  CHECK(enricher.EnqueueSidMarker(kParent, "/p/sid-1"));
  CHECK(enricher.QueueDepth() == 4);
  CHECK(enricher.DropCount() == 0);

  CHECK_FALSE(enricher.EnqueueIFile(0, kParent, "$ID000004.txt", "/p/d"));
  CHECK_FALSE(enricher.EnqueueIFile(0, kParent, "$IE000005.txt", "/p/e"));
  CHECK_FALSE(enricher.EnqueueSidMarker(kParent, "/p/sid-2"));
  CHECK(enricher.QueueDepth() == 4);
  CHECK(enricher.DropCount() == 3);

  enricher.Stop();
  CHECK(enricher.QueueDepth() == 0);
  CHECK(enricher.DropCount() == 7);
}

TEST_CASE("CancelByPath removes a queued task and leaves the others") {
  RecycleBinMetadataStore store;
  RecycleBinEnricher enricher(&store);  // Not started.

  CHECK(enricher.EnqueueIFile(0, kParent, "$IA000001.txt", "/p/a"));
  CHECK(enricher.EnqueueIFile(0, kParent, "$IB000002.txt", "/p/b"));
  CHECK(enricher.CancelByPath("/p/a"));
  CHECK_FALSE(enricher.CancelByPath("/p/a"));
  CHECK_FALSE(enricher.CancelByPath("/p/unknown"));
  CHECK(enricher.QueueDepth() == 1);
  CHECK(enricher.DropCount() == 0);

  CHECK(enricher.EnqueueIFile(0, kParent, "$IA000001.txt", "/p/a"));
  CHECK(enricher.QueueDepth() == 2);
  enricher.Stop();
}

TEST_CASE("Duplicate paths coalesce without consuming queue slots") {
  RecycleBinMetadataStore store;
  RecycleBinEnricher enricher(&store, 4);  // Not started.

  CHECK(enricher.EnqueueIFile(0, kParent, "$IDUP0001.txt", "/p/dup"));
  CHECK(enricher.EnqueueIFile(0, kParent, "$IDUP0001.txt", "/p/dup"));
  CHECK(enricher.EnqueueIFile(0, kParent, "$IDUP0001.txt", "/p/dup"));
  CHECK(enricher.QueueDepth() == 1);
  CHECK(enricher.DropCount() == 0);

  CHECK(enricher.EnqueueIFile(0, kParent, "$IX000001.txt", "/p/x1"));
  CHECK(enricher.EnqueueIFile(0, kParent, "$IX000002.txt", "/p/x2"));
  CHECK(enricher.EnqueueIFile(0, kParent, "$IX000003.txt", "/p/x3"));
  CHECK(enricher.QueueDepth() == 4);
  CHECK_FALSE(enricher.EnqueueIFile(0, kParent, "$IX000004.txt", "/p/x4"));
  CHECK(enricher.DropCount() == 1);
  enricher.Stop();
}

TEST_CASE("SID marker expands into one task per $I and ignores the rest") {
  EnricherHarness h;
  const std::filesystem::path sid = h.dir.Path() / "S-1-5-21-1000";
  std::error_code ec;
  std::filesystem::create_directories(sid, ec);
  WriteBytes(sid / "$IAAA001.txt", MakeV2Record("C:\\one.txt", 1, kV2FileTime));
  WriteBytes(sid / "$IBBB002.bin", MakeV1Record("C:\\two.bin", 2, kV1FileTime));
  WriteBytes(sid / "$RAAA001.txt", {1, 2, 3});
  WriteBytes(sid / "desktop.ini", {'[', 'x', ']'});

  h.store.IngestRFile(MakeR(941, "$RAAA001.txt"));
  h.store.IngestRFile(MakeR(942, "$RBBB002.bin"));

  CHECK(h.enricher.EnqueueSidMarker(kParent, sid.string()));
  REQUIRE(h.Wait([&h] { return h.store.Current()->by_file_id.size() == 2; }));

  CHECK(h.store.Current()->by_file_id.count(941) == 1);
  CHECK(h.store.Current()->by_file_id.count(942) == 1);
  CHECK(h.store.Current()->by_file_id.size() == 2);
  CHECK(h.enricher.DropCount() == 0);
}

TEST_CASE("Stop with an empty queue is idempotent and refuses later enqueues") {
  RecycleBinMetadataStore store;
  RecycleBinEnricher enricher(&store);
  enricher.Start();
  enricher.Stop();
  enricher.Stop();
  CHECK(enricher.QueueDepth() == 0);
  CHECK(enricher.DropCount() == 0);

  CHECK_FALSE(enricher.EnqueueIFile(0, kParent, "$IZ000001.txt", "/p/z"));
  CHECK_FALSE(enricher.EnqueueSidMarker(kParent, "/p/sid"));
  CHECK(enricher.DropCount() == 2);
}

TEST_CASE("Destructor joins a running worker that still has queued work") {
  RecycleBinMetadataStore store;
  {
    RecycleBinEnricher enricher(&store);
    enricher.Start();
    CHECK(enricher.EnqueueIFile(0, kParent, "$IGONE001.txt", "/p/gone"));
    CHECK(enricher.EnqueueSidMarker(kParent, "/p/no-such-sid"));
  }
  CHECK(store.Version() == 0);
}

TEST_CASE("Zero capacity is raised to one and degenerate input never throws") {
  RecycleBinMetadataStore store;
  RecycleBinEnricher enricher(&store, 0);
  CHECK(enricher.EnqueueIFile(0, 0, "", ""));
  CHECK_FALSE(enricher.EnqueueIFile(0, 0, "", "/p/other"));
  CHECK(enricher.CancelByPath(""));
  CHECK(enricher.EnqueueSidMarker(0, "/no/such/sid-dir"));

  CHECK_NOTHROW(enricher.Start());
  CHECK_NOTHROW(enricher.Stop());
  CHECK_NOTHROW(static_cast<void>(enricher.QueueDepth()));
  CHECK_NOTHROW(static_cast<void>(enricher.DropCount()));
}

TEST_CASE("ReadRecycleIndexFile reports failures by kind and never throws") {
  RecycleFileBytes out;
  std::string kind;
  CHECK_FALSE(ReadRecycleIndexFile("/definitely/not/here/$IXYZ123.txt", out, kind));
  CHECK(kind == "not_found");
  CHECK(out.bytes.empty());

  TempDir dir;
  const std::filesystem::path small = dir.Path() / "$ISMALL01.txt";
  WriteBytes(small, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10});
  CHECK(ReadRecycleIndexFile(small.string(), out, kind));
  CHECK(out.bytes.size() == 10);
  CHECK(out.bytes.front() == 1);
  CHECK(out.bytes.back() == 10);
#ifndef _WIN32
  CHECK(out.file_frn == 0);
#endif  // _WIN32

  const std::filesystem::path huge = dir.Path() / "$IHUGE001.txt";
  WriteBytes(huge, std::vector<std::uint8_t>(kOversizeBytes, 0));
  CHECK_FALSE(ReadRecycleIndexFile(huge.string(), out, kind));
  CHECK(kind == "too_large");
  CHECK(out.bytes.empty());
}
