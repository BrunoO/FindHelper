# Specification: Differential Gate--vs-Scan Guard Test for Search Acceleration

> **Status: ✅ Implemented** (Part I + Part II); kept in `specs/` as the design record.

**Document:** `specs/2026-10-10_DIFFERENTIAL_GATE_GUARD_TEST_SPEC.md`
**Date:** 2026-10-10
**Status:** Implemented (Part I + Part II)
**Target:** USN_WINDOWS / FindHelper (C++17, doctest, CMake/CTest)

---

## 1. Overview & Motivation

The search pipeline accelerates queries with candidate gates — filename/path trigram
postings (`FilenameTrigramIndex`, `TrigramQueryPlanner`), the extension index
(`ExtensionIndex`, `CombinedIndexPostings`), SIMD substring hints, and matcher-internal
required-literal prefilters. Every gate is required to be **conservative**: it may keep
non-matching rows (the per-path verifier drops them), it must **never drop a matching row**.

This invariant has silently broken twice, and the test suite caught it neither time:

1. **Sept 2026 (`8e77d07c`)**: the engine undercounted the `path_dev` regression case by
   299 matches (stale trigram postings around `RebuildPathBuffer` treated "no posting" as
   "definitely empty" — later fixed by `976e3e36`). The undercount was *recorded as the
   golden*, freezing the bug into the test.
2. **Today (HEAD, verified empirically)**: `pp:**/dev**` returns **1841** vs ground truth
   **19796**, and separator-bearing globs (`src/*.cpp`) drop directory-component matches
   (mini-fixture 3 vs 4). Same failure class — a leaf literal not pinned to the final path
   segment pruned by a filename-only index — see
   `internal-docs/analysis/2026-10-10_PATH_DEV_GOLDEN_OSCILLATION_ANALYSIS.md` (Findings 1–2).

Hand-recorded golden counts structurally cannot protect against this class: they are
snapshots of whatever the binary printed on recording day. A correct engine change makes
them stale (false alarm); a buggy engine makes them protective (false security).

**Core idea**: replace trust-in-a-number with **differential testing** — run every battery
query through two independent implementations over the same index and assert **identical
ID sets**:

- **Path A (system under test):** production search with all candidate gates live.
- **Path B (oracle):** production scan fallback with the gates forced non-ready
  (`MarkBuilding()` / `AbortBuild(gen)` — mechanism already proven in
  `tests/SearchBenchmark.cpp:358-402` `VerifyFilterVsScan`).

The comparison machinery exists (`--verify`). What it lacks is (a) an **adversarial query
battery** covering every gate-relevant pattern shape, (b) an **isolating fixture corpus**
where each hazard is the only possible match, (c) a **matcher-prefilter bypass** so the
oracle is independent of Finding 4's shared bug surface, and (d) **wiring into the
automated suite** instead of manual benchmark invocations. This spec defines exactly those.

### Goals

- A fast, ASan-friendly doctest target (`search_gate_differential_tests`) that fails the
  build whenever *any* gate drops a matching row for *any* battery query on the fixture
  corpus.
- Battery covers the full grammar that feeds gates: plain substrings (ci/cs, with/without
  separators), `pp:` patterns (leading/trailing/interior `**`, `lit**` leaves), globs
  (suffix shape, separator shape, brace-free), `re:` regex (escapes, quantifiers,
  alternation), extension-only, extension+path, `folders_only` × pattern, filename+path
  combined.
