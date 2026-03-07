# Responsive UI with Partial Results Streaming for Large Indexes

**Date:** January 17, 2026  
**Document Version:** 1.0 - Analysis & Design Proposal  
**Purpose:** Define strategy for displaying partial search results to users while final sorting/filtering completes  
**Status:** Design Recommendation Phase

---

## Executive Summary

This document presents a comprehensive analysis and design proposal for enabling responsive UI when the index contains millions of files. The core challenge is showing partial results to users immediately while sorting and filtering operations complete in the background.

**Key Principles:**
1. **Non-Breaking:** All changes are backward compatible with current architecture
2. **Incremental:** Can be implemented in phases without complete rewrites
3. **Pattern-Driven:** Uses well-established design patterns (Observer, Pipeline, Streaming)
4. **Coherent:** Leverages existing lazy loading and async patterns
5. **Pragmatic:** Minimizes complexity while maximizing user responsiveness

**Recommendation:** Implement a **Streaming Results Pipeline** with multi-tier buffering and progressive notification system, starting with Phase 1 (Core Infrastructure).

---

## Problem Analysis

### Current Limitations

The current implementation collects **complete** results before UI notification:

```
Search Start
    ↓
[Parallel Search Engine processes all chunks]
    ↓
SearchWorker::GetResults() returns COMPLETE result set
    ↓
UI receives notification and displays 100% complete results
    ↓
Sorting/Filtering happens on FULL dataset
```

**Issues with millions of files:**
1. **Long startup delay** - User waits for all search to complete before seeing ANY results
2. **UI freeze risk** - Large bulk operations (sorting, filtering) on millions of items can cause visible jank
3. **Memory spike** - All results held in memory before display
4. **Unpredictable latency** - User experience varies dramatically based on result count

**Current Metrics (from SearchMetrics):**
- `last_search_time_ms`: Can be several seconds for large indexes
- `max_results_count`: Can easily reach millions
- No distinction between search completion and result availability

### User Expectations

Modern applications (VSCode, Chrome, Slack, etc.) show partial results:
- Search starts → first results appear in <100ms
- As search continues → more results appear incrementally
- UI remains responsive to user actions
- Sorting/filtering work on results as they arrive

---

## Architecture Assessment

### Current Strengths to Preserve

1. **Lazy Attribute Loading** (`LazyAttributeLoader`)
   - Already handles asynchronous attribute loading
   - Pattern-based double-check locking
   - Can be extended for partial results

2. **Component Separation** (Component-Based Architecture)
   - `ParallelSearchEngine` handles search orchestration
   - `SearchWorker` handles threading and result collection
   - `SearchResultsService` handles filtering/sorting
   - Loose coupling enables incremental changes

3. **Thread Pool Infrastructure** (`SearchThreadPool`)
   - Already manages parallelism
   - Can support streaming notification tasks
   - Load balancing strategies already in place

4. **ImGui Immediate Mode Paradigm**
   - UI rebuilds every frame (~60 FPS)
   - Can naturally incorporate new results each frame
   - No persistent widget state to manage

5. **Async/Streaming-Ready Patterns**
   - `std::future` used for sort/filter async operations
   - Thread-safe result exchange via `SearchWorker`
   - Atomic flags for state management

### Constraints & Compatibility

1. **Windows-First Design**
   - Must work on Windows (primary platform)
   - Linux/macOS as secondary targets
   - No platform-specific streaming APIs required

2. **C++17 Feature Set**
   - `std::optional`, `std::variant` available
   - Limited C++20 features
   - Must avoid heavy async/await patterns

3. **No External Dependencies**
   - Must use existing infrastructure
   - Cannot add new libraries for reactive programming

4. **Backward Compatibility**
   - SearchWorker API should remain compatible
   - Current code paths must continue working
   - Opt-in feature for partial results

---

## Proposed Design: Streaming Results Pipeline

### Core Concept

Implement a **multi-tier streaming pipeline** that:
1. Collects search results in **batches** from the search engine
2. Publishes batches to UI at regular intervals (every 50-100ms)
3. Allows UI to render **partial results immediately**
4. Continues searching in background
5. Marks results as **final** when search completes
6. Applies sorting/filtering to results as they become final

### Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                   ParallelSearchEngine                       │
│  (Orchestrates parallel search, yields results in chunks)   │
└────────────────┬────────────────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────────────────────┐
│             StreamingResultsCollector (NEW)                 │
│  - Batches results from search engine                       │
│  - Implements multi-tier buffering                          │
│  - Coordinates with SearchWorker                            │
│  - Throttles notifications                                  │
└────────────────┬────────────────────────────────────────────┘
                 │
                 ▼ (notifies every 50-100ms or when batch fills)
┌─────────────────────────────────────────────────────────────┐
│           ResultUpdateNotifier (NEW - Observer)             │
│  - Callbacks when new batches available                     │
│  - Called from background thread                            │
│  - Wakes up UI rendering if sleeping                        │
└────────────────┬────────────────────────────────────────────┘
                 │
                 ▼ (updates UI with new results each frame)
