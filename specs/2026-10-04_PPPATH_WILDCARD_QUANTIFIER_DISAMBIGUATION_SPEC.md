# PathPattern `*` / `?` Disambiguation Spec (2026-10-04)

Status: IMPLEMENTED (2026-10-04) — option B adopted as a single flip, with
`ppstrict:` and saved-history migration both rejected by the maintainer. See
§4.3 for what was actually built and §6 for the test plan as executed.
Follow-on work agreed after review — pattern length limits and user-visible
compile diagnostics — is in §4.5 and §4.6.

## 1. Problem (reminder)

`*find*pg[cd]` finds nothing while `*find*pgc` finds the expected items, on
identical file sets. Root cause, verified by atom dump on 2026-10-04: the pp
grammar gives `*` (and `?`) **two meanings**. `ParseQuantifier`
(`src/path/PathPatternMatcher.cpp`) consumes a `*`/`?` following a literal,
class, `\d`, or `\w` as a *quantifier*, so `*find*pg[cd]` compiles to
`STAR, f, i, n, d{0,∞}, p, g, CLASS[c,d]` — "find, zero-or-more d's, pg,
[c/d]" — not "find, anything, pg, [c/d]". Same for `?` (`a?` = optional `a`,
not `a` + wildcard). Behavior dates to the Dec 2025 prototype (`24464064`);
no recent commit changed it. The header (`PathPatternMatcher.h:133-172`)
documents both meanings without stating precedence, so users writing
glob-shaped patterns get silently wrong (empty) results whenever a `*`/`?`
lands after a quantifiable atom.

## 2. Goals / non-goals

Goals: one spelling, one meaning; glob-shaped pp queries behave like globs;
no silent empty results; a migration path for saved searches/history relying
on current behavior.
Non-goals: changing `[]`, `\d`/`\w`, `+`, `{m,n}`, anchors, `**` semantics,
the NFA path, or anything outside `CompilePattern`/`ParseQuantifier`.

## 3. Options considered

