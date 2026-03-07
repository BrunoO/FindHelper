# 2026-01-30 Benchmarking System Options (Low Effort)

## Goal

Design a low-effort benchmarking system that reuses existing building blocks: **index from text file** and **search configuration from JSON**.

---

## Existing Building Blocks

### 1. Index from text file

| Component | Location | Usage |
|-----------|----------|--------|
| `populate_index_from_file(file_path, file_index)` | `src/index/IndexFromFilePopulator.cpp` | One path per line → `FileIndex::InsertPath(line)` |
| `LoadIndexFromFile(file_path, file_index, verbose)` | `src/core/AppBootstrapCommon.h` | Wraps above + `file_index.FinalizeInitialPopulation()` |
| CLI | `CommandLineArgs::index_from_file` | `--index-from-file <path>` (already used by bootstrap) |

**Contract:** Text file with one absolute or relative path per line. No header. Empty lines skipped.

### 2. Search configuration from JSON

| Component | Location | Usage |
|-----------|----------|--------|
| `ParseSearchConfigJson(json_response)` | `src/api/GeminiApiUtils.cpp` | Returns `GeminiApiResult` with `SearchConfig` |
| `SearchConfig` | `src/api/GeminiApiUtils.h` | `filename`, `extensions`, `path`, `folders_only`, `case_sensitive`, `time_filter`, `size_filter` |

**Expected JSON shape (direct):**

```json
{
  "version": "1.0",
  "search_config": {
    "path": "pp:src/**/*.cpp",
    "filename": "",
    "extensions": ["cpp", "h"],
    "folders_only": false,
    "case_sensitive": false,
    "time_filter": "None",
    "size_filter": "None"
  }
}
```

**Mapping to search API:**  
`SearchConfig` maps to `FileIndex::SearchAsyncWithData(primaryQuery, …, pathQuery, extensions*, folders_only, case_sensitive)`:  
- `filename` → primary query (matches filename only)  
- `path` → path_query (matches full path: directory + filename)  
- `extensions` → extensions pointer  
- `folders_only`, `case_sensitive` → same  
- **Time/size filters:** Applied *after* search in UI (`SearchResultUtils`). For “search engine only” benchmarks they can be **ignored** (no code change).

### 3. Search execution and timing

| Component | Location | Usage |
|-----------|----------|--------|
| `FileIndex::SearchAsyncWithData(...)` | `src/index/FileIndex.cpp` | Returns `vector<future<vector<SearchResultData>>>` |
| `test_helpers::CollectFutures(futures)` | `tests/TestHelpers.cpp` | Wait on all futures, return concatenated results |
| `CollectSearchResults(index, query, …)` | `tests/FileIndexSearchStrategyTests.cpp` | Wrapper: SearchAsyncWithData + CollectFutures |

**Timing:** Call `SearchAsyncWithData`, then `CollectFutures`; measure wall time around that. No synchronous Search() API; async + wait is the pattern (same as tests).

### 4. Existing benchmark pattern

| Component | Location | Usage |
|-----------|----------|--------|
| `Benchmark::Run(name, iterations, func)` | `tests/PathPatternBenchmark.cpp` | `high_resolution_clock`, runs `func` N times, prints total us, avg us/op, ops/sec |

No external benchmark lib; simple inline timing. Reusable as a building block.

---

## Options (Low Effort)

### Option A: Standalone benchmark executable (recommended base)

**Idea:** New executable (e.g. `search_benchmark`) that:

1. Loads index from a text file (`populate_index_from_file` + `FinalizeInitialPopulation`).
2. Loads one or more search configs from JSON (file path or inline).
3. For each config: map `SearchConfig` → (query, path_query, extensions, folders_only, case_sensitive), call `SearchAsyncWithData`, `CollectFutures`, measure wall time.
4. Optionally run each scenario N times (warmup + timed runs) and report min/avg/max and result count.

**Reuse:**  
- Index: `IndexFromFilePopulator`, `LoadIndexFromFile` (or same logic).  
- Config: `ParseSearchConfigJson` (accept JSON string from file or CLI).  
- Search: `FileIndex::SearchAsyncWithData` + existing `CollectFutures`-style wait.  
- Timing: Copy `Benchmark::Run`-style loop from `PathPatternBenchmark.cpp` or extract to a small shared util.

**Gap:** Map `SearchConfig` → parameters for `SearchAsyncWithData`.  
- `filename` → primary query (filename-only match).  
- `path` → path_query (full-path match).  
- `extensions` → `vector<string>*` (or empty).  
- `folders_only`, `case_sensitive` → same.  
- Time/size: omit for v1 (benchmark “search only”).