┌─────────────────────────────────────────────────────────────┐
│                    GuiState                                 │
│  - Add: `partialResults` vector (incrementally updated)    │
│  - Add: `resultsComplete` flag (marks final results)       │
│  - Add: `resultsBatchNumber` for tracking                  │
│  - Existing: `searchResults` (final, for sorting)          │
└────────────────┬────────────────────────────────────────────┘
                 │
                 ▼ (each frame renders visible rows only)
┌─────────────────────────────────────────────────────────────┐
│                    ResultsTable (UI)                        │
│  - Renders from `partialResults` until complete            │
│  - Virtual scrolling with ImGuiListClipper                 │
│  - Lazy loads attributes (size, mod time) as needed        │
└─────────────────────────────────────────────────────────────┘
```

### Key Components

#### 1. StreamingResultsCollector

**Purpose:** Batch and throttle results from the search engine.

**Responsibilities:**
- Collects individual results from the parallel search
- Batches results into groups (e.g., every 500-1000 results)
- Implements throttling (notify UI no more than every 50ms)
- Tracks completion status
- Thread-safe result exchange

**Design:**
```cpp
class StreamingResultsCollector {
public:
    // Initialize with search context
    StreamingResultsCollector(size_t batch_size = 500,
                             uint64_t notification_interval_ms = 50);
    
    // Called by search engine to add a single result
    void AddResult(SearchResult result);
    
    // Called when search is complete
    void MarkSearchComplete();
    
    // Check if new batch is available (non-blocking)
    bool HasNewBatch() const;
    
    // Get new batch (clears "new batch" flag)
    std::vector<SearchResult> GetNewBatch();
    
    // Register callback for when new batch becomes available
    using BatchAvailableCallback = std::function<void()>;
    void OnBatchAvailable(BatchAvailableCallback callback);
    
    // Get all results accumulated so far
    const std::vector<SearchResult>& GetAllResults() const;
    
    // Check if search is complete
    bool IsSearchComplete() const;
    
private:
    std::vector<SearchResult> current_batch_;
    std::vector<SearchResult> all_results_;
    size_t batch_size_;
    uint64_t notification_interval_ms_;
    std::chrono::steady_clock::time_point last_notification_;
    
    mutable std::shared_mutex mutex_;
    std::atomic<bool> search_complete_{false};
    std::atomic<bool> has_new_batch_{false};
    
    BatchAvailableCallback on_batch_available_;
    
    void NotifyBatchAvailable() const;
};
```

**Key Design Decisions:**
- Configurable batch size (trades off latency vs overhead)
- Configurable notification interval (prevents excessive UI updates)
- Simple non-blocking interface for UI polling
- Optional callback for platform-specific wake-up (if needed)

---

#### 2. ResultUpdateNotifier

**Purpose:** Coordinate notifications between search thread and UI thread.

**Responsibilities:**
- Observes `StreamingResultsCollector` for new batches
- Signals UI thread when new results available
- Handles platform-specific notification mechanisms
- Thread-safe event propagation

**Design:**
```cpp
class ResultUpdateNotifier {
public:
    // Initialize with collector and UI callback
    ResultUpdateNotifier(StreamingResultsCollector& collector);
    
    // Start monitoring (called by SearchWorker)
    void StartMonitoring();
    
    // Stop monitoring
    void StopMonitoring();
    
    // Register callback to be called when new results available
    using UpdateCallback = std::function<void()>;
    void OnResultsUpdated(UpdateCallback callback);
    
    // Get number of batches received
    size_t GetBatchCount() const;
    
private:
    StreamingResultsCollector& collector_;
    std::atomic<bool> monitoring_{false};
    std::thread monitor_thread_;
    UpdateCallback update_callback_;
    
    void MonitoringLoop();
};
```

**Key Design Decisions:**
- Lightweight monitoring loop (just polls and calls callback)
- Optional callback for advanced wake-up scenarios
- Thread-safe state management with atomics

---

#### 3. GuiState Extensions

**Purpose:** Track partial and final results separately.

**New Fields:**
```cpp
class GuiState {
    // Existing fields...
    
    // NEW: Partial results (updated incrementally during search)
    std::vector<SearchResult> partialResults;
    
    // NEW: Flag indicating if results are final/complete
    bool resultsComplete = false;
    
    // NEW: Batch number for tracking updates
    uint64_t resultsBatchNumber = 0;
    
    // NEW: Position in partial results (for resuming sort)
    size_t lastProcessedPartialIndex = 0;
    
    // NEW: Track if we're showing partial vs final results
    bool showingPartialResults = false;
    
    // Existing fields continue...
    std::vector<SearchResult> searchResults;  // Final results (sorted/filtered)
    TimeFilter timeFilter;
    SizeFilter sizeFilter;
    // ... etc
};
```

---

#### 4. SearchWorker Integration

**Purpose:** Integrate streaming into existing SearchWorker.

**Modified Methods:**
```cpp
class SearchWorker {
public:
    // Existing methods remain...
    
    // NEW: Get streaming results collector
    StreamingResultsCollector* GetStreamingCollector();
    
    // NEW: Check if partial results are available
    bool HasPartialResults() const;
    
