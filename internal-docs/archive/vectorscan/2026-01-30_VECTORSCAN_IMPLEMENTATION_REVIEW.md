# VectorScan Implementation Review: Why Expected Regex Performance Gains Did Not Materialize

**Date:** 2026-01-30  
**Goal:** Review our VectorScan integration for flaws, blind spots, and misunderstandings that explain the lack of expected performance improvement over std::regex.

---

## Executive Summary

VectorScan (Hyperscan) was integrated to provide 10–100× faster regex matching for the `vs:` prefix. In practice, it did not deliver the expected gains and in some scenarios was slower. This review identifies **workload mismatch**, **per-thread compilation cost**, **scratch allocation in the hot path**, **flag/semantic trade-offs**, and **lack of batch usage** as the main causes. Fixing these would require non-trivial design changes; given the removal plan, this document serves as a post-mortem and reference.

---

## 1. Workload Mismatch: Many Tiny Scans vs. Large-Buffer Throughput

**Finding:** Our usage is the opposite of Hyperscan’s sweet spot.

- **What we do:** One `hs_scan()` per path or filename. Each buffer is **small** (typically 20–200 bytes).
- **What Hyperscan is tuned for:** High-throughput scanning of **large** blocks. Documentation describes throughput in Gbps and “a 3000-bit block scanned in 1 microsecond” — i.e. large blocks, not millions of tiny ones.

**Why it hurts:**

- Each call has fixed overhead: function call into the library, scratch validation, callback setup, and (for very short buffers) SIMD may not pay off.
- std::regex on a 50-byte string is already fast; the relative cost of that overhead is high.
- So **per-scan overhead can dominate** when the workload is “many small buffers” rather than “few large buffers.”

**Blind spot:** We assumed “VectorScan is 10–100× faster” would apply to our “one regex, one short path/filename per call” pattern. That claim applies to scanning large data with the same pattern; it does not automatically apply to millions of tiny, independent scans.

---

## 2. Per-Thread Matcher Creation → N Compilations

**Finding:** We compile the same VectorScan pattern **once per worker thread**, not once per search.

**Where it happens:**

- `CreatePatternMatchers(context)` is called **inside each worker** (e.g. `ParallelSearchEngine.cpp` inside the worker lambda, and `LoadBalancingStrategy.cpp` in `ThreadSetupAfterLock`).
- So each thread runs `CreateFilenameMatcher` / `CreatePathMatcher` → `CreateMatcherImpl` → for `vs:` → `vectorscan_utils::GetCache().GetDatabase(pattern, case_sensitive)`.
- `GetCache()` is **thread_local**. So each thread has its own cache and compiles the pattern when it first builds its matchers.

**Impact:**

- With N worker threads we do **N Hyperscan compilations** of the same pattern per search.
- Hyperscan compilation is expensive (far more than std::regex compile).
- std::regex has the same “per-thread cache” design, but its compile cost is much lower, so the impact is smaller.

**Correct approach (not implemented):** Create matchers **once** (e.g. on the thread that runs the search orchestration) and **pass** the same matchers (lambdas holding `shared_ptr<hs_database_t>`) into all workers. Then we’d have **one** compilation per pattern per search, shared across threads.

**Blind spot:** The comment says “Create pattern matchers once per thread (optimization)” — the intent was to avoid creating matchers inside the inner loop, but creating them per thread still causes N compilations. The real optimization would be “once per search, shared by all threads.”

---

## 3. Scratch Allocation in the Hot Path (Hyperscan Guidance Violation)

**Finding:** We call `hs_alloc_scratch()` in the path that runs immediately before every `hs_scan()`.

**Hyperscan documentation states:**

- “Do not allocate scratch space for your pattern database **just before** calling a scan function.”
- “Scratch allocation is not necessarily a cheap operation” and should be done “just after the pattern database is compiled or deserialized,” then reused.

**Our code:**

- `RegexMatchPrecompiled()` calls `GetThreadLocalScratch(database)` on **every** match.
- `GetThreadLocalScratch()` calls `hs_alloc_scratch(database, &g_scratch)` every time (thread_local `g_scratch` is reused, but the **call** is in the hot path).

So we invoke `hs_alloc_scratch` in the application’s **scanning path** just before `hs_scan()`, which is exactly what the docs say to avoid. Even if the implementation is a no-op after the first use, we still pay the cost of the call and any internal checks on every match.

**Better approach:** Allocate scratch **once per thread** when the database is first used (e.g. when building the matcher or on first match), store it in thread-local state or capture it in the matcher, and in the hot path only **use** that scratch, never call `hs_alloc_scratch`.

---

## 4. HS_FLAG_MULTILINE Removed → Possible Wrong Semantics for `$`

**Finding:** We may have reverted the fix for end-anchor behavior.

- Bug doc `2026-01-17_VECTORSCAN_END_ANCHOR_NOT_MATCHING.md`: `vs:.*\.cpp$` failed to match; adding `HS_FLAG_MULTILINE` fixed it.
- Current `GetCompileFlags()` in `VectorScanUtils.cpp`: comment says “**HS_FLAG_MULTILINE temporarily removed** to test if it’s causing regression.”

So we might be back to **incorrect or inconsistent `$` behavior** for some patterns (e.g. no match or different than std::regex). That doesn’t directly explain “no speedup,” but it does mean:

