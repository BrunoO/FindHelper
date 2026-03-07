# Technical Debt Review - 2026-02-01

## Executive Summary
- **Health Score**: 7/10
- **Critical Issues**: 1
- **High Issues**: 2
- **Total Findings**: 6
- **Estimated Remediation Effort**: 12 hours

## Findings

### Critical
1. **Performance Bottleneck: File I/O in Search Hot Path**
   - **File**: `src/index/FileIndex.cpp` (Line 235), `src/search/ParallelSearchEngine.cpp` (Lines 192, 285)
   - **Debt type**: Memory & Performance Debt
   - **Risk explanation**: `LoadSettings` is called for every search request, which triggers synchronous file I/O to read `settings.json`. In the `SearchAsyncWithData` path it is called 2–3 times (FileIndex once, ParallelSearchEngine once for strategy, and again in `DetermineThreadCount` when thread_count ≤ 0). This significantly increases search latency, especially on slow disks or network drives.
   - **Suggested fix**: Cache settings in `Application` and pass the search-relevant subset (dynamicChunkSize, hybridInitialWorkPercent, loadBalancingStrategy, searchThreadPoolSize) through SearchController → SearchWorker → FileIndex / SearchContext so the search path never calls `LoadSettings`.
   - **Severity**: Critical
   - **Effort**: 2 hours (re-routing settings through context)
   - **Verdict**: **DO** — Relevant; fix is clear and Application already has settings at trigger time.

### High
2. **Naming Convention Violations: Constants and Member Variables**
   - **File**: Multiple (e.g., `src/usn/UsnMonitor.h`, `src/utils/Logger.h`)
   - **Debt type**: Naming Convention Violations
   - **Risk explanation**: Mixing `kPascalCase` for constants with `ALL_CAPS` and `snake_case` for member variables without trailing underscores. This reduces code consistency and makes it harder for automated tools to verify style.
   - **Suggested fix**: Systematic rename of constants to `kPascalCase` and member variables to `snake_case_`.
   - **Severity**: High
   - **Effort**: 4 hours (system-wide rename)

3. **Inconsistent Ownership: Raw Pointers in `InitialIndexPopulator.cpp`**
   - **File**: `src/index/InitialIndexPopulator.cpp:42,44`
   - **Debt type**: C++ Technical Debt
   - **Risk explanation**: Using `std::atomic<size_t>*` and `MftMetadataReader*` as raw pointers without clear ownership or lifecycle management in a multi-threaded context.
   - **Suggested fix**: Use `std::unique_ptr` or reference wrappers if ownership is external.
   - **Severity**: High
   - **Effort**: 1 hour
   - **Verdict**: **Optional / Low priority.** Ownership is clear when traced: (1) `indexed_file_count` is **caller-owned** (UsnMonitor owns `indexed_file_count_`, passes `&indexed_file_count_`; pointer is used only during the synchronous `PopulateInitialIndex` call). (2) `mft_reader` points to a **stack object** in the same function (`MftMetadataReader mft_reader(volume_handle)` in `PopulateInitialIndex`; pointer is non-owning, never escaped, same thread). So no leak, no use-after-free; “multi-threaded” risk is overstated (PopulationContext is used only in the populator thread). **Do not** use `unique_ptr` for `indexed_file_count` (caller owns it). If improving: document “caller-owned, must outlive call” for the atomic; optionally use `MftMetadataReader&` in the `#ifdef ENABLE_MFT_METADATA_READING` path to make non-owning explicit. **Drop** from critical/high backlog; at most a short comment or doc update.

### Medium
4. **Missing `[[nodiscard]]` on Error-Returning Functions**
   - **File**: `src/index/FileIndexStorage.h:77,78`
   - **Debt type**: C++17 Modernization
   - **Risk explanation**: Functions like `RenameLocked` and `MoveLocked` return `bool` indicating success but are not marked `[[nodiscard]]`, allowing callers to accidentally ignore failure.
   - **Suggested fix**: Add `[[nodiscard]]` to all functions returning error codes or success booleans.
   - **Severity**: Medium
   - **Effort**: 2 hours
   - **Assessment (2026-02-02):** Confirmed. `RenameLocked` and `MoveLocked` are the only `bool`-returning functions in `FileIndexStorage.h` and neither has `[[nodiscard]]`. Both are called only from `IndexOperations.cpp` (lines 103, 139), and both call sites check the return value (`if (!storage_.RenameLocked(...)) return false;` / `if (!storage_.MoveLocked(...)) return false;`) — no current bug. Risk is preventive: future callers could ignore the return value; `[[nodiscard]]` would trigger a compiler warning. Fix for these two declarations is trivial (~5 min); 2 h effort applies if auditing all error-returning functions project-wide. **Verdict:** Valid finding; add `[[nodiscard]]` to both as a quick win.

5. **Manual Memory Management in `LightweightCallable.h`**
   - **File**: `src/utils/LightweightCallable.h:48,193`
   - **Debt type**: Memory & Performance Debt
   - **Risk explanation**: Uses placement `new` and explicit destructor calls. While necessary for some type-erasure patterns, it increases the risk of memory leaks or use-after-free if not handled perfectly.
   - **Suggested fix**: Use `std::aligned_storage` (deprecated in C++23, but okay for C++17) or verify with static analysis.
   - **Severity**: Medium
   - **Effort**: 2 hours
   - **Assertion (2026-02-02):** Verified. Placement `new` at lines 46, 195, 201; explicit destructor calls at lines 191 (`DestroyImpl`), 202 (`MoveImpl`). Implementation is correct: destructor and copy/move assignment call `destroy_` before overwriting; `MoveImpl` destroys source after move; empty state uses null `destroy_` so no call on uninitialized storage. Storage is `alignas(kStorageAlign) char storage_[kStorageSize]` — valid for placement new in C++17. Replacing with `std::aligned_storage_t<kStorageSize, kStorageAlign>` would be stylistic only; current approach is correct and already justified with NOSONAR/NOLINT. No change required for correctness; optional refactor if preferring aligned_storage idiom.

### Low
6. **Commented-out Code and TODOs**
   - **File**: Multiple
   - **Debt type**: Dead/Unused Code
   - **Risk explanation**: "TODO" and "POTENTIAL BUG" comments left in code without tracking can lead to forgotten issues.
   - **Suggested fix**: Move TODOs to issue tracker and resolve commented-out code.
   - **Severity**: Low
   - **Effort**: 1 hour
   - **Assertion (2026-02-02):** Verified against current codebase. The example `FileIndex.cpp:106` is **outdated**: line 106 in `src/index/FileIndex.cpp` is `} else {` (normal logic), not a TODO or POTENTIAL BUG. Grep of `src/` and `tests/` for `TODO` and `POTENTIAL BUG` found **no matches** in project source; all such comments are in `external/` (freetype, nlohmann_json, doctest). No significant commented-out code blocks were found in `src/`. The finding’s **general guidance** (track TODOs, avoid leaving commented-out code) remains valid as preventive policy; **current project source is clean** for this item.

## Quick Wins
1. Add `[[nodiscard]]` to `FileIndexStorage` methods (15 min).
2. Fix `UpdateAtomicMax` template naming (5 min).
3. Cache `settings` in `FileIndex` to avoid repeated `LoadSettings` calls during a single search (30 min).

## Recommended Actions
1. **Immediate**: Fix the `LoadSettings` I/O bottleneck by passing settings into the search engine.
2. **Short-term**: Audit and fix member variable naming convention (`snake_case_`).
3. **Long-term**: Implement a centralized settings management service that pushes updates to subscribers rather than requiring polling/re-loading.