    // NEW: Get partial results (doesn't clear flag)
    const std::vector<SearchResult>& GetPartialResults() const;
    
private:
    // NEW: Streaming collector
    std::unique_ptr<StreamingResultsCollector> streaming_collector_;
    
    // Modified: StartSearch() initializes streaming_collector
    // Modified: WorkerThread() feeds results to streaming_collector
};
```

**Key Modification Points:**
1. Initialize `StreamingResultsCollector` in `StartSearch()`
2. Feed results to collector instead of accumulating in vector
3. Call `MarkSearchComplete()` when search finishes
4. Keep existing API intact for backward compatibility

---

### Data Flow with Streaming

**Timeline:**
```
T=0ms:     Search starts
           ├─ StreamingResultsCollector initialized
           └─ ParallelSearchEngine starts processing

T=50ms:    First batch available (e.g., 500 results)
           ├─ Collector batches and notifies
           └─ UI receives notification

T=55ms:    UI frame renders
           ├─ Reads partial results (500 items)
           ├─ Renders visible rows only (virtual scrolling)
           ├─ Shows status: "Found 500 results (searching...)"
           └─ No jank, UI remains responsive

T=100ms:   Second batch available (another 500 results)
           ├─ Collector has 1000 total
           ├─ Notifies UI
           └─ UI updates on next frame

T=110ms:   UI frame renders
           ├─ Reads partial results (1000 items)
           ├─ Renders visible rows
           ├─ Status: "Found 1000 results (searching...)"
           └─ Smooth increment

T=2000ms:  Search complete
           ├─ ParallelSearchEngine finished
           ├─ Collector marks complete
           ├─ Total results: 10,000
           └─ Notifies "final batch"

T=2010ms:  UI frame renders
           ├─ Marks resultsComplete = true
           ├─ Status changes to "Found 10,000 results"
           ├─ Can now trigger final sorting/filtering
           └─ Already displayed thousands before this point
