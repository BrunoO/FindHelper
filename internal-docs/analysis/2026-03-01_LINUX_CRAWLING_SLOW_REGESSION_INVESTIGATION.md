# 2026-03-01 Linux crawling slow / single-thread regression investigation

## Summary

Crawling on Linux was reported as "much slower, perhaps working with only one thread". Investigation shows a **plausible regression** when `std::thread::hardware_concurrency()` returns **0**.

## Root cause

- **FolderCrawler** uses `config_.thread_count` (default 0 = auto). When 0, it sets  
  `thread_count = std::thread::hardware_concurrency()`.
- If `hardware_concurrency()` returns **0**, the code previously set `thread_count = 1` (single-thread fallback).
- On Linux, `hardware_concurrency()` can return 0 in:
  - Docker/containers with CPU limits (cgroups)
  - Some VMs or constrained environments
  - See: [cppreference](https://en.cppreference.com/w/cpp/thread/thread/hardware_concurrency), Docker/cgroups discussions

So in those environments, crawling was effectively single-threaded by design, not by a recent code change. If tests were moved to Docker or a cgroup-limited setup, the slowdown would appear as a "regression" even though the logic was unchanged.

## Code path (no intentional regression)

- **FolderCrawlerIndexBuilder** always uses default `FolderCrawlerConfig{}` (thread_count = 0).
- No caller sets `thread_count` to 1; the only way to get 1 thread was the fallback when `hardware_concurrency() == 0`.
- Recent commits (e.g. NOLINT for readdir) did not change thread count or crawler behavior.

## Fix applied

- In **FolderCrawler.cpp**, when `hardware_concurrency()` returns 0 we now:
  - Use a **minimum of 2 threads** (`kCrawlerMinThreadsWhenHardwareUnknown = 2`), aligned with SearchThreadPool’s fallback.
  - Log: `LOG_WARNING_BUILD("hardware_concurrency() returned 0 (e.g. cgroups/Docker); using N worker threads for crawling")`.
- Effect: constrained environments get at least 2-way parallelism and a clear log message when the fallback is used.

## How to confirm on Linux

1. Check startup/crawl logs for:
   - `"hardware_concurrency() returned 0"` → fallback was used (fix now gives 2 threads).
   - `"Using N worker threads for crawling"` → N is the actual worker count.
2. At bootstrap, **AppBootstrapCommon::LogCpuInformation()** logs  
   `hardware_concurrency(): <value>`. If that value is 0, the crawler was (before the fix) using 1 thread.

## References

- `src/crawler/FolderCrawler.cpp` – thread count and fallback
- `src/crawler/FolderCrawlerIndexBuilder.cpp` – uses default config (thread_count 0)
- `src/core/AppBootstrapCommon.h` – `LogCpuInformation()` logs `hardware_concurrency()`
- `src/search/SearchThreadPool.cpp` – uses 2 as fallback when `hardware_concurrency()` is 0

## Related: path-dedup hot path (FileIndex)

Path deduplication (one path = one entry) runs on every insert and can add cost when many paths hash to the same value. The collision path was optimized:

- **Before:** For each chain entry we called `GetPathLocked(e.id)` (allocates a string) and `NormalizePathForDedup(...)` (second allocation), then compared.
- **After:** Use `GetPathViewLocked(e.id)` (zero-copy) and `PathViewEqualsNormalized(view, norm)` (compare in-place, no allocation). Same semantics, less work and no extra allocations in the collision loop.

See `src/index/FileIndex.cpp`: `PathViewEqualsNormalized`, `InsertPathUnderLock`.
