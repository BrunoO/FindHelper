#include <array>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "doctest/doctest.h"
#include "index/recycle/RecycleBinMetadataStore.h"

using recycle_bin::EvictKind;
using recycle_bin::EvictOp;
using recycle_bin::IFileInfo;
using recycle_bin::RecycleBinMetadataStore;
using recycle_bin::RecycleFileMetadata;
using recycle_bin::RecycleNamePrefix;
using recycle_bin::RFileInfo;
using recycle_bin::TokenizeRecycleName;

namespace {

constexpr std::uint64_t kParent = 5;

RFileInfo MakeR(std::uint64_t r_frn, std::string name, bool is_directory = false) {
  RFileInfo info;
  info.r_frn = r_frn;
  info.parent_frn = kParent;
  info.name = std::move(name);
  info.is_directory = is_directory;
  return info;
}

IFileInfo MakeI(std::uint64_t i_frn, std::string name, std::string original_path, std::uint64_t size = 7,
                std::uint64_t filetime = 42) {
  IFileInfo info;
  info.i_frn = i_frn;
  info.parent_frn = kParent;
  info.name = std::move(name);
  info.meta.original_path = std::move(original_path);
  info.meta.original_name = info.meta.original_path.substr(info.meta.original_path.find_last_of('\\') + 1);
  info.meta.original_size = size;
  info.meta.deletion_time_filetime = filetime;
  return info;
}

}  // namespace

TEST_CASE("TokenizeRecycleName: tokenization and rejection rules") {
  struct Case {
    const char* input = "";
    bool valid = false;
    RecycleNamePrefix prefix = RecycleNamePrefix::kR;
    const char* token = "";
    const char* ext = "";
  };
  constexpr std::array<Case, 15> kCases = {{
      {"$R1A2B3C.docx", true, RecycleNamePrefix::kR, "1A2B3C", "docx"},
      {"$i1a2b3c", true, RecycleNamePrefix::kI, "1a2b3c", ""},
      {"$I1A2B3C.txt", true, RecycleNamePrefix::kI, "1A2B3C", "txt"},
      {"$r1A2B3C.txt", true, RecycleNamePrefix::kR, "1A2B3C", "txt"},
      {"$R1A2B3C.tar.gz", true, RecycleNamePrefix::kR, "1A2B3C", "tar.gz"},
      {"$R1A2B3C.", true, RecycleNamePrefix::kR, "1A2B3C", ""},
      {"$R1", true, RecycleNamePrefix::kR, "1", ""},
      {"", false, RecycleNamePrefix::kR, "", ""},
      {"$I", false, RecycleNamePrefix::kR, "", ""},
      {"$R", false, RecycleNamePrefix::kR, "", ""},
      {"$I.", false, RecycleNamePrefix::kR, "", ""},
      {"$R.txt", false, RecycleNamePrefix::kR, "", ""},
      {"report.docx", false, RecycleNamePrefix::kR, "", ""},
      {"R1A2B3C.docx", false, RecycleNamePrefix::kR, "", ""},
      {"$X1A2B3C.docx", false, RecycleNamePrefix::kR, "", ""},
  }};
  for (const auto& c : kCases) {
    const auto res = TokenizeRecycleName(c.input);
    CHECK(res.has_value() == c.valid);
    if (res.has_value()) {
      CHECK(res->prefix == c.prefix);
      CHECK(res->token == c.token);
      CHECK(res->extension == c.ext);
    }
  }
}