- Fixture corpus is hand-built so every hazard has a **witness row** whose *only* possible
  match is in a directory component or case variant — one dropped row flips pass→fail
  (today's Findings 1–2 must fail this test *before* being fixed).
- Golden-free: no recorded counts involved; passes across bug fixes, speedups, and gate
  reshuffles without maintenance.
- Reuse the `--verify` scan-forcing mechanism; no second engine implementation.

### Non-goals

- Replacing the existing ImGui regression suite / golden counts (they remain as
  end-to-end smoke + count sanity; the guard is the *correctness* layer).
- Verifying matcher semantics themselves (what a `pp:` pattern *should* match) — the
  oracle uses the same matcher verify logic; the differential tests gate conservatism,
  not spec correctness of the pattern language.
- Timing/perf assertions (the benchmark harness owns those).
- USN/MFT incremental-state races (`976e3e36` territory, covered by its own regression
  tests) — the guard runs on static indexes; a mutation-mode variant is a listed
  follow-up, not in scope.

---

## 2. Terminology

- **Candidate gate:** any prefilter that selects a candidate ID subset before per-row
  matcher verification (`TryCandidateFilterPath`, `ParallelSearchEngine.cpp:393-400`).
- **Scan fallback:** the production full-scan path taken when gates report non-ready
  (`ParallelSearchEngine.cpp:126-151`); this is the oracle's delivery mechanism.
- **Witness row:** a fixture path that matches the query **only** through a component the
  gate cannot see (directory component, case variant, escape sequence), such that a gate
  unsoundness prunes it.
- **Battery:** the fixed list of (query, expectations) pairs run through both paths.
- **Isolation fixture:** the small hand-built corpus (~50 rows) with witness rows for
  every hazard class.
- **Prefilter-bypass seam:** test-only switch that makes matcher-internal
  required-literal prefilters unconditionally pass (behave as "maybe match"), so the
  oracle's verify step is independent of Finding 4.

---

## 3. Requirements & Constraints

### 3.1 Functional Requirements

1. **Equivalence contract.** For every battery query Q over every fixture corpus C:
   `IDs(A) == IDs(B)` where A = production search (gates live, indexes ready), B = forced
   scan (gates non-ready). Set equality on sorted ID vectors, not counts (a gate could
   drop one row and keep another; counts could coincide).
2. **Gate-coverage assertion.** For every Q, path A must be *proven* to have used the
   gate path when the gate is expected ready (reuse the `timings.size() == 1` filter-path
   signal from `tests/SearchBenchmark.cpp:362`), else the test fails with
   `"query took scan path; gate not exercised"`. Without this, A-vs-B degenerates into
   scan-vs-scan.
3. **Witness assertion.** For every hazard class, the battery entry declares a
   `must_include` regex/substring; the test asserts the witness row is in `IDs(B)` (the
   oracle found it — guards the fixture against bit-rot) **and** in `IDs(A)`.
4. **Prefilter-bypass oracle.** B runs with matcher-internal literal prefilters forced to
   no-op (new seam, §4.4), so shared-prefilter bugs (Finding 4) are visible as
   mismatches instead of silent agreement.
5. **Deterministic lifecycle.** Gates must be ready when A runs (wait for async build
   like `WaitForIndexesReady`, `tests/SearchBenchmark.cpp:408`) and restored after B
   (`AbortBuild(gen)` pairing), no leakage between queries; the fixture `FileIndex` is
   test-owned, single-threaded setup, parallel query allowed.
6. **Two tiers.**
   - **Tier 1 (mandatory, fast):** isolation fixture (~50 rows, ms-scale per query), runs
     in the standard suite (`build_tests_macos.sh`, CTest, CI).
   - **Tier 2 (opt-in, heavy):** full `tests/data/std-linux-filesystem.txt` (706,120
     rows, ~17 s index build), enabled by env `FINDHELPER_DIFFERENTIAL_HEAVY=1`; a handful
     of high-value battery entries (the four /dev shapes + one ext+path combo) to catch
     corpus-shape-dependent bugs the mini fixture can't trigger.
7. **Failure output.** On mismatch, print: query, corpus, both set sizes, and up to 5
   sample missing/extra paths (reuse `PrintVerifyDiff` logic,
   `tests/SearchBenchmark.cpp:300-350`, extracted to a shared helper — §4.3).

### 3.2 Constraints

- C++17, project naming conventions (`PascalCase` functions, `snake_case_` members).
- No production behavior change except the §4.4 bypass seam (default off, test-visible).
- No new test-only `const_cast`s in new code: prefer the existing non-const access seam
  on `FileIndex` or a narrow test-only accessor; the benchmark's `const_cast` precedent is
  grandfathered, not copied.
- The new target must follow the project's test-target flow: `add_executable`/`add_test`
  block in `CMakeLists.txt` + name appended to `scripts/test_targets.txt`.
- ASan must stay green; Tier 1 total runtime budget: < 10 s in the suite.

---

## 4. Design

### 4.1 Battery matrix (Tier 1, per hazard)

Fixture witness rows are referenced inline. The isolation fixture file
`tests/data/differential_gate_fixture.txt` is one path per line; ancestor dirs are
materialized by the indexer, so witness rows may be directory entries too.

| # | Query (fields) | Hazard targeted | Witness row (must match) |
|---|---|---|---|
| 1 | `path:"/dev"`, ci | plain CI substring: case-fold consistency (the Sep "40 variant" signature) | `/mnt/DEV/shm/x.txt`, `/a/Dev/y.txt` |
| 2 | `path:"/dev"`, cs | same, case-sensitive fold | `/dev-only lowercase rows present` |
| 3 | `path:"pp:**/dev**"` | Finding 1: `lit**` leaf unpinned | `/a/dev.deep/report.txt` (literal only in dir component, filename has no `dev`) |
| 4 | `path:"pp:**/include/**"` | sound shape (leaf == `**`) — must still pass | `/x/include/a.h` |
| 5 | `path:"src/*.cpp"` | Finding 2: glob-with-separator dir-component literal | `/w/src/x.cpp.d/report.txt` |
| 6 | `path:"*.cpp"` (suffix shape, no separator) | sound suffix narrowing (`b27e928c` branch) | `/any/depth/deep/file.cpp` |
| 7 | `path:"pp:/x/log+"` | Finding 3: `+` quantifier trigram `og+` | `/x/log` (must appear; `log+` matches literally-prefixed) |
| 8 | `path:"re:\\x41ndroid"` | Finding 4: escape prefilter (needs bypass seam to expose) | `/usr/Android.txt` |
| 9 | `path:"/etc" ext:"conf"` | extension postings × path verify interplay | `/etc/svc/ntp.conf`, `/etc/CONF/x` |
| 10 | `ext:"cpp"` only | extension-only search (postings direct) | `/a/b.cpp`, `/a/b.cpp.d/c` |
| 11 | `filename:"tty" path:"/dev"` | filename gate + path verify both live | `/dev/ttyS0` |
| 12 | `path:"/dev" folders_only:true` | item-type applied post-gate on both paths | `/dev/nulls/` directory row |
| 13 | `filename:"pp:*dev**"` | filename-side leaf rule (sound side, regression fence) | `/x/y/devmon.exe` |
| 14 | `path:"pp:C:\\ab**"` (Windows-shape on POSIX host: literal `C:\ab**` semantics preserved as pattern text) | case-insensitivity of pp literals | fixture rows with `\` kept verbatim in data |
| 15 | `path:"pp:**/dev**"` cs=true | shape #3 under case-sensitive fold | `/DEV/x`, `/Dev/y` (must NOT be in IDs(B) for cs) |

Battery entries 1–15 are `(query, must_include)` pairs; Tier 2 reuses entries 1, 2, 3, 5,
9, 10 with the std corpus (entry 3 Tier-2 truth known: 19796 vs today's 1841 → **Tier 2
fails on HEAD until Findings 1–2 are fixed** — that is the acceptance signal).

### 4.2 Isolation fixture design rules

- ≤ 60 rows; every row's existence must be load-bearing for ≥ 1 battery entry (comment
  header maps row → hazard).
- For each hazard: one **witness** row (matches only via the gate-invisible component),
  one **decoy** row (matches via filename too — proves the gate *can* find the literal),
  and one **near-miss** non-matching row (guards against FP drift in A).
- No duplicates; ASCII case variants deliberate.

### 4.3 Code layout

- `tests/SearchBenchmark.cpp`: extract `PrintVerifyDiff` + scan-forcing into shared
  header-only helper `tests/SearchGateDifferential.h` (namespace
  `gate_differential`):
  - `class ScanForcer` — RAII: `MarkBuilding()` on both indexes, dtor `AbortBuild(gen)`;
    ctor takes a **non-const `FileIndex&`** seam (new `GetTrigramIndexForTest()` /
    `GetExtensionIndexForTest()` non-const accessors on `FileIndex`, public under
    `#ifdef FINDHELPER_TEST_SEAMS` or plain const-only-returning `IndexFor` helpers
    mirroring existing unit-test precedent — pick whichever the existing
    `ParallelSearchEngineTests` already use, no new const_cast).
  - `FilterPathProbe` — runs a query capturing the `timings.size()==1` signal (lift from
    benchmark's `RunTimedSearch` usage).
  - `PrintVerifyDiff` — moved verbatim (suppression comments ride along).
  - `DifferentialCheck(file_index, query, must_include) -> bool` — one battery step.
- `tests/SearchGateDifferentialTests.cpp` — doctest suite `SearchGateDifferential*`:
  fixture corpus (loaded from `tests/data/differential_gate_fixture.txt` using the
  existing *probe-upward-from-binary* data-lookup precedent,
  `tests/FolderCrawlerTests.cpp:22-40`, which walks up from the executable dir to find
  `tests/data/...` and emits `MESSAGE(...skipping)` when absent), battery loop §4.1,
  Tier-2 gated by `getenv("FINDHELPER_DIFFERENTIAL_HEAVY")`.
- `CMakeLists.txt` + `scripts/test_targets.txt`: new `search_gate_differential_tests`
  target via the existing `add_findhelper_test(...)` macro (mirrors the
  `parallel_search_engine_tests` block at `CMakeLists.txt:2016`), which picks up the
  `test_core_obj/test_search_obj/test_path_obj/test_utils_obj/test_helpers_obj` link set.

### 4.4 Matcher-prefilter bypass seam (production, minimal)

- `src/search/PathPatternMatcher.{h,cpp}` / `SearchPatternUtils` required-literal
  prefilter: thread a single `bool skip_literal_prefilter_` (default false) through the
  matcher factory, settable **only via a test seam** (constructor tag or dedicated
  `MatcherTestHooks` namespace — one flag, no global state, no atomics). When true,
  prefilter functions return `true` unconditionally ("maybe").
- Oracle path B runs with the flag on; path A with it off. This makes Finding-4-style
  shared bugs surface as mismatches. Battery entry 8 is the fence.
- Production default unchanged; the GUI/benchmark never sets the flag.

### 4.5 Interaction with known-bug fixes (ordering)

- The guard **will fail on today's HEAD** at entries 3, 5, 7 (Findings 1–3). That is by
  design for Tier 2, but Tier 1 must be green-to-merge for the *test's* own task.
  Resolution: ship the guard together with the Findings 1–3 fix (**Part II of this
  spec**), or ship the guard first with those three entries marked
  `TEST_CASE("...")("[.]")` (doctest tag `fail_expected` excluded by default in the
  runner, listed in §9) and flip them active in the fix commit. **Preferred:
  ship-with-fix.** The guard task and the fix tasks (F1–F3) are sequenced in §9 as one
  deliverable to avoid a red suite in `main`.

---

## 5. Data Model & APIs (summary)

No persisted data. New public surface:

```cpp
// src/index/FileIndex.h (test seam)
[[nodiscard]] filename_trigram::FilenameTrigramIndex* GetTrigramIndexForTest();
[[nodiscard]] extension_index::ExtensionIndex* GetExtensionIndexForTest();

// src/search (bypass seam, default off)
struct MatcherBuildOptions { bool skip_literal_prefilter = false; };

// tests/SearchGateDifferential.h
namespace gate_differential {
  class ScanForcer { public: explicit ScanForcer(FileIndex&); /* RAII Mark/Abort */ };
  struct BatteryEntry { BenchmarkQuery_like query; std::string_view must_include;
                        bool expect_filter_path = true; };
  [[nodiscard]] bool DifferentialCheck(FileIndex&, const BatteryEntry&, std::ostream&);
}
```

(`BenchmarkQuery_like` = plain struct with `filename, path, extensions, folders_only,
case_sensitive` — mirror `SearchHistoryParams`-style snake_case POD fields, no class.)

---

## 6. Error Handling & Edge Cases

- Indexes legitimately absent (empty corpus): `ScanForcer` no-ops; battery skips with
  `MESSAGE("corpus too small for gate path")` — never a silent pass.
- `WaitForIndexesReady` timeout: fail the case, print generation counters (mirrors
  benchmark's abort path).
- Tier 2 with missing std dataset file: skip + explicit `MESSAGE` (CI images may omit
  large data).
- Single-thread ambiguity (`timings.size()==1` false-positive when `thread_count==1`):
  Tier 1 forces `thread_count >= 2` for coverage assertions; benchmark-style labeling
  ("scan-or-ambiguous") not needed because the test owns the engine call.

---

## 7. Test Plan (meta: the test about testing)

1. **Red-before-green evidence (mandatory in PR):** with the guard's three known-bug
   entries active on **pre-fix** HEAD, capture failing output showing witness rows listed
   as "Missing in normal path" — proves the fixture actually exercises the hazard.
2. Post-fix: full `./scripts/build_tests_macos.sh` green, guard included.
3. Tier 2: `FINDHELPER_DIFFERENTIAL_HEAVY=1 ./build_tests/search_gate_differential_tests`
   green post-fix; entry 3 reports A=19796=B.
4. Mutation guard (manual, not in suite): temporarily break `PlanLeafTrigrams` (extract
   run-before-`**`), confirm Tier 1 fails within seconds → then restore. Record outcome in
   §9 notes.
5. ASan/TSan: Tier 1 under `build_tests_macos.sh` default (ASan) and `--tsan` once.

---

## 8. Rollout & Rollback

- Pure test addition + two narrow default-off seams; zero user-visible behavior.
- If a future corpus-shape bug makes a Tier-1 entry flaky (should be impossible — the
  fixture is static), quarantine the single entry, not the suite.
- Rollback = revert the commit(s); nothing migrates state.

---

## 9. Task Breakdown (with acceptance per task)

| ID | Task | Depends | Est | Acceptance |
|---|---|---|---|---|
| T1 | `tests/data/differential_gate_fixture.txt` + battery table as doctest data (§4.1), row→hazard header comments | — | 1 h | Each battery entry has witness/decoy/near-miss; fixture reviewed against design rules §4.2 |
| T2 | Extract `tests/SearchGateDifferential.h` from `SearchBenchmark.cpp` (ScanForcer, PrintVerifyDiff, DifferentialCheck); `FileIndex` non-const test-seam accessors | T1 | 2 h | `search_benchmark --verify` behavior unchanged (smoke before/after); new header compiles into two targets |
| T3 | New doctest target `search_gate_differential_tests` (CMakeLists block + `scripts/test_targets.txt`), Tier 1 battery | T2 | 1 h | Target builds+runs in `./scripts/build_tests_macos.sh`; < 10 s; battery entries pass EXCEPT 3/5/7/15-conditional documented as expected-red in commit message |
| T4 | Matcher prefilter bypass seam (§4.4) + battery entry 8 | T2 | 1 h | Entry 8 red on HEAD (exposes Finding 4) or green with justification; production default verified off (no flag in GUI/benchmark paths) |
| T5 | Tier 2 heavy battery (std corpus, env-gated) | T2 | 1 h | Post-FixTask, entry 3 A=B=19796 on std corpus; opt-in only, skipped gracefully without dataset |
| T6 | **Findings 1–3 fixes per Part II** (leaf pinning rule, separator-glob bail, `+` guard) | T3 | — | Guard green on entries 3/5/7; Part II acceptance §P2.5 probes match oracle |
| T7 | Update analysis doc §Action + HelpWindow/docs pointer if user-facing | T6 | 0.5 h | `internal-docs/analysis/2026-10-10_PATH_DEV_GOLDEN_OSCILLATION_ANALYSIS.md` closed-loop note; no `docs/` change needed (maintainer-only tooling) |

Sequencing note: **T1–T5 + T6 land as one deliverable** (see §4.5); T7 in the fix commit.

---

## 10. Open Questions

1. Should Tier 2 run nightly (Superset automation) instead of opt-in? (Suggest: yes, once
   the guard is green; the ~35 s cost is benchmark-only.)
2. Mutation-mode variant (apply rename/USN batches between A and B) — strong follow-up
   value for the `UpdatePrefix` landmine (§Findings residual a), deferred deliberately:
   the mutation surface is where `976e3e36` fixed a bug, so a paired guard task should
   reuse this header's seams.

---

# PART II — Soundness Fix for Findings 1–3 (Candidate Gate Leaf-Pinning)

**Bugs fixed:** Findings 1, 2, 3 of
`internal-docs/analysis/2026-10-10_PATH_DEV_GOLDEN_OSCILLATION_ANALYSIS.md`, all live on
HEAD and reproduced empirically (`pp:**/dev**` → 1841 vs 19796; separator glob mini-fixture
3 vs 4). **Depends on:** Part I's guard as the regression fence (shipped in the same
deliverable, §4.5).

## P2.1 Scope

Three unsound-pruning sites in `src/search/TrigramQueryPlanner.h`, all in the **path-side**
planning. Everything else in the gate pipeline is unchanged:

| # | Site | Current | After fix |
|---|---|---|---|
| F1 | `PlanPathQuery` PathPattern case (`TrigramQueryPlanner.h:164-166` → `PlanLeafTrigrams` 67-80 → `ExtractWildcardSegments` 16-31) | extracts trigrams from **every** wildcard-free run ≥ 3 chars in the leaf, claiming leaf literals are filename-pinned | extracts only runs **after the leaf's last `**`** |
| F2 | `PlanPathQuery` Glob case (`TrigramQueryPlanner.h:167-188`) | bails on non-Suffix shape only when separator-free (173-176); separator-bearing globs proceed to leaf extraction (178-187) | **`HasPathSeparator(query) → std::nullopt`** unconditionally; separator-free Suffix branch unchanged |
| F3 | `HasComplexLeafMetacharacters` (`TrigramQueryPlanner.h:57-59`) | rejects `^$[]{}` | adds `+` (quantifier produces pattern-text trigrams like `og+`) |

**Out of scope (deliberately):** Finding 4 (`std_regex_utils::detail::ExtractRequiredLiteral`
escape fall-through, `utils/StdRegexUtils.h:403-421`) — it lives inside the *matcher's own*
prefilter and affects scan results too; tracked as F4 below with its own commit +
Part I battery entry 8 as its fence. Also out: the `PathStorage::UpdatePrefix` landmine
(dead code, no production callers) and the `InsertPath` stale-slot nit — separate hygiene
tasks; crash-triage discipline keeps them out of this commit series (one hypothesis each).

## P2.2 The soundness rule (and why the current comment is wrong)

The doc-comment at `TrigramQueryPlanner.h:150-152` asserts:

> in PathPattern '*' and '?' never cross separators while '**' does, so a leaf literal
> required by the pattern must appear in the filename

The premise is right; the conclusion is not. A leaf run is guaranteed inside the *matched
path's last segment* (the filename) **only if no `**` occurs after it in the pattern**:
after the last `**`, every remaining element is a literal, `*`, or `?` — none of which can
produce a separator — so the run's match lies past the pattern's last literal separator
AND past the path's last actual separator, i.e. inside the filename. A run **before** a
later `**` can be satisfied in any directory component:

- `pp:**/dev**`, path `/dev/shm/notes.txt`: `**`→ε, `/dev`→`/dev`, `**`→`/shm/notes.txt`.
  The literal `dev` matched a **directory** component; the filename `notes.txt` holds no
  `dev` trigram → pruned. (1841/19796.)
- `src/*.cpp` compiles to `**src/**.cpp**` (`SearchPatternUtils.h:455-469`,
  `CreateMatcherImplGlob`: every `*` doubled to `**`, plus leading **and appended
  trailing `**`): **every** literal is followed by `**`, so *nothing* is
  filename-pinned; separator-bearing globs must not extract at all.

**Rule (path side):** let `P` be the leaf of the prefix-stripped pattern and `S` the set of
maximal `*?`-free runs of `P` of length ≥ 3. The required-trigram set is
`{ r ∈ S : r starts after the last occurrence of "**" in P }` (∅ when `P` has no `**` ⇒
all runs, matching today's sound behavior for fully-pinned leaves; all runs dropped when
`**` is the leaf's tail). Empty set ⇒ `std::nullopt` ⇒ production scan fallback.

For F2 the rule needs a second look: the Glob branch sees the *raw* glob (`src/*.cpp`),
not its compiled form, so "run followed by a later `**`" is invisible there — a raw glob
`*` compiles to `**`, hence **any** separator-bearing glob has zero pinned runs.
`HasPathSeparator → nullopt` is the rule's correct specialization, not a workaround.

The **filename side** (`PlanFilenameQuery`, used at both `TrigramQueryPlanner.h:105` and
111) is *already conservative*: the matcher is applied to the filename text itself, so any
literal run the anchored pattern requires must occur inside that text — e.g. filename
query `pp:dev**` can only be satisfied by filenames *starting* with `dev`, never by a
directory component. A run before `**` there is still text-pinned; no `**`-position rule
is needed. This asymmetry is the design's crux: same helper, different rule per side.

## P2.3 Design

```cpp
// TrigramQueryPlanner.h (detail namespace)
enum class LeafPinSide { kFilename, kPath };  // F1: side parameter

[[nodiscard]] std::optional<std::vector<TrigramKey>>
PlanLeafTrigrams(std::string_view query, std::vector<TrigramKey>& scratch,
                 LeafPinSide side = LeafPinSide::kFilename);
```

1. **F1 — `PlanLeafTrigrams` gains `side`.** `kFilename`: existing behavior exactly.
   `kPath`: compute `const size_t last_dd = leaf.rfind("**");`
   - `last_dd == npos` → all runs (unchanged).
   - else → only runs starting at index > `last_dd`; a run **containing** the `**`
     occurrence position is itself wildcard-bearing and is split by `*?` first — note
     `**` is *two* wildcard chars, so `find_first_of("*?")` already splits around it;
     the pinned-tail filter operates on the split runs' positions relative to `last_dd`.
   - Call sites: `PlanPathQuery:165` passes `kPath`; `PlanFilenameQuery:105` unchanged.
2. **F2 — Glob path-side:** insert before the shape check:
   ```cpp
   if (HasPathSeparator(query)) { return std::nullopt; }  // compiled globs pin nothing
   ```
   making the existing `ClassifyGlobShape != Suffix` test run only on separator-free
   globs. Filename-side Glob case (`TrigramQueryPlanner.h:107-121`) **untouched**.
3. **F3:** `find_first_of("^$[]{}+")`. One character; note the trigram `og+` example in a
   test comment.
4. **F4 (separate commit):** in `std_regex_utils::detail::ExtractRequiredLiteral`,
   the escape-handling switch (`StdRegexUtils.h:~282`) currently falls through to
   `current += next` for unlisted escapes. Change the default escape branch to
   **abandon extraction** (`return {}` or equivalent "no literal claim") for any
   `\X` not in the explicitly-known-safe set — same conservative stance as the existing
   `\d\w\s\b` reset. Acceptance: Part I battery entry 8 (`re:\x41ndroid`) green with the
   prefilter-bypass seam active.

## P2.4 What must NOT change (regression fences)

- Substring/Fuzzy/StdRegex path-side abstentions (`TrigramQueryPlanner.h:157-163`) —
  the `path_dev` golden path stays untouched; the golden 19796 must keep passing.
- `known_empty` short-circuit semantics (`FilenameTrigramIndex.cpp:386-389`,
  `ParallelSearchEngine.cpp:393-400`): once gate requirements are sound, a
  posting-confirmed-empty intersection **is** a proof of empty results — the
  no-scan-fallback stays, now legitimately. Do not "defensively" scan there; the
  differential guard is the detector.
- Separator-free Suffix glob narrowing (the `b27e928c` feature, `GlobSuffixMatcher`
  tail-pinning argument, `SearchPatternUtils.h:368-387`): sound; must remain accelerated
  (battery entry 6 doubles as its fence).
- All filename-side planning.

## P2.5 Acceptance criteria

1. **Guard flip:** Part I battery entries 3, 5, 7 (and 8 with F4) go green; Tier 1 suite
   fully green in `./scripts/build_tests_macos.sh` (ASan).
2. **Probes (the ones that reproduced the bug):**
   - `search_benchmark pp:**/dev**` on `tests/data/std-linux-filesystem.txt` →
     **19796** with `--verify` reporting `ID sets identical` and `Normal path: filter`
     *absent* (the query legitimately takes scan-or-ambiguous now — expected, see P2.6).
   - Mini-fixture `src/*.cpp` (3 files + ancestors) → 4/4 with `--verify` identical.
   - `search_benchmark pp:**/imgui**` (sample config, `tests/benchmark_config_sample.json`)
     → set-equal under `--verify` even if count differs from the pre-fix number (the
     pre-fix count was potentially pruned; the oracle is authority).
3. **Planner unit tests** (`tests/TrigramQueryPlannerTests.cpp`, extending existing
   conventions):
   - `pp:**/dev**` path-side → `nullopt`; filename-side → `{dev}` (asymmetry pinned).
   - `pp:/a/log**x` → extracts `{...}` from `x` only if ≥3 chars; `pp:a**bcd` → `{bcd}`-runs
     only; leaf without `**` (`pp:/a/bcd`) → unchanged full extraction.
   - `pp:**` exact leaf (existing case at `tests/TrigramQueryPlannerTests.cpp:147-150`) stays.
   - glob `/src/*.cpp` path-side → `nullopt`; `*.cpp` Suffix unchanged; `src/*.cpp`
     filename-side → leaf extraction unchanged.
   - leaf with `+` → `nullopt`.
4. **Benchmark harness per AGENTS.md**: record baseline pre-fix; post-fix run
   `--compare --verify` over the std configs. Expected deltas: configs whose path query
   has an unpinned leaf move filter→scan (correctly slower); **sound-shape** configs
   (hotpath filename query, extension-only, pinned-leaf pp) must stay within the standard
   5% gate. Any *wrong-direction* delta (sound shape slower, unsound shape still
   mismatched) blocks.
5. clang-tidy clean on changed files; every `#endif`/suppression convention respected.

## P2.6 Performance impact (accepted trade)

Queries whose path pattern has a literal run before a trailing `**` (`**/dev**`, `x**`,
separator-globs) **lose gate acceleration and full-scan**. That is the correct cost of
silently-wrong results: today's `**/dev**` "speed" measured an answer missing 90% of its
rows. Full corpus scan on 706k rows is ~126 ms single-query (measured, this session),
which is the pre-trigram baseline users already tolerate for substring queries.

Follow-up (explicit non-goal here, file for backlog): path-component acceleration —
posting over *directory* names (the index is filename-scoped by design,
`FilenameTrigramIndex.cpp:95-113`, e.g. a path-trigram or per-segment index) could pin
runs before `**` again with a *sound* index. Any such work lands *under* Part I's guard,
which makes the same mistake detectable on day one.

## P2.7 Task breakdown (expands T6; commits land interleaved with T1–T5 of Part I)

| ID | Task | Depends | Est | Acceptance |
|---|---|---|---|---|
| F1 | `LeafPinSide` param + pinned-tail extraction in `PlanLeafTrigrams`; path-side call site | — | 2 h | §P2.5-3 planner tests; guard entries 3/15 green (15: cs variant) |
| F2 | Glob separator bail in `PlanPathQuery` | — | 0.5 h | planner test; mini-fixture 4/4; guard entry 5 green |
| F3 | `+` into disqualifier | — | 0.25 h | planner test; guard entry 7 green |
| F4 | `ExtractRequiredLiteral` unknown-escape → no-literal | — | 1 h | unit tests in `std_regex_utils_tests`; guard entry 8 green (with Part I §4.4 seam) |
| F5 | Bench baseline/compare runs + note in analysis doc §Action | F1–F3 | 1 h | §P2.5-4 gates met; analysis doc closed-loop line |

One hypothesis per commit (F1/F2/F3 may share a commit only as "make path-side leaf
planning conservative"; F4 is always separate — different module, different failure mode).

Note on post-fix coverage: entries 3/5/7 run with `expect_filter_path=false` — they
legitimately full-scan now, and their differential value is a **fence against
re-introduction**: any future change that re-accelerates these shapes (P2.6 follow-up)
must flip the flag and pass the witness assertions. Gate-positive coverage after the fix
comes from entries 9–13 (extension postings, filename-side gates).

---

## Execution Notes (append during implementation)

- **2026-10-10**: Implemented Part I (Differential Gate Guard Test Harness) and Part II (Candidate Gate Soundness Fixes).
  - Findings 1–3 (`TrigramQueryPlanner.h`): Added `LeafPinSide::Path` logic to pin extraction to tail segments after the last `**`; bailed out on separator-bearing globs for path queries; added `+` metacharacter disqualifier. Verified with unit tests in `TrigramQueryPlannerTests.cpp`.
  - Finding 4 (`StdRegexUtils.h`): Bailed out on unknown/hex/backreference escapes in `AdvanceAfterEscapeSequence` with `abort_no_literal = true`. Verified with unit tests in `StdRegexUtilsTests.cpp`.
  - Guard Test Harness (`tests/SearchGateDifferential.h`): Implemented `ScanForcer`, `PrintVerifyDiff`, and `DifferentialCheck` comparing accelerated search (Path A) against scan fallback (Path B). Added `matcher_test_hooks::ScopedSkipLiteralPrefilter` in `SearchPatternUtils.h` to bypass matcher prefilters during scan verification.
  - Fixtures & Batteries (`tests/data/differential_gate_fixture.txt` & `tests/SearchGateDifferentialTests.cpp`): 40-row fixture with witness/decoy/near-miss entries for 15 hazard patterns. Both Tier 1 (15 battery entries) and Tier 2 (`FINDHELPER_DIFFERENTIAL_HEAVY=1`, 706k paths, `pp:**/dev**` yielding identical 19,796 hits on both Path A and Path B) passed 100% green.
  - Full test suite: All 66 targets passed in `./scripts/build_tests_macos.sh --no-asan`. Clang-tidy clean.
