# Contract Candidates: Where to Add Invariants, Preconditions, and Postconditions

**Date:** 2026-02-01  
**Purpose:** Identify code areas where adding invariants, preconditions, or postconditions would be most beneficial (using either `assert` or a contract library).  
**Reference:** `docs/plans/2026-02-01_CONTRACT_LIBRARY_STUDY_AND_MIGRATION_PLAN.md`.

---

## Summary Table

| Area | Preconditions | Postconditions | Invariants | Priority |
|------|---------------|----------------|------------|----------|
| **Index / Path** | | | | |
| FileIndexStorage (InsertLocked, RemoveLocked, GetEntry) | id valid; lock held | — | — | High |
| PathStorage (GetPathByIndex, GetPathComponentsByIndex) | index in range; not deleted | — | Arrays in sync | High |
| PathBuilder (BuildPathFromComponents, CollectPathComponents) | component_count in [0, kMaxPathDepth]; pointers non-null | — | — | Medium |
| IndexOperations (Insert, Remove, Rename, Move) | parent_id exists (Insert); id exists (Remove/Rename/Move) | — | — | Medium |
| LazyAttributeLoader (GetFileSize, GetModificationTime) | id exists in index | — | — | Medium |
| **Search** | | | | |
| ParallelSearchEngine (ProcessChunkRange, chunk loop) | chunk_start ≤ chunk_end ≤ soaView.size; thread_pool non-null | — | Loop: i in [chunk_start, chunk_end) | High |
| SearchWorker (StartSearch, GetResults, GetStreamingCollector) | file_index valid; params non-empty where required | — | — | Medium |
| SearchResultsService (GetDisplayResults, UpdateFilterCaches) | state flags consistent | — | display_results non-null; cache validity vs vectors | High |
| SearchResultUtils (SortSearchResults, PrefetchAndFormatSortDataBlocking) | column_index in range; results non-empty where needed | — | — | Already has assert |
| **UI / State** | | | | |
| GuiState (filter cache vs vectors) | — | — | timeFilterCacheValid ⇒ filteredResults populated; sizeFilterCacheValid ⇒ sizeFilteredResults populated | High |
| ResultsTable (selectedRow clamp, display_results) | — | — | selectedRow in [-1, size) after clamp | Medium |
| **Crawler / Lifecycle** | | | | |
| FolderCrawler (ProcessBatch, worker loop) | config valid (already asserted) | — | — | Low |
| Application (StartIndexBuilding, StopIndexBuilding) | — | — | index_builder_ stopped before destructor | Medium |
| UsnMonitor (Stop, GetQueueSize) | — | — | monitoring_active_ consistent with thread/handle state | Medium |

---

## 1. Index and Path Layer

### 1.1 FileIndexStorage

**Location:** `src/index/FileIndexStorage.cpp`, `.h`

**Preconditions (beneficial):**

- **InsertLocked(id, parent_id, name, ...):**
  - `id` not already in `index_` (or document that re-insert is allowed).
  - `parent_id` exists in `index_` when building paths (or document root convention).
- **RemoveLocked(id):** No formal precondition; current code handles “id not found.”
- **GetEntry(id) / GetEntryMutable(id):** No precondition; return nullptr if not found.  
  **Invariant (caller):** Callers that use the returned pointer must check for nullptr. Asserting “caller holds lock” is possible only with a shared lock abstraction; otherwise document in comments.

**Why:** InsertLocked is used by IndexOperations::Insert, which calls PathBuilder::BuildFullPath(parent_id, ...). If parent_id is missing, BuildFullPath walks the parent chain and can produce an incomplete path. A precondition “parent_id exists or is root” would catch bugs early.

**Suggested (assert):**

- In `IndexOperations::Insert`: assert or precondition that `parent_id` is in storage or is the root sentinel (if any), before calling BuildFullPath.
- Optionally in `FileIndexStorage::InsertLocked`: assert `index_.find(id) == index_.end()` if insert is required to be “new id only.”

---

### 1.2 PathStorage

**Location:** `src/path/PathStorage.cpp`, `.h`

**Preconditions:**

- **GetPathByIndex(index):** `index < path_offsets_.size()` and `is_deleted_[index] == 0`.  
  Current code returns empty string_view if invalid; an assertion would catch programming errors (e.g. wrong index from GetIndexForId).