TEST_CASE("Recycle pairing order and orphan handling") {
  SUBCASE("R-then-I pairs into one published entry") {
    RecycleBinMetadataStore store;
    store.IngestRFiles({MakeR(100, "$RABC123.txt", true)});
    CHECK(store.Version() == 0);
    store.PublishIEntries({MakeI(200, "$IABC123.txt", "C:\\Users\\me\\a.txt", 7, 42)});
    CHECK(store.Version() == 1);
    const auto snap = store.Current();
    CHECK(snap->generation == 1);
    REQUIRE(snap->by_file_id.count(100) == 1);
    const RecycleFileMetadata& meta = snap->by_file_id.at(100);
    CHECK(meta.original_name == "a.txt");
    CHECK(meta.original_size == 7);
    CHECK(meta.deletion_time_filetime == 42);
    CHECK(meta.original_path == "C:\\Users\\me\\a.txt");
    CHECK(meta.is_directory);
  }
  SUBCASE("I-then-R pairs into one published entry") {
    RecycleBinMetadataStore store;
    store.PublishIEntries({MakeI(200, "$IABC123.txt", "C:\\b.txt")});
    CHECK(store.Version() == 0);
    CHECK(store.Current()->by_file_id.empty());
    store.IngestRFiles({MakeR(100, "$RABC123.txt")});
    CHECK(store.Version() == 1);
    const auto snap = store.Current();
    REQUIRE(snap->by_file_id.size() == 1);
    CHECK(snap->by_file_id.at(100).original_path == "C:\\b.txt");
  }
  SUBCASE("Orphan $R alone publishes no entry") {
    RecycleBinMetadataStore store;
    store.IngestRFiles({MakeR(100, "$RABC123.txt")});
    CHECK(store.Version() == 0);
    CHECK(store.Current()->by_file_id.empty());
  }
  SUBCASE("Orphan $I pends and kByI eviction clears it before later $R") {
    RecycleBinMetadataStore store;
    store.PublishIEntries({MakeI(200, "$IABC123.txt", "C:\\a.txt")});
    CHECK(store.Version() == 0);
    CHECK(store.Current()->by_file_id.empty());
    store.EvictMany({EvictOp{EvictKind::kByI, 200}});
    CHECK(store.Version() == 0);
    store.IngestRFiles({MakeR(100, "$RABC123.txt")});
    CHECK(store.Version() == 0);
    CHECK(store.Current()->by_file_id.empty());
  }
}

TEST_CASE("Eviction semantics and batching") {
  SUBCASE("Each eviction path removes entry and bumps version") {
    RecycleBinMetadataStore store;
    store.IngestRFiles(
        {MakeR(100, "$RAAAAAA.txt"), MakeR(101, "$RBBBBBB.txt"), MakeR(102, "$RCCCCCC.txt")});
    store.PublishIEntries({MakeI(200, "$IAAAAAA.txt", "C:\\a"), MakeI(201, "$IBBBBBB.txt", "C:\\b"),
                           MakeI(202, "$ICCCCCC.txt", "C:\\c")});
    REQUIRE(store.Version() == 1);
    REQUIRE(store.Current()->by_file_id.size() == 3);

    store.EvictMany({EvictOp{EvictKind::kByR, 100}});
    CHECK(store.Version() == 2);
    CHECK(store.Current()->by_file_id.size() == 2);
    CHECK(store.Current()->by_file_id.count(100) == 0);

    store.EvictMany({EvictOp{EvictKind::kByI, 201}});
    CHECK(store.Version() == 3);
    CHECK(store.Current()->by_file_id.count(101) == 0);
    CHECK(store.Current()->by_file_id.size() == 1);

    store.EvictMany({EvictOp{EvictKind::kRenamedOut, 102}});
    CHECK(store.Version() == 4);
    CHECK(store.Current()->by_file_id.empty());
  }
  SUBCASE("No-op evictions and empty batches bump nothing") {
    RecycleBinMetadataStore store;
    store.IngestRFiles({MakeR(100, "$RAAAAAA.txt")});
    store.PublishIEntries({MakeI(200, "$IAAAAAA.txt", "C:\\a")});
    const std::uint64_t before = store.Version();
    REQUIRE(before == 1);

    store.EvictMany({EvictOp{EvictKind::kByR, 999}});
    store.EvictMany({EvictOp{EvictKind::kByI, 888}});
    store.EvictMany({EvictOp{EvictKind::kRenamedOut, 777}});
    store.EvictMany({});
    CHECK(store.Version() == before);
    CHECK(store.Current()->by_file_id.size() == 1);
  }
  SUBCASE("EvictMany applies a batch in one publish") {
    RecycleBinMetadataStore store;
    store.IngestRFiles({MakeR(100, "$RAAAAAA.txt"), MakeR(101, "$RBBBBBB.txt")});
    store.PublishIEntries({MakeI(200, "$IAAAAAA.txt", "C:\\a"), MakeI(201, "$IBBBBBB.txt", "C:\\b")});
    REQUIRE(store.Version() == 1);

    store.EvictMany({EvictOp{EvictKind::kByR, 100}, EvictOp{EvictKind::kByI, 201}});
    CHECK(store.Version() == 2);
    CHECK(store.Current()->by_file_id.empty());
  }
}

