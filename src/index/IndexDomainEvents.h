#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <variant>

namespace index_domain_events {

// Domain events published by the volume-state index (see the glossary,
// docs/design/USN_MFT_UBIQUITOUS_LANGUAGE.md). Value types: equality by
// value, no behavior. Sinks are plain callbacks following the existing
// PathHashRefreshCallback pattern; a null sink disables publication.
//
// Threading (per family — not all events share one context):
// - Under the FileIndex unique_lock (processor thread or test): file
//   lifecycle events, ParentHealed, NeverArrivingEvicted, RenameDivergence.
//   Sinks must not re-enter the index.
// - Lock-free: JournalWrapped / JournalIdChanged (population thread),
//   JournalLost / ConsecutiveErrorsExceeded / QueueBuffersDropped (reader
//   thread), CorruptBufferTail (processor thread, after the per-buffer lock
//   is released). No index lock is held, but sinks must still not take one
//   (see the lock-ordering rule below).
// In both cases: no I/O, no other mutex (FileIndex / SearchThreadPool /
// UsnMonitor / UsnJournalQueue ordering rule), no index mutation. If a
// future subscriber needs heavy work, it must queue the event and drain it
// off the publishing thread instead of subscribing synchronously.
//
// Exceptions: sinks must not throw. Under-lock sinks run mid-mutation (after
// the bucket swap/erase on heal, inside the evict loop); a throw would abort
// the cascade and strand entries. Keep sinks to non-throwing work (atomics,
// moves, appends with reserved capacity).

// An awaiting child re-resolved on parent arrival (heal path). Skipped
// entries (removed, moved away, pathless) are not heals and emit nothing.
struct ParentHealed {
  uint64_t child_id = 0;
  uint64_t parent_id = 0;
};

// A bare-name placeholder evicted after waiting at least max_age_ms for a
// parent that never arrives (filtered, dropped, deleted before CREATE).
// usn_at_track is the journal position of the tracking record (0 = unknown);
// eviction forensics correlate it with USN logs in journal order.
struct NeverArrivingEvicted {
  uint64_t id = 0;
  uint64_t age_ms = 0;
  int64_t usn_at_track = 0;
};

using ParentHealedSink = std::function<void(const ParentHealed&)>;
using NeverArrivingEvictedSink = std::function<void(const NeverArrivingEvicted&)>;

// File-lifecycle facts, one per applied record. Published instead of bare
// counter increments so forensics and metrics share one mechanism; the
// metrics object subscribes its files_* counters (see UsnMonitor).
struct FileCreated {
  uint64_t id = 0;
};
struct FileDeleted {
  uint64_t id = 0;
};
struct FileRenamed {
  uint64_t id = 0;
};
struct FileModified {
  uint64_t id = 0;
};

using FileLifecycleEvent = std::variant<FileCreated, FileDeleted, FileRenamed, FileModified>;
using FileEventSink = std::function<void(const FileLifecycleEvent&)>;

// Integrity facts: the journal is lossy, so every set-once latch carries its
// cause instead of a bare "compromised" flag. Published alongside (not
// instead of) the latch + log in UsnMonitor, so forensics can distinguish
// Wrap{expected, lowestValid} vs QueueDrop{depth} vs CorruptTail{offset}.
// Same threading/exception contract as above: published under lock or on the
// reader/processor thread; sinks must not re-enter the index and must not
// throw. A null sink disables publication.
struct JournalWrapped {
  int64_t expected_usn = 0;
  int64_t lowest_valid_usn = 0;
};
struct JournalIdChanged {
  uint64_t expected_id = 0;
  uint64_t observed_id = 0;
};
struct JournalLost {
  uint32_t error_code = 0;  // Windows error: JOURNAL_ENTRY_DELETED / NOT_ACTIVE / DELETE_IN_PROGRESS
};
struct QueueBuffersDropped {
  uint64_t dropped_total = 0;
  size_t queue_depth = 0;
};
struct CorruptBufferTail {
  uint32_t offset = 0;  // byte offset of the first corrupt record in the buffer
};
struct ConsecutiveErrorsExceeded {
  size_t consecutive_errors = 0;
};
struct RenameDivergence {
  uint64_t file_reference_number = 0;
};

using IntegrityEvent = std::variant<JournalWrapped, JournalIdChanged, JournalLost,
                                    QueueBuffersDropped, CorruptBufferTail,
                                    ConsecutiveErrorsExceeded, RenameDivergence>;
using IntegrityEventSink = std::function<void(const IntegrityEvent&)>;

}  // namespace index_domain_events