- Users might see **wrong results** with `vs:` and switch to `rs:` (so we never measure VectorScan in practice), or
- We might have tuned/flags that hurt performance (e.g. MULTILINE) and removed them at the cost of correctness.

**Blind spot:** Treating “regression” and “correctness” independently can lead to disabling the flag that makes `$` correct, without a clear decision on semantics.

---

## 5. No Batching of Scans

**Finding:** We never scan one large buffer with one pattern; we always do one pattern per call and one small buffer per call.

Hyperscan can scan a single large block very efficiently. Our design is:

- One pattern (compiled once per thread, as above).
- One path or filename per `hs_scan()`.

We never aggregate many paths into one buffer and scan that buffer once (e.g. with newline-separated paths). So we don’t use the “single large block” optimization Hyperscan is built for. Implementing that would require a different API and data flow (e.g. batching paths, scanning, then mapping matches back to paths).

---

## 6. Pattern Syntax and Semantics (ECMAScript vs. PCRE Subset)

**Finding:** Potential for subtle mismatches and silent failures.

- **std::regex** (our `rs:`): ECMAScript grammar.
- **Hyperscan/VectorScan:** PCRE subset (no lookahead, lookbehind, backreferences; different behavior for some constructs).

We have `RequiresFallback()` for lookahead, lookbehind, and backreferences. But:

- Other constructs (e.g. `\s`, `\d`, `$`, `\Z`) can differ between ECMAScript and PCRE.
- When a pattern doesn’t compile or isn’t supported, we **return false** (no match) and do not fall back to std::regex. So `vs:` can “silently” fail for valid `rs:` patterns.

**Blind spot:** We present `vs:` as “same syntax as rs:” in the UI, but semantics and support differ. Users hitting unsupported or different behavior may abandon `vs:` and use `rs:` only, so we don’t see VectorScan in real use and can’t measure its performance where it does apply.

---

## 7. Single-Match Flag Overhead (Minor)

**Finding:** Hyperscan docs note that `HS_FLAG_SINGLEMATCH` has “some overhead” for tracking whether each pattern has matched, and “some applications with infrequent matches may see reduced performance” with it.

We use `HS_FLAG_SINGLEMATCH` for the “one match per pattern” case (filename/path match). For a single pattern and a small buffer this may still be a net win, but it’s a possible secondary factor and worth being aware of if tuning.

---

## 8. What We Did Right (After Previous Fixes)

- **Pre-compiled database in the matcher:** We fixed the original bug (per-match allocation + per-match cache lookup) by pre-compiling and capturing `shared_ptr<hs_database_t>` in the lambda and using `RegexMatchPrecompiled()` with `std::string_view`. The hot path no longer does string conversion or cache lookup per match.
- **Thread-local cache:** Avoids mutex contention (same idea as std::regex cache).
- **HS_FLAG_SINGLEMATCH and HS_FLAG_DOTALL:** Align with “one match per pattern” and “`.` matches newlines” for path matching.
- **Raw pointer in hot path:** Passing `db.get()` instead of `shared_ptr` avoids reference counting in the inner loop.

So the **per-match** path is already optimized; the remaining issues are **design-level** (workload, N compilations, scratch in hot path, batching) and **correctness/semantics** (MULTILINE, pattern support).

---

## 9. Summary Table

| Issue | Severity | Type | Fix complexity |
|-------|----------|------|----------------|
| Workload: many tiny scans vs. large buffers | High | Design / workload fit | Would require batching or different use case |
| N compilations (matchers created per thread) | High | Design | Create matchers once, pass to workers |
| Scratch allocated in hot path | Medium | API usage | Allocate scratch once per thread, use in hot path only |
| HS_FLAG_MULTILINE removed | Medium | Correctness / semantics | Restore or document and test `$` behavior |
| No batching of inputs | Medium | Design | Would require API and data flow changes |
| Pattern syntax/semantics vs. rs: | Low–Medium | Documentation / UX | Document differences; consider fallback for unsupported |
| HS_FLAG_SINGLEMATCH overhead | Low | Tuning | Optional: measure with/without |

---

## 10. Recommendations (If VectorScan Were to Be Retained)

1. **Create matchers once per search:** Build `PatternMatchers` (and thus VectorScan databases) on a single thread and pass the same matcher objects to all workers. This removes N−1 redundant compilations.
2. **Move scratch out of the hot path:** Allocate scratch once per thread (e.g. when the database is first used), store it (e.g. thread-local or alongside the matcher), and in `RegexMatchPrecompiled()` only use that scratch; do not call `hs_alloc_scratch` in the hot path.
3. **Restore or clarify MULTILINE:** Either restore `HS_FLAG_MULTILINE` and re-evaluate any regression, or document and test that `$` has different semantics without it.
4. **Consider batching (larger change):** If we ever need regex over many paths, consider batching paths into one buffer (e.g. newline-separated) and scanning once per pattern, then mapping matches back to paths.
5. **Document vs: vs rs::** Clearly document that `vs:` is a PCRE subset, list unsupported constructs, and that unsupported patterns return no match (no automatic fallback to `rs:`).

Given the existing **VectorScan removal plan** (`docs/plans/2026-01-30_VECTORSCAN_REMOVAL_PLAN.md`), these recommendations are for reference only; the main takeaway is that the missing performance gain is explained by workload fit, per-thread compilation, and scratch usage rather than by a single small bug in the hot path.
