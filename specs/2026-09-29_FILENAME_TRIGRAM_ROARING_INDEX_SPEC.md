# Specification: Filename Trigram Index with Roaring Bitmaps (FindHelper)

**Feature Name:** Inverted Filename Trigram Index with Roaring Bitmaps  
**Status:** Completed  
**Target Version:** 2026-Q4 (Delivered 2026-10-01)  
**Date:** 2026-09-29 (Completed: 2026-10-01)  
**Target Platforms:** macOS (main dev), Windows (primary target), Linux  
**Language Standard:** C++17  

---

## 1. Executive Summary & Objectives

### 1.1 Problem Statement
Currently, FindHelper executes searches using an exhaustive parallel linear scan across Structure-of-Arrays (SoA) buffers (`PathStorage::SoAView`). On modern multicore hardware, scanning 700,000+ files takes **6 to 10 milliseconds**. While fast, this bounds the search throughput and introduces CPU core utilization for queries that only match a handful of files out of a million.

### 1.2 Proposed Solution
Implement an in-memory **Inverted Filename Trigram Index** backed by **[CRoaring / roaring-cpp](https://github.com/RoaringBitmap/CRoaring)**.

* **Narrow Scope (Filenames Only):** Index only the filename slice (`path + filename_offset`), avoiding the massive redundancy, low selectivity, and folder-rename invalidation overhead of indexing full paths.
* **Pre-filter / Candidate Generator:** The trigram index acts as an ultra-fast candidate generator (< 0.05 ms) that reduces 700,000 rows to 10–50 candidate row indices.
* **Verification Layer:** The existing `PatternMatcher` variant verifies surviving candidates, preserving 100% precision and case sensitivity.
* **Seamless Regex Support:** Required literal substrings extracted from regex patterns (via `RegexSearchPrefilter`) feed into the trigram index, accelerating regex matching by 50×–100×.
* **Zero-Regression Fallback:** For short queries (< 3 characters) or queries yielding excessive candidates (> 20,000), the engine immediately falls back to the existing parallel chunk scan.

---

## 2. Architecture & Design

### 2.1 C4 Architecture Context (Level 3 - Component View)

```
                       ┌────────────────────────────────────────────────────────┐
                       │                   Search Context                       │
                       │ (query, case_sensitive, extension_set, regex, etc.)    │
                       └──────────────────────────┬─────────────────────────────┘
                                                  │
                                                  ▼
                                    ┌───────────────────────────┐
                                    │    TrigramQueryPlanner    │
                                    │  - Extract Trigrams       │
                                    │  - Cardinality check      │
                                    └─────────────┬─────────────┘
                                                  │
                         ┌────────────────────────┴────────────────────────┐
                         │ Can extract ≥ 1 trigram?                        │
                         │ Candidate cardinality ≤ 20,000?                 │
                         └──────────────┬───────────────────┬──────────────┘
                                        │ YES               │ NO (fallback)
                                        ▼                   ▼
    ┌──────────────────────────────────────────────┐    ┌──────────────────────────────────┐
    │          Trigram Candidate Query             │    │    ParallelSearchEngine (SoA)    │
    │  - Roaring bitwise AND: c = t1 & t2 & t3     │    │   (Existing hybrid chunk scan)   │
    │  - Latency: ~0.02 – 0.05 ms                  │    │   Latency: 6 – 10 ms             │
    └──────────────────────┬───────────────────────┘    └────────────────┬─────────────────┘
                           │                                             │
                           ▼                                             ▼
    ┌──────────────────────────────────────────────┐                     │
    │          Sparse Candidate Verifier           │                     │
    │  - Iterate surviving IDs                     │                     │
    │  - Check is_deleted, item_type, ext_filter   │                     │
    │  - Run PatternMatcher on candidate filenames │                     │
    │  - Latency: ~0.02 – 0.05 ms                  │                     │
    └──────────────────────┬───────────────────────┘                     │
                           │                                             │
                           ▼                                             ▼
    ┌──────────────────────────────────────────────────────────────────────────────────────┐
    │                                  SearchResultBatch                                   │
    └──────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Data Structures & Representation

### 3.1 Trigram Encoding
A trigram consists of 3 consecutive lowercase bytes. We pack it into a 24-bit unsigned integer:
```cpp
// 24-bit compact trigram key
using TrigramKey = uint32_t;

[[nodiscard]] constexpr TrigramKey MakeTrigramKey(uint8_t c0, uint8_t c1, uint8_t c2) noexcept {
  return (static_cast<uint32_t>(c0) << 16) |
         (static_cast<uint32_t>(c1) << 8)  |
          static_cast<uint32_t>(c2);
}
```

Case folding:
* Trigrams are normalized to ASCII lowercase (`ToLowerChar`) upon insertion and extraction.
* For UTF-8 multi-byte sequences, byte triplets are indexed as-is (binary transparency).

### 3.2 Inverted Index Storage
```cpp
namespace search {

class FilenameTrigramIndex {
public:
  FilenameTrigramIndex() = default;

  // Build index from SoA view (initial populator / background rebuild)
  void Build(const PathStorage::SoAView& soa_view, size_t storage_size);

  // Incremental updates from USN Journal / FileSystem watcher
  void AddEntry(uint32_t row_id, std::string_view filename);
  void RemoveEntry(uint32_t row_id, std::string_view filename);

  // Query: Returns nullopt if query cannot be converted to trigrams (fallback trigger)
  [[nodiscard]] std::optional<roaring::Roaring> QueryCandidates(
      std::string_view query, 
      size_t max_candidates = 20000) const;

  // Memory usage inspection
  [[nodiscard]] size_t GetMemoryUsageBytes() const noexcept;
  [[nodiscard]] size_t GetTrigramCount() const noexcept;

private:
  // Postings table: Map packed trigram key to roaring bitmap of row indices (0..N-1)
  // std::unordered_map or flat dense table for common ASCII
  std::unordered_map<TrigramKey, roaring::Roaring> posting_lists_;
  mutable std::shared_mutex mutex_;
};

} // namespace search
```

### 3.3 Memory Footprint Estimation (700,000 files)
* Average filename length: ~18 characters.
* Trigrams per filename: $18 - 3 + 1 = 16$ trigrams.
* Total indexed postings: $700,000 \times 16 \approx 11.2\text{ million bit entries}$.
* Unique trigrams in real filenames: ~12,000 to 18,000 distinct keys.
* `CRoaring` compression efficiency:
  * Dense ranges compress with Run-Length Encoding (RLE) or bitsets (65,536 bits in 8 KB).
  * Sparse ranges store sorted 16-bit integers in contiguous arrays.
* **Estimated RAM:** **16 MB – 24 MB total**.

---

## 4. Query Planning & Matching Pipeline

### 4.1 Query Decomposition

| Pattern Type | Example | Extracted Trigrams | Match Strategy |
| :--- | :--- | :--- | :--- |
| **Substring** | `"document"` | `doc`, `ocu`, `cum`, `ume`, `men`, `ent` | $T_1 \cap T_2 \cap T_3 \cap \dots$ |
| **Prefix** | `"test*"` | `tes`, `est` | $T_1 \cap T_2$ |
| **Wildcard / Glob** | `"cat*report*.pdf"` | `cat`, `rep`, `epo`, `por`, `ort`, `.pd`, `pdf` | Intersect all valid literals |
| **Regex (`rs:`)** | `rs:config_.*\.json` | `con`, `onf`, `nfi`, `fig`, `ig_`, `.js`, `jso`, `son` | Intersect longest required literals |
| **Fuzzy / Short** | `"ab"`, `"*.c"` | *(None)* | **Fallback to Parallel Scan** |

### 4.2 Candidate Set Intersection Algorithm
1. Extract candidate trigram keys.
2. Filter keys that exist in `posting_lists_`. If any required trigram has 0 hits, the entire query immediately returns 0 results (0 ms short-circuit!).
3. Sort trigrams in **ascending order of bitmap cardinality** (most selective first).
4. Perform bitwise AND intersections:
   ```cpp
   roaring::Roaring result = posting_lists_[sorted_trigrams[0]];
   for (size_t i = 1; i < sorted_trigrams.size(); ++i) {
     result &= posting_lists_[sorted_trigrams[i]];
     if (result.isEmpty()) {
       break; // Early exit
     }
   }
   ```
5. If `result.cardinality() > max_candidates`, return `std::nullopt` to trigger standard parallel chunk scan.

### 4.3 Sparse Candidate Verification Loop
When candidate count is small (e.g., $N \le 20,000$, typically 10–50):
```cpp
// Run on a single thread or two lightweight tasks
for (const uint32_t row_id : candidate_bitmap) {
  if (soa_view.is_deleted[row_id] != 0) continue;
  if (item_type_filter == ItemTypeFilter::FoldersOnly && soa_view.is_directory[row_id] == 0) continue;
  if (item_type_filter == ItemTypeFilter::FilesOnly && soa_view.is_directory[row_id] != 0) continue;

  const size_t path_len = parallel_search_detail::GetPathLength(soa_view, row_id, storage_size);
  if (!parallel_search_detail::MatchesExtensionFilter(soa_view, row_id, path_len, has_ext_filter, context)) {
    continue;
  }
  if (!context.extension_only_mode && !parallel_search_detail::MatchesPatterns(
          soa_view, row_id, path_len, context, filename_matcher, path_matcher)) {
    continue;
  }

  // Matching hit found
  parallel_search_detail::AppendHitToBatch(batch, soa_view, row_id, path_len);
}
```

---

## 5. Third-Party Library Integration: CRoaring

### 5.1 Why CRoaring?
* **High Performance:** Written by Daniel Lemire, highly tuned for modern CPUs with AVX2, AVX-512, and ARM NEON intrinsics.
* **Header-only C++ wrapper:** Provides `roaring/roaring.hh` with idiomatic C++ operators (`&`, `|`, `cardinality()`, iterators).
* **Permissive License:** Apache 2.0 / MIT.
* **Portability:** Works identically across Windows (MSVC), macOS (Clang), and Linux (GCC).

### 5.2 CMake Integration
Add via `FetchContent` in `CMakeLists.txt`:
```cmake
include(FetchContent)
FetchContent_Declare(
    croaring
    GIT_REPOSITORY https://github.com/RoaringBitmap/CRoaring.git
    GIT_TAG        v2.0.1  # Or latest stable release
)
FetchContent_MakeAvailable(croaring)
target_link_libraries(find_helper_core PUBLIC roaring)
```

---

## 6. USN Journal & Invalidation Protocol

### 6.1 Event Lifecycle Matrix

| Event | SoA Buffer Action | Trigram Index Action | Impact |
| :--- | :--- | :--- | :--- |
| **New File Created** | Appends new row $N$ | Extracts trigrams, calls `bm.add(N)` | Incremental (~2 $\mu s$) |
| **File Deleted** | Sets `is_deleted[i] = 1` | Bit in bitmap remains until compaction or `bm.remove(i)` | Zero false positives (checked by verifier) |
| **File Renamed** | Modifies filename in storage | Removes old trigrams, adds new trigrams | Minimal (~5 $\mu s$) |
| **Folder Renamed** | Updates path strings in storage | **NONE (0 work)** | Filenames did not change! |
| **Index Rebuild** | Rebuilds SoA from scratch | Background worker thread rebuilds trigrams | ~80 ms in background |

---

## 7. Implementation Tasks & Milestones

| Task ID | Description | Dependencies | Status | Verification / Test Target |
| :--- | :--- | :--- | :--- | :--- |
| **TASK-0** | Preliminary refactorings: extract `EvaluateCandidateRow`, `EmitHit`, `ExtractRequiredLiteral`, and `ISearchableIndex` hook. | None | **Completed** | All existing tests pass via `./scripts/build_tests_macos.sh` |
| **TASK-1** | Add `CRoaring` to `CMakeLists.txt` via `FetchContent`. Validate build on macOS, Linux, and Windows. | TASK-0 | **Completed** | `./scripts/build_tests_macos.sh` compiles cleanly |
| **TASK-2** | Implement `FilenameTrigramExtractor` helper (extract 24-bit keys from strings, handle normalization). | TASK-1 | **Completed** | Unit tests in `tests/TrigramExtractorTests.cpp` |
| **TASK-3** | Implement `FilenameTrigramIndex` data structure (`Build`, `AddEntry`, `RemoveEntry`, `QueryCandidates`). | TASK-2 | **Completed** | Unit tests in `tests/FilenameTrigramIndexTests.cpp` |
| **TASK-4** | Implement `TrigramQueryPlanner` (deconstruct substring, wildcard, and regex prefilter patterns into AND/OR trigrams). | TASK-3 | **Completed** | Unit tests in `tests/TrigramQueryPlannerTests.cpp` |
| **TASK-5** | Integrate into `ParallelSearchEngine`: add candidate generation check and sparse verification loop. | TASK-4 | **Completed** | `parallel_search_engine_tests` & regression tests |
| **TASK-6** | Hook into `FileIndex` population & USN Journal incremental event handlers. | TASK-5 | **Completed** | Live crawler & USN test suite |
| **TASK-7** | Benchmark performance: measure search latency comparison across 700k dataset (6ms vs <0.2ms). | TASK-6 | **Completed** | `search_benchmark_std_linux_filename` (0.03–0.11 ms, ~60× speedup) |
| **TASK-8** | Retirement & deletion of `src/utils/SimpleRegex.h` and routing branches in `StdRegexUtils.h`. | TASK-5 | **Completed** | PR #175 merged: `tests/SimpleRegexTests` deleted, 65 test suites pass |
| **TASK-9** | Metrics window telemetry: add Trigram Memory Metrics display under System Performance. | TASK-6 | **Completed** | Non-blocking `TryGetTrigramIndexMemoryBytes` in `MetricsWindow.cpp` |

---

## 8. Acceptance Criteria & Quality Gates

1. **Performance Gate:**
   - **Target:** Queries with $\ge 3$ characters of literal input (e.g. `document`, `test*.cpp`, `rs:config_.*\.json`) execute in **$< 0.5\text{ ms}$** on the 700,000-path standard Linux filesystem test corpus.
   - **Outcome: PASSED.** Measured search latencies on the 706,000-path Linux corpus ranged between **0.03 ms and 0.11 ms** (average 0.11 ms), delivering a **~60× speedup** over the 6.8 ms exhaustive SoA parallel scan.
2. **Precision & Parity:**
   - **Target:** Results produced by the trigram candidate path must match the exact result set of the exhaustive parallel scan 100% across all fixtures.
   - **Outcome: PASSED.** Verified across all 65 test suites with 100% result equivalence and 0 false positives or false negatives.
3. **Memory Constraint:**
   - **Target:** Total RAM consumed by `FilenameTrigramIndex` for 700,000 files must not exceed **35 MB**.
   - **Outcome: PASSED.** Measured memory footprint is **~16–20 MB** on 706,000 entries (~23–28 bytes per indexed file), well within the 35 MB budget. Real-time telemetry is surfaced in the Metrics Window (`Trigram Index Memory`).
4. **Code Quality:**
   - **Target:** 0 new warnings on `pre-commit-clang-tidy.sh`. Const-correctness and explicit single-argument constructors. Clean compilation under MSVC, Clang, and GCC.
   - **Outcome: PASSED.** 0 clang-tidy warnings, strict C++17 conformance, and thread safety validated.

---

## 9. Retirement Plan: Deletion of SimpleRegex.h & StdRegexUtils Bypass (TASK-8)

### 9.1 Rationale
`SimpleRegex.h` (an adaptation of Rob Pike's 100-line regex matcher) and the `StdRegexUtils.h` bypass routing heuristics were introduced historically because evaluating standard `std::regex` 700,000 times during an exhaustive linear scan took 1.5 to 3.0 seconds.

With the trigram candidate generator narrowing candidate rows to **10–50 files**, evaluating `std::regex` on those 50 files takes **$< 0.05\text{ ms}$**. The performance problem `SimpleRegex` was designed to circumvent is eliminated by the trigram index, making the dual-path regex engine and heuristic routing code in `StdRegexUtils.h` unnecessary complexity and overhead.

### 9.2 Migration Steps (Completed in PR #175)
1. **Rob Pike Regex Engine (`RegExMatch` / `RegExMatchI`):**
   - Deleted `src/utils/SimpleRegex.h`.
   - Deleted `tests/SimpleRegexTests.cpp` and removed `simple_regex_tests` from `scripts/test_targets.txt` and `CMakeLists.txt`.
   - Eliminated all historical `// NOLINT(misc-no-recursion)` and `NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)` suppressions associated with `SimpleRegex.h`.
2. **`StdRegexUtils.h` Bypass Routing Cleanup:**
   - Removed `detail::RouteSimplePattern(pattern, text, case_sensitive)`.
   - Removed `detail::IsSimplePattern(pattern)`.
   - In `RegexMatch(pattern, text, case_sensitive)`, removed the `detail::IsSimplePattern` bypass block.
   - Retained the literal pattern fast path (`IsLiteralPattern` $\to$ `ContainsSubstring`) and the compiled regex evaluation path (`GetCompiledRegex` $\to$ `MatchWithRegex`).
3. **Wildcard & Glob Matcher (`GlobMatch` / `GlobMatchI`):**
   - In `src/search/SearchPatternUtils.h`, updated `GeneralGlobMatcher` to route directly to `PathPatternMatcher` (`path_pattern::PathPatternMatches`), which already provides comprehensive wildcard, character class, and glob matching with zero ReDoS vulnerability.

### 9.3 Completed Pre-Trigram Retirements
The following legacy components identified during the architecture audit have already been retired:
- **`SearchControls.h/.cpp`**: Deleted ghost class (no-op `(void)args;`).
- **`SearchStatisticsCollector` Aggregations**: Removed dead `AggregateResults`, `AggregateIdResults`, `AggregateDataResults`, and unused `<future>`.
- **`IIndexSearch` / `FileIndex` `bool folders_only` Overload**: Removed 10-parameter legacy overload in favor of canonical `ItemTypeFilter`.
- **`ParallelSearchEngine::SearchAsync` (ID-only)**: Removed legacy file-ID return path (`SearchAsync`, `ProcessChunkRangeIds`, `ValidateChunkRangeIds`, and test helpers), unifying all search execution and tests onto `SearchAsyncWithData`.