**A. Status quo + docs.** Document precedence ("`?`/` *` bind as quantifiers
after quantifiable atoms; use `**` for wildcards there"). Zero breakage, zero
fix: the trap keeps firing (`*find*pg[cd]` stays empty).

**B. Wildcards always win (recommended, phased).** In pp, `*` and `?` are
*always* wildcards; quantifiers are spelled `+`, `{m}`, `{m,}`, `{m,n}`
(`{0,1}` and `{0,}` replace `?` and `*`-as-quantifier). `a*b` then means `a`,
anything, `b` — the glob reading — in every position.

**C. Quantifiers always win.** Formalizes today, but breaks the far more
common glob-shaped usage (`file*` would stop meaning "file…" as users expect
— actually `file*` today: `e` then `*`-quantifier = "fil, zero-or-more e",
which already surprises). Rejected: optimizes for the rarer intent.

**D. Positional compromise (e.g. quantifier only inside explicit groups).**
No groups exist in the language; inventing them is a bigger change than B
for less clarity. Rejected.

## 4. Recommended design (option B, phased)

### 4.1 Grammar delta (pp only; `rs:` untouched)

In `CompilePattern`, stop calling `ParseQuantifier` for `*` and `?`
(keep it for `+` and `{...}`). A `*`/`?` following any atom compiles to the
same wildcard atoms the pattern-start position produces today (`kStar` /
`kQuestion` semantics: `*` = zero+ non-separators, `?` = exactly one
non-separator; `**` unchanged). Everything else parses as today.

### 4.2 Rewrite rules (old → new, for migration tooling and docs)

| Old spelling | Old meaning | New spelling |
|---|---|---|
| `a*` (after atom) | `a{0,}` | `a{0,}` (unchanged meaning) |
| `a?` (after atom) | `a{0,1}` | `a{0,1}` |
| `a*` intended as wildcard | (broken today) | `a**` or `a*` (works after fix) |
| `[ab]*`, `\d*`, `\w*` | quantified | `[ab]{0,}`, `\d{0,}`, `\w{0,}` |

### 4.3 Rollout phases — as implemented

Phases 1-3 (detector metric, `ppstrict:` opt-in, saved-pattern rewrite) were
**skipped**: the maintainer takes the flip directly, with no compat mode and no
history migration. Old saved patterns keep their text and are re-read with the
new meaning, which is the wanted direction. What shipped instead:

1. **`ParseQuantifier` → `ParseRepetitionSuffix`.** The `?` and `*` branches are
   deleted; only `+` and `{...}` remain. In `CompilePattern` an unconsumed `*`
   or `?` simply becomes the next iteration's wildcard atom.
   Note the grammar fork was already narrow: `CompilePathPattern` routes
   patterns **without** `[ ] { } \d \w` to the simple tokenizers, where `*`/`?`
   have always been wildcards. Only "advanced" patterns were ambiguous, so the
   change is confined to `CompileAdvancedPattern`.
2. **Prefilter guard.** `IsRunLastCharOptional` no longer treats `?`/`*` as
   zero-able, so a literal run followed by a wildcard becomes an exact
   requirement again (`extra_required_substrings`).
3. **Atom-limit correctness (added during implementation).** `CompilePattern`
   silently truncated patterns over `kMaxPatternTokens` atoms while still
   reporting `valid`, so an over-long advanced pattern matched only its first
   atoms. Option B makes this easier to reach (each `*`/`?` now costs an atom),
   so the loop now reports invalid when input remains, matching
   `CompileSimplePatternAsBacktracking`.

### 4.4 Known remaining gap (deferred)

`ExtractRequiredSubstring` picks the *primary* prefilter substring without the
`IsRunLastCharOptional` guard (only the secondary runs in
`CollectExtraRequiredSubstrings` are guarded). A pattern whose longest literal
run is immediately followed by a zero-able `{0...}` can therefore require a
substring that valid matches lack (e.g. `[a]abc{0,2}` matches `aab` but is
prefiltered on `abc`). Pre-existing, unrelated to option B, and rarer after it
— deferred, not addressed here.

### 4.5 Pattern length limits (follow-on, implemented)

Reviewing the §4.3 item 3 fix surfaced a worse problem underneath it: the
64-token cap was not an atom budget but a **hard architectural** limit of the
simple compile path, and a pattern exceeding it was rejected outright. It was
far easier to hit than the atom limit — any `pp:` glob with a wildcard over 63
characters (`**/Users/name/Documents/projects/app/build/**/*.cpp`) matched
nothing at all, with no diagnostic. Measurements taken before deciding: literal
patterns bypass the cap at any length (they never build a token list); `*` plus
62 literals (63 tokens) compiled; `*` plus 70 literals did not.

1. **The two limits are now separate constants, because they are unrelated
   things.** `kMaxSimpleTokens` (64) stays in the header with its real
   constraint documented — the NFA tracks positions in one 64-bit `StateMask`,
   so token *i* needs bit *i* and the accept state needs bit *n*, and
   `Bit(64+)` would be undefined behaviour. `kMaxPatternAtoms` (128) is the new
   public budget for the backtracking and advanced paths. Making them one shared
   constant, as §4.3 originally proposed, was the mistake: it hid that one is
   structural and the other is a budget.
2. **Overflow falls back instead of failing.** `CompileSimplePattern` now returns
   `std::optional`; when `BuildSimpleTokens` overflows, `CompilePathPattern`
   retries the pattern on `CompileSimplePatternAsBacktracking`, which reads the
   same token meanings and has no mask constraint. A dedicated `kTooManyTokens`
   enum value was rejected: with the fallback there is no such user-facing
   failure, so the value would have existed only to be immediately overwritten.
   `optional` says "no compiled pattern produced", which is what actually
   happens.
3. **`kMaxAtoms` 64 → 128.** `Atom` is 272 bytes (the `CharClass` bitmap
   dominates), so this is ~34 KiB per compiled pattern and a search holds at
   most two. The matcher has no global step budget (only the `count > 1024`
   double-star guard), so this does trade worst-case backtracking latency for
   reach; 128 is the point where realistic path patterns stop being rejected.

Equivalence of the two simple paths is the load-bearing assumption of the
fallback, and it is structural: both consume `BuildSimpleTokens` output with
the same meanings, and the only extra behaviour in the backtracking matcher —
`IsDoubleStarSlashSkip` / `IsTrailingSlashDoubleStarSkip` — is gated on the
`**/` shapes that `NeedsSegmentSemantics` already routes there today. Pinned in
`tests/PathPatternMatcherTests.cpp` by asserting that an over-limit pattern
gives the same verdicts as its short equivalent, including that `*` still stays
inside one segment and `?` still consumes exactly one character.

### 4.6 Compile diagnostics (follow-on, implemented)

A pattern that fails to compile matches nothing, which the search reports as an
ordinary empty result — so "your pattern is broken" and "nothing matches that"
were indistinguishable. `bool valid` could not express the difference.

1. **`CompileStatus` replaces `bool valid`** on both `CompiledPathPattern` and
   the internal `Pattern`. `IsValid()` is the single read API. Values:
   `kOk`, `kTooManyAtoms`, `kUnterminatedClass`, `kMovedFrom`. `kMovedFrom`
   exists so the move operations keep marking a moved-from object unusable
   without mislabelling it as a compile failure; it is a lifetime marker, not a
   diagnosis. `DescribeCompileStatus()` is the one place the reason becomes text.
2. **`UpdatePatternDiagnostics` (`src/gui/PatternDiagnostics.*`) records the
   reason** on `SearchPipelineState` (`pattern_error`, plus the inputs it was
   computed from so it is recomputed only on change, not per frame). It compiles
   each `PathPattern`-typed query, so a query that is not a path pattern is
   never reported. It lives in `gui/` rather than `search/` because it is
   UI-thread derived state, and `search/` must not depend on `gui/`.
3. **`WelcomePanel` renders it in the empty-result view**, naming the field
   ("Path pattern: ...") and the reason, in the warning colour. The path query
   is checked first: when both are broken it is the more specific complaint.

### 4.7 Malformed brace groups are literals (follow-on, found while implementing §4.6)

Designing `CompileStatus` surfaced an inconsistency between the two existing
compile paths, independent of option B and not covered by it.
`CompileSimplePattern` and `CompileSimplePatternAsBacktracking` treat a brace
group that is not a valid quantifier as literal text, which is the documented
intent — but `ParseRepetitionSuffix` consumed the `{` *before* discovering the
group did not parse, and returned without restoring it. So `a{x}b` compiled to
the atoms `a`, `x`, `}` and matched `ax}b`, not `a{x}b`. A pattern the simple
path accepted was silently given a different meaning by the advanced path.

Fixed by restoring `pos` on every failure path in the brace branch, so the `{`
falls through to the next iteration and is compiled as a literal. Restoring is
chosen over reporting a new error status because it makes the paths agree and
keeps the documented "literal when it does not parse" rule; reporting would have
broken patterns that previously matched. Trailing garbage (`{2,x}`) and an
unterminated group (`{2,3`) get the same treatment.

## 5. Correctness contract

- Unambiguous patterns (no `*`/`?` in quantifier position): byte-identical
  verdicts old-vs-new. Satisfied structurally: patterns routed to the simple
  tokenizers never reached `ParseQuantifier`, so only "advanced" patterns can
  change, and only where a `*`/`?` follows a quantifiable atom.
- `?`/`*`-in-quantifier-position patterns: new verdicts follow wildcard
  semantics; the changed shapes are pinned by the new doctests.
- `+`/`{m,n}`/classes/anchors/`**`/case-folding: unchanged.
- Prefilter (`AppendPrefilterRuns`, `IsRunLastCharOptional`) changed as
  described in §4.3 item 2: more runs are exact, so the guard narrows to
  `{0...}`. The trigram planner needed no change — it already assumed `*`/`?`
  are wildcards (`TrigramQueryPlanner.h`), an assumption this flip makes true.
- Fallback equivalence (§4.5): a simple pattern over `kMaxSimpleTokens` gives
  the same verdicts on the backtracking matcher as the NFA gives it below the
  limit. Patterns that fit are unaffected — the fallback is only reachable on
  overflow.
- `IsValid()` is exactly `status == kOk`; no read site consults `status`
  directly, so there is no second source of truth for usability.
- A malformed brace group means the same thing on all three compile paths
  (§4.7): literal text, not a quantifier, not an error.

## 6. Test plan — as executed

1. ~~Differential harness old-vs-new~~ — not applicable: there is no coexisting
   old engine to diff against. Instead the "unchanged" subset is pinned by the
   pre-existing suite (`**/[A-Za-z]{2}_test.cpp`, `**/*\d{3}*.log`,
   `**/\w+.txt`, all prefilter cases), and each changed shape is pinned by a
   new doctest.
2. New doctests in `tests/PathPatternMatcherTests.cpp`:
   - `*find*pg[cd]` matches `findpgc` / `afindbpgc` / `findxxpgd` (§1 report as
     a regression test), and rejects `x/findypgc` and `findpgcx`.
   - `**/[0-9]*.log` and `**/\d*.log` now require one digit (they previously
     matched any `.log`).
   - `[a]xyz?1234` / `[a]xyz*1234` need the full `xyz` run, while
     `{0,1}` / `{0,}` / `+` keep the old readings.
   - Prefilter: `[a]xyz*1234` puts `xyz` back in `extra_required_substrings`.
   - Atom limit: a pattern at `kMaxPatternAtoms` compiles, one atom more is
     `status == kTooManyAtoms` and matches nothing.
3. Follow-on tests, §4.5:
   - An over-limit simple pattern compiles and matches its positive case, and
     still rejects the wrong extension / wrong directory / short path — i.e. the
     same verdicts as the short equivalent.
   - The fallback keeps `*` inside one segment and `?` at one character.
6. Follow-on tests, §4.6:
   - `kUnterminatedClass` for `**/[abc.log`; `kMovedFrom` after a move, and a
     moved-from pattern matches nothing.
   - Every `CompileStatus` value has a non-`"ok"` (or `"ok"`) description.
   - `UpdatePatternDiagnostics`: a usable pattern reports nothing; queries that
     are not `PathPattern`-typed are never reported even with unbalanced
     brackets/braces; the offending field is named; the path query wins when
     both are broken; an over-limit pattern reports "too many atoms"; the
     verdict is reused while the inputs are unchanged, cleared when the input is
     fixed, and case-sensitivity is part of the cache key.
7. Follow-on tests, §4.7: `a{x}b` matches `a{x}b` and not `ax}b` (both with and
   without a `**/` prefix, so the simple and advanced paths are covered), the
   same for `a{2,x}b`, and genuine repetition (`{2}`, `{2,}`, `{2,3}`) unchanged.
8. Full suite green; pre-commit guardrails and clang-tidy clean.
9. `path_pattern_benchmark` unchanged (`Advanced Pattern` 29.3 → 25.8 µs/op,
   within noise; the enlarged `Atom` array is allocated once per compile and is
   not on the per-match path).
10. ~~Telemetry review gate~~ — dropped along with phases 1-3.

## 7. Open questions — resolved

- Telemetry threshold: moot, no detector was built.
- `ppstrict:` naming: rejected; `pp:` is the only path-pattern dialect.
- Saved-search/history storage: not rewritten and not versioned. History keeps
  raw pattern text in `AppSettings`, so old entries simply take the new
  meaning. Accepted because the new meaning is the one those patterns were
  written to mean.
- Could the 64-atom / 64-token limit be raised instead of worked around?
  Yes for the atom budget (it is a storage budget — §4.5 item 3), no for the
  token limit (it is the 64-bit `StateMask` — §4.5 item 1). Splitting them was
  the answer, plus the fallback so raising the budget is no longer the only
  remedy.
- Can the user be told when the limit is reached? Yes, and it is worth more than
  the raise: the limit was reached constantly by ordinary patterns and said
  nothing. Implemented in §4.6 rather than as an atom-count warning, because
  "this pattern cannot be used" is the useful message and the atom count is an
  implementation detail.

## 8. Prior art

- 2026-10-04 investigation: `*find*pgc` (glob → `**find**pgc**`) vs
  `*find*pg[cd]` (pp → `STAR,f,i,n,d*,p,g,CLASS`) atom dumps; prototype
  commit `24464064` (2025-12-22).
- Deferred segment-split matcher: `specs/2026-10-03_PATHPATTERN_SEGMENT_SPLIT_MATCHER_SPEC.md`
  (orthogonal; implement in either order, re-benchmark after both).