TEST_CASE("Re-deletion and metadata update semantics") {
  SUBCASE("Re-delete reuses a fileId with new metadata winning") {
    RecycleBinMetadataStore store;
    store.IngestRFiles({MakeR(100, "$RAAAAAA.txt")});
    store.PublishIEntries({MakeI(200, "$IAAAAAA.txt", "C:\\old.txt")});
    REQUIRE(store.Version() == 1);

    store.EvictMany({EvictOp{EvictKind::kByR, 100}});
    REQUIRE(store.Version() == 2);

    store.IngestRFiles({MakeR(100, "$RZZZZZZ.txt")});
    store.PublishIEntries({MakeI(201, "$IZZZZZZ.txt", "C:\\new.txt", 99, 1234)});
    CHECK(store.Version() == 3);
    const auto snap = store.Current();
    REQUIRE(snap->by_file_id.size() == 1);
    CHECK(snap->by_file_id.at(100).original_path == "C:\\new.txt");
    CHECK(snap->by_file_id.at(100).original_size == 99);
  }
  SUBCASE("Re-ingest of published fileId under new key replaces stale row without evict") {
    RecycleBinMetadataStore store;
    store.IngestRFiles({MakeR(100, "$RAAAAAA.txt")});
    store.PublishIEntries({MakeI(200, "$IAAAAAA.txt", "C:\\old.txt")});
    REQUIRE(store.Version() == 1);

    store.IngestRFiles({MakeR(100, "$RZZZZZZ.txt")});
    CHECK(store.Version() == 2);
    CHECK(store.Current()->by_file_id.empty());

    store.PublishIEntries({MakeI(201, "$IZZZZZZ.txt", "C:\\new.txt", 99, 1234)});
    CHECK(store.Version() == 3);
    const auto snap = store.Current();
    REQUIRE(snap->by_file_id.size() == 1);
    CHECK(snap->by_file_id.at(100).original_path == "C:\\new.txt");
    CHECK(snap->by_file_id.at(100).original_size == 99);

    store.PublishIEntries({MakeI(200, "$IAAAAAA.txt", "C:\\old.txt")});
    CHECK(store.Version() == 3);
    CHECK(store.Current()->by_file_id.size() == 1);
  }
  SUBCASE("kByR eviction on a pending $R stops later matching $I from publishing") {
    RecycleBinMetadataStore store;
    store.IngestRFiles({MakeR(100, "$RAAAAAA.txt")});
    CHECK(store.Version() == 0);
    store.EvictMany({EvictOp{EvictKind::kByR, 100}});
    CHECK(store.Version() == 0);
    store.PublishIEntries({MakeI(200, "$IAAAAAA.txt", "C:\\a.txt")});
    CHECK(store.Version() == 0);
    CHECK(store.Current()->by_file_id.empty());
  }
  SUBCASE("Re-reports of unchanged pair publish nothing; changed metadata refreshes") {
    RecycleBinMetadataStore store;
    store.IngestRFiles({MakeR(100, "$RAAAAAA.txt", true)});
    store.PublishIEntries({MakeI(200, "$IAAAAAA.txt", "C:\\dir", 7, 42)});
    REQUIRE(store.Version() == 1);

    store.PublishIEntries({MakeI(200, "$IAAAAAA.txt", "C:\\dir", 7, 42)});
    store.IngestRFiles({MakeR(100, "$RAAAAAA.txt", true)});
    CHECK(store.Version() == 1);

    store.PublishIEntries({MakeI(200, "$IAAAAAA.txt", "C:\\dir", 8, 42)});
    CHECK(store.Version() == 2);
    const auto snap = store.Current();
    CHECK(snap->by_file_id.at(100).original_size == 8);
    CHECK(snap->by_file_id.at(100).is_directory);
  }
}