- **GetPathComponentsByIndex(index):** Same as above.
- **GetPathView(uint64_t id):** No strict precondition; returns empty if id not found.  
  **Invariant:** `path_offsets_.size() == path_ids_.size() == is_deleted_.size() == ...` (all parallel arrays same size). RebuildPathBuffer and ClearAll maintain this; an assertion after any mutation would guard against future bugs.

**Why:** PathStorage is used with indices derived from id_to_path_index_. Off-by-one or use-after-delete can cause out-of-bounds or deleted entry access. Asserting index range and non-deleted in debug catches such bugs.

**Suggested (assert):**

- At start of GetPathByIndex / GetPathComponentsByIndex:  
  `assert(index < path_offsets_.size() && is_deleted_[index] == 0);`  
  (Keep the existing runtime check and return empty for Release.)
- Optionally: private helper `AssertInvariant()` that checks parallel-array sizes and call it at end of RebuildPathBuffer, ClearAll, and InsertPath/RemovePath.

---

### 1.3 PathBuilder

**Location:** `src/path/PathBuilder.cpp`, `.h`

**Preconditions:**

- **BuildPathFromComponents(components, component_count):**
  - `0 <= component_count && component_count <= kMaxPathDepth`.
  - For `i in [0, component_count)`, `components[i] != nullptr`.
- **CollectPathComponents:** Return value is in [0, kMaxPathDepth]. Callers (BuildFullPath, BuildFullPathWithLogging) already pass the result as component_count; asserting the range in BuildPathFromComponents is cheap.

**Why:** BuildPathFromComponents loops over `[0, component_count)` and accesses `components[i - 1]`. Invalid component_count or null pointer could cause undefined behavior.

**Suggested (assert):**

- In BuildPathFromComponents:  
  `assert(component_count >= 0 && component_count <= kMaxPathDepth);`  
  and optionally assert non-null for each used component.

---

### 1.4 IndexOperations

**Location:** `src/crawler/IndexOperations.cpp`

**Preconditions:**

- **Insert(id, parent_id, name, ...):** `parent_id` exists in storage (or is root).  
  BuildFullPath(parent_id, ...) walks the parent chain; if parent is missing, path may be wrong.
- **Remove(id):** No strict precondition; code handles “not in index.”
- **Rename(id, new_name) / Move(id, new_parent_id):** `id` exists (already checked via GetEntry); for Move, `new_parent_id` exists.

**Suggested (assert):**

- In Insert: after lock, assert storage.GetEntry(parent_id) != nullptr (or document root convention and assert that).
- In Move: assert storage.GetEntry(new_parent_id) != nullptr before updating path.

---

### 1.5 LazyAttributeLoader

**Location:** `src/index/LazyAttributeLoader.cpp`, `.h`

**Preconditions:**

- **GetFileSize(id) / GetModificationTime(id):** Document that `id` must exist in the index; otherwise GetEntry returns nullptr and the loader returns default/failed.  
  Asserting “entry != nullptr” at the point we need it (after GetEntry) can catch callers passing invalid ids (e.g. from a stale SearchResult).

**Why:** LazyAttributeLoader is used with file_id from search results. If the index is cleared or the id is wrong, we want to fail fast in debug rather than silently return defaults.

**Suggested (assert):**

- In GetFileSize/GetModificationTime (and LoadFileSize/LoadModificationTime), after getting entry:  
  `assert(entry != nullptr && "id must exist in index");`  
  when the design assumes “caller must pass valid id.” If the design allows “missing id ⇒ default,” keep current behavior and do not assert.

---

## 2. Search Layer

### 2.1 ParallelSearchEngine

**Location:** `src/search/ParallelSearchEngine.cpp`, `.h`

**Preconditions:**

- **SearchAsync:** Already checks thread_pool_ non-null and GetThreadCount() > 0; could assert after early returns that we only enqueue when pool is valid.
- **ProcessChunkRange / ProcessChunkRangeIds (chunk loop):**
  - `chunk_start <= chunk_end`
  - `chunk_end <= soaView.size` (or equivalent total_items).
  - `soaView.path_offsets`, `soaView.path_ids`, etc. are valid for indices in [chunk_start, chunk_end).

**Invariants (loop):**

