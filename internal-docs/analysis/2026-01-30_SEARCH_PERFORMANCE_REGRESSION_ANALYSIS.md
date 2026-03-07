# Search Performance Regression Analysis (vs January 7, 2026)

**Date:** 2026-01-30  
**Scope:** Code changes since 2026-01-07 that could explain a slight search slowdown.

---

## Summary

Three changes since January 7 can explain a degradation of search performance:

| Cause | When | Impact | Scenario |
|-------|------|--------|----------|
| **1. VectorScan removal** | 2026-01-30 (4ae52a7) | High | Path pattern uses regex (`vs:` or `rs:`) |
| **2. Extension filter always uses cascading** | 2026-01-30 (0bc74f1) | Low–medium | Extension filter is active |
| **3. CheckMatchers extraction in hot path** | 2026-01-22 (219a35c) | Low | ProcessChunkRangeIds path (ID-only results) |

---

## 1. VectorScan removal (main cause for regex path searches)

**Commit:** `4ae52a7` – refactor: remove VectorScan; move VectorScan docs to docs/archive/vectorscan/

**What changed:**

- **Before (Jan 7):** Path pattern with `vs:` used VectorScan (SIMD/hyperscan-style regex). Path pattern with `rs:` used `std::regex`.
- **After (Jan 30):** `vs:` is an alias for `rs:`. Both use `std::regex`.

**Why it’s slower:**

- VectorScan used a SIMD-optimized, pre-compiled regex engine.
- `std::regex` is a general-purpose engine and is typically slower for the same patterns.
- Any search whose **path** pattern is regex (`vs:` or `rs:`) is affected.

**Who is affected:**

- Users who use path regex (e.g. `vs:.*\.(cpp|h)$`, `rs:^src/.*\.py$`, or Gemini-generated `rs:...` path patterns).

**Mitigation options:**

- Prefer **path pattern** (`pp:`) when the pattern can be expressed without alternation/lookahead (faster than regex).
- If regex is required, the only way to restore VectorScan-level speed would be to re-add VectorScan (or similar) and route `vs:` (or a new prefix) to it again.

---

## 2. Extension filter: always cascading (no “hash-only” path)

**Commit:** `0bc74f1` – refactor: remove Bloom filter (unused; use cases below threshold)

**What changed:**

- **Before:**  
  - If `extension_bloom_filter` was set (extension set ≥ 24): use `ExtensionMatchesCascading` (Bloom → sorted → hash).  
  - If not (common case): use **only** `ExtensionMatches(ext_view, context.extension_set, ...)` → single hash lookup.
- **After:**  
  - Always use `ExtensionMatchesCascading(ext_view, context)` → Layer 1 (binary search on `sorted_extensions`) + Layer 2 (hash set).  
  - No more “hash-only” path when Bloom was not built.

So for the typical case (extension set &lt; 24, no Bloom):

- **Before:** One hash lookup (and for case-insensitive, one lowercase string build + hash).
- **After:** Binary search on `sorted_extensions` (several string comparisons), then possibly hash set fallback.

**Why it can be slower:**

- We do more work per path: binary search (O(log n) comparisons) instead of a single hash lookup (O(1) average).
- For small extension sets (e.g. 5–10), the extra comparisons and branchiness can add measurable cost in a tight loop.

**Who is affected:**

- Any search with an **extension filter** (e.g. “.cpp”, “.py”, or multiple extensions).

**Mitigation options:**

- Restore a fast path when Bloom is not used: if `sorted_extensions.empty()` or when extension set is small, call `ExtensionMatches(...)` directly instead of always going through `ExtensionMatchesCascading`.
- Or keep current behavior and accept a small cost for simpler code (current design).

---

## 3. CheckMatchers extraction in ProcessChunkRangeIds

**Commit:** `219a35c` – Phase 3A: Complete remaining cpp:S134 nesting depth fixes (2026-01-22)

**What changed:**

- In `ProcessChunkRangeIds` (ParallelSearchEngine.cpp), the inline filename/path matcher checks were replaced by a call to `parallel_search_detail::CheckMatchers(...)`.
- This path is used when the search returns only IDs (e.g. certain callers), not the main result path that uses `ProcessChunkRange` in the header.

**Why it might be slightly slower:**

