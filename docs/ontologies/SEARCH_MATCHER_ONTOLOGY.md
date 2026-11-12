# Search Matcher Ontology (`rx:` and related prefixes)

Single source of truth for the **search-pattern / matcher** domain vocabulary used
in FindHelper. Use these terms in conversations, docs, specs, and new code.
Do not introduce synonyms. Companion: `specs/2026-10-29_RX_MATCHER_SPEC.md`,
`src/search/rx/RxCompiler.h`, `src/search/SearchPatternUtils.h`.

## Entities

| Term | Meaning | Code pointer | Not this |
|---|---|---|---|
| RxPattern (StructuredRegex) | A readable `rx:` S-expression front-end that compiles once into an ECMAScript regex. It is **not** a new matching engine: execution reuses `re:` machinery. | `rx::CompileRx` in `src/search/rx/RxCompiler.h`; `PatternType::Rx` in `src/search/SearchPatternUtils.h` | `RegexPattern`, `LispRegex`, `SexpMatcher` |
| PathClause | Top-level `(path ...)` container that composes directory/name/extension/exclusion clauses into one anchored regex. | `Op::kPath` in `src/search/rx/RxCompiler.cpp` | `PathPattern`, `PathQuery` |
| DirClause | `(dir D1 D2 ...)` — requires one of the named directory segments anywhere in the ancestor chain. | `Op::kDir` | `FolderClause`, `DirectoryClause` |
| ExcludeDir (NotDir) | `(exclude-dir D1 D2 ...)` / `(not-dir ...)` — negative lookahead forbidding named directory segments anywhere in the path. | `Op::kExcludeDir` | `NotDirClause`, `PruneDir` |
| NameClause | `(name P)` — constrains the filename (leaf) stem, optionally with an extension provided by `ExtClause`. | `Op::kName` | `FileNameClause`, `LeafClause` |
| ExtClause | `(ext E1 E2 ...)` — matches trailing file extensions; leading dots are optional in source. | `Op::kExt` | `ExtensionClause`, `SuffixClause` |
| RxError | Structured failure reason for `CompileRx`; never throws for malformed input. | `rx::RxError` in `src/search/rx/RxCompiler.h` | `ParseError`, `CompileStatus` (path-pattern) |

## Value objects

| Term | Meaning | Code pointer | Not this |
|---|---|---|---|
| RxSource | The text after the `rx:` prefix; subject to `kMaxRxSourceLength` (1000 bytes). | `rx::CompileRx(std::string_view source)` | `Pattern`, `Query` |
| EmittedRegex | The lowered ECMAScript regex; subject to `kMaxRxEmittedLength` (1000 bytes). Empty on error or for an empty source (all-pass). | `RxCompileResult::regex` | `CompiledPattern`, `RegexString` |
| RequiredLiteral | Longest literal every match must contain (>= 3 chars), harvested from the AST. Case preserved; callers lowercase for case-insensitive matching. | `RxCompileResult::required_literal` | `Prefilter`, `MandatoryLiteral` |
| RxCompileResult | Outcome of `CompileRx`: error kind, message, offset, emitted regex, required literal, open-paren count. | `rx::RxCompileResult` | `CompileResult`, `ParseResult` |
| NamedClass | ASCII character-class keyword (`digit`, `alpha`, `alnum`, `space`, `blank`, `word`, `xdigit`, plus `any-char`/`any`/`anything`). | `kNamedClasses`, `IsAnyCharAtom` in `src/search/rx/RxCompiler.cpp` | `CharClass`, `PosixClass` |
| Anchor | Zero-width anchor keyword (`bos`, `eos`, `bow`, `eow`, `word-boundary`). | `kAnchors` | `Boundary`, `AnchorToken` |
| Op | Internal operator kind (`kSeq`, `kOr`, `kStar`, `kPlus`, `kOpt`, `kRepeat`, `kAny`, `kNot`, `kPath`, `kDir`, `kExcludeDir`, `kName`, `kExt`). | `Op` enum in `src/search/rx/RxCompiler.cpp` | `Operator`, `RxOp` |

## Operations (lowering)