- Inside the loop: `chunk_start <= i < chunk_end`, and `i < soaView.size`, so `soaView.path_offsets[i]`, `soaView.path_offsets[i + 1]` (when i+1 < chunk_end) are in range.  
  Asserting `i < soaView.size` at loop entry (or once per chunk) would guard against wrong chunk bounds.

**Why:** Chunk bounds come from load balancing. If chunk_end > soaView.size, we get out-of-bounds access. This is a high-value assertion.

**Suggested (assert):**

- At start of ProcessChunkRange and ProcessChunkRangeIds:  
  `assert(chunk_start <= chunk_end && chunk_end <= soaView.size);`  
- Optionally at top of loop: `assert(i < soaView.size);` (or rely on chunk_end check).

---

### 2.2 SearchWorker

**Location:** `src/search/SearchWorker.cpp`, `.h`

**Preconditions:**

- **StartSearch(params):** FileIndex reference is valid (constructor requirement). Params: document or assert that required fields (e.g. at least one of filename/path/extension non-empty for a “real” search) are set if that’s a precondition for correctness.
- **GetStreamingCollector():** No precondition; returns nullptr if not streaming.  
  Callers (SearchController::PollResults) already check for nullptr.

**Postconditions:**

- After StartSearch: IsBusy() or IsSearching() true until search finishes (already implied by implementation).
- After MarkSearchComplete (in worker): collector.IsSearchComplete() (already asserted).

**Suggested (assert):** Optional: in StartSearch, assert `file_index_.Size() >= 0` or similar if you want to enforce “index ready”; currently the code relies on SearchController not starting search during index build.

---

### 2.3 SearchResultsService and GuiState Display Logic

**Location:** `src/search/SearchResultsService.cpp`, `GetDisplayResults`; `src/gui/GuiState.h`

**Preconditions:**

- **GetDisplayResults(state):** None strictly required; returns &state.searchResults or &state.partialResults or &state.filteredResults/sizeFilteredResults depending on flags.  
  **Invariant (state):** When `state.timeFilterCacheValid`, `state.filteredResults` and `state.filteredCount` are consistent. When `state.sizeFilterCacheValid`, `state.sizeFilteredResults` and `state.sizeFilteredCount` are consistent. When `!state.resultsComplete && state.showingPartialResults`, we show partialResults; otherwise we show searchResults or filter caches.  
  Asserting these in GetDisplayResults (e.g. “if timeFilterCacheValid then !filteredResults.empty() or filteredCount==0”) would catch cache/flag bugs.

**Why:** Filter cache invalidation and display logic are easy to get wrong (e.g. valid flag but empty vector after clear). Assertions would document and enforce the intended invariants.

**Suggested (assert):**

- In GetDisplayResults:
  - If returning &state.filteredResults: assert state.timeFilterCacheValid (or document that we may return it even when invalid).
  - If returning &state.sizeFilteredResults: assert state.sizeFilterCacheValid.
  - Assert the returned pointer is non-null (always true for references; N/A if we keep returning pointer to member).
- After UpdateFilterCaches: assert state.timeFilterCacheValid and state.sizeFilterCacheValid (or that filtered counts match vector sizes).

---

### 2.4 SearchResultUtils

**Location:** `src/search/SearchResultUtils.cpp`

**Current:** SortSearchResults already has `assert(column_index >= ResultColumn::Filename && column_index <= ResultColumn::Extension);` and runtime clamp.

**Additional:**

- **PrefetchAndFormatSortDataBlocking:** If it assumes `results` non-empty or that indices are valid, add precondition assert.
- **StartAttributeLoadingAsync / ValidateIndexAndCancellation:** The check `i >= results.size()` is a runtime guard; an assertion at the start of the lambda (e.g. assert(i < results.size())) would make the “index valid for this batch” assumption explicit (optional; may be noisy if many tasks).

---

## 3. UI and State

### 3.1 GuiState

**Location:** `src/gui/GuiState.h`, `GuiState.cpp`

**Invariants (document or assert):**

- **Filter caches:**  
  `timeFilterCacheValid ⇒ (filteredResults.size() == filteredCount or filteredCount consistent with filteredResults)`.  
  `sizeFilterCacheValid ⇒ (sizeFilteredResults.size() == sizeFilteredCount or same).`
- **Streaming:**  
  `resultsComplete && !showingPartialResults` when showing final results;  
  `!resultsComplete && showingPartialResults` when showing partial results (already partially asserted in SearchController).
