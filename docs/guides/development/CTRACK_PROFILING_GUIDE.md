# Profiling FindHelper with Compaile/ctrack

This guide explains how to profile and benchmark function-level execution in the **FindHelper** application using [Compaile/ctrack](https://github.com/Compaile/ctrack)—a lightweight, single-header, high-performance C++ profiling and tracking library.

---

## Table of Contents

1. [Overview](#overview)
2. [Key Features of ctrack](#key-features-of-ctrack)
3. [Integration into FindHelper](#integration-into-findhelper)
   - [Method 1: Header-Only Integration (Vendored)](#method-1-header-only-integration-vendored)
   - [Method 2: CMake Integration via FetchContent](#method-2-cmake-integration-via-fetchcontent)
4. [Core Instrumentation Macros](#core-instrumentation-macros)
   - [Standard, Development, and Production Macros](#standard-development-and-production-macros)
   - [Custom Naming](#custom-naming)
   - [Scope-Based Tracking Mechanics](#scope-based-tracking-mechanics)
   - [Disabling ctrack at Compile Time](#disabling-ctrack-at-compile-time)
5. [Targeted Instrumentation for FindHelper Subsystems](#targeted-instrumentation-for-findhelper-subsystems)
   - [SearchWorker (Query Execution)](#searchworker-query-execution)
   - [FileIndex (Index Lookups and Traversal)](#fileindex-index-lookups-and-traversal)
   - [FolderSizeAggregator (Async Folder Size Calculation)](#foldersizeaggregator-async-folder-size-calculation)
   - [SearchController (Batch Processing & State Updates)](#searchcontroller-batch-processing--state-updates)
   - [UIRenderer & ResultsTable (Frame Rendering)](#uirenderer--resultstable-frame-rendering)
   - [UsnMonitor (NTFS Change Journal Processing)](#usnmonitor-ntfs-change-journal-processing)
6. [Retrieving and Customizing Profiling Results](#retrieving-and-customizing-profiling-results)
   - [Console Output](#console-output)
   - [String Logging and File Export](#string-logging-and-file-export)
   - [Structured Data Table Access](#structured-data-table-access)
   - [Customizing Output Settings](#customizing-output-settings)
7. [Understanding ctrack Metrics](#understanding-ctrack-metrics)
8. [Real-World macOS Case Study Analysis](#real-world-macos-case-study-analysis)
9. [Cross-Platform Build Considerations](#cross-platform-build-considerations)
10. [Best Practices](#best-practices)

---

## Overview

While traditional profilers (such as Apple Instruments, MSVC Performance Profiler, or Valgrind/perf) offer system-wide CPU sampling and memory tracking, **ctrack** provides explicit, scope-based timing metrics embedded directly in C++ source code.

`ctrack` is designed for:
- **Zero/minimal overhead**: Capable of recording tens of millions of timing events per second with virtually no runtime impact.
- **Multithreaded profiling**: Accurate handling of concurrent function execution across worker threads.
- **Production & Development tracking**: Granular profiling macros that can be conditionally compiled in or out.
- **Instant bottleneck identification**: Metrics such as *active time* and *active exclusive time* allow quick isolation of hot paths.

---

## Key Features of ctrack

- **Single-header library**: Written in C++17 (`ctrack.hpp`) with zero required external dependencies.
- **RAII-based tracking**: Functions and blocks are automatically measured when `CTRACK` objects enter and exit scope.
- **Thread-safe event aggregation**: Handles parallel searches and worker thread pools seamlessly.
- **Flexible output channels**: Renders colored console tables, std::string summary logs, or structured data tables for custom processing (e.g. JSON export).
- **Percentile filtering & statistical distribution**: Calculates min, mean, median, max, standard deviation, and customizable percentile intervals (e.g., `[1-99]`).

---

## Integration into FindHelper

### Method 1: Header-Only Integration (Vendored)

Because `ctrack` is a single-header header-only library, you can place `ctrack.hpp` into FindHelper's `external/` folder:

1. Download `ctrack.hpp` into `external/ctrack/ctrack.hpp`.
2. Add the directory to `find_helper`'s include paths in `CMakeLists.txt`:

```cmake
target_include_directories(find_helper PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/external/ctrack
)
```

3. Include `ctrack.hpp` in the source files where profiling is needed:

```cpp
#include "ctrack.hpp"
```

### Method 2: CMake Integration via FetchContent

For projects managing external dependencies via CMake, add the following to `CMakeLists.txt`:

```cmake
include(FetchContent)
FetchContent_Declare(
    ctrack
    GIT_REPOSITORY https://github.com/Compaile/ctrack.git
    GIT_TAG        main
)
FetchContent_MakeAvailable(ctrack)

target_link_libraries(find_helper PRIVATE ctrack::ctrack)
```

---

## Core Instrumentation Macros

### Standard, Development, and Production Macros

`ctrack` provides three levels of instrumentation macros:

| Macro | Purpose |
|---|---|
| `CTRACK` | Standard profiling macro placed at the top of a function or scope. |
| `CTRACK_DEV` | Profiling macro intended exclusively for development builds. |
| `CTRACK_PROD` | Profiling macro retained in production builds for telemetry/monitoring. |

### Custom Naming

By default, `CTRACK` records the enclosing function name. To track specific blocks within a large function or give a scope a distinct label, use custom naming macros:

```cpp
CTRACK_NAME("SearchWorker::ExecuteQueryPhase1");
CTRACK_DEV_NAME("FileIndex::BuildFlatHashMap");
CTRACK_PROD_NAME("UsnMonitor::ProcessRecordBatch");
```

### Scope-Based Tracking Mechanics

`ctrack` relies on RAII (Resource Acquisition Is Initialization). Events are recorded when the `CTRACK` object goes out of scope.

#### Function-Level Tracking
```cpp
void SearchWorker::PerformSearch() {
    CTRACK; // Measures from function entry to return
    // ... search logic ...
}
```

#### Block-Level Tracking
When you want to measure a specific section within a function without measuring the entire function (or before printing results), use an explicit block `{ ... }`:

```cpp
void RunBatchOperation() {
    {
        CTRACK; // Start tracking this block
        ExecuteHeavyProcessing();
    } // CTRACK records event upon exiting the block

    // Print profiling summary after the heavy block completes
    ctrack::result_print();
}
```

### Disabling ctrack at Compile Time

To ensure zero overhead in non-profiling or production release builds, define `CTRACK_DISABLE`:

```cmake
# In CMakeLists.txt for standard non-profiling builds
target_compile_definitions(find_helper PRIVATE CTRACK_DISABLE)
```

Alternatively, to disable only development macros while keeping production macros active:

```cmake
target_compile_definitions(find_helper PRIVATE CTRACK_DISABLE_DEV)
```

**In this project**: the `ENABLE_CTRACK_PROFILING` CMake option (default `ON`) wraps `CTRACK_DISABLE`.
Shipping/PGO builds pass `-DENABLE_CTRACK_PROFILING=OFF` — see `scripts/pgo_build.ps1` and the
release artifact builds in `.github/workflows/build.yml`. Application exit-report logging is also
skipped when instrumentation is compiled out.

---

## Targeted Instrumentation for FindHelper Subsystems

Below are concrete examples demonstrating how to instrument key FindHelper subsystems with `ctrack`.

### SearchWorker (Query Execution)

Profile parallel search worker iterations and query execution in `src/search/SearchWorker.cpp`:

```cpp
#include "ctrack.hpp"

void SearchWorker::ExecuteSearchTask(const SearchContext& context) {
    CTRACK; // Measures total execution time per search task

    {
        CTRACK_NAME("SearchWorker::StringMatchingPhase");
        // Perform string matching against file path index
    }

    {
        CTRACK_NAME("SearchWorker::ResultCollectionPhase");
        // Collect matching SearchResult items
    }
}
```

### FileIndex (Index Lookups and Traversal)

Measure path lookups and hash map access in `src/index/FileIndex.cpp`:

```cpp
#include "ctrack.hpp"

const FileEntry* FileIndex::GetEntry(uint64_t file_id) const {
    CTRACK_DEV; // Track index lookup overhead in dev builds
    auto it = entries_.find(file_id);
    return (it != entries_.end()) ? &it->second : nullptr;
}
```

### FolderSizeAggregator (Async Folder Size Calculation)

Profile asynchronous folder size computation in `src/search/FolderSizeAggregator.cpp`:

```cpp
#include "ctrack.hpp"

void FolderSizeAggregator::ComputeFolderSizesBatch(const std::vector<std::string>& folder_paths) {
    CTRACK; // Track folder aggregation batch duration
    for (const auto& path : folder_paths) {
        {
            CTRACK_NAME("FolderSizeAggregator::CalculateSingleFolder");
            CalculateSizeRecursive(path);
        }
    }
}
```

### SearchController (Batch Processing & State Updates)

Monitor streaming batch reconciliation on the UI thread in `src/search/SearchController.cpp`:

```cpp
#include "ctrack.hpp"

void SearchController::PollResults(GuiState& state) {
    CTRACK_DEV_NAME("SearchController::PollResults");

    if (HasPendingBatches()) {
        {
            CTRACK_NAME("SearchController::ReconcileAttributes");
            ReconcileComputedDirectoryAttributes(state);
        }
    }
}
```

### UIRenderer & ResultsTable (Frame Rendering)

Track Dear ImGui table rendering and layout timing in `src/ui/ResultsTable.cpp`:

```cpp
#include "ctrack.hpp"

void ResultsTable::Render(GuiState& state) {
    CTRACK_DEV_NAME("ResultsTable::RenderFrame");

    // ImGui table rendering loop
}
```

### UsnMonitor (NTFS Change Journal Processing)

Measure USN journal event processing on Windows in `src/usn/UsnMonitor.cpp`:

```cpp
#include "ctrack.hpp"

void UsnMonitor::ProcessUsnRecordBatch(const std::vector<USN_RECORD_V2>& records) {
    CTRACK_PROD_NAME("UsnMonitor::ProcessUsnRecordBatch");

    for (const auto& record : records) {
        // Process USN close reasons and update index
    }
}
```

---

## Retrieving and Customizing Profiling Results

### Console Output

To print formatted, color-coded profiling results to standard output:

```cpp
#include "ctrack.hpp"

// Prints detail tables followed by the summary table
ctrack::result_print();
```

### String Logging and File Export

To retrieve profiling output as a `std::string` for writing to FindHelper log files:

```cpp
#include "ctrack.hpp"
#include "utils/LoggingUtils.h"

std::string report = ctrack::result_as_string();
LOG_INFO_BUILD("Performance Profile:\n{}", report);
```

### Structured Data Table Access

For programmatic access to raw metrics (such as emitting JSON telemetry or UI display):

```cpp
#include "ctrack.hpp"

// Retrieve summary and detail tables as structured C++ objects
auto tables = ctrack::result_get_tables();
auto summary = ctrack::result_get_summary_table();
auto details = ctrack::result_get_detail_table();

for (const auto& row : summary.rows) {
    // Process row.filename, row.function_name, row.line, row.call_count, etc.
}
```

### Customizing Output Settings

You can filter noise (such as extremely fast functions) or adjust percentile bounds using `ctrack_result_settings`:

```cpp
#include "ctrack.hpp"

ctrack_result_settings settings;
settings.non_center_percent = 2;                        // Center interval range [2-98]
settings.min_percent_active_exclusive = 1.0;            // Exclude events < 1.0% active time
settings.percent_exclude_fastest_active_exclusive = 5.0; // Filter out fastest 5% of calls

std::string filtered_report = ctrack::result_as_string(settings);
```

---

## Real-World macOS Case Study Analysis

Analyzing actual ctrack output captured on exit during a 7-minute macOS session reveals key performance characteristics and demonstrates how to interpret ctrack metrics:

### Profiling Run Summary
- **Session Duration**: 422.60 seconds (~7 minutes)
- **Tracked Execution Time**: 130.23 seconds (30.82% of total session time)

### Key Metric Findings & Diagnostics

#### 1. ImGui UI Frame Rendering (`ResultsTable::Render`)
- **Calls**: 21,083 frames (rendered at ~50 FPS)
- **Time Active (`time a`)**: 104.47s
- **Time Active Exclusive (`time ae`)**: 86.22s (20.40% of total ctracked time)
- **Mean Frame Time**: 4.65 ms (well within 16.6ms 60 FPS target budget)
- **Diagnostics**: UI rendering accounts for the majority of main thread activity. The max spike (409 ms) occurs during initial window layout/font creation.

#### 2. File Attribute Loading & OS I/O Breakdown (`LazyAttributeLoader::LoadAttributes` vs `GetFileSize`)
- **Calls**: 646,487 `LoadAttributes` calls vs 1,268,121 `GetFileSize` calls
- **Time Active Exclusive (`time ae`)**: 18.26s in `LoadAttributes` (17.32% of ctracked time) vs 718.57 ms in `GetFileSize`
- **Mean Duration per Call**: 28.2 μs for `LoadAttributes` (OS `GetFileAttributes`/`stat` call) vs 5.79 μs median for `GetFileSize`
- **Diagnostics**: Finer-grained instrumentation isolates the root bottleneck: 96.2% of attribute loading time is spent directly inside OS file system I/O (`LoadAttributes`), proving that lock contention and hash table lookup overhead in `LazyAttributeLoader` are negligible (<3.8%).

#### 3. Directory Crawling & Batch Insertion (`FolderCrawler::FlushBatch` vs `FileIndex::InsertPaths`)
- **Calls**: 309 batch insertions across `FolderCrawler::Crawl` (taking 3.18s)
- **Time Active Exclusive (`time ae`)**: 1.10s in `FileIndex::InsertPaths` (1.04% of ctracked time) vs 579.96 μs in `FolderCrawler::FlushBatch`
- **Mean Duration per Batch**: 3.56 ms per batch in `FileIndex::InsertPaths`
- **Diagnostics**: Of the 3.18s spent crawling directories, 2.08s (65%) is spent in directory enumeration (`WorkerThread`) and 1.10s (35%) in batch mutex lock & map insertion (`FileIndex::InsertPaths`). `FlushBatch` overhead outside `InsertPaths` is under 1 millisecond total (<0.1%).

#### 4. High-Efficiency Path Truncation (`ResultsTable::RenderPathColumnWithEllipsis`)
- **Calls**: 99,682 path column renders across 4,341 visible frames
- **Time Active Exclusive (`time ae`)**: 95.75 ms total (0.09% of ctracked time)
- **Mean Duration per Call**: 960.00 ns (~0.96 μs per column render)
- **Diagnostics**: Confirming that path truncation, font calculations, and string highlight clipping in `ResultsTable` operate with sub-microsecond efficiency per cell.

#### 5. Multithreaded Active Exclusive vs Active Time (`FolderSizeAggregator::ComputeSizeBatch`)
- **Calls**: 1 batch computation across background threads
- **Total Active Time (`time a`)**: 18.19s (wall-clock duration spent computing sizes)
- **Active Exclusive Time (`time ae`)**: 557.81 ms (only 0.53% net computation!)
- **Key Insight**: ctrack's `time ae` metric clearly proves that 96.9% of `FolderSizeAggregator`'s wall-clock time was spent inside tracked child functions (specifically `LazyAttributeLoader::GetFileSize`), not in `FolderSizeAggregator`'s own data structures or lock contention.

> **Post-optimization note (2026-09-09, parallel Pass 2):** the stat sweep above was
> single-threaded (`threads=1`, one batch ≈ 18–19s). ComputeSizeBatch now dedupes miss ids
> and stats them on up to 8 transient threads spawned per batch (~5.4s measured, ≈3.5×).
> Expect `LazyAttributeLoader::LoadAttributes` to report ~17 threads during a batch, and
> `ComputeSizeBatch` `time a ≈ time ae` (child stat events run on the spawned threads, so
> they no longer nest in the worker's thread-local event tree). The transient threads are
> deliberate — see the invariant comment in `FolderSizeAggregator::ComputeSizeBatch` for
> why the shared `SearchThreadPool` is not used.

#### 6. Parallel Search Engine Execution (`SearchWorker::RunFilteredSearchPath`)
- **Calls**: 5 searches
- **Total Search Time**: 152.96 ms across all 5 searches
- **Mean Search Duration**: 30.59 ms per search
- **Diagnostics**: Search query execution against the in-memory path index is extremely fast and scalable (~30 ms per query).

---

## Understanding ctrack Metrics

When viewing `ctrack` reports, metrics are presented in auto-scaled units (`ns`, `mcs` [μs], `ms`, `s`):

| Metric | Definition |
|---|---|
| `min`, `mean`, `med`, `max` | Minimum, arithmetic mean, median, and maximum execution durations. |
| `sd` | Standard Deviation of execution times. |
| `cv` | Coefficient of Variation (`sd / mean`), showing timing variability regardless of scale. |
| `time a` (time active) | Total wall-clock duration the function was active. In multi-threaded workloads, concurrent calls across threads do not artificially multiply this time. |
| `time ae` (time active exclusive) | Total active time excluding time spent in tracked child functions. This isolates the function's own net computation. |
| `time [x-y]` | Percentile interval (e.g. `[1-99]`) excluding extreme outliers. |
| `time acc` | Accumulated total execution time summed across all calls. |
| `threads` | Total count of distinct threads that executed the tracked scope. |

---

## Cross-Platform Build Considerations

### C++ Standard Requirements
- Requires **C++17** or higher (`CMAKE_CXX_STANDARD 17`). FindHelper already mandates C++17 across all platforms.

### Parallel Execution Policy (TBB vs Sequential)
By default, `ctrack` uses standard C++ parallel algorithms (`std::execution::par`) to compute statistics.

- **GCC / Clang (Linux / macOS)**: If GCC/Clang requires Intel TBB for `<execution>`, link `-ltbb`. If TBB is not installed, define `CTRACK_DISABLE_EXECUTION_POLICY` to switch `ctrack` to sequential result calculation without affecting recording speed:

```cmake
target_compile_definitions(find_helper PRIVATE CTRACK_DISABLE_EXECUTION_POLICY)
```

- **MSVC (Windows)**: C++17 parallel algorithms in MSVC do not require external TBB libraries. `ctrack` runs natively out of the box.

---

## Best Practices

1. **Profile Release Builds**: Always benchmark using `Release` or `RelWithDebInfo` builds (`cmake -B build -DCMAKE_BUILD_TYPE=Release`). Debug builds introduce artificial overhead (un-inlined calls, debug assertions).
2. **Use Representative Workloads**: Test with realistic file index sizes (100,000+ files) and actual search queries.
3. **Isolate Hot Paths with Explicit Scope Blocks**: Use `{ CTRACK_NAME("SubTask"); ... }` around critical loops instead of wrapping entire multi-thousand-line functions.
4. **Compare Before and After**: Capture a baseline profile before optimizing, perform the code refactor, and run the same workload again to verify speedup.
5. **Disable in Final Shipping Binaries**: Use `CTRACK_DISABLE` in production builds unless specifically building a telemetry-enabled profiling executable.

---

*See also:*
- [MACOS_PROFILING_GUIDE.md](MACOS_PROFILING_GUIDE.md) — System-level Apple Instruments and Xcode profiling.
- [PGO_SETUP.md](../building/PGO_SETUP.md) — Profile-Guided Optimization on Windows with MSVC.
