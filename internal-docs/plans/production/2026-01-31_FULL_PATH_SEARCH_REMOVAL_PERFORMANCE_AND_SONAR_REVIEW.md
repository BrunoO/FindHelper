# 2026-01-31 full_path_search Removal – Performance & Sonar Review

**Scope:** Verify the refactoring introduced no performance penalties and no new SonarQube violations.

---

## 1. Performance Review

### 1.1 Hot path: ProcessChunkRangeIds / ProcessChunkRange

**Before (conceptually):**
- `search_target = context.full_path_search ? path : filename` → **one branch per item** when pattern matching.
- `path_matcher(dir_path)` with `dir_path_len = (filename_offset > 0) ? (filename_offset - 1) : 0` → path matcher received directory part only.

**After:**
- `filename_matcher(filename)` → **no branch**; always pass `path + filename_offset`.
- `path_matcher(full_path)` with `path_len = (i+1 < chunk_end) ? (path_offsets[i+1] - path_offsets[i] - 1) : (storage_size - path_offsets[i] - 1)` → path matcher receives full path.

**Impact:**

| Aspect | Before | After | Verdict |
|--------|--------|-------|--------|
| Branch per item | Branch on `context.full_path_search` when both matchers used | No branch; always filename for filename_matcher | **Improvement** – one fewer branch in hot path |
| Context read | Read `context.full_path_search` when pattern matching | Not read | **Improvement** – one fewer member read |
| Length computation | `dir_path_len` (from `filename_offset`) for path_matcher | `path_len` (from next offset or storage_size) for path_matcher | **Neutral** – same order of work (conditional + arithmetic) |
| Allocations | None | None (`std::string_view` is stack-only) | **No regression** |

**Conclusion:** Hot path is **neutral or slightly better** (branch and one context read removed).

---

### 1.2 GuiState::BuildCurrentSearchParams()

**Before:** Conditional block: if filename and extension empty, copy pathInput → filenameInput, clear pathInput, set fullPathSearch; else direct copy of all fields.

**After:** Direct copy of all fields only (filenameInput, extensionInput, pathInput, foldersOnly, caseSensitive).

**Impact:** Fewer branches and fewer assignments per call. **Improvement.**

---

### 1.3 SearchContextBuilder::Build()

**Before:** One extra parameter `full_path_search` and one assignment `context.full_path_search = full_path_search`.

**After:** Parameter and assignment removed.

**Impact:** Fewer parameters to pass, one fewer write, slightly smaller call sites. **Improvement.**

---

### 1.4 SearchContext struct

**Before:** Included `bool full_path_search`.

**After:** Member removed.

**Impact:** Slightly smaller struct (better cache behavior when passing by value or in arrays). **Minor improvement.**

---

### 1.5 Allocations in hot path

- `CheckMatchers` and `MatchesPatterns` use `std::string_view full_path(path, path_len)` – no allocation.
- No new `std::string` or container growth in the refactored hot path.

**Conclusion:** No new allocations; **no regression.**

---

### 1.6 Performance summary

| Area | Result |
|------|--------|
| ProcessChunkRangeIds / ProcessChunkRange | Neutral or slight improvement (branch + context read removed) |
| GuiState::BuildCurrentSearchParams | Improvement (fewer branches/assignments) |
| SearchContextBuilder::Build | Improvement (fewer params, one less write) |
| SearchContext size | Minor improvement |
| Allocations | No new allocations |

**Overall:** No performance penalties; several small improvements.

---

## 2. SonarQube (and related) review

### 2.1 Rules checked

| Rule | Description | Refactor impact |
|------|-------------|-----------------|
| **cpp:S134** | Nesting depth ≤ 3 | No new nesting. `CheckMatchers` keeps nesting at 3; comment already cites cpp:S134. No new NOSONAR needed. |
| **cpp:S3776** | Cognitive complexity ≤ 25 | Logic removed; no new complex branches. `ProcessChunkRange` and `ProcessChunkRangeIds` already have NOSONAR(cpp:S3776). No new violation. |
| **cpp:S107** | Parameter count ≤ 7 | **Improved:** `SearchContextBuilder::Build` and `FileIndex::SearchAsync` / `SearchAsyncWithData` have one fewer parameter. |
| **cpp:S1066** | Merge nested if | Existing NOSONAR on intentional separate conditions (cancellation check) remain; no new nested-if pattern added. |
| **cpp:S6004** | Init-statement for variable only used in if | No new “variable before if” in refactored code. No new violation. |
| **NOSONAR placement** | Same line as issue (per project rule) | No new NOSONAR added in this refactor; existing NOSONAR are on the same line. |

---

### 2.2 Existing NOSONAR in changed files

- **ParallelSearchEngine.cpp:** NOSONAR for S1854, S1905, S6004, S112, S3776, S107, S1066 – all pre-existing or justified (hot path, parameter list, readability).
- **ParallelSearchEngine.h:** NOSONAR for S3776, S1066, S107; NOLINT for init-variables, magic-numbers – unchanged.
- **ProcessChunkRangeIds:** NOSONAR(cpp:S3776, cpp:S107) kept; still justified (hot path, explicit parameter list).

No new suppressions were required; none were removed.

---

### 2.3 New code paths

- **CheckMatchers:** Uses `filename` and `path_len`/`full_path` only; no new conditionals that would increase nesting or complexity.
- **MatchesPatterns (header):** Same logic without `context.full_path_search`; condition count unchanged.
- **GuiState::BuildCurrentSearchParams:** Simpler (fewer branches).
- **SearchContextBuilder::Build:** Simpler (fewer parameters and assignments).

**Conclusion:** No new Sonar violations introduced; parameter count improved.

---

## 3. Summary

| Dimension | Result |
|-----------|--------|
| **Performance** | No penalties; hot path slightly better (branch + context read removed); cold path (BuildCurrentSearchParams, SearchContextBuilder) improved. |
| **Sonar** | No new violations; S107 improved (fewer parameters); existing NOSONAR remain appropriate and on same line. |
| **Allocations** | No new allocations in hot or refactored paths. |

No follow-up code changes required for performance or Sonar compliance from this refactor.