**Effort:** Low. One new `.cpp`, one CMake target linking existing libs (FileIndex, GeminiApiUtils, IndexFromFilePopulator, Settings, TestHelpers or a minimal `CollectFutures` + Benchmark helper). No new public API.

**CLI sketch:**  
`search_benchmark --index paths.txt --config scenario.json`  
or  
`search_benchmark --index paths.txt --config "{\"version\":\"1.0\", \"search_config\":{...}}"`  
Plus optional `--iterations N`, `--warmup M`.

---

### Option B: JSON benchmark manifest (extends A)

**Idea:** One JSON file describing multiple scenarios; benchmark runs all and prints a table.

**Manifest shape (example):**

```json
{
  "index_file": "data/large_index.txt",
  "scenarios": [
    { "name": "extension-only .cpp", "config": { "extensions": ["cpp"] } },
    { "name": "path pattern", "config": { "path": "pp:src/**/*.cpp" } }
  ],
  "iterations": 5,
  "warmup": 1
}
```

**Reuse:** Same as Option A. Each scenario’s `config` is merged into the standard `search_config` shape and parsed with `ParseSearchConfigJson` (or a minimal parser that only reads `search_config`).

**Effort:** Low on top of Option A (one manifest format + loop over scenarios).

---

### Option C: Test-based benchmark (doctest + timing)

**Idea:** A benchmark “test” that uses the same infrastructure as tests: `TestFileIndexFixture`, optional `populate_index_from_file` for a fixed index file, or `CreateTestFileIndex` for synthetic data. Run search N times, print timing (and optionally result count) to stdout. No separate executable; run with the test suite.

**Reuse:**  
- `TestFileIndexFixture`, `CreateTestFileIndex`, or load from file in test.  
- `CollectSearchResults` or `SearchAsyncWithData` + `CollectFutures`.  
- Same `Benchmark::Run`-style timing.

**Config:** Hardcoded `SearchContext` or a single JSON file read in test (e.g. `ParseSearchConfigJson` + map to search params).

**Effort:** Low. One test file or section; possible small helper to “run search and return duration”.

**Pros:** No new target; runs in CI with tests.  
**Cons:** Index size/config tied to test data or one checked-in file; less flexible than a dedicated CLI.

---

### Option D: Script-driven app (not recommended for “low effort”)

**Idea:** Shell script that invokes the **GUI app** with `--index-from-file` and some “run one search and print time” mode.

**Blocker:** The app is GUI-driven; there is no headless “run one search and exit” mode. Adding that would require a dedicated CLI/headless path in the main app (e.g. `--benchmark-search 'query'` or `--benchmark-config config.json`), which is more invasive.

**Effort:** Medium (app changes) plus script. Not recommended for minimal effort.

---

## Recommendation

1. **Implement Option A** as the base: one standalone `search_benchmark` executable using:
   - Index: `populate_index_from_file` + `FinalizeInitialPopulation`.
   - Config: `ParseSearchConfigJson` (from file or inline string).
   - Search: `FileIndex::SearchAsyncWithData` + wait on futures (e.g. `CollectFutures`).
   - Timing: simple loop (warmup + N timed runs), report wall time and result count.

2. **Add a small “SearchConfig → search params” helper** (could live in the benchmark binary or in a shared util) so JSON config drives the existing search API without duplicating logic.

3. **Optionally add Option B** (manifest with multiple scenarios) once A is in place, for multi-scenario tables.

4. **Option C** can be added in parallel for CI-friendly “run with tests” benchmarks using test fixtures and optionally one checked-in index file.

---

## Minimal implementation checklist (Option A)

- [ ] New target `search_benchmark` in CMake (link: FileIndex, GeminiApiUtils, IndexFromFilePopulator, Settings, path/search libs).
- [ ] Parse CLI: `--index <path>`, `--config <path or inline JSON>`.
- [ ] Load index: `LoadIndexFromFile(index_path, file_index)` (or equivalent).
- [ ] Parse config: read JSON string → `ParseSearchConfigJson` → `SearchConfig`.
- [ ] Map `SearchConfig` → (query, path_query, extensions, folders_only, case_sensitive); call `SearchAsyncWithData`; wait on futures; measure time.
- [ ] Optional: `--iterations`, `--warmup`; run loop; print min/avg/max and result count.
- [ ] Reuse or copy `Benchmark::Run`-style reporting (us/op or ms/search, result count).

Time/size filters can be left out for v1 (benchmark measures “search engine only” time).