```

---

## Implementation Strategy

### Phase 1: Core Infrastructure (Foundation)

**Objective:** Implement basic streaming pipeline without breaking existing code.

**Changes Required:**

1. **Create `StreamingResultsCollector` class**
   - File: `src/search/StreamingResultsCollector.h/cpp`
   - Status: New component
   - Dependencies: SearchResult, threading primitives
   - Complexity: Low-Medium

2. **Create `ResultUpdateNotifier` class**
   - File: `src/search/ResultUpdateNotifier.h/cpp`
   - Status: New component
   - Dependencies: StreamingResultsCollector
   - Complexity: Low

3. **Extend `GuiState`**
   - Add 4-5 new fields (partial results, complete flag, batch tracking)
   - Backward compatible (all have sensible defaults)
   - Complexity: Very Low

4. **Modify `SearchWorker`**
   - Initialize streaming collector
   - Feed results to collector instead of accumulating
   - Add getter for streaming collector
   - Backward compatible: existing `GetResults()` still works
   - Complexity: Medium

5. **Add to `CMakeLists.txt`**
   - New source files
   - Test targets (new unit tests)
   - Complexity: Low

**Estimated Work:**
- `StreamingResultsCollector`: ~150 lines of code + 100 lines of tests
- `ResultUpdateNotifier`: ~80 lines of code + 50 lines of tests
- `GuiState` extension: ~10 lines of code
- `SearchWorker` modification: ~20 lines of code changes
- **Total: ~400 lines of new code, minimal existing code changes**

**Testing Strategy:**
- Unit tests for collector (batching, notification, thread safety)
- Unit tests for notifier (monitoring, callbacks)
- Integration tests with SearchWorker
- Performance tests (ensure no regression)

**Risk Level:** Low
- No breaking changes to existing API
- New components are isolated
- Can enable/disable with feature flag if needed
- Existing code paths unchanged

---

### Phase 2: UI Integration (Partial Display)

**Objective:** Display partial results in UI while maintaining backward compatibility.

**Changes Required:**

1. **Modify `ResultsTable::Render()`**
   - Add check for `resultsComplete` flag
   - If false and partial results available, render from `partialResults`
   - If true or no partial results, render from `searchResults` (existing behavior)
   - Add status indicator ("Searching..." with count)
   - Virtual scrolling applies to either source
   - Complexity: Medium

2. **Update `SearchResultsService`**
   - Add logic to handle partial vs final results
   - Defer sorting until `resultsComplete` is true
   - Show count of partial results being displayed
   - Complexity: Low

3. **Extend `StatusBar`**
   - Show dynamic result count as search progresses
   - Display "Searching..." indicator
   - Complexity: Low

4. **Update search start/completion logic**
   - Set `showingPartialResults = true` when results start arriving
   - Set `showingPartialResults = false` and trigger sort when complete
   - Complexity: Low

**Estimated Work:**
- ResultsTable modification: ~30 lines
- SearchResultsService update: ~20 lines
- StatusBar extension: ~15 lines
- UI logic updates: ~25 lines
- **Total: ~90 lines of changes**

**Testing Strategy:**
- Integration tests with UI rendering
- Visual regression tests (ensure no visual changes to existing code paths)
- Performance tests (ensure virtual scrolling still works)
- User acceptance testing

**Risk Level:** Low-Medium
- Changes are localized to UI layer
- Existing code paths unchanged for backward compatibility
- Virtual scrolling already proven to work at scale

---

### Phase 3: Smart Sorting & Filtering (Progressive Finalization)

**Objective:** Apply sorting/filtering progressively to partial results.

**Concept:**
- While search is ongoing, sorting can begin on results received so far
- Results are re-sorted as new batches arrive
- When search completes, final sort on complete set is quick
- User sees sorted results incrementally, even before search ends

**Changes Required:**

1. **Modify `SearchResultsService::HandleTableSorting()`**
   - Check if `resultsComplete` is true
   - If false, perform lightweight sort on partial results only
   - If true, perform full sort on complete results
   - Complexity: Low

2. **Add progressive filter updates**
   - Apply filters as partial results come in
   - Track which results have been filtered
   - Recompute incrementally when new batch arrives
   - Complexity: Medium

3. **Update filter cache invalidation**
   - Invalidate caches only on new batches, not every frame
   - Defer expensive operations until search complete
   - Complexity: Low

**Estimated Work:**
- Sorting modification: ~20 lines
- Progressive filtering: ~40 lines
- Cache logic updates: ~15 lines
- **Total: ~75 lines of changes**

**Testing Strategy:**
- Unit tests for progressive sorting
- Unit tests for progressive filtering
- Integration tests with search workflow
- Performance tests (ensure no regression on small result sets)

**Risk Level:** Low
- Changes are additive (improve responsiveness)
- Fallback to existing behavior if partial results disabled
- No breaking changes

**Benefits:**
- Users see sorted results before search completes
- Responsive even with millions of results
- Familiar UX from modern applications

---

### Phase 4: Advanced Notification & Wake-up (Optional)

**Objective:** Wake up UI rendering on new results (platform-specific).

**Context:**
- Current ImGui design renders every frame regardless
- With streaming results, could optimize by waking up on demand
- Optional enhancement for better battery life (on mobile) or UI responsiveness

**Changes Required (Windows-specific example):**

1. **Platform-specific wake-up mechanism**
   - Windows: Post custom message to window
   - macOS: Trigger display refresh
   - Linux: N/A (always rendering anyway)
   - Complexity: Medium

2. **Integrate into `ResultUpdateNotifier`**
   - Call platform-specific wake-up when new batch available
   - Complexity: Low

**Estimated Work:**
- Platform implementations: ~50 lines each
- Integration: ~10 lines
- **Total: ~60-100 lines (platform-specific)**

**Risk Level:** Medium
- Platform-specific code
- Requires careful testing on each platform
- Can be disabled if issues arise

**Defer to:** Later phase (not critical for initial implementation)

---

## Migration Path & Backward Compatibility

### How Existing Code Continues Working

**Scenario 1: Old Code Using `SearchWorker::GetResults()`**
```cpp
// Old pattern (still works)
if (search_worker.HasNewResults()) {
    auto results = search_worker.GetResults();  // Gets COMPLETE results
    // Process complete results
}
```

**How It Works:**
- `GetResults()` still collects results from streaming collector
- Returns full accumulated results (same as before)
- Existing code path unchanged

**Scenario 2: UI Code Using `GuiState::searchResults`**
```cpp
// Old pattern (still works)
// Results are placed in searchResults when ready
for (const auto& result : state.searchResults) {
    // Render result
}
```

**How It Works:**
- If not using partial results, searchResults populated as before
- If using partial results, triggers on completion
- Backward compatible

### Streaming as an Option (Full Result Set Supported)

Streaming is **implemented as an optional behaviour**, not the only path. Some use cases need the **full result set** before any display or processing; those use the existing "collect all, then deliver once" path.

**Option location:** A user-facing setting (e.g. in `AppSettings`) controls whether streaming is used for a given search or session. Example name: `streamPartialResults` or `enableStreamingResults` (default can be `true` for responsive UI, or `false` for maximum compatibility).

```cpp
// In AppSettings (persisted, user can toggle in Settings UI)
bool streamPartialResults = true;  // true = streaming, false = full result set only

// In GuiState / streaming config (can also hold batch tuning)
struct StreamingConfig {
    bool enabled = true;   // Driven by AppSettings.streamPartialResults
    size_t batch_size = 500;
    uint64_t notification_interval_ms = 50;
};