- One extra function call per item in the hot loop. If the compiler does not inline `CheckMatchers`, call overhead can add a few percent on this path.
- The main search hot path (`ProcessChunkRange` in ParallelSearchEngine.h) was **not** changed to use `CheckMatchers`; it still uses inline `MatchesPatterns`. So this only affects the ID-only code path.

**Who is affected:**

- Callers that use the engine in “IDs only” mode (ProcessChunkRangeIds).

**Mitigation options:**

- Mark `CheckMatchers` with `inline` and ensure it’s defined in the same TU so the compiler can inline it.
- If needed, re-inline the matcher checks in ProcessChunkRangeIds and satisfy cpp:S134 by other means (e.g. early returns, different structure).

**Addressed (2026-01-30):** `CheckMatchers` is now declared `inline` in ParallelSearchEngine.cpp so the compiler can inline it in the ProcessChunkRangeIds hot loop, removing call overhead.

---

## What did *not* change vs Jan 7

- **Bloom filter:** It was added and removed on 2026-01-30. On Jan 7 there was no Bloom, so its removal does **not** explain “slower than Jan 7.” It could only explain “slower than a brief window when Bloom was enabled” (extension set ≥ 24).
- **CompareSearchResults (94f6647):** Logic was only refactored into helpers; behavior and cost are effectively unchanged.
- **Perf optimizations (ebc61b7, Jan 23):** try_emplace, string_view for queries, vector reserve – these improve performance; they do not cause regression.

---

## Recommended next steps

1. **Confirm scenario:**  
   - If the slowdown is with **path regex** (`vs:`/`rs:`): VectorScan removal is the prime cause; consider preferring `pp:` or re-adding a SIMD regex path.  
   - If the slowdown is with **extension filter** only: extension cascading change is the likely cause; consider restoring a direct hash path for small extension sets.  
   - If the slowdown is in **ID-only** searches: consider inlining or re-inlining the matcher checks in ProcessChunkRangeIds.

2. **Optional micro-optimization:**  
   - In `MatchesExtensionFilter`, when `context.sorted_extensions.empty()` (or when not using Bloom and set is small), call `ExtensionMatches(ext_view, context.extension_set, context.case_sensitive)` directly to restore the former “hash-only” fast path for extension filtering.

3. **Benchmark:**  
   - Run the same search scenarios (substring, glob, path pattern, path regex, with/without extension filter) before and after reverting or applying the mitigations above to quantify each contribution.

---

## Search flow inlining review (2026-01-30)

Hot-path helpers are correctly inlined so the compiler can eliminate call overhead.

### ProcessChunkRange (main search path)

- **Where:** Template in `ParallelSearchEngine.h`; instantiated in `LoadBalancingStrategy.cpp` when calling `ProcessChunkRange`.
- **Per-item helpers (all in header, `inline`):**
  - `GetExtensionView` – inline in header
  - `MatchesExtensionFilter` – inline in header → calls `search_pattern_utils::ExtensionMatches` (inline in `SearchPatternUtils.h`)
  - `MatchesPatterns` – inline in header
  - `CreateResultData` – template inline in header
  - `SimdCheckDeleted` – inline in header (when SIMD_CODE_AVAILABLE)
  - `ValidateChunkRange`, `ShouldSkipItem` – inline in header
- **Result:** All per-item work is in the same header; the compiler can inline everything when the template is instantiated.

### ProcessChunkRangeIds (ID-only path)

- **Where:** Implemented in `ParallelSearchEngine.cpp`; calls helpers from the same TU or from the included header.
- **Per-item helpers:**
  - `MatchesExtensionFilter` – defined **inline in header**; `ParallelSearchEngine.cpp` includes the header → can be inlined.
  - `CheckMatchers` – defined **inline** in `ParallelSearchEngine.cpp` (same TU) → can be inlined.
- **Per-chunk (once per chunk, not per item):**
  - `ValidateChunkRangeIds` – **static inline** in `ParallelSearchEngine.cpp` (same TU) → can be inlined.
- **Result:** Hot loop only uses inlined helpers; no extra call overhead.

### Not in hot path

- `ExtractFilenameAndExtension` (in `parallel_search_detail`) – defined in `.cpp`, not used in the search hot loop (extension path uses `GetExtensionView`). No change needed.
- `CreatePatternMatchers` – called once per search; not in the per-item hot path.
