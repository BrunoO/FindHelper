#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <thread>
#include <vector>

#include "usn/UsnActivityTracker.h"

TEST_CASE("UsnActivityTracker - Record and GetSnapshot") {
  UsnActivityTracker tracker(10);
  CHECK(tracker.Size() == 0);

  tracker.RecordChange(UsnChangeType::Created, "C:\\test\\file1.txt");
  tracker.RecordChange(UsnChangeType::Modified, "C:\\test\\file1.txt");
  tracker.RecordChange(UsnChangeType::Renamed, "C:\\test\\file2.txt", "C:\\test\\file1.txt");
  tracker.RecordChange(UsnChangeType::Deleted, "C:\\test\\file2.txt");

  CHECK(tracker.Size() == 4);

  const auto snapshot = tracker.GetSnapshot();
  REQUIRE(snapshot.size() == 4);

  CHECK(snapshot[0].change_type == UsnChangeType::Created);
  CHECK(snapshot[0].path == "C:\\test\\file1.txt");

  CHECK(snapshot[1].change_type == UsnChangeType::Modified);
  CHECK(snapshot[1].path == "C:\\test\\file1.txt");

  CHECK(snapshot[2].change_type == UsnChangeType::Renamed);
  CHECK(snapshot[2].path == "C:\\test\\file2.txt");
  CHECK(snapshot[2].old_path == "C:\\test\\file1.txt");

  CHECK(snapshot[3].change_type == UsnChangeType::Deleted);
  CHECK(snapshot[3].path == "C:\\test\\file2.txt");
}

TEST_CASE("UsnActivityTracker - Capacity limits and ring eviction") {
  UsnActivityTracker tracker(3);

  tracker.RecordChange(UsnChangeType::Created, "C:\\item1.txt");
  tracker.RecordChange(UsnChangeType::Created, "C:\\item2.txt");
  tracker.RecordChange(UsnChangeType::Created, "C:\\item3.txt");
  CHECK(tracker.Size() == 3);

  // Exceed capacity
  tracker.RecordChange(UsnChangeType::Created, "C:\\item4.txt");
  CHECK(tracker.Size() == 3);

  const auto snapshot = tracker.GetSnapshot();
  REQUIRE(snapshot.size() == 3);
  // Earliest item ("item1.txt") should have been evicted
  CHECK(snapshot[0].path == "C:\\item2.txt");
  CHECK(snapshot[1].path == "C:\\item3.txt");
  CHECK(snapshot[2].path == "C:\\item4.txt");
}

TEST_CASE("UsnActivityTracker - Clear") {
  UsnActivityTracker tracker(5);
  tracker.RecordChange(UsnChangeType::Created, "C:\\file.txt");
  CHECK(tracker.Size() == 1);

  tracker.Clear();
  CHECK(tracker.Size() == 0);
  CHECK(tracker.GetSnapshot().empty());
}

TEST_CASE("UsnActivityTracker - Empty path ignored") {
  UsnActivityTracker tracker(5);
  tracker.RecordChange(UsnChangeType::Created, "", "");
  CHECK(tracker.Size() == 0);
}

TEST_CASE("UsnActivityTracker - Concurrent writes") {
  UsnActivityTracker tracker(100);

  constexpr int kNumThreads = 4;
  constexpr int kWritesPerThread = 25;

  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);

  for (int t = 0; t < kNumThreads; ++t) {
    threads.emplace_back([&tracker, t]() {
      for (int i = 0; i < kWritesPerThread; ++i) {
        tracker.RecordChange(UsnChangeType::Modified, "C:\\thread_" + std::to_string(t) + ".txt");
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  CHECK(tracker.Size() == kNumThreads * kWritesPerThread);
}