- **selectedRow:** In ResultsTable we clamp to display size; invariant “after clamp, selectedRow in [-1, display_results.size())” could be asserted after clamp.

**Suggested (assert):** Add a debug-only GuiState::AssertInvariants() (or free function in namespace) that checks the above and call it at the end of UpdateFilterCaches and after state transitions in SearchController (only in Debug).

---

### 3.2 ResultsTable

**Location:** `src/ui/ResultsTable.cpp`

**Postcondition / invariant:**

- After clamping: `state.selectedRow >= -1 && state.selectedRow < static_cast<int>(display_results.size())` (with the usual -1 for “no selection” when empty).  
  Already enforced by the clamp; an assertion right after the clamp would document the invariant.

**Suggested (assert):** After the selectedRow clamp block:  
`assert(display_results.empty() ? state.selectedRow == -1 : (state.selectedRow >= 0 && state.selectedRow < static_cast<int>(display_results.size())));`

---

## 4. Crawler and Lifecycle

### 4.1 FolderCrawler

**Location:** `src/crawler/FolderCrawler.cpp`

**Current:** Constructor asserts config_.batch_size > 0 and config_.progress_update_interval > 0.

**Additional:**

- **ProcessBatch / worker loop:** If there is an assumption that “file_index_ is not cleared during processing,” it could be documented; asserting it is hard without a generation or “building” flag. Low priority.

---

### 4.2 Application

**Location:** `src/core/Application.cpp`

**Invariant:**

- Destructor must stop index_builder_ before destroying members that the builder uses. Current code: `if (index_builder_) index_builder_->Stop();`.  
  Postcondition of Stop(): builder is stopped (threads joined).  
  Could assert after Stop() that IsIndexBuilding() is false (if such a query exists on the builder), or leave as comment.

---

### 4.3 UsnMonitor

**Location:** `src/usn/UsnMonitor.cpp`

**Invariant:**

- When monitoring_active_ is false, reader_thread_ and processor_thread_ are joined, volume_handle_ is closed, queue_ is reset.  
  Stop() enforces this. Asserting “!monitoring_active_ ⇒ !reader_thread_.joinable()” in Stop() after join (or in a debug-only CheckInvariant()) would guard against partial shutdown bugs.

**Suggested (assert):** In Stop(), after all joins and cleanup:  
`assert(!reader_thread_.joinable() && !processor_thread_.joinable());`

---

## 5. Priority and Suggested Order

1. **High (correctness, hot path or subtle bugs)**  
   - **ParallelSearchEngine:** chunk_start/chunk_end vs soaView.size.  
   - **PathStorage:** index in range and not deleted in GetPathByIndex / GetPathComponentsByIndex; optional parallel-array invariant.  
   - **SearchResultsService / GuiState:** filter cache validity vs vectors and GetDisplayResults return value.

2. **Medium (documentation and catch misuse)**  
   - **PathBuilder:** component_count in range; non-null components.  
   - **IndexOperations:** parent_id (and new_parent_id for Move) exists.  
   - **LazyAttributeLoader:** entry != nullptr when design assumes valid id.  
   - **ResultsTable:** selectedRow in range after clamp.  
   - **Application / UsnMonitor:** lifecycle invariants (builder stopped, threads joined).

3. **Low (already safe or low impact)**  
   - FolderCrawler beyond existing config asserts.  
   - SearchWorker StartSearch param asserts (unless we tighten “valid search” rules).

---

## 6. Implementation Notes

- Use **assert** (or `#ifndef NDEBUG` blocks) so checks are debug-only and match current project policy (see AGENTS document).
- For **class invariants** (PathStorage arrays, GuiState filter state), consider a single `AssertInvariants()` method or free function called at end of key methods or at frame start in Debug.
- **Loop invariants:** For hot loops (e.g. ProcessChunkRange), one assertion at loop start (e.g. chunk_end <= soaView.size) is enough; avoid per-iteration asserts in tight loops unless needed.
- If the project later adopts Lib.Contract, these same spots map to precondition/postcondition/invariant macros; this document remains the source of “where and what” to check.

---

## References

- `docs/plans/2026-02-01_CONTRACT_LIBRARY_STUDY_AND_MIGRATION_PLAN.md`
- AGENTS document § “Add Assertions for Debug Builds”
