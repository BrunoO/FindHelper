# Plan: Remove Bloom Filter (Unused in Current Use Cases)

**Date:** 2026-01-30  
**Goal:** Remove Bloom filter–related code that is never used (extension sets are always below `kMinExtensionCountForBloom`). Minimize lines and simplify maintenance. **No code changes in this document—plan only.**

---

## Rationale

- In your use cases extension sets are always below the threshold (24), so `extension_bloom_filter` is never built; only `sorted_extensions` (Layer 1) and the hash set (Layer 2) are used.
- Removing the Bloom layer removes dead code, reduces surface (no `BloomFilter.h`, no Bloom tests), and simplifies the extension-matching path.

---

## What Stays (No Bloom)

- **Layer 1:** `sorted_extensions` in `SearchContext` — built in `InitializeExtensionCascading()`, used for allocation-free binary search. Keep.
- **Layer 2:** `ExtensionMatches(ext_view, context.extension_set, context.case_sensitive)` — hash set fallback. Keep.
- **Flow:** When `HasExtensionFilter()`, use one function that does: if `!sorted_extensions.empty()` then binary search (Layer 1), else `ExtensionMatches` (Layer 2). No Layer 0 (Bloom).

---

## Removal Steps (Order Matters)

### 1. Delete Bloom filter implementation and its tests

| Action | Item |
|--------|------|
| **Delete file** | `src/utils/BloomFilter.h` |
| **Delete file** | `tests/BloomFilterTests.cpp` |
| **Edit** | `CMakeLists.txt`: remove the `bloom_filter_tests` executable block (add_executable, target_include_directories, target_compile_features, configure_vectorscan_for_test, compile defs/options, add_test, message). Search for `bloom_filter_tests` to find the exact block. |

**Result:** No Bloom filter type or Bloom-only tests. Build will break until step 2–4 are done.

---

### 2. Remove Bloom from SearchContext

**File:** `src/search/SearchContext.h`

| Action | Detail |
|--------|--------|
| **Remove include** | `#include "utils/BloomFilter.h"` |
| **Remove member** | `std::optional<bloom_filter::BloomFilter> extension_bloom_filter{};` |
| **Remove constant** | `static constexpr size_t kMinExtensionCountForBloom = 24;` |
| **Simplify** `InitializeExtensionCascading()` | Remove the entire `if (extension_set.size() >= kMinExtensionCountForBloom) { ... } else { extension_bloom_filter = std::nullopt; }` block. Keep only: (1) empty check → set `sorted_extensions.clear()` and return; (2) build `sorted_extensions` from `extension_set` (clear, reserve, push_back, sort). Do not reference `extension_bloom_filter` anywhere. |
| **Update comment** | Section "Cascading Extension Pre-Filter" comment: drop mention of Bloom; describe only sorted list for binary search. |

**Result:** SearchContext no longer references Bloom; only `sorted_extensions` is built for the cascading path.

---

### 3. Simplify extension matching (remove Layer 0 from cascading)

**File:** `src/search/SearchPatternUtils.h`

| Action | Detail |
|--------|--------|
| **Remove Layer 0 block** | In `ExtensionMatchesCascading()`, remove the entire block: `if (context.case_sensitive && context.extension_bloom_filter && !context.extension_bloom_filter->MayContain(ext_view)) { return false; }`. |
| **Keep** | Empty check (return based on `extension_set.find("")`). Then Layer 1 (binary search on `sorted_extensions`). Then Layer 2 (`ExtensionMatches(...)`). |
| **Update doc comment** | `@param context` and function comment: remove references to `extension_bloom_filter`; state that context must have `sorted_extensions` (and `extension_set`) initialized. |

**Result:** `ExtensionMatchesCascading` does only Layer 1 + Layer 2; no Bloom.

---

### 4. Simplify call site (always use cascading when extension filter is on)

**File:** `src/search/ParallelSearchEngine.h`

