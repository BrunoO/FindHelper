# 2026-02-21 Move semantics audit (rvalue / push_back patterns)

## Purpose

After fixing `StreamingResultsCollector::AddResult(SearchResultData&&)` to use `std::move(result)` when pushing to `current_batch_`, search the codebase for similar patterns: rvalue parameters not moved, or locals copied into containers where move would suffice.

## Findings

### 1. Already correct (no change)

- **SearchController.cpp**  
  `UpdateSearchResults(std::vector<SearchResult>&& new_results)` and `ApplyNonStreamingSearchResults(..., std::vector<SearchResult>&& new_results)` already use `std::move(new_results)` when assigning or forwarding. NOLINTs are for paths where the vector is only read (e.g. `ShouldUpdateResults`) and not consumed.

- **SearchWorker.cpp**  
  `results.push_back(std::move(result))` is already used when building the results vector.

- **StreamingResultsCollector.cpp**  
  `AddResult(const SearchResultData& result)` correctly copies (const ref).  
  `AddResult(SearchResultData&& result)` now uses `std::move(result)` (fixed earlier).

- **FolderCrawler.cpp**  
  Uses `std::move(entry)` / `std::move(dir_entry)` when pushing to `entries`.

### 2. Same-pattern fixes (local then push_back/insert)

- **Settings.cpp**  
  In `LoadSavedSearches` and `LoadRecentSearches`, locals `saved` and `recent` are only used for a single `push_back`. Use non-const locals and `push_back(std::move(...))` to avoid a copy.

- **TimeFilterUtils.cpp**  
  In `RecordRecentSearch`, local `recent` is built then inserted once. Use `insert(..., std::move(recent))` to avoid a copy.

### 3. Not applicable

- **Popups.cpp**  
  `SaveOrUpdateSearch(const SavedSearch& saved)` takes a const reference; must copy. No change.

- **SearchResultUtils.cpp**  
  `result` comes from range-for over a const collection; copy is required.

## Applied fixes

- **Settings.cpp**: Use `SavedSearch saved = ...` / `SavedSearch recent = ...` and `push_back(std::move(saved))` / `push_back(std::move(recent))` in the two load helpers.
- **TimeFilterUtils.cpp**: Use `insert(settings.recentSearches.begin(), std::move(recent))` in `RecordRecentSearch`.

## References

- TECH_DEBT / PERFORMANCE: avoid unnecessary copies when appending to containers.
- Similar fix: `StreamingResultsCollector::AddResult(SearchResultData&&)` (use `std::move(result)`).