TEST_CASE("Store reset, immutability, and prefix filtering") {
  SUBCASE("Non-matching prefixes are ignored") {
    RecycleBinMetadataStore store;
    store.IngestRFiles({MakeR(100, "$IAAAAAA.txt")});
    store.PublishIEntries({MakeI(200, "$RAAAAAA.txt", "C:\\a")});
    store.IngestRFiles({MakeR(101, "report.docx")});
    CHECK(store.Version() == 0);

    store.IngestRFiles({MakeR(102, "$RAAAAAA.txt")});
    CHECK(store.Version() == 0);
    CHECK(store.Current()->by_file_id.empty());
  }
  SUBCASE("Published snapshots stay immutable across later changes") {
    RecycleBinMetadataStore store;
    store.IngestRFiles({MakeR(100, "$RAAAAAA.txt")});
    store.PublishIEntries({MakeI(200, "$IAAAAAA.txt", "C:\\a")});
    const auto before = store.Current();
    REQUIRE(before->by_file_id.size() == 1);

    store.EvictMany({EvictOp{EvictKind::kByR, 100}});
    CHECK(before->generation == 1);
    CHECK(before->by_file_id.size() == 1);
    CHECK(before->by_file_id.count(100) == 1);
    CHECK(store.Current()->generation == 2);
    CHECK(store.Current()->by_file_id.empty());
  }
  SUBCASE("Reset clears published state and pending halves; empty reset is no-op") {
    RecycleBinMetadataStore store;
    store.Reset();
    CHECK(store.Version() == 0);

    store.IngestRFiles({MakeR(100, "$RCCCCCC.txt")});
    store.PublishIEntries({MakeI(200, "$ICCCCCC.txt", "C:\\c")});
    store.IngestRFiles({MakeR(300, "$RAAAAAA.txt")});
    store.PublishIEntries({MakeI(301, "$IBBBBBB.txt", "C:\\b")});
    REQUIRE(store.Version() == 1);

    store.Reset();
    CHECK(store.Version() == 2);
    CHECK(store.Current()->by_file_id.empty());

    store.IngestRFiles({MakeR(302, "$RBBBBBB.txt")});
    CHECK(store.Version() == 2);
    store.PublishIEntries({MakeI(303, "$IAAAAAA.txt", "C:\\a")});
    CHECK(store.Version() == 2);
    CHECK(store.Current()->by_file_id.empty());
  }
}

