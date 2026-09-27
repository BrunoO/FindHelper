#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "imgui.h"
#include "utils/FileTimeTypes.h"

// A token to signal cancellation to running sort attribute loading tasks.
// This prevents tasks from continuing to run when a new sort is requested.
struct SortCancellationToken {
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like token; camelCase matches surrounding UI/token style
  std::shared_ptr<std::atomic<bool>> cancelled = std::make_shared<std::atomic<bool>>(false);

  // NOLINTNEXTLINE(readability-make-member-function-const) - False positive: not libc code; Cancel() modifies state
  void Cancel() {
    if (cancelled) {
      cancelled->store(true);
    }
  }

  [[nodiscard]] bool IsCancelled() const {
    return cancelled && cancelled->load();
  }
};

// A generation counter to uniquely identify each sort operation.
// This prevents race conditions where results from an old (and superseded)
// sort operation could overwrite the results of a newer one.
using SortGeneration = uint64_t;

// Three-way state machine for an async sort operation.
// Replaces the previous dual-bool (sortDataReady + loading) that allowed
// illegal combinations. Illegal combinations are now unrepresentable.
//
//  Idle    — no sort in progress; counter null, no visible status bar message.
//  Loading — tasks are running; counter > 0; "Loading attributes..." shown.
//  Ready   — tasks complete or nothing to load; sort can proceed this frame.
enum class SortReadyState : uint8_t {
  Idle,     // No sort in progress.
  Loading,  // Tasks are running; counter > 0.
  Ready,    // Tasks complete or pre-loaded; apply + sort can proceed.
};

// All state for one async sort operation: cancellation token, countdown counter,
// ready-state machine, and staging vectors that workers write under mutex_.
//
// PrefetchAndFormatSortDataBlocking allocates a local AsyncSortState to reuse
// StartAttributeLoadingAsync without constructing a full GuiState.
//
// Lifetime rules:
//  - counter, sort_ready_state_, token — UI-thread-only; no mutex required.
//  - sizes_, times_, flags_ — written by worker threads under mutex_.
//  - generation_ — incremented only by BeginNewSort() on the UI thread; never
//    zeroed; monotonically increasing.
//  - staging_generation_ — tag written when staging buffers are (re)allocated;
//    ApplyPendingSortAttributeUpdates rejects values when this does not match the
//    sort_generation passed from the UI (superseded sort).
struct AsyncSortState {
  // Countdown latch: initialised to task_count before enqueueing; each task
  // decrements on completion (including cancellation). Null when idle.
  // NOLINTNEXTLINE(readability-identifier-naming)
  std::shared_ptr<std::atomic<int>> counter;

  // Three-way state machine replacing the previous dual-bool (sortDataReady + loading).
  // NOLINTNEXTLINE(readability-identifier-naming)
  SortReadyState sort_ready_state_ = SortReadyState::Idle;

  // Cancellation token shared with worker tasks via pointer in SortAttributeEnqueueContext.
  // Replaced (not mutated) by BeginNewSort() — always after draining old tasks.
  // NOLINTNEXTLINE(readability-identifier-naming)
  SortCancellationToken token;

  // Canonical sort-operation counter (BeginNewSort only).
  // NOLINTNEXTLINE(readability-identifier-naming)
  SortGeneration generation_ = 0;

  // Generation tag for the current staging buffer contents (set when buffers are
  // prepared; may differ from generation_ only in tests that simulate stale staging).
  // NOLINTNEXTLINE(readability-identifier-naming)
  SortGeneration staging_generation_ = 0;

  // Last user-requested sort specification (ResultsTable header click via
  // HandleTableSorting writes; SearchController sorts/drain reads). Kept here
  // so the full sort contract lives in one single-writer substate.
  int last_sort_column = -1;
  ImGuiSortDirection last_sort_direction = ImGuiSortDirection_None;

  // Staging buffers written by worker threads under mutex_.
  std::vector<uint64_t> sizes_{};   // NOLINT(readability-identifier-naming,readability-redundant-member-init)
  std::vector<FILETIME>  times_{};  // NOLINT(readability-identifier-naming,readability-redundant-member-init)
  std::vector<std::byte> flags_{};  // NOLINT(readability-identifier-naming,readability-redundant-member-init)
  std::mutex mutex_;                 // NOLINT(readability-identifier-naming)

  // True while tasks are actively running (Loading state). Used by the status bar
  // and UpdateDisplayedTotalSizeIfNeeded to defer work until loading completes.
  [[nodiscard]] bool IsLoading() const {
    return sort_ready_state_ == SortReadyState::Loading;
  }

  // True when a sort is pending — either tasks are running (Loading) or all data
  // is available and the sort can proceed this frame (Ready). False only when Idle.
  [[nodiscard]] bool HasPendingSort() const {
    return sort_ready_state_ != SortReadyState::Idle;
  }

  // Test-only: directly transition to Ready to bypass counter check.
  // Mirrors what StartSortAttributeLoading does when no tasks are enqueued.
  void ForceReadyForTest() {
    sort_ready_state_ = SortReadyState::Ready;
  }

  // Cancel current tasks, install a fresh cancellation token, and bump the
  // generation counter. Returns the new generation.
  //
  // The caller must drain in-flight tasks (spin on counter) between calling
  // token.Cancel() / BeginNewSort() and reusing any shared buffer, as usual.
  // BeginNewSort() cancels the old token as its first action so the caller can
  // also pre-cancel before the drain and rely on BeginNewSort() being idempotent.
  SortGeneration BeginNewSort() {
    token.Cancel();
    token = SortCancellationToken{};
    ++generation_;
    return generation_;
  }

  // Clear all in-flight state. Safe to call from the UI thread after counter has
  // reached zero (i.e. all tasks have drained).
  // Does NOT modify generation_ — that counter is monotonically increasing and is
  // only advanced by BeginNewSort().
  void Reset() {
    counter.reset();
    sort_ready_state_ = SortReadyState::Idle;
    const std::scoped_lock lock(mutex_);
    sizes_.clear();
    times_.clear();
    flags_.clear();
    staging_generation_ = 0;
  }
};
