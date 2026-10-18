#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

#include "doctest/doctest.h"
#include "index/IndexDomainEvents.h"

using index_domain_events::ConsecutiveErrorsExceeded;
using index_domain_events::CorruptBufferTail;
using index_domain_events::FileCreated;
using index_domain_events::FileDeleted;
using index_domain_events::FileLifecycleEvent;
using index_domain_events::FileModified;
using index_domain_events::FileRenamed;
using index_domain_events::IntegrityEvent;
using index_domain_events::JournalIdChanged;
using index_domain_events::JournalLost;
using index_domain_events::JournalWrapped;
using index_domain_events::NeverArrivingEvicted;
using index_domain_events::ParentHealed;
using index_domain_events::QueueBuffersDropped;
using index_domain_events::RenameDivergence;

static_assert(std::variant_size_v<FileLifecycleEvent> == 4,
              "file-lifecycle variant covers create/delete/rename/modify");
static_assert(std::is_nothrow_move_constructible_v<FileLifecycleEvent>,
              "events must move without throwing across queue boundaries");

// Mirrors the production metrics subscriber (UsnMonitor::CountFileLifecycleEvent):
// one counter per alternative, dispatched with std::visit.
struct LocalCounters {
  std::atomic<size_t> created{0};
  std::atomic<size_t> deleted{0};
  std::atomic<size_t> renamed{0};
  std::atomic<size_t> modified{0};

  void Count(const FileLifecycleEvent& event) {
    std::visit(
        [this](const auto& file_event) {
          using Event = std::decay_t<decltype(file_event)>;
          if constexpr (std::is_same_v<Event, FileCreated>) {
            created.fetch_add(1);
          } else if constexpr (std::is_same_v<Event, FileDeleted>) {
            deleted.fetch_add(1);
          } else if constexpr (std::is_same_v<Event, FileRenamed>) {
            renamed.fetch_add(1);
          } else if constexpr (std::is_same_v<Event, FileModified>) {
            modified.fetch_add(1);
          }
        },
        event);
  }
};

TEST_SUITE("IndexDomainEvents - file lifecycle dispatch") {

  TEST_CASE("each alternative routes to its own counter") {
    LocalCounters counters;
    counters.Count(FileLifecycleEvent{FileCreated{7U}});
    counters.Count(FileLifecycleEvent{FileDeleted{8U}});
    counters.Count(FileLifecycleEvent{FileRenamed{9U}});
    counters.Count(FileLifecycleEvent{FileModified{10U}});
    counters.Count(FileLifecycleEvent{FileCreated{11U}});
    CHECK(counters.created.load() == 2U);
    CHECK(counters.deleted.load() == 1U);
    CHECK(counters.renamed.load() == 1U);
    CHECK(counters.modified.load() == 1U);
  }

  TEST_CASE("sink receives the published payload") {
    std::vector<FileLifecycleEvent> received;
    const index_domain_events::FileEventSink sink =
        [&received](const FileLifecycleEvent& event) { received.push_back(event); };
    sink(FileLifecycleEvent{FileDeleted{42U}});
    REQUIRE(received.size() == 1U);
    CHECK(std::holds_alternative<FileDeleted>(received[0]));
    CHECK(std::get<FileDeleted>(received[0]).id == 42U);
  }
}

TEST_SUITE("IndexDomainEvents - value semantics") {

  TEST_CASE("heal event carries child and parent") {
    constexpr ParentHealed healed{3U, 2U};
    CHECK(healed.child_id == 3U);
    CHECK(healed.parent_id == 2U);
  }

  TEST_CASE("evict event carries id and wait age") {
    constexpr NeverArrivingEvicted evicted{5U, 300000U};
    CHECK(evicted.id == 5U);
    CHECK(evicted.age_ms == 300000U);
  }
}

static_assert(std::variant_size_v<IntegrityEvent> == 7,
              "integrity variant covers wrap/id-change/loss/drop/corrupt/errors/divergence");
static_assert(std::is_nothrow_move_constructible_v<IntegrityEvent>,
              "integrity events must move without throwing across thread handoff");

// Mirrors the forensics subscriber (UsnMonitor::SetIntegrityEventSink): one
// counter per cause, dispatched with std::visit. Guards the latch/event
// pairing — every latch site publishes exactly one alternative.
struct IntegrityCounters {
  std::atomic<size_t> wrapped{0};
  std::atomic<size_t> id_changed{0};
  std::atomic<size_t> lost{0};
  std::atomic<size_t> dropped{0};
  std::atomic<size_t> corrupt{0};
  std::atomic<size_t> errors{0};
  std::atomic<size_t> divergence{0};

  void Count(const IntegrityEvent& event) {
    std::visit(
        [this](const auto& integrity_event) {
          using Event = std::decay_t<decltype(integrity_event)>;
          if constexpr (std::is_same_v<Event, JournalWrapped>) {
            wrapped.fetch_add(1);
          } else if constexpr (std::is_same_v<Event, JournalIdChanged>) {
            id_changed.fetch_add(1);
          } else if constexpr (std::is_same_v<Event, JournalLost>) {
            lost.fetch_add(1);
          } else if constexpr (std::is_same_v<Event, QueueBuffersDropped>) {
            dropped.fetch_add(1);
          } else if constexpr (std::is_same_v<Event, CorruptBufferTail>) {
            corrupt.fetch_add(1);
          } else if constexpr (std::is_same_v<Event, ConsecutiveErrorsExceeded>) {
            errors.fetch_add(1);
          } else if constexpr (std::is_same_v<Event, RenameDivergence>) {
            divergence.fetch_add(1);
          }
        },
        event);
  }
};

TEST_SUITE("IndexDomainEvents - integrity cause dispatch") {

  TEST_CASE("each cause routes to its own counter") {
    IntegrityCounters counters;
    counters.Count(IntegrityEvent{JournalWrapped{100, 200}});
    counters.Count(IntegrityEvent{JournalIdChanged{7U, 8U}});
    counters.Count(IntegrityEvent{JournalLost{0xDEADU}});
    counters.Count(IntegrityEvent{QueueBuffersDropped{50U, 3U}});
    counters.Count(IntegrityEvent{CorruptBufferTail{128U}});
    counters.Count(IntegrityEvent{ConsecutiveErrorsExceeded{100U}});
    counters.Count(IntegrityEvent{RenameDivergence{42U}});
    CHECK(counters.wrapped.load() == 1U);
    CHECK(counters.id_changed.load() == 1U);
    CHECK(counters.lost.load() == 1U);
    CHECK(counters.dropped.load() == 1U);
    CHECK(counters.corrupt.load() == 1U);
    CHECK(counters.errors.load() == 1U);
    CHECK(counters.divergence.load() == 1U);
  }

  TEST_CASE("sink receives the published payload with cause") {
    std::vector<IntegrityEvent> received;
    const index_domain_events::IntegrityEventSink sink =
        [&received](const IntegrityEvent& event) { received.push_back(event); };
    sink(IntegrityEvent{JournalWrapped{100, 200}});
    REQUIRE(received.size() == 1U);
    REQUIRE(std::holds_alternative<JournalWrapped>(received[0]));
    CHECK(std::get<JournalWrapped>(received[0]).expected_usn == 100);
    CHECK(std::get<JournalWrapped>(received[0]).lowest_valid_usn == 200);
  }

  TEST_CASE("null sink disables publication (guarded publish convention)") {
    const index_domain_events::IntegrityEventSink sink;
    CHECK_FALSE(static_cast<bool>(sink));
  }
}