TEST_CASE("IngestRFiles batch processing and deduplication") {
  SUBCASE("One IngestRFiles batch pairs both report orders") {
    RecycleBinMetadataStore store;
    store.PublishIEntries({MakeI(200, "$IAAAAAA.txt", "C:\\a.txt")});
    CHECK(store.Version() == 0);

    store.IngestRFiles({MakeR(101, "$RBBBBBB.txt"), MakeR(100, "$RAAAAAA.txt")});
    CHECK(store.Version() == 1);
    const auto after_batch = store.Current();
    REQUIRE(after_batch->by_file_id.size() == 1);
    CHECK(after_batch->by_file_id.count(100) == 1);
    CHECK(after_batch->by_file_id.count(101) == 0);

    store.PublishIEntries({MakeI(201, "$IBBBBBB.txt", "C:\\b.txt")});
    CHECK(store.Version() == 2);
    const auto snap = store.Current();
    REQUIRE(snap->by_file_id.size() == 2);
    REQUIRE(snap->by_file_id.count(100) == 1);
    REQUIRE(snap->by_file_id.count(101) == 1);
    CHECK(snap->by_file_id.at(100).original_path == "C:\\a.txt");
    CHECK(snap->by_file_id.at(101).original_path == "C:\\b.txt");
  }
  SUBCASE("Resolving many pairs bumps version once; empty batch leaves version untouched") {
    RecycleBinMetadataStore store;
    store.PublishIEntries({MakeI(200, "$IAAAAAA.txt", "C:\\a.txt"), MakeI(201, "$IBBBBBB.txt", "C:\\b.txt"),
                           MakeI(202, "$ICCCCCC.txt", "C:\\c.txt")});
    CHECK(store.Version() == 0);

    store.IngestRFiles({MakeR(100, "$RAAAAAA.txt"), MakeR(101, "$RBBBBBB.txt"), MakeR(102, "$RCCCCCC.txt")});
    CHECK(store.Version() == 1);
    const auto snap = store.Current();
    CHECK(snap->generation == 1);
    REQUIRE(snap->by_file_id.size() == 3);

    store.PublishIEntries({MakeI(203, "$IDDDDDD.txt", "D:\\d.txt"), MakeI(204, "$IEEEEEE.txt", "D:\\e.txt")});
    store.IngestRFiles({MakeR(103, "$RDDDDDD.txt"), MakeR(104, "$REEEEEE.txt")});
    CHECK(store.Version() == 2);
    CHECK(store.Current()->by_file_id.size() == 5);

    const auto before = store.Current();
    store.IngestRFiles({});
    CHECK(store.Version() == 2);
    CHECK(store.Current().get() == before.get());
  }
  SUBCASE("Repeated R-identity is deduped; distinct keys and duplicates handled cleanly") {
    RecycleBinMetadataStore store;
    store.PublishIEntries({MakeI(200, "$IAAAAAA.txt", "C:\\a.txt")});
    store.IngestRFiles({MakeR(100, "$RAAAAAA.txt"), MakeR(100, "$RBBBBBB.txt")});
    CHECK(store.Version() == 1);
    REQUIRE(store.Current()->by_file_id.size() == 1);
    CHECK(store.Current()->by_file_id.at(100).original_path == "C:\\a.txt");

    store.PublishIEntries({MakeI(201, "$IBBBBBB.txt", "C:\\b.txt")});
    CHECK(store.Version() == 2);
    REQUIRE(store.Current()->by_file_id.size() == 1);
    CHECK(store.Current()->by_file_id.at(100).original_path == "C:\\b.txt");

    store.IngestRFiles({MakeR(100, "$RBBBBBB.txt")});
    CHECK(store.Version() == 2);

    store.IngestRFiles({MakeR(300, "$RCCCCCC.txt"), MakeR(300, "$RCCCCCC.txt")});
    store.PublishIEntries({MakeI(301, "$ICCCCCC.txt", "C:\\c.txt")});
    CHECK(store.Version() == 3);
    CHECK(store.Current()->by_file_id.at(300).original_path == "C:\\c.txt");

    store.PublishIEntries({MakeI(400, "$IDDDDDD.txt", "C:\\d.txt")});
    store.IngestRFiles({MakeR(401, "$RDDDDDD.txt"), MakeR(401, "$RDDDDDD.txt")});
    CHECK(store.Version() == 4);

    store.PublishIEntries({MakeI(402, "$IDDDDDD.txt", "C:\\d2.txt")});
    CHECK(store.Version() == 4);
    CHECK(store.Current()->by_file_id.at(401).original_path == "C:\\d.txt");

    store.IngestRFiles({MakeR(310, "$REEEEEE.txt"), MakeR(310, "$RFFFFFF.txt")});
    CHECK(store.Version() == 4);
    store.EvictMany({EvictOp{EvictKind::kByR, 310}});
    CHECK(store.Version() == 4);
  }
}
