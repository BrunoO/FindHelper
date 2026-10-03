# PathPattern Segment-Split Matcher — Deferred Implementation Spec (2026-10-03)

Status: SPEC ONLY. Not urgent; implement when `pp:` backtracking cost matters
again. All measurements in `internal-docs/benchmarks/2026-10-03_TRIGRAM_GATE_PP_PREFILTER.md`.

## 1. Why we are looking into this (reminder)

A user report — `pp:**/USN_Windows**/*.md` taking 179 ms in the path field vs
37 ms for the equivalent `rs:.*[/\\]USN_Windows[/\\].*\.md` — reproduced at
~5x on synthetic corpora (104 ms vs 20 ms over 94,739 rows, identical 24,372
hits, Release + `boost::regex`). Root causes, measured:

- Per path, the `pp:` backtracking matcher costs ~1.1 µs vs ~0.2 µs for
  `boost::regex` on `**/` shapes.
- Profile per backtracking invocation (`pp:**/USN_Windows**/*.md`):
  short paths (~55 chars): 79 `MatchFrom` calls, 56 unbounded retry probes,
  162 atom consumes; deep paths (avg 143 chars): 109 calls, 82 probes,
  308 consumes. Each `**` greedily eats the path remainder, then retries the
  continuation at every length; nested `**`s multiply the visits.
- The already-shipped mitigations (adaptive trigram gate,
  `ShouldUseTrigramFilter`; multi-literal AND-prefilter + suffix constraint)
  avoid *calling* the matcher. This spec kills the per-call cost itself.

## 2. Goals / non-goals

Goals: `pp:` `**/`-shape matching at parity with `boost::regex` or better;
preserve exact current verdicts on every input; additive rollout with
one-line rollback.
Non-goals: changing the pattern language, the NFA path (`MatchSimplePath-
Pattern`), the advanced class/quantifier engine, trigram planning,
prefiltering, highlighting, or ranking.

## 3. Design

### 3.1 Compile: chunk the atom sequence

Split atoms at `**/` units (existing `IsDoubleStarSlashSkip` positions) and
lone `**` into chunks referencing atom ranges. Computable per match in one
pass over ≤ 64 atoms, so no `Pattern`/`CompiledPathPattern` struct changes
are required (verify against current struct sizes if this changes).

### 3.2 Match: segments, not characters

1. Split the path into segments with one O(n) separator scan. Preserve empty
   segments (leading `/`, trailing `/`, `//`): `MatchFrom` treats every
   separator literally, so collapsing empties changes verdicts.
2. First chunk anchors at path start, last chunk at path end, middle chunks
   substring-searched over the segment list.
3. Within a segment, `*` / `?` / literals use the classic linear two-pointer
   glob. `?` must not cross separators; `/` ≅ `\` interchange and
   case folding must reuse the existing helpers (`IsSeparator`, `CharsEqual`,
   `ToLowerChar` on `unsigned char`, including high-bit bytes) — not
   reimplementations.
4. `**/` advances by whole segment index, never by character. This enforces
   the `NeedsSegmentSemantics` rule structurally (`report*` matches
   `report_q1`, not `my_report`). Replicate `IsTrailingSlashDoubleStarSkip`
   (`foo/**` matches `foo` itself).
5. Lone mid-pattern `**` (e.g. `USN_Windows**`): find occurrences of the
   following chunk at/after the position and verify the rest per occurrence.
   Bounded by occurrence count, not path length — still backtracking in the
   worst case, but occurrences ≪ positions.

Expected complexity per match: O(segments × chunks) linear scans, vs ~80
recursive calls × up to ~300 atom steps today.

### 3.3 Eligibility (phase 1)

Reroute only simple-shaped atoms (literal / `*` / `?` / separator / `**`),
i.e. current `CompileSimplePatternAsBacktracking` output. Anything with
classes, `\d`/`\w`, or bounded quantifiers keeps `MatchFrom`. Dispatch lives
in `MatchAdvancedPathPattern`; `MatchFrom` is retained as fallback, so
rollback is a dispatch change.

## 4. Correctness contract (must all hold)

- Byte-identical verdicts with `MatchFrom` on the differential corpus (§5).
- Segment-boundary cases: `report*` vs `my_report`; `**/` zero-dir skip;
  trailing `/**` matching the directory itself; `**` alone matching all;
  adjacent `**/**`; `**` glued to literals on either side.
- Empty segments, both slash flavors, case-insensitive folding, anchors
  (explicit `^`/`$` and implicit full-match), single-char segments.
- Prefilter/compile/highlight paths untouched; no `CompiledPathPattern`
  layout change required (confirm before coding).

## 5. Test plan

1. **Differential fuzz harness (the de-risker, ~half the work):** generate
   thousands of pattern/path pairs (segment-heavy, deep paths, empty
   segments, both cases, boundary shapes) asserting new-vs-old verdict
   equality. Implement the segment matcher alongside `MatchFrom`; run the
   harness in CI; switch dispatch only when green.
2. **Targeted doctests:** §4 cases as explicit `TEST_CASE`s in
   `tests/PathPatternMatcherTests.cpp` (extend the `**/` boundary coverage).
3. **Regression:** full macOS suite via `scripts/build_tests_macos.sh`;
   `path_pattern_integration_tests` must pass unmodified.
4. **Benchmark:** repeat the §1 matrix (`manyMD`, deep, std-linux corpora in
   `$TMPDIR/ppbench_*` generation scripts are inline in shell history, not
   checked in — regenerate); accept on `pp:` ≤ `rs:` time with identical hits.

## 6. Risks

Medium correctness risk (silent wrong-results class), bought down to low by
the differential harness + additive rollout. Performance risk is low (worst
case ties current). No API, format, or language changes.

## 7. Prior art in-tree

- `MatchFromUnbounded` greedy-then-retry loop (`src/path/PathPatternMatcher.cpp`).
- `IsDoubleStarSlashSkip` / `IsTrailingSlashDoubleStarSkip` (segment rules).
- `ShouldUseTrigramFilter` cost model (`src/search/ParallelSearchEngine.h`).
- AND-prefilter + suffix constraint + quantifier guard (`CollectExtraRequired-
  Substrings`, `IsRunLastCharOptional`); class-interior exclusion
  (`AppendPrefilterRuns`).