| rx form | ECMAScript equivalent | Notes |
|---|---|---|
| `(seq A B ...)` / `(: ...)` / `(and ...)` | `(?:AB...)` | Concatenation; `and` is an alias for `seq`. |
| `(or A B ...)` / `(\| ...)` | `(?:A\|B\|...)` | Alternation. |
| `(* A)` / `(zero-or-more A)` | `(?:A)*` | Greedy. |
| `(+ A)` / `(one-or-more A)` | `(?:A)+` | Greedy. |
| `(? A)` / `(opt A)` | `(?:A)?` | |
| `(repeat N A)` | `(?:A){N}` | Exact. |
| `(repeat MIN MAX A)` | `(?:A){MIN,MAX}` | `MAX <= kMaxRxRepeatCount` (10000). |
| `(any "a-z" "_")` | `[a-z_]` | Char-set fragment; quoted strings are class fragments, not literals. |
| `(not (any ...))` | `[^...]` | `not` is **char-class-only** in V1; general negation is `kGeneralNegationNotSupported`. |
| `bos` / `eos` | `^` / `$` | |
| `bow` / `eow` / `word-boundary` | `\b` | |
| `digit` / `alpha` / `alnum` / `space` / `blank` / `word` / `xdigit` | `[0-9]` / `[a-zA-Z]` / `[a-zA-Z0-9]` / `[ \t\r\n\v\f]` / `[ \t]` / `[a-zA-Z0-9_]` / `[0-9a-fA-F]` | ASCII only. |
| `any-char` / `any` / `anything` | `.` (or `[^/\\]` inside `(name ...)`) | Must not cross directory separators in a name clause. |
| `(path ...)` | Composed anchored regex | Applies `ExcludeDir` lookaheads, `Dir`, `Name`, `Ext` in one expression. |
| `(dir D1 D2 ...)` | `(?:^|[/\\])(?:D1\|D2)(?:[/\\]\|$)` | Directory segment anywhere in ancestor chain. |
| `(exclude-dir ...)` / `(not-dir ...)` | `^(?!.*(?:^|[/\\])(?:...)(?:[/\\]\|$))` | Negative lookahead anchored at string start. |
| `(name P)` | leaf-stem pattern | Standalone is end-anchored; composed with `ExtClause`, `ExtClause` supplies `$`. |
| `(ext E1 E2 ...)` | `\.(?:E1\|E2)$` | Leading dots optional in source. |

## Invariants

- `rx:` is a **front-end only**: it never adds a new `PatternMatcher` variant; it lowers to `StdRegexMatcher` (+ `SimdSubstringHints`).
- **No allocations in the match loop**: parsing, AST creation, and regex compilation happen at matcher construction time, never per row.
- **Source length**: `RxSource` length <= `kMaxRxSourceLength` (1000); otherwise `RxError::kPatternTooLong`.
- **Emitted length**: `EmittedRegex` length <= `kMaxRxEmittedLength` (1000); otherwise `RxError::kEmittedRegexTooLong`.
- **Nesting depth**: parenthesis nesting <= `kMaxRxNestingDepth` (32); otherwise `RxError::kNestingTooDeep`.
- **Repeat counts**: `MIN <= MAX` and `MAX <= kMaxRxRepeatCount` (10000); otherwise `RxError::kInvalidRange`.
- **No nested quantifiers**: a quantifier body may not itself be a quantifier; otherwise `RxError::kNestedQuantifier` (ReDoS guard).
- **`not` is char-class-only** in V1; `(not X)` where X is not `(any ...)` yields `RxError::kGeneralNegationNotSupported`.
- **Path-clause placement**: `dir`/`name`/`ext`/`exclude-dir`/`path` outside a top-level or `(path ...)` context yields `RxError::kPathOperatorMisplaced`.
- **At most one** `(name ...)` and at most one `(ext ...)` per path; otherwise `RxError::kDuplicatePathClause`.
- **Empty source** compiles successfully to an empty regex (all-pass query), like a bare `pp:`.
- **Escaping**: bare words and quoted strings outside `any` are literal runs and must be regex-escaped; inside `any`, quoted strings are character-class fragments.
- **ASCII only**: `NamedClass` expansions are ASCII ranges; no Unicode property classes.
- **Case sensitivity** is carried by the `re:` compilation flag, not by the emitted character classes.

## Forbidden (do not introduce)

- Synonyms: `RegexPattern`, `LispRegex`, `SexpMatcher`, `FolderClause`, `FileNameClause`, `ExtensionClause`, `ParseError` (use `RxError`).
- A new `PatternMatcher` variant for rx (it must lower to `StdRegexMatcher`).
- General negation / lookahead via `not` in V1.
- Comment syntax (not defined by the grammar).
- Capturing groups or backreferences (non-goal in V1).
- Unicode-aware character classes.
- Editing the USN/MFT ubiquitous-language doc to add search terms (create a search-scoped doc instead).

## Rules for new code

- Use the preferred term (left column) in new identifiers, comments, and docs.
- Do not add a synonym for an existing term; extend this file instead.
- Any new `RxError` kind must be added to `rx::RxError`, `DescribeRxError`, and this ontology.
- Any new operator or named class must be added to `kOperators` / `kNamedClasses` / `kAnchors` **and** this ontology in the same change.