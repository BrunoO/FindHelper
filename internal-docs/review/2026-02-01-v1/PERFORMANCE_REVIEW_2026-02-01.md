# Performance Review - 2026-02-01

## Executive Summary
- **Health Score**: 8/10
- **Critical Issues**: 1
- **High Issues**: 1
- **Total Findings**: 5
- **Estimated Remediation Effort**: 15 hours

## Findings

### Critical
1. **Redundant Synchronous File I/O in Search Path**
   - **Location**: `ParallelSearchEngine::SearchAsyncWithData`, `ParallelSearchEngine::DetermineThreadCount`, and `FileIndex::SearchAsyncWithData`.
   - **Issue Type**: I/O & System Call Overhead
   - **Current Cost**: `LoadSettings` is called 3 times per search request. Each call performs a synchronous disk read and JSON parsing of `settings.json`. This can add 10-50ms of latency per search on standard hardware, and much more on slow disks.
   - **Improvement**: Pass `AppSettings` by const reference into the search functions or use a cached version.
   - **Benchmark Suggestion**: Measure search latency with and without `LoadSettings` calls in the hot path.
   - **Effort**: S
   - **Risk**: Low

### High
2. **Expensive Map Lookups in Result Post-Processing**
   - **Location**: `SearchWorker::ConvertSearchResultData`
   - **Issue Type**: Algorithm Complexity / Cache Efficiency
   - **Current Cost**: Every result found (up to hundreds of thousands) triggers a `file_index.GetEntry(data.id)` call, which performs a hash map lookup. This is O(N) where N is the number of results, and each lookup has overhead and potential cache misses.
   - **Improvement**: Include basic metadata (size, time) in the SoA (Structure of Arrays) used during parallel search, or use a more efficient way to bulk-retrieve metadata.
   - **Benchmark Suggestion**: Measure post-processing time for a search returning 500,000 results.
   - **Effort**: L
   - **Risk**: Medium (Requires changing index layout)

### Medium
3. **Unbounded Regex Cache Growth**
   - **Location**: `std_regex_utils::ThreadLocalRegexCache`
   - **Issue Type**: Memory Allocation Patterns
   - **Current Cost**: Each thread maintains a cache of compiled regex objects that never clears until the thread dies. This can lead to significant memory consumption if many unique regexes are used.
   - **Improvement**: Implement LRU (Least Recently Used) eviction for the regex cache.
   - **Benchmark Suggestion**: Measure memory usage after performing 1000 unique regex searches.
   - **Effort**: M
   - **Risk**: Low

### Low
4. **Pass-by-Value of `std::string_view`**
   - **Location**: Multiple locations in `ParallelSearchEngine.cpp`
   - **Issue Type**: Memory Allocation Patterns
   - **Current Cost**: Minor overhead from copying `std::string_view` objects (which are small but still incur some cost).
   - **Improvement**: Ensure `std::string_view` is passed by value (which is correct for its size), but check for any `const std::string&` that could be `string_view`.
   - **Effort**: S
   - **Risk**: Low
   - **Assessment**: **Optional / very low impact.** (1) Passing `std::string_view` by value is the recommended practice (isocpp / Arthur O’Dwyer); “copying” is cheap (two pointers, often in registers). ParallelSearchEngine already passes `query` by value. (2) No `const std::string&` in ParallelSearchEngine that should become `string_view`. (3) One spot in the search path: `ExtensionMatches(const std::string_view& ext_view, ...)` in `SearchPatternUtils.h` could use `std::string_view ext_view` (by value) for consistency; benefit is trivial. **Drop** from backlog or treat as optional micro-optimization when touching that code.

5. **Wait Interval in Streaming Search**
   - **Location**: `SearchWorker::ProcessStreamingSearchFutures`
   - **Issue Type**: Lock Contention & Synchronization
   - **Current Cost**: Uses a 5ms `wait_for` poll interval when no results are ready. This adds a small delay to results appearing in the UI.
   - **Improvement**: Use a condition variable or a more reactive signaling mechanism to notify the worker when futures are ready.
   - **Effort**: M
   - **Risk**: Medium
   - **Assessment**: **Optional.** The 5ms interval is a deliberate trade-off: it avoids busy-waiting while keeping worst-case latency to “worker notices a ready future” at ~5ms. (1) **Full fix** (condition variable): would require thread-pool tasks to notify when they complete—i.e. wrap submitted work so that on completion they signal a CV the worker waits on. That touches the parallel search / strategy submission path and adds shared synchronization; effort M and risk of missed wakeups or deadlock if done wrong. (2) **Low-effort tweak**: reduce interval to 1ms to slightly improve time-to-first-result at the cost of more wakeups when many futures are pending. (3) **Reality**: time-to-first-result is dominated by actual search work (tens–hundreds of ms); 5ms is a small fraction. **Drop** from backlog unless streaming latency is a measured UX problem; if so, try 1ms first before investing in a CV-based design.

## Summary Requirements

### Performance Score: 8/10
Justification: The core search engine uses highly optimized AVX2 SIMD and SoA layouts, which provides excellent baseline performance. The main issues are "death by a thousand cuts" from redundant I/O and inefficient post-processing lookups.

### Top 3 Bottlenecks
1. **Redundant `LoadSettings` calls**: Synchronous I/O in search hot path.
2. **Post-processing map lookups**: Bottleneck when results count is high.
3. **Regex compilation/matching**: `std::regex` is significantly slower than literal string search.

### Scalability Assessment
The application will handle 10x more files (e.g., 10M instead of 1M) well due to the SoA design, but memory usage will grow linearly. The post-processing map lookups will become a more significant bottleneck at larger scales.

### Quick Wins
1. Cache `AppSettings` to eliminate redundant file I/O during search (High impact, low risk).
2. Implement LRU for regex cache (Stability improvement).

### Benchmark Recommendations
- **Latency Benchmark**: Measure time from search trigger to first results in UI.
- **Throughput Benchmark**: Measure items processed per second for different query types (Literal, Regex, Extension-only).
- **Memory Benchmark**: Measure memory overhead per million indexed files.
