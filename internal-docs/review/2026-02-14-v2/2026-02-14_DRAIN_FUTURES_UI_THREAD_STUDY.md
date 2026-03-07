# Drain Futures and UI Thread Blocking Study - 2026-02-14

## Reviewer Request

> Draining futures during worker teardown should avoid blocking the UI thread. If DrainFuture may block, ensure it is called only for ready futures or off the UI thread. Add a fast-path check or a timeout strategy with logging.

## Thread Model

| Component | Thread | Notes |
|-----------|--------|-------|
| SearchWorker::WorkerThread | Background (SearchWorker's `thread_`) | Runs searches, ProcessStreamingSearchFutures, DrainRemainingSearchFutures |
| Application main loop | UI thread | Renders ImGui, handles input |
| SearchController::Update | UI thread | Polls SearchWorker, updates GuiState |

**DrainRemainingSearchFutures runs on the SearchWorker thread, not the UI thread.** It is called from ProcessStreamingSearchFutures, which runs inside ExecuteSearch on the WorkerThread.

## Current Implementation (SearchWorker.cpp:469-490)

```cpp
static void DrainRemainingSearchFutures(...) {
  constexpr auto k_drain_poll = std::chrono::milliseconds(50);
  constexpr int k_max_drain_waits = 40;  // 2s total per future
  for (const size_t idx : pending_indices) {
    if (!search_futures[idx].valid()) continue;
    auto& fut = search_futures[idx];
    // Fast-path: try-wait first
    if (fut.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
      for (int i = 0; i < k_max_drain_waits; ++i) {
        if (fut.wait_for(k_drain_poll) == std::future_status::ready) break;
      }
    }
    exception_handling::DrainFuture(fut, "SearchWorker drain");
  }
}
```

- **Fast-path**: `wait_for(0)` – if ready, no blocking before `get()`
- **Bounded wait**: Up to 2s per future (40 × 50ms)
- **After loop**: `DrainFuture` calls `get()` – will block if future still not ready

## When Does the UI Thread Block?

1. **Normal operation**: DrainRemainingSearchFutures runs on the worker thread. UI thread is not blocked.
2. **Shutdown**: Application destructor → SearchWorker destructor → `thread_.join()`. The main thread waits for the worker to finish. If the worker is in DrainRemainingSearchFutures (blocking on `get()`), the main thread waits at `join()`. By then the window is usually destroyed, so the UI is not actively rendering.

## Other Future-Draining Sites (UI Thread Risk)

| Location | Thread | Blocking? |
|----------|--------|-----------|
| GuiState::ClearInputs | UI thread (user action) | Yes – calls `f.get()` directly |
| Application::DrainFutures (shutdown) | Main thread | Yes – during teardown |
| SearchController::WaitForAllAttributeLoadingFutures | UI thread | Yes – `future.get()` |
| SearchResultUtils::CleanupAttributeLoadingFutures | UI thread | Depends on `blocking` flag |

The reviewer’s comment targets SearchWorker.cpp specifically. SearchWorker’s drain is off the UI thread.

## Recommendations

### 1. Add Logging When Timeout Is Exhausted

Log when the bounded wait (2s) is exhausted and we are about to call `get()`, which may block. This helps diagnose slow drains.

### 2. Document Thread Affinity

Add a comment in DrainRemainingSearchFutures stating that it runs on the SearchWorker thread, not the UI thread, so future readers understand the design.

### 3. Optional: Skip Blocking Drain After Timeout

We cannot safely skip calling `get()` on valid futures; the destructor would still need to run. Skipping is not viable. The only improvement is to log when we hit the timeout.

## Implementation (Minimal)

Add logging when the future is still not ready after the bounded wait:

```cpp
if (fut.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
  int waited = 0;
  for (; waited < k_max_drain_waits; ++waited) {
    if (fut.wait_for(k_drain_poll) == std::future_status::ready) break;
  }
  if (waited >= k_max_drain_waits) {
    LOG_WARNING_BUILD("SearchWorker drain: future not ready after "
                      << (k_max_drain_waits * 50) << "ms, get() may block");
  }
}
```

And add a brief comment that this runs on the SearchWorker thread.