// In SearchWorker: choose path based on setting
if (settings.streamPartialResults && streaming_collector_) {
    // Consume futures in completion order, feed StreamingResultsCollector
    // UI gets partial results via GetNewBatch() until search complete
} else {
    // Current behaviour: collect all futures (thread or completion order),
    // then set results_ once. UI gets one notification with full result set.
}
```

**When streaming is OFF (full result set):**
- Search runs as today; `ProcessSearchFutures()` (or equivalent) collects all results.
- No partial updates; UI is notified **once** when search is complete with the full `results_` set.
- Use this when: exporting all results, scripting/batch use, or user preference for "show only when complete".

**When streaming is ON:**
- Results are fed into `StreamingResultsCollector` as futures complete.
- UI receives partial results via `GetNewBatch()` and shows "Found X results (searching...)" until complete.
- Final notification still delivers the full set for sorting/filtering.

**Use cases for full result set (streaming OFF):**
- **Export to file:** Need complete, ordered set before writing.
- **Batch/scripting:** Consumer expects one complete response (e.g. CLI, automation).
- **User preference:** "Show results only when complete" (simpler UI, no partial state).
- **Downstream logic:** Any code that assumes a single, complete result set.

**Benefits of making it an option:**
- No breaking change for code that expects one full result set.
- User or deployment can disable streaming if issues arise.
- Batch size and notification interval remain tunable when streaming is enabled.

---

## Design Patterns & Best Practices

### Patterns Used

1. **Observer Pattern** (ResultUpdateNotifier)
   - Decouples search from UI notification
   - Enables future extensions (e.g., multiple observers)
   - Clean separation of concerns

2. **Pipeline Pattern** (StreamingResultsCollector)
   - Results flow through stages: individual → batch → notify
   - Natural for incremental processing
   - Matches existing parallel search pipeline

3. **Throttling Pattern** (StreamingResultsCollector)
   - Batching with time-based throttling
   - Prevents overwhelming UI with updates
   - Proven pattern in reactive systems

4. **Facade Pattern** (SearchWorker)
   - Hides streaming complexity from callers
   - Maintains existing API
   - Single point of integration

### Thread Safety Guarantees

**Invariants:**
- `StreamingResultsCollector` is fully thread-safe (shared_mutex for reads, atomic flags)
- `ResultUpdateNotifier` is thread-safe (only manages atomics and callbacks)
- `SearchWorker` passes results safely via collector
- UI thread reads from collector atomically (snapshot semantics)

**Race Condition Analysis:**

1. **Result Lost?**
   - No: Once added to collector, in memory until UI reads

2. **Notification Lost?**
   - Unlikely: Notification flag is atomic, checked every poll interval
   - Even if missed one notification, next batch will trigger

3. **Double Processing?**
   - No: Batch number increments atomically, UI tracks which batches seen

4. **Memory Leak?**
   - No: All data structures RAII with clear ownership
   - Collector owns results until UI consumes them

---

## Implementation Considerations

### Performance

**Overhead Analysis:**
- Streaming collector: O(1) per result (append to vector)
- Batching: O(n) where n = results since last batch
- Notification: O(1) (just set flag and call callback)
- Total: Negligible compared to actual search work

**Memory Impact:**
- Partial results kept in memory (existing vector anyway)
- Extra 5 atomic flags: ~40 bytes
- Callback function pointer: ~8-16 bytes per notifier
- **Total Extra: <1KB regardless of result count**

**Latency Benefits:**
- First results in <50ms (batching interval) after search starts
- UI responds to user input during search
- No blocking operations on main thread

### Scalability

**With 10 Million Results:**
- Search: 2-10 seconds (existing engine performance)
- First results: 50ms (batch arrives)
- Batches: ~20,000 batches of 500 results each
- Notifications: ~200 (one every 50ms for 10 seconds)
- UI load: Same (virtual scrolling renders ~40 visible rows regardless)

### Edge Cases

**Case 1: Very Fast Search (<100ms)**
- All results collected into one batch
- Single notification
- Works same as before

**Case 2: Very Slow Search (>60s)**
- Results stream in continuously
- Batches arrive regularly
- UI stays responsive
- User can cancel or interact while searching

**Case 3: Empty Search Results**
- No results to batch
- Collector marked complete
- UI shows "0 results found"
- Works correctly

**Case 4: Search Cancelled**
- Collector stopped receiving results
- Last batch marked as final
- UI shows partial results received so far
- Correct behavior

---

## Integration with Current Architecture

### How It Fits with Lazy Loading

**Scenario:** User searches millions of files, sees 5,000 results in first batch

```
┌─────────────────────────────────┐
│ Partial Result (row 0)          │
│ ├─ fullPath: loaded            │
│ ├─ filename: loaded (from path) │
│ ├─ fileSize: NOT LOADED (lazy)  │
│ └─ modTime: NOT LOADED (lazy)   │
└─────────────────────────────────┘
        ↓ (user scrolls/sort)
        ├─ Lazy loader loads size for visible rows
        ├─ Attributes loaded in background
        └─ Display updates as attributes arrive