| Action | Detail |
|--------|--------|
| **Replace branch** | In `MatchesExtensionFilter()`: today it does `if (context.extension_bloom_filter) return ExtensionMatchesCascading(...); else return ExtensionMatches(...)`. Change to: when `HasExtensionFilter()`, always call `ExtensionMatchesCascading(ext_view, context)` (since `InitializeExtensionCascading()` always builds `sorted_extensions` when there is an extension filter). So: `if (!context.HasExtensionFilter()) return true; const std::string_view ext_view = GetExtensionView(...); return search_pattern_utils::ExtensionMatchesCascading(ext_view, context);` (remove the `if (context.extension_bloom_filter)` branch and the `return ExtensionMatches(...)` fallback for "no cascading"). |
| **Update comment** | "Phase 1: Use cascading..." → describe only sorted + hash (no Bloom). |

**Result:** Single path when extension filter is on: always cascading (Layer 1 + Layer 2).

---

### 5. Tests: remove Bloom tests, keep ExtensionMatchesCascading behavior covered

| Action | Detail |
|--------|--------|
| **BloomFilterTests.cpp deleted** | Already removed in step 1. |
| **Add or extend tests for extension matching** | Ensure behavior of `ExtensionMatchesCascading` (without Bloom) is still tested: e.g. in `SearchContextTests` or a small test for `SearchPatternUtils` / extension matching. Test: context with `extension_set` and `sorted_extensions` populated (via `InitializeExtensionCascading()`), case_sensitive true/false, a few extensions; call `ExtensionMatchesCascading` and assert same results as `ExtensionMatches`. No assertion on `extension_bloom_filter` (removed). If such tests already exist outside `BloomFilterTests.cpp`, keep them and only remove Bloom-specific expectations. |

**Result:** No Bloom tests; extension cascading (Layer 1 + Layer 2) still tested.

---

### 6. Documentation (optional, minimize churn)

| Action | Detail |
|--------|--------|
| **Optional** | In `docs/optimization/` and `docs/investigation/`: add a short note that the Bloom filter was removed for extension filtering (use cases always below threshold; see investigation doc). Or leave existing Bloom docs as historical and add one sentence: "Bloom filter was removed; see docs/plans/2026-01-30_BLOOM_FILTER_REMOVAL_PLAN.md." |
| **Do not** | Large rewrites of IMPLEMENTATION_PLAN.md, ARCHITECTURE_INTEGRATION_ANALYSIS.md, BLOOM_FILTER_CASCADING_STRATEGY.md unless you want to fully align them with "no Bloom". |

**Result:** Future readers know Bloom was removed; optional minimal doc updates.

---

## Summary Table

| Item | Action |
|------|--------|
| `src/utils/BloomFilter.h` | Delete |
| `tests/BloomFilterTests.cpp` | Delete |
| `CMakeLists.txt` | Remove `bloom_filter_tests` block |
| `SearchContext.h` | Remove Bloom include, member, constant; simplify `InitializeExtensionCascading()` to only build `sorted_extensions` |
| `SearchPatternUtils.h` | Remove Layer 0 (Bloom) block and Bloom from comments in `ExtensionMatchesCascading` |
| `ParallelSearchEngine.h` | Always use `ExtensionMatchesCascading` when `HasExtensionFilter()`; remove `extension_bloom_filter` branch |
| Tests | Ensure extension matching (Layer 1 + Layer 2) still covered; no Bloom assertions |
| Docs | Optional one-line note that Bloom was removed |

---

## Estimated Line Count Change

- **Removed:** ~200 (BloomFilter.h) + ~150 (BloomFilterTests.cpp) + ~45 (CMakeLists) + ~25 (SearchContext) + ~5 (SearchPatternUtils) + ~3 (ParallelSearchEngine) ≈ **430 lines**.
- **Added:** 0 (except optional doc sentence).
- **Net:** Fewer lines and simpler maintenance.

---

## Risk and Rollback

- **Risk:** Low; Bloom is unused in your scenarios. Only behavior change is removing dead code paths.
- **Rollback:** Revert the commit(s) that implement this plan; or re-add `BloomFilter.h` and the Bloom branch behind a threshold (e.g. `n >= 24`) if you later need it.
