# Specification: Extension Index — Clean Implementation at Trigram Parity (FindHelper)

> **Status: ✅ Implemented.** The `ExtensionIndex` is live; this spec is retained in `specs/` as the design record.

**Feature Name:** Inverted File-Extension Index with Roaring Bitmaps (clean rewrite, no reuse of `feat/extension-index-*`)
**Status:** ✅ Implemented
**Date:** 2026-10-09
**Target Platforms:** macOS (dev), Windows (primary target), Linux
**Language Standard:** C++17
**Relevant prior art:** `specs/2026-09-29_FILENAME_TRIGRAM_ROARING_INDEX_SPEC.md`, `src/search/FilenameTrigramIndex.{h,cpp}`, `src/index/FileIndexMaintenance.{h,cpp}`, `src/index/FileIndex.cpp` (`RecomputeAllPaths`), `src/path/PathOperations.cpp`, `src/search/ParallelSearchEngine.cpp`

---

## 1. Objective

Build a file-extension inverted index from scratch that is a **mechanical twin of `FilenameTrigramIndex`**:

- Same async lifecycle (generation, ticket, off-lock build, mutation replay, readiness flags).
- Same mutation touchpoints (`PathOperations`, compaction, full recompute, clear).
- **One shared SoA extraction loop** producing both trigram and extension postings — never two passes over `PathStorage::SoAView`.
- Query integration in `ParallelSearchEngine` as a union (OR over extensions) intersected (AND) with the trigram intersection.

Non-goals: changing trigram semantics, changing the cost-model constants, changing `SearchContext` extension parsing.

## 2. Correctness contract

1. **Candidate-generator semantics (superset, never subset).** The index may return false positives; it must never cause false negatives. Precision is enforced downstream by the existing verifier (`ExtensionMatches` + `PatternMatcher`).
2. **Case policy.** Store keys lowercase-normalized. All queries against the index use lowercase-normalized terms regardless of `case_sensitive`. Rationale: the verifier re-checks case exactly, so a lowercase index is a valid superset for case-sensitive queries. Document this in the header.
3. **Row-id stability.** Posting row-ids are SoA array indices (`uint32_t`). Any operation that reassigns row-ids (compaction, prefix-move with tombstone+append, full recompute) must refresh or move extension postings in the same critical section pattern as trigrams.
4. **Empty semantics.** `QueryCandidates({})` → `std::nullopt` (unknown / no filter). Known-empty → `std::optional` holding an empty bitmap. `UnionCardinality` mirrors this contract; it must not materialize the union just to count.
5. **Lock order.** Index → extension/trigram locks only; never take the index lock while holding an index-internal lock. `SubmitBuild` waits outside the index lock (same rule as trigram).

## 3. New files

### 3.1 `src/search/ExtensionIndex.h` / `src/search/ExtensionIndex.cpp`

Namespace `extension_index`. Class `ExtensionIndex`, non-copyable, non-movable (same as trigram).

State (mirror trigram exactly):

```cpp
mutable std::shared_mutex mutex_;
flat_hash_map_t<std::string, roaring::Roaring> posting_lists_;
struct PendingMutation { std::string extension; uint32_t row_id; bool is_add; };
std::vector<PendingMutation> pending_mutations_;
std::atomic<uint64_t> build_generation_{0};
std::atomic<bool> is_ready_{true};
std::atomic<bool> is_building_{false};
std::mutex build_submit_mutex_;
std::future<void> build_future_;
```

Plus `ExtensionBuildTicket` (move-only `{generation, postings}`) and `kInvalidGeneration`, mirroring `TrigramBuildTicket`.

API (one-to-one with trigram, names adapted):