```

**Benefits of Integration:**
- Lazy loading continues working unchanged
- Attributes loaded asynchronously as before
- Partial results leverage existing lazy infrastructure
- No blocking operations on any path

### How It Fits with Load Balancing Strategies

Load balancing strategies (Static, Hybrid, Dynamic, Interleaved, WorkStealing) **do not change**. They still decide how work is split across threads and return one `std::future<std::vector<SearchResultData>>` per thread. Streaming only changes **how SearchWorker consumes those futures**.

**Current behaviour (no streaming):**
- `ProcessSearchFutures()` iterates futures in **index order** (future 0, 1, 2, …).
- For each index it calls `futures[i].get()`, so results are collected in thread order.
- All futures are consumed before any results are exposed to the UI.

**With streaming:**
- Consume futures in **completion order**: as soon as **any** future is ready, take its vector, push it into `StreamingResultsCollector`, and repeat until all futures are consumed.
- Load balancing is unchanged: Static still assigns fixed chunks, Hybrid/Dynamic still do initial + dynamic chunks, Interleaved/WorkStealing still assign work as today.
- Result **order** may differ (whichever thread finishes first delivers first); progressive sorting and final sort when complete handle that.

**Per-strategy behaviour:**
- **Static:** One chunk per thread; when the fastest thread finishes, its results stream first. Good for uniform workloads.
- **Hybrid / Dynamic:** Each thread may process multiple chunks but returns one vector when done. When that thread completes, its full vector is pushed to the collector (which may batch/throttle for UI). With N threads, up to N "chunks" can stream as threads complete.
- **Interleaved / WorkStealing:** Same as above—one future per thread; streaming consumes each future as it completes.

**Implementation note (C++17):** There is no `std::when_any`. To consume by completion order, use a loop that checks which future is ready (e.g. `future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready`), then call `.get()` on that future, push to `StreamingResultsCollector`, and remove it from the set. Repeat until all futures are consumed. Alternatively, a small thread pool can block on one future each and push to a thread-safe collector.

**Summary:** Load balancing strategies stay as-is. Only the **consumer** of the futures (SearchWorker) changes: from "collect all in thread order" to "collect in completion order and feed StreamingResultsCollector".

### How It Fits with SearchResultsService

**Current Flow:**
```
SearchWorker::GetResults() 
    ↓
GuiState::searchResults = results
    ↓
SearchResultsService::GetDisplayResults()
    ├─ Applies time filter
    ├─ Applies size filter
    └─ Returns filtered set
```

**New Flow:**
```
SearchWorker::GetStreamingCollector()->GetNewBatch()
    ↓
GuiState::partialResults += new_batch
    ↓
When search complete:
    GuiState::searchResults = final results
    resultsComplete = true
    
SearchResultsService::GetDisplayResults()
    ├─ If resultsComplete: Apply filters to searchResults
    ├─ Else: Return partialResults (minimal filtering)
    └─ Returns set to display
