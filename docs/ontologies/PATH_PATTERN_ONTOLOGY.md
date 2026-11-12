# PathPattern (`pp:`) Ontology

Single source of truth for the **path-pattern / `pp:` matcher** domain vocabulary.
Companion: `src/path/PathPatternMatcher.h`, `docs/design/PARALLEL_SEARCH_ENGINE_DESIGN.md`.

## Entities

| Term | Meaning | Code pointer | Not this |
|---|---|---|---|
| PathPattern | Whole-path glob dialect (auto-activated by `**`, `^`, `$`, `[...]`, `{n}`, or `pp:` prefix). Implicitly anchored `^pattern$`. | `path_pattern::PathPatternMatches` / `CompilePathPattern` in `src/path/PathPatternMatcher.h` | `Glob`, `RegexPattern`, `RxPattern` |
| CompiledPathPattern | Pre-compiled, reusable pattern (status, tokens, epsilon mask, cached advanced pattern). | `path_pattern::CompiledPathPattern` | `Regex`, `Matcher` |
| CompileStatus | Why a pattern is (not) usable: `kOk`, `kTooManyAtoms`, `kUnterminatedClass`, `kMovedFrom`. | `path_pattern::CompileStatus` | `RxError`, `ParseError` |

## Value objects

| Term | Meaning | Code pointer | Not this |
|---|---|---|---|
| MatchOptions | `kNone` or `kCaseInsensitive`. | `path_pattern::MatchOptions` | `CaseSensitive` flag |
| SimpleToken | 2-byte NFA token (kind + literal). | `CompiledPathPattern::simple_tokens_storage` | `Atom`, `Char` |
| Atom | 272-byte advanced matcher atom (char-class bitmap dominates). | `Pattern` (opaque) | `Token`, `Node` |
| RequiredSubstring | Literal run (>= 3 chars) used for SIMD prefilter. | `kMinRequiredSubstringLength`, `AppendPrefilterRuns` | `Prefilter`, `MandatoryLiteral` |
| LiteralRun | Safe literal run (alnum/`.`/`_`/`-`) used for highlighting and prefilter. | `AppendLiteralRuns` | `Substring` |

## Wildcards and atoms

| Symbol | Meaning |
|---|---|
| `*` | Any characters within a **single** path segment (never crosses `/` or `\`). |
| `?` | Any single character within a segment. |
| `**` | Any characters **including** separators (cross-directory). |
| `**/` | Zero or more whole segments (boundary kept: `report*` matches `report_q1`, not `my_report`). |
| `+`, `{m}`, `{m,}`, `{m,n}` | Repetition of a single atom (literal or class). |
| `^` / `$` | Explicit path start / end anchors. |

## Invariants

- PathPattern matches the **entire path** by default (`^pattern$`); use anchors explicitly only when needed.
- `*` and `?` are **always wildcards** in every position; repetition is spelled `+` or `{...}` only.
- `\` is **always a path separator**, never an escape; there is no `\d`/`\w` shorthand.
- `**/` advances by **whole segment index**, never by character.
- Trailing `/**` matches the directory itself (`IsTrailingSlashDoubleStarSkip`).
- Empty segments (leading/trailing/double separators) are preserved; collapsing them changes verdicts.
- `kMaxSimpleTokens` = 64 (NFA 64-bit state mask); above this, the backtracking matcher is used.
- `kMaxPatternAtoms` = 128 (storage budget); above this, `CompileStatus::kTooManyAtoms`.
- A `{...}` that does not parse as one of the four repetition forms is a literal `{`.
- Patterns consisting only of `*` (optionally anchored) are all-pass (`IsAllPassPathPattern`).
- No capturing groups, alternation, backreferences, or lookaround (by design).

## Forbidden (do not introduce)

- Synonyms: `Glob`, `RegexPattern`, `Token`, `Node`, `Prefilter`, `Substring` for the terms above.
- Treating `*` as repetition or `?` as optional.
- Escaping `\` as an escape character.
- Collapsing empty path segments.
- A new matcher variant (PathPattern reuses the pp engine).