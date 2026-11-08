#include <chrono>
#include <condition_variable>
#include <string>
#include <thread>
#include <vector>

#include "doctest/doctest.h"
#include "utils/Synchronized.h"

TEST_SUITE("Synchronized") {
  TEST_CASE("DefaultConstructible") {
    utils::Synchronized<std::vector<int>> sync_vec;
    sync_vec.WithLock([](auto& vec) {
      CHECK(vec.empty());
      vec.push_back(42);
      CHECK(vec.size() == 1U);
    });

    auto guard = sync_vec.Lock();
    CHECK(guard->size() == 1U);
    CHECK((*guard)[0] == 42);
  }

  TEST_CASE("ValueConstructible") {
    utils::Synchronized<std::string> sync_str("initial_value");
    CHECK(sync_str.WithLock([](const auto& str) { return str; }) == "initial_value");

    sync_str.WithLock([](auto& str) { str += "_modified"; });
    CHECK(sync_str.Lock().Get() == "initial_value_modified");
    CHECK(*sync_str.Lock() == "initial_value_modified");
  }

  TEST_CASE("ConstAccess") {
    const utils::Synchronized<int> const_sync(100);
    {
      const auto guard = const_sync.Lock();
      CHECK(*guard == 100);
      CHECK(guard.Get() == 100);
    }

    const int val = const_sync.WithLock([](const int& v) { return v * 2; });
    CHECK(val == 200);
  }

  TEST_CASE("ConcurrentContention") {
    utils::Synchronized<int> counter(0);
    constexpr int kThreads = 8;
    constexpr int kIncrementsPerThread = 500;

    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    for (int i = 0; i < kThreads; ++i) {
      threads.emplace_back([&counter] {
        for (int j = 0; j < kIncrementsPerThread; ++j) {
          counter.WithLock([](int& c) { ++c; });
        }
      });
    }

    for (auto& t : threads) {
      t.join();
    }

    CHECK(counter.WithLock([](const int& c) { return c; }) == kThreads * kIncrementsPerThread);
  }

  TEST_CASE("ConditionVariableIntegration") {
    utils::Synchronized<std::vector<int>> queue;
    std::condition_variable cv;
    bool consumer_done = false;

    std::thread consumer([&] {
      auto guard = queue.Lock();
      cv.wait(guard.GetLock(), [&] { return !guard->empty(); });
      CHECK(guard->front() == 999);
      guard->pop_back();
      consumer_done = true;
    });

    // Give consumer a moment to enter wait
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    {
      auto guard = queue.Lock();
      guard->push_back(999);
    }
    cv.notify_one();

    consumer.join();
    CHECK(consumer_done);
    CHECK(queue.WithLock([](const auto& q) { return q.empty(); }));
  }
}