```

**Backward Compatibility:**
- If not using streaming, `GetResults()` still works
- `GetDisplayResults()` behavior unchanged for completed searches
- Filtering logic reused for partial results

---

## Success Criteria & Metrics

### User Experience Metrics

1. **Time to First Result**
   - **Target:** <100ms for searches with millions of files
   - **Current:** 1-10 seconds
   - **Measurement:** Time from search start to first batch displayed

2. **Result Visibility Progression**
   - **Target:** 50% of final results visible within 500ms
   - **Current:** 0% until complete
   - **Measurement:** Cumulative % of final results shown over time

3. **UI Responsiveness**
   - **Target:** <16ms frame time (60 FPS) during streaming
   - **Current:** May spike during large sorts
   - **Measurement:** Frame time histogram

4. **User Interaction Latency**
   - **Target:** <100ms response to clicks/input during search
   - **Current:** May be <100ms but UI updates sluggish
   - **Measurement:** Input-to-response latency

### Technical Metrics

1. **Throughput Impact**
   - **Target:** <2% overhead vs current implementation
   - **Measurement:** Comparisons on identical search workloads

2. **Memory Overhead**
   - **Target:** <5MB for any configuration
   - **Measurement:** Peak memory usage with 10M results

3. **Batch Processing Latency**
   - **Target:** <5ms from batch filled to notification
   - **Measurement:** Time from AddResult(final_in_batch) to HasNewBatch() == true

4. **Collector Thread Overhead**
   - **Target:** <1% CPU usage when idle
   - **Measurement:** CPU % with no new results arriving

### Quality Metrics

1. **Test Coverage**
   - New components: ≥85% line coverage
   - Integration: All scenarios tested
   - Platform-specific: Tested on Windows, macOS, Linux

2. **No Regressions**
   - All existing tests pass
   - Performance benchmarks show no degradation
   - Memory usage not increased

3. **Stability**
   - Thread sanitizer: No data races
   - Address sanitizer: No memory leaks
   - Stress testing: 1M+ results handled stably

---

## Implementation Roadmap

### Timeline

**Week 1-2: Phase 1 (Core Infrastructure)**
- Implement `StreamingResultsCollector`
- Implement `ResultUpdateNotifier`
- Integrate with `SearchWorker`
- Unit tests
- **Status:** Foundation ready

**Week 3: Phase 2 (UI Integration)**
- Extend `GuiState`
- Modify `ResultsTable::Render()`
- Update status bar
- Integration tests
- **Status:** Partial results displayed

**Week 4: Phase 3 (Progressive Sorting)**
- Progressive sort logic
- Progressive filter updates
- Integration tests
- **Status:** Smart filtering working

**Week 5: Polish & Optimization**
- Performance tuning
- Cross-platform testing
- Documentation
- **Status:** Ready for production

**Week 6+: Phase 4 (Advanced Notifications)**
- Platform-specific wake-up (if needed)
- Optional feature
- **Status:** Enhanced responsiveness

### Acceptance Criteria

**Phase 1 Complete:**
- [ ] `StreamingResultsCollector` compiles and passes unit tests
- [ ] `ResultUpdateNotifier` compiles and passes unit tests
- [ ] `SearchWorker` integration verified with old API still working
- [ ] No regressions in existing tests
- [ ] Cross-platform compile successful

**Phase 2 Complete:**
- [ ] UI shows partial results while searching
- [ ] Virtual scrolling works with partial results
- [ ] Status bar shows result count increasing
- [ ] No visual glitches or jank
- [ ] Users can interact while searching

**Phase 3 Complete:**
- [ ] Results appear sorted/filtered while search ongoing
- [ ] Final sort completes quickly after search ends
- [ ] Filters work on partial results
- [ ] No sorting/filtering regressions

**Overall Complete:**
- [ ] All metrics achieved (see Success Criteria section)
- [ ] Documentation complete
- [ ] Code review approved
- [ ] Ready for release

---

## Risk Assessment & Mitigation

### Risk 1: Thread Safety Issues

**Risk:** Race conditions in streaming pipeline

**Probability:** Medium (threading is complex)  
**Impact:** Data corruption, crashes, hard-to-reproduce bugs  

**Mitigation:**
1. Use proven patterns (shared_mutex, atomic flags)
2. Comprehensive thread sanitizer testing
3. Peer code review with threading expert
4. Stress tests with 100+ concurrent threads

**Action Items:**
- [ ] Design review by threading expert
- [ ] Add thread sanitizer to CI
- [ ] Stress tests before release

---

### Risk 2: Performance Regression

**Risk:** Streaming adds overhead that degrades performance

**Probability:** Low (changes are additive)  
**Impact:** Users notice slowdown in searches  

**Mitigation:**
1. Overhead analysis upfront (see Performance section)
2. Performance benchmarks before/after
3. Profiling to identify hot paths
4. Tuning batch size and intervals

**Action Items:**
- [ ] Benchmark all phases before release
- [ ] Profile with 10M+ results
- [ ] Compare old vs new implementation metrics

---

### Risk 3: UI Glitches

**Risk:** Virtual scrolling or rendering breaks with partial results

**Probability:** Medium (UI changes can be subtle)  
**Impact:** Visual artifacts, crashes, poor user experience  

**Mitigation:**
1. Visual regression testing
2. Test at various result counts (100, 1K, 10K, 1M)
3. Cross-platform testing (Windows, macOS, Linux)
4. User acceptance testing

**Action Items:**
- [ ] Set up visual regression tests
- [ ] Manual testing with increasing result counts
- [ ] QA sign-off before release

---

### Risk 4: Breaking Change to API

**Risk:** Changes to SearchWorker force updates in consuming code

**Probability:** Low (backward compatibility planned)  
**Impact:** Refactoring work in other code, delays  

**Mitigation:**
1. Keep existing API intact
2. Add new methods, don't remove
3. Test backward compatibility path
4. Migration guide if needed

**Action Items:**
- [ ] Audit all SearchWorker callers
- [ ] Verify old API still works
- [ ] Document migration path if any

---

### Risk 5: Complexity Explosion

**Risk:** Implementation becomes too complex, hard to maintain

**Probability:** Low (design is simple)  
**Impact:** Increased bugs, harder to debug, maintenance burden  

**Mitigation:**
1. Incremental phases (start small, add features)
2. Keep components focused and independent
3. Comprehensive documentation
4. Code review for complexity

**Action Items:**
- [ ] Design review for complexity
- [ ] Document design decisions
- [ ] Keep components <200 lines each

---

## Alternative Approaches Considered

### Alternative 1: Simple Streaming (No Batching)

**Concept:** Notify UI on EVERY result added

**Pros:**
- Simplest implementation
- Results appear fastest
- Minimal buffering

**Cons:**
- Excessive notifications (might overwhelm UI)
- Poor performance (too many updates)
- Thrashing garbage collection
- Noisy for large result sets

**Why Not Chosen:** Too much overhead for scale

---

### Alternative 2: Pagination Model

**Concept:** Divide results into fixed pages (e.g., 100 results per page)

**Pros:**
- Simple to understand
- Clear pagination UX
- Predictable memory usage

**Cons:**
- User must click "Next" to see more
- Not true streaming (not responsive)
- Doesn't match modern UX expectations
- Poor for large result sets

**Why Not Chosen:** Not responsive enough

---

### Alternative 3: Result Priority Queue

**Concept:** Results added to priority queue (sorted as they arrive)

**Pros:**
- Results arrive pre-sorted
- No resort needed at end
- Seamless ordering

**Cons:**
- Complex sorting logic in hot path
- Performance degradation during search
- Hard to maintain sort order across new batches
- Unpredictable sort criteria while searching

**Why Not Chosen:** Too complex, violates principle of separation

---

### Alternative 4: Reactive Streaming (RxCpp/Folly)

**Concept:** Use reactive library for streaming pipeline

**Pros:**
- Powerful abstractions
- Proven in industry
- Composable operations

**Cons:**
- External dependency (violates constraints)
- Steep learning curve
- Overkill for this use case
- Harder to integrate with C++17 base

**Why Not Chosen:** Violates "no external dependencies" constraint

---

## Conclusion & Recommendation

### Summary

This document has analyzed the challenge of displaying responsive UI when searching large indexes (millions of files) and proposed a pragmatic, phased approach to implementing streaming partial results.

**Key Findings:**
1. **Current Architecture is Sound** - Component-based design with lazy loading already supports incremental updates
2. **Minimal Changes Needed** - New components are independent, changes to existing code are localized
3. **Proven Patterns** - Uses well-established design patterns (Observer, Pipeline, Throttling)
4. **Backward Compatible** - Existing code continues working unchanged
5. **Phased Approach** - Can be implemented incrementally with clear success criteria

### Recommendation

**Implement a Streaming Results Pipeline with multi-tier buffering, starting with Phase 1 (Core Infrastructure).**

**Rationale:**
1. **Non-Breaking:** Backward compatible with existing code
2. **Incremental:** Can deliver value after each phase
3. **Pattern-Driven:** Uses proven design patterns
4. **Coherent:** Aligns with existing architecture and lazy loading
5. **Pragmatic:** Minimizes complexity while maximizing user responsiveness

**Expected Benefits:**
- First results visible in <100ms (currently 1-10 seconds)
- 50% of results visible within 500ms
- UI remains responsive during search
- No jank or freezing
- Familiar UX matching modern applications

**Next Steps:**
1. Review and approve design
2. Implement Phase 1 (Core Infrastructure)
3. Integration testing and review
4. Proceed to Phase 2 (UI Integration)
5. Performance validation
6. Release and monitor

---

## Appendix: Code Sketches & Examples

### Example 1: Basic Usage Pattern

```cpp
// In SearchWorker::StartSearch()
streaming_collector_ = std::make_unique<StreamingResultsCollector>(
    500,    // batch size
    50      // notification interval (ms)
);