- `IsReady()`, `IsBuilding()`, `MarkBuilding()` — identical doc comments on generation-advance obligations. `MarkBuilding` is the only generation-advancing entry; `BeginRebuild` below never marks.
- `BeginRebuild(std::vector<ExtensionGroup> groups)` — takes **pre-extracted, already-grouped** postings (produced by the single combined extraction in §3.2, owned by the caller). Validates the generation is still current, wraps groups in an `ExtensionBuildTicket`, and returns it. Takes no `SoAView`: this is what guarantees a single SoA pass — the extractor runs once per rebuild, never once per index. On allocation failure calls `AbortBuild` and returns an invalid ticket.
- `SubmitBuild(ticket, thread_pool)` — identical wait-then-offload structure (dedicated `build_submit_mutex_`, stale-generation early-out, sync fallback when pool is null, catch-all → `AbortBuild`). Takes a pre-built ticket only; never extracts.
- `RebuildAsync(soa_view, storage_size, thread_pool)` — standalone convenience only (tests, `Build`): `MarkBuilding()` + combined extraction + `BeginRebuild(extension half)` + `SubmitBuild`. Wired call sites (`RebuildPathBuffer`, `RecomputeAllPaths`) must NOT use this — they run one extraction for both indexes and call each index's `MarkBuilding` + `BeginRebuild(pre-extracted)` + `SubmitBuild` manually.
- `BuildFromPostings(generation, std::vector<ExtensionGroup> groups)` — groups arrive already grouped by extension (see §3.2). Off-lock: sort groups by extension for determinism, bulk-add each group's row-ids with `roaring::BulkContext`, `runOptimize()`, then under `unique_lock`: stale-generation check → swap → `ReplayPendingMutationsLocked()` → clear buffer → `is_building_=false`, `is_ready_=true`. `catch (bad_alloc / exception)` → `AbortBuild`.
- `AbortBuild(generation)` noexcept — replay-or-clear buffer, restore flags (copy trigram's logic, including the empty-index `is_ready` decision).
- `Build(soa_view, storage_size)` = `RebuildAsync(..., nullptr)` (sync, lock-held only — same constraint comment as trigram).
- `AddEntry(row_id, extension)` / `RemoveEntry(row_id, extension)` — normalize; if `is_building_` buffer the mutation and return, else mutate live map. `RemoveEntry` erases the key when its bitmap becomes empty.
- `Clear()` — bump generation, clear map + buffer (invalidates in-flight builds, same as trigram).
- `~ExtensionIndex()` — bump generation, wait on `build_future_` under `build_submit_mutex_` (copy trigram).
- `QueryCandidates(query_extensions)` — union with `|=`; shared-lock.
- `UnionCardinality(query_extensions)` — **exact sum, zero bitmap work**: posting lists are pairwise disjoint by construction (each row carries at most one extension; extension-less rows are indexed nowhere), so the union size is the sum of per-term cardinalities. Terms are deduped once in `NormalizeIndexableExtensions` (sole producer of `Normalized` inputs, which also enforces the decline-on-any-empty rule). The gate therefore never materializes a bitmap; only the filter builds the union once.
- `GetMemoryUsageBytes()` — same formula as trigram (per-key `capacity()` + `getSizeInBytes()`; no `sizeof(*this)`, matching `FilenameTrigramIndex::GetMemoryUsageBytes`).
- `NormalizeExtension(view)` — strip one leading `.`, ASCII-lowercase via `string_search::ToLowerChar`. Takes the raw view; **no case_sensitive parameter** (per §2.2 the index is always lowercase; verifier owns case).

Grouped extraction output (one owning string per **distinct** extension, never per row):

```cpp
struct ExtensionGroup {
  std::string extension;            // normalized, lowercase
  std::vector<uint32_t> row_ids;    // ascending (rows visited in order)
};
```

`ExtensionGroup` is defined in `ExtensionIndex.h`. The extractor interns normalized extensions during the single pass (at most one owning string per distinct extension; per-row records are plain `uint32_t` appends into the group's vector), so the transient peak is `#live-rows × 4 B` plus distinct-extension strings — not one allocation per row.

### 3.2 `src/search/CombinedIndexPostings.h` (header-only extractor)

Single function, single loop:

```cpp
struct CombinedIndexPostings {
  std::vector<uint64_t> trigram_postings;   // packed (key << 32 | row_id), as today
  std::vector<ExtensionGroup> extension_groups;  // grouped by normalized extension
};
CombinedIndexPostings ExtractCombinedPostings(const PathStorage::SoAView& soa_view,
                                              size_t storage_size);
```

Loop body per live row `i` (deleted skipped once):

1. `path_len = GetPathLength(i, storage_size)` — computed **once**, shared by both halves.
2. Trigram half: identical to `FilenameTrigramIndex::ExtractPostings` inner loop (filename slice from `filename_start`, rolling lowercase pack, push packed uint64). Keep the existing max-postings pre-pass and `kMaxPostingReservation` clamp behavior for the trigram vector.
3. Extension half: read `extension_start[i]`; skip `SIZE_MAX` / out-of-range / empty-after-normalize; intern the normalized extension into `extension_groups` (one owning string per distinct extension) and append the row-id to that group's vector (ascending by construction). Reserve group count to a small constant (distinct extensions are in the hundreds); row-id vectors grow geometrically.
4. Grouping happens **during** extraction (interning under the caller's index lock, where SoA views are valid); `BuildFromPostings` only sorts the groups and bulk-adds — no second pass over rows, no per-row strings, no views into SoA surviving past unlock.

`FilenameTrigramIndex::ExtractPostings` stays untouched. The trigram half of the combined loop is kept byte-identical by construction (same slice, same packing, same push order); `filename_trigram_index_tests` guard against drift. No wrapper, no refactor of the trigram path. No double traversal anywhere: the combined extractor is the **only** SoA pass per rebuild (see §5).

## 4. Wiring (same loops and mechanisms)

### 4.1 `src/index/FileIndexStorage.h` / `.cpp`

- Member `extension_index::ExtensionIndex extension_index_;` next to `trigram_index_`, with mutable/const accessors.
- `ClearLocked()` clears both (extension `Clear()` bumps its generation, invalidating in-flight builds — required).

### 4.2 `src/index/ISearchableIndex.h` + `src/index/FileIndex.h`

- `virtual const extension_index::ExtensionIndex* GetExtensionIndex() const { return nullptr; }`, overridden in `FileIndex` to return `&storage_.GetExtensionIndex()`.
- `FileIndex`: add `GetExtensionIndexMemoryBytes()` (try-lock variant included) and `IsExtensionIndexBuilding()` next to the trigram equivalents.

### 4.3 `src/index/FileIndexMaintenance.{h,cpp}`

- Ctor takes `extension_index::ExtensionIndex& extension_index` (bundled next to `trigram_index`; keep param-count rule — if the ctor grows past the limit, introduce a small `IndexRefs` struct holding both refs).
- `RebuildPathBuffer()`: after `path_storage_.RebuildPathBuffer(...)` and **in the same lock scope**, call `MarkBuilding()` on both indexes, run **one** `ExtractCombinedPostings(view, size)` guarded by try/catch(`bad_alloc`) → `AbortBuild` both on failure (tickets stay invalid; submits after unlock are safe no-ops), wrap each half via its index's `BeginRebuild(pre-extracted)` into a ticket; release lock; call both `SubmitBuild`s. The single extraction from the same `SoAView` is what keeps both indexes atomically consistent with the compacted rows. Extend the existing TOCTOU comment to cover both indexes.
- Destructor: nothing to do (index destructors own their futures).

### 4.4 `FileIndex::RecomputeAllPaths()` (`src/index/FileIndex.cpp`)

- Same pattern: under `index_mutex_`, `MarkBuilding()` on both indexes, single combined extraction guarded by try/catch(`bad_alloc`) → `AbortBuild` both on failure (tickets stay invalid; submits after unlock are safe no-ops), both tickets via `BeginRebuild(pre-extracted)`, record the single-pass timing; after unlock, submit both builds. Do not add a second SoA pass.

### 4.5 `src/path/PathOperations.cpp`

- `InsertPath`: remove old extension + add new extension alongside the existing trigram calls. One `GetExtension` call per path; reuse the already-fetched path view.
- `RemovePath`: same pairing.
- `UpdatePrefix` row-move callback: preserve the existing early return exactly (both indexes skipped when filename is empty). Rationale: an empty filename (path ending in a separator) implies an empty extension slice, so both the trigram and extension ops are no-ops there — there is no leak on either side and no behavior change. Do not reorder; keep the `uint32_t` range guard as-is.

### 4.6 `src/search/ParallelSearchEngine.cpp`

- Rename `TrigramCandidateQuery` → `CandidateQuery` with `use_trigram / use_extension` flags, `required_trigrams`, `extensions`, `cardinality`, `query_attempted`, `known_empty`.
- Gate phase (`QueryCandidates`): plan trigrams; `has_extensions = context.HasExtensionFilter()`. Set `use_trigram` only if planner yields trigrams **and** trigram `IsReady()`; set `use_extension` if filter present **and** ext `IsReady()`. If neither, return untouched (full scan). `query_attempted=true` if either is used. Cardinality = min of the used sides (each side's fast-cardinality call); `0` → `known_empty`; both-unused sentinel (`max`) must be unreachable here — assert it. Note: min is an upper bound of the true intersection size, so `batch.hits`/`arena` may over-reserve when the two sides barely overlap — safe, never under-reserves.
- Filter phase (`FilterCandidates`): under **one** shared lock scope, re-query trigram intersection and extension union fresh (TOCTOU rule, same comment as today), intersect them (`combined &= ext` / first-wins move), snapshot `SoAView`, iterate `combined` through the existing verifier (which already enforces extension + item-type + matcher). If either index flipped non-ready mid-flight → return false (fall back to scan). Definite-empty → return true without work.
- Extension-only queries (no trigrams, e.g. `*.cpp` or extension-only mode) must take the filter path with `combined` = extension union alone.
- Keep `ShouldUseTrigramFilter` / mispredict logging unchanged (rename only if trivial; prefer minimal diff — parameterize on the combined cardinality).

## 5. Locking and generation protocol (normative)

- `Begin*` (mark + wrap) runs **under** the caller's index lock; extraction also runs under that lock (it reads live SoA); `SubmitBuild` (wait + enqueue) runs **after** unlock. Both tickets in `RebuildPathBuffer` / `RecomputeAllPaths` are created from the **same** `ExtractCombinedPostings` result — exactly one SoA pass per rebuild.
- At most one build per index in flight; `SubmitBuild` serializes on `build_submit_mutex_`, waits the predecessor, drops stale generations without enqueueing.
- Mutations arriving while `is_building_` are buffered (`pending_mutations_`) and replayed in order at commit and at abort (trigram's exact semantics).
- `Clear()` and `~Index()` bump the generation so orphaned tasks discard.

## 6. Tests (new target `extension_index_tests`)

Register in `scripts/test_targets.txt` + `CMakeLists.txt` (copy the `filename_trigram_index_tests` block).

Unit (`tests/ExtensionIndexTests.cpp`):

1. Normalize: `".CPP"→"cpp"`, `""→""`, `"."`→`""`.
2. Add/query/remove/union/clear basics.
3. Fast `UnionCardinality` matches materialized union size; empty filter → `nullopt`.
4. Build-generations: `MarkBuilding()` + combined extraction + `BeginRebuild(groups)` + mutate-during-build, assert mutations replayed at commit; stale generation discarded; `Clear()` during build leaves a consistent empty-ready state.
5. Memory accounting smoke test.

Integration (extend existing suites, no new harness):

- `path_operations_tests`: insert/remove/prefix-move keeps extension postings in sync (query after each op), including dotted directory names (e.g. `foo.d/`): pin whether directory extensions are indexed and assert the verifier's item-type filter still governs visibility.
- `file_index_maintenance_tests`: populate → delete past threshold → `Maintain()` → extension queries reflect postings at **new** row-ids (regression test for the stale-compaction bug).
- `parallel_search_engine_tests`: extension-only filter uses filter path; combined filename+extension intersects; not-ready index falls back to scan with identical results; case-sensitive query returns exact-case results (verifier precision over lowercase index).

## 7. Telemetry, revert criteria, and acceptance

### 7.1 Build-side logs (mirror trigram lines, split by index)

- `CombinedIndexPostings::Extract` time + transient MB (trigram postings, extension group/row counts) alongside the existing `FilenameTrigramIndex extraction` line, so the two halves of the single combined pass are attributable separately. The `FileIndex trigram postings extract` line keeps its historical name and now measures the combined pass (per-half split is in the extractor line).
- `ExtensionIndex build` line with groups/unique-extensions/index MB (mirror `FilenameTrigramIndex: postings=…, unique_trigrams=…, index_mb=…`).
- Combined-extraction total in `RecomputeAllPaths` benchmark logs next to `FileIndex trigram postings extract`.

### 7.2 Live diagnostics (`MetricsWindow.cpp` parity)

- Extension memory + status lines next to the trigram block (`TryGetTrigramIndexMemoryBytes` pattern, same 250 ms TTL cache): `Extension Index Memory`, `Extension Index Status` (Ready/Rebuilding).
- Combined total line so memory growth is attributable without mental math.

### 7.3 Query attribution (required for a data-driven revert decision)

- Extend the filter-path logging to record the mode per take: trigram-only / extension-only / combined. Minimum: atomic counters (`extension_filter_takes_`, per-mode) plus union cardinality at gate time on the existing mispredict path, so a "filter slower than scan" event can be blamed on (or exonerate) the extension half.
- No per-query log spam: counters + the existing rate-limited `LOG_IMPORTANT_BUILD` discipline.

### 7.4 Kill switch (retired)

Retired in `fbab4a31`: the `use_extension_index` context flag, its gate
handling, and its differential test were removed in favor of direct
measurement. Before/after comparison is across builds (Phase 0 baselines
are the off-state reference), not a runtime flip. If a runtime escape
hatch is ever needed again, reintroduce the flag at the gate with the
not-ready-equivalence contract and its differential test.

### 7.5 Benchmark configs

- Two `SearchBenchmark` JSON configs with extension filters: (a) extension-only (`*.cpp`-style / extension-only mode), (b) filename + extension combined. Run against the same tree per build; record min/avg/max per config (max-of-5 stands in for p95 — see harness note in §7.6).

### 7.6 Revert criteria (normative)

Revert if any holds on the reference large tree:

1. p95 extension-filtered query latency regresses vs. scan/trigram baseline beyond noise, or
2. combined build time (extract + both commits) exceeds the agreed budget vs. trigram-only baseline, or
3. combined index memory exceeds budget.

#### Baseline (Phase 0, recorded 2026-10-09, commit `6cb4bd98`)

Environment: macOS, Debug build (`--no-asan`, ASan/TSan off), 8 HW threads, AVX2 off.
Reference tree: `tests/data/std-linux-filesystem.txt` — 706,120 indexed paths, 560,109 with extensions, **962 distinct** extensions (top: html 93k, js 62k, png 41k).

Build (trigram-only, from `search_benchmark_*` logs):

| Metric | Value |
|---|---|
| Trigram extract | ~372 ms, 11,404,932 postings, 87 MB peak transient |
| Trigram build (async commit) | ~2.3 s, 26,247 unique trigrams |
| Trigram index memory | 18 MB (`index_mb=18`) |
| `RecomputeAllPaths` total | ~9.8 s (path recompute dominates; index extraction is ~4%) |

Query (`search_benchmark --iterations 5 --warmup 1`, min/avg/max over 5 runs):

| Config | Matches | min / avg / max (ms) | Path taken today |
|---|---|---|---|
| A extension-only (`extensions:["cpp"]`) | 1,486 | 28.21 / 29.08 / 29.91 | full scan + per-candidate verify — the path the index accelerates |
| B filename+extension (`pp:**/imgui/**` + `cpp,h`) | 114 | 0.40 / 0.41 / 0.41 | trigram filter + verify |
| C filename-only rare substring (hotpath) | 0 | 0.04 / 0.04 / 0.04 | trigram filter |
| D match-all + extensions | 29,242 | 51.42 / 53.10 / 54.45 | full scan, worst-case extension cardinality |

Harness note: `search_benchmark` reports min/avg/max only (no per-iteration times); max over 5 runs is used as the p95 proxy. Same-build-config comparison is what matters for revert decisions, not absolute numbers.

#### Budgets (same build config, same tree)

- Query: max-of-5 on configs A/B/D must not exceed the baseline max above by more than 20%. Config A is expected to **improve** substantially (target: single-digit ms); failure to improve is not a revert trigger by itself, regression is.
- Build: combined extract ≤ 700 ms Debug on the reference tree (see on-state note below); combined async commits ≤ 2.9 s (≈2.3 s + 25%).
- Memory: extension index ≤ 10 MB on the reference tree; combined index ≤ 30 MB (18 + 10 + headroom).

#### On-state results (2026-10-09, with index wired, same config/tree)

| Check | Baseline (off) | On-state | Budget | Verdict |
|---|---|---|---|---|
| A ext-only max | 29.91 | **1.55** | improve | ✅ ~19x win |
| B combined max | 0.41 | 0.48 | 0.49 | ✅ inside (+17%) |
| D match-all max | 54.45 | 60.67 | 65.34 | ✅ inside (+11%) |
| C trigram-only | 0.04 | 0.01–0.05 | noise | ✅ |
| Combined extract | ~372 | ~615 (3 runs: 615/618/650) | 700 | ✅ inside |
| Ext build commit | — | 16.1 ms | (in 2.9 s) | ✅ |
| Ext index memory | — | 0 MB (rounds down; 1,395 groups) | 10 MB | ✅ |
| Recompute total | ~9.9 s | ~10.2 s | — | ✅ +3% |

Notes: combined extract ≈ 1.65x trigram-only extract in Debug (`-O0` string/hash cost, ~350 ns/row for the extension half; reserving the interning map did not move it — the cost is per-row normalize+lookup, not rehashing). Absolute share is ~6% of the 10 s recompute, which is path-recompute dominated; Release ratio will be smaller. B/D sit inside budget but above baseline — both are weak-prefilter shapes (common extensions, 29k-candidate D); the `used_extension` bit on the mispredict path (§7.3) attributes any future slow-filter events there. Extension group count (1,395) exceeds the naive distinct-extension estimate (962) because SoA `extension_start` also covers dotted directory names and case variants pre-normalization — superset semantics hold either way.

Windows Release follow-ups on the Documents preset (11 terms), same tree as the 13 ms no-index baseline: `2e61339f` (disjoint-sum gate + verify skip) avg 1.72 ms; `fbab4a31` (bulk `toUint32Array` unpack, throttled cancel poll, retuned arena reserve) avg **1.04 ms** — 12.5x end-to-end. The bulk decode + plain-array loop beat Roaring iteration (container dispatch per value) plus the removed per-row atomic cancel load; remaining cost is union build + enumerate + result copies, i.e. the materialization floor.

### 7.7 Acceptance

- Full suite via `./scripts/build_tests_macos.sh` green; new tests green; manual check on a large tree that (a) extension-filtered search returns identical results with the filter path forced on vs. scan, (b) results stay identical across a `Maintain()` compaction, (c) no new clang-tidy/sonar issues in touched files.
- Telemetry checklist: extract/build/memory lines present for both halves, MetricsWindow parity rendered, kill switch verified on/off, benchmark configs run with numbers recorded in §7.6.

## 8. Rollout order (one hypothesis per commit)

1. `ExtensionIndex` + ticket + tests (no wiring except the `CMakeLists.txt` test-target block, so clang-tidy/sonar see the new TU from the start; index unreachable from production code until step 3).
2. `CombinedIndexPostings` extractor; reimplement trigram extraction on top; trigram tests green, zero behavior change.
3. Storage/interface/`FileIndex` accessors + memory/telemetry helpers.
4. `PathOperations` pairing + `UpdatePrefix` ordering fix.
5. `FileIndexMaintenance` + `RecomputeAllPaths` dual-ticket wiring (single extraction).
6. `ParallelSearchEngine` combined filter + kill switch + integration tests.
7. Telemetry pass (build/extract/memory lines, MetricsWindow parity, mode counters), benchmark configs + baseline numbers for §7.6; remove any temporary logs.

## 9. Why not reuse the existing branch

The branch (`feat/extension-index-5285805162599068482`, +398/−42) proves relevance but bakes in the gaps found in review: synchronous `Build` under a held lock, no generation/pending-mutation machinery, `UnionCardinality` materializing the union, maintenance untouched (stale row-ids after compaction), no readiness gate, and a second SoA pass. Retrofitting parity onto it costs as much as a clean twin built on the trigram template, with higher regression risk. This plan builds the twin directly.