// Register callback for UI updates
if (streaming_collector_) {
    streaming_collector_->OnBatchAvailable([this]() {
        // Could trigger UI wake-up here if needed
        LOG_DEBUG("New batch available");
    });
}

// In SearchWorker::WorkerThread()
// When search engine produces results:
for (const auto& result : search_results) {
    if (streaming_collector_) {
        streaming_collector_->AddResult(result);
    }
}

// When search complete:
if (streaming_collector_) {
    streaming_collector_->MarkSearchComplete();
}

// In UI code (ResultsTable::Render())
if (!state.resultsComplete && state.showingPartialResults) {
    // Display partial results with "Searching..." indicator
    auto display_results = search_worker.GetPartialResults();
    RenderPartialTable(display_results);
} else {
    // Display final results (existing behavior)
    auto display_results = SearchResultsService::GetDisplayResults(state);
    RenderFinalTable(display_results);
}
```

### Example 2: Threading Model

```cpp
// Main Thread (UI)
SearchWorker search_worker(file_index);
search_worker.StartSearch(params);

// ... each frame:
if (search_worker.HasPartialResults()) {
    // Render partial results
    auto results = search_worker.GetPartialResults();
    RenderTable(results);
}

// Background Thread (Search)
// SearchWorker thread processes search:
while (has_more_chunks) {
    auto chunk_results = process_chunk();
    for (auto& result : chunk_results) {
        streaming_collector.AddResult(result);  // Thread-safe
        // UI gets notified periodically (batching)
    }
}
streaming_collector.MarkSearchComplete();

// UI Thread (Next Frame)
// If notified of new batch:
state.partialResults = search_worker.GetPartialResults();
state.resultsBatchNumber++;
// Render will automatically show updated count
```

### Example 3: Integration with Existing Sort/Filter

```cpp
// Before: Results finalized before any filtering
if (search_complete) {
    SearchResultsService::HandleTableSorting(state, file_index, thread_pool);
    // Sorts entire result set
}

// After: Results can be filtered progressively
void UpdatePartialFilters(GuiState& state) {
    if (!state.resultsComplete) {
        // Apply filters to partial results for preview
        ApplyTimeFilter(state.partialResults, state.timeFilter);
        ApplyTimeFilter(state.sizeFilteredResults, state.sizeFilter);
        // Don't sort yet - just preview
    } else {
        // Now do full sort and filter on complete set
        SearchResultsService::HandleTableSorting(state, file_index, thread_pool);
    }
}
```

---

## References & Further Reading

1. **Reactive Programming Pattern**
   - Microsoft Reactive Extensions (RxCpp) - concept source
   - Rx design patterns for observable sequences

2. **Thread Safety Patterns**
   - "C++ Concurrency in Action" - Williams, A.
   - Double-check locking pattern (used in LazyAttributeLoader)

3. **ImGui Performance**
   - ImGui Virtual Scrolling (ImGuiListClipper)
   - Immediate mode rendering best practices

4. **Design Patterns**
   - Gang of Four Design Patterns - Observer, Pipeline
   - Architecture patterns for concurrent systems

5. **Project Resources**
   - `docs/design/ARCHITECTURE_COMPONENT_BASED.md` - Current architecture
   - `docs/design/LAZY_ATTRIBUTE_LOADER_DESIGN.md` - Lazy loading pattern
   - `src/search/SearchWorker.h` - Current search threading

---

**Document End**

*For questions or clarifications about this design, refer to the author or the architecture team.*
