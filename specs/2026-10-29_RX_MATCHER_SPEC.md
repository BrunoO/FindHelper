# Formal Specification: S-Expression Structured Pattern Matcher (`rx:`)

**Document ID:** `specs/2026-10-29_RX_MATCHER_SPEC.md`  
**Status:** PROPOSED / REVIEWED & REVISED  
**Date:** 2026-10-29  
**Target:** FindHelper Search Engine & UI  

---

## 1. Motivation & Context

FindHelper provides four primary query modes: Substring (SIMD Boyer-Moore-Horspool), Glob (`*`, `?`), Fuzzy (`fz:`), and PathPattern (`pp:`). When queries require non-trivial logic (negated directory branches, alternatives, complex character classes, repetition bounds), users must resort to raw ECMAScript regular expressions via `re:`.

However, raw regular expressions have notable limitations in file-search workflows:
1. **Punctuation density and escaping pain**: File paths heavily use slashes, dots, and backslashes. Matching Windows paths in raw regex requires quadruple backslashes or unwieldy character classes (`[/\\]`).
2. **Fragility in search inputs**: Lookaheads for directory exclusion (e.g. `^(?!.*[\\/](node_modules|build)[\\/]).*\.cpp$`) are error-prone and hard to verify at a glance.
3. **AI Search (Gemini) inaccuracy**: LLMs frequently introduce subtle regex escaping errors, unescaped path dots, or accidental greedy `.*` hazards.
4. **`pp:` limits**: `pp:` is fast (64-bit bitmask NFA), but cannot express negated directories, word boundaries, or character-level alternatives.

Inspired by the Emacs Lisp **`rx`** macro, this specification defines **`rx:`**, an S-expression structured regular expression dialect tailored for cross-platform file and path matching. It transpiles into optimized ECMAScript regular expressions (and SIMD substring prefilters) with zero modifications to the core search worker threads.

---

## 2. Goals & Non-Goals

### Goals
* **Readability**: S-expression based syntax with self-documenting keywords (`seq`, `or`, `dir`, `ext`, `not`).
* **Ergonomics**: Bare words by default (no mandatory quotes for identifiers/tokens like `(ext cpp h)`), with optional double quotes for strings containing spaces or special characters (`(dir "Program Files")`).
* **Path-Aware Semantics**: First-class path operators (`path`, `dir`, `exclude-dir`, `ext`, `name`) that transparently handle cross-platform directory separators (`/` and `\`).
* **Zero Hot-Path Overhead**: Query parsing and AST lowering happen once at matcher construction time on the UI/coordinator thread. Worker threads execute standard matchers (`StdRegex` / `Boyer-Moore` prefilter).
* **Immediate-Mode UI Guidance**: Real-time syntax validation, accent coloring, parenthesis balance tracking, and translated preview in Dear ImGui without heavy external dependencies.
* **Deterministic ReDoS Safety**: Strict nesting depth, emitted regex length cap, and rejection of nested quantifiers.

### Non-Goals
* Embedding a general-purpose Lisp runtime or interpreter.
* Backreferences or complex capturing groups in V1.
* General arbitrary lookahead negation (`not` on arbitrary sub-expressions) in V1 (restricted to character classes).
* Replacing `pp:` or `re:` (co-exists alongside existing prefixes in `SearchPrefixes.h`).

---

## 3. Language Grammar (EBNF) & Lexer Rules

The `rx` dialect is whitespace-insensitive outside quoted literals.

```ebnf
Query            ::= 'rx:' SExpr?
SExpr            ::= Atom | List
List             ::= '(' Operator Argument* ')'
Argument         ::= SExpr

Operator         ::= CoreOp | PathOp

CoreOp           ::= 'seq' | ':' | 'and'
                   | 'or'  | '|'
                   | 'zero-or-more' | '*'
                   | 'one-or-more'  | '+'
                   | 'opt'          | '?'
                   | 'repeat'
                   | 'any'
                   | 'not'
                   | 'bos' | 'eos'
                   | 'word-boundary' | 'bow' | 'eow'

PathOp           ::= 'path'
                   | 'dir'
                   | 'exclude-dir' | 'not-dir'
                   | 'name'
                   | 'ext'

Atom             ::= QuotedString | BareWord | NamedClass
QuotedString     ::= '"' [^"]* '"'
BareWord         ::= [a-zA-Z0-9_\-\.\*\?\+\\\/]+
NamedClass       ::= 'digit' | 'alpha' | 'alnum' | 'space' | 'blank' 
                   | 'word'  | 'xdigit' | 'any-char'
```

### 3.1 Lexical & Disambiguation Rules
1. **Positional Token Disambiguation**:
   * A token is an **Operator** if and only if it appears as the first element immediately following an opening parenthesis `(`.
   * In all other positions (arguments/values), tokens like `*`, `+`, `?` or names matching operators are parsed as **BareWord literals**.
   * Identifiers matching `NamedClass` (`digit`, `alpha`, etc.) are recognized as keywords only when appearing in argument position within `any` or character-class contexts; otherwise, they are treated as literal strings.
2. **Whitespace and Delimiters**: Whitespace (spaces, tabs, newlines) and parentheses `(`, `)` act as token delimiters.
3. **Empty Query**: An empty query (`rx:` alone or whitespace only) compiles to `AllPassMatcher` (identical to bare `pp:` or empty search).

---

## 4. Lowering Rules to ECMAScript Regex

`rx:` expressions are unanchored by default (matching anywhere in the target string). Anchoring requires explicit `bos` / `eos`.

### 4.1 Literal Escaping Contract
All literal characters inside `BareWord` and `QuotedString` (except inside `any`) **must be regex-escaped** when lowering to ECMAScript regex. The following metacharacters are escaped with a preceding backslash `\`:
```text
\ ^ $ . | ? * + ( ) [ ] { }
```
* Example: `(seq foo.bar)` lowers to `(?:foo\.bar)`.
* Example: `(dir "Program Files (x86)")` lowers to `(?:^|[/\\])(?:Program\ Files\ \(x86\))(?:[/\\]|$)`.

### 4.2 Core Operators Table

| `rx:` Form | ECMAScript Equivalent | Description & Rules |
|---|---|---|
| `(seq A B ...)`<br>`(: A B ...)`<br>`(and A B ...)` | `(?:AB...)` | Concatenation sequence. `and` is an alias for `seq` and `:`. |
| `(or A B ...)`<br>`(\| A B ...)` | `(?:A\|B\|...)` | Alternatives. |
| `(* A)` / `(zero-or-more A)` | `(?:A)*` | 0 or more repetitions (greedy). Nested quantifiers rejected. |
| `(+ A)` / `(one-or-more A)` | `(?:A)+` | 1 or more repetitions (greedy). Nested quantifiers rejected. |
| `(? A)` / `(opt A)` | `(?:A)?` | 0 or 1 repetition. Nested quantifiers rejected. |
| `(repeat MIN MAX A)` | `(?:A){MIN,MAX}` | Bounded repetition ($0 \le MIN \le MAX \le 10000$). |
| `(repeat N A)` | `(?:A){N}` | Exact repetition ($0 \le N \le 10000$). |
| `bos` / `eos` | `^` / `$` | String start / string end anchors. |
| `bow` / `eow` / `word-boundary` | `\b` | Word boundary. |
| `(any "a-z" "_")` | `[a-z_]` | Character class. Quotes inside `any` supply ranges/chars. |
| `(not (any ...))` | `[^...]` | Negated character class. |
| `(not CLASS)` | `[^...]` | Negated named class (e.g., `(not digit)` $\rightarrow$ `[^0-9]`). |

### 4.3 Named Character Classes (ASCII)

| `NamedClass` | Emitted ECMAScript Regex | Semantic Definition |
|---|---|---|
| `digit` | `[0-9]` | Decimal digits |
| `alpha` | `[a-zA-Z]` | ASCII letters |
| `alnum` | `[a-zA-Z0-9]` | Alphanumeric characters |
| `space` | `[ \t\r\n\v\f]` | All ASCII whitespace |
| `blank` | `[ \t]` | Space and horizontal tab |
| `word` | `[a-zA-Z0-9_]` | Word characters |
| `xdigit` | `[0-9a-fA-F]` | Hexadecimal digits |
| `any-char` | `.` | Any character (matching dotall / standard regex semantics) |

### 4.4 Scope of `not` in V1
In V1, **`not` is strictly restricted to character classes**:
* Valid: `(not (any "0-9"))`, `(not digit)`, `(not (any "/" "\\"))`.
* Emitted form: `[^0-9]`, `[^/\\\\]`.
* Rejection: `(not (seq "foo"))` or general expression negation is rejected with `RxError::GeneralNegationNotSupported` (see §8 for rationale).

---

## 5. Path Composition Semantics

All path operators normalize slashes to `[/\\]` to maintain cross-platform parity across Windows, macOS, and Linux.

### 5.1 Standalone vs. Composed Semantics
Operators can be used either standalone or composed inside a `(path ...)` wrapper:
* **Standalone `(name STEM)`**: Anchored at the end of path: `(?:^|[/\\])(?:<STEM>)$`.
* **Standalone `(ext E1 E2)`**: Anchored at the end of path: `\.(?:<E1>|<E2>)$`.
* **Standalone `(dir D1 D2)`**: Unanchored directory segment: `(?:^|[/\\])(?:<D1>|<D2>)(?:[/\\]|$)`.

### 5.2 Deterministic `(path ...)` Composition Pipeline
Inside `(path ...)`, components are assembled in four deterministic stages:

```
[Exclusion Lookaheads] -> [Directory Ancestor Chain] -> [Filename Stem] -> [Extension]
```

1. **Stage 1: Exclusions (`exclude-dir` / `not-dir`)**
   All `exclude-dir` and `not-dir` clauses collect into a single anchored negative lookahead at the start of the emitted regex:
   ```text
   ^(?!.*(?:^|[/\\])(?:D1|D2|...)(?:[/\\]|$))
   ```
2. **Stage 2: Directory Chain (`dir`)**
   Multiple `(dir D)` elements are joined in order using `.*[/\\]` as intermediate glue:
   ```text
   .*(?:^|[/\\])(?:DIR_1)(?:[/\\]).*?(?:^|[/\\])(?:DIR_2)(?:[/\\])
   ```
   If no `dir` clause is present, the path starts with `.*(?:^|[/\\])?`.
3. **Stage 3: Filename Stem (`name`)**
   If a `(name STEM)` clause is present:
   * Connected to the directory chain by `(?:.*[/\\])?`.
   * Followed by the lowered `<STEM>` pattern.
   * If **no** `(ext ...)` follows, anchored with `$` (end of string).
   * If `(ext ...)` **does** follow, `$ ` is omitted so `ext` can bind to the stem.
4. **Stage 4: Extension (`ext`)**
   If an `(ext E1 E2 ...)` clause is present:
   * Strips any user-provided leading dots from arguments (`.cpp` $\rightarrow$ `cpp`).
   * Appends: `\.(?:E1|E2|...)$`.

#### Canonical Composition Example
```lisp
rx:(path
     (exclude-dir build target node_modules)
     (dir src include)
     (name (seq test_ (+ alnum)))
     (ext cpp h))
```
**Lowers deterministically to:**
```text
^(?!.*(?:^|[/\\])(?:build|target|node_modules)(?:[/\\]|$)).*(?:^|[/\\])(?:src|include)[/\\](?:.*[/\\])?test_[a-zA-Z0-9]+\.(?:cpp|h)$
```

---

## 6. SIMD Prefilter Harvesting

To preserve FindHelper's high-throughput search (>700k files in under 20ms), `rx:` extracts mandatory substring literals to construct `string_search::SimdSubstringHints`.

### 6.1 Recursive "Must-Appear" Harvesting Algorithm
The compiler executes a recursive AST analysis starting from the root node:
1. **Pass-through nodes**: Descend into `seq`, `path`, and extract all mandatory children.
2. **Path node extraction**:
   * For `(dir D1)` (single argument): `D1` is a mandatory substring.
   * For `(name LITERAL)` (when literal): `LITERAL` is a mandatory substring.
   * For `(ext E1)` (single argument): `.<E1>` is a mandatory substring.
3. **Pruned branches**:
   * Do **not** descend into `or` / `|` (alternatives mean neither is guaranteed).
   * Do **not** descend into `*`, `?`, `opt`, `zero-or-more` (occurrences may be 0).
   * Do **not** descend into `exclude-dir` / `not-dir` (negative assertions).
   * For `+` / `one-or-more`: Descend (guaranteed at least 1 occurrence).

### 6.2 Selection Policy
* Filter all harvested strings to those with length $\ge kMinRequiredSubstringLength$ (3 characters).
* Select the **longest** harvested literal (maximizes SIMD Boyer-Moore skip distances).
* Respect the active `case_sensitive` toggle when building `SimdSubstringHints`.

---

## 7. ReDoS Safety & Guardrails

To prevent search worker thread hangs and Catastrophic Backtracking:
1. **Source Length Limit**: Source pattern string exceeding `kMaxPatternLength = 1000` bytes is rejected with `RxError::PatternTooLong`.
2. **Emitted Regex Length Limit**: The generated ECMAScript regex string exceeding 1,000 bytes is rejected with `RxError::EmittedRegexTooLong`.
3. **Nesting Depth Limit**: Parenthesis depth exceeding `kMaxRxNestingDepth = 32` is rejected with `RxError::NestingTooDeep`.
4. **Prohibition of Nested Quantifiers**:
   The parser tracks quantifier context. Any quantifier (`*`, `+`, `?`, `repeat`) whose direct child or descendant is another quantifier is immediately rejected with `RxError::NestedQuantifierProhibited`.
5. **No Allocation on Hot-Path**:
   Parsing, AST creation, regex compilation, and prefilter initialization occur entirely on the UI/coordinator thread prior to dispatching search tasks.

---

## 8. C++ Codebase Integration

All required modifications are localized to clear module boundaries:

| File / Component | Required Change |
|---|---|
| [`src/search/SearchPrefixes.h`](file:///Users/brunoorsier/dev/USN_WINDOWS/src/search/SearchPrefixes.h) | Add `kRx = "rx:"`; update `kAll` to `std::array<std::string_view, 4>{kPathPattern, kRegex, kFuzzy, kRx}`; update `IsExplicitPrefixToken`. |
| [`src/search/SearchPatternUtils.h`](file:///Users/brunoorsier/dev/USN_WINDOWS/src/search/SearchPatternUtils.h) | Add `PatternType::Rx`; update `DetectPatternType`; update `ClassifyMatcherKind`; add `CreateMatcherImplRx`. |
| `src/search/rx/` (New Directory) | Implement `RxScanner`, `RxParser`, `RxAst`, `RxEmitterRegex`, and `RxDiagnostics`. |
| [`src/gui/PatternDiagnostics.cpp`](file:///Users/brunoorsier/dev/USN_WINDOWS/src/gui/PatternDiagnostics.cpp) | Add `CheckRxQuery()` validation; write errors directly to `SearchPipelineState::pattern_error`. |
| [`src/ui/MatchHighlight.h`](file:///Users/brunoorsier/dev/USN_WINDOWS/src/ui/MatchHighlight.h) | Handle `PatternType::Rx` by emitting regex and invoking `FindRegexSpans`. |
| [`src/ui/SearchInputs.cpp`](file:///Users/brunoorsier/dev/USN_WINDOWS/src/ui/SearchInputs.cpp) | Render accent borders and status pills when `rx:` is detected in the input field. |
| [`src/ui/SearchHelpWindow.cpp`](file:///Users/brunoorsier/dev/USN_WINDOWS/src/ui/SearchHelpWindow.cpp) | Document `rx:` syntax, path extensions, and examples in Search Help. |
| [`src/ai/GeminiApiUtils.cpp`](file:///Users/brunoorsier/dev/USN_WINDOWS/src/ai/GeminiApiUtils.cpp) | Instruct Gemini to generate `rx:(...)` for structured path queries and exclusions. |
| `CMakeLists.txt` & `scripts/test_targets.txt` | Add `rx_matcher_tests` test target. |

### 8.1 Implementation Definition: `CreateMatcherImplRx`
```cpp
inline PatternMatcher CreateMatcherImplRx(std::string_view pattern, bool case_sensitive) {
  const std::string_view rx_source = ExtractPatternView(pattern);
  if (rx_source.empty()) {
    return PatternMatcher{AllPassMatcher{}};
  }
  
  rx::CompilationResult result = rx::CompileRxToRegex(rx_source);
  if (!result.is_valid) {
    // Malformed pattern matches nothing; surfaced via PatternDiagnostics
    return PatternMatcher{InvalidPatternMatcher{}};
  }
  
  return search_pattern_utils_detail::CreateMatcherImplStdRegex(
      result.emitted_regex, case_sensitive, result.harvested_literal);
}
```

---

## 9. UI Immediate-Mode Diagnostics & Syntax Guidance

### 9.1 Data Flow & Confinement
In conformance with project UI confinement rules, syntax checking does not happen ad-hoc in ImGui loops:
1. `UpdatePatternDiagnostics()` in `PatternDiagnostics.cpp` checks `rx:` queries whenever inputs change.
2. Writes diagnostic text and parenthesis balance state to `SearchPipelineState`.
3. `SearchInputs.cpp` reads `SearchPipelineState` to render visual cues.

### 9.2 Visual Guidance
* **Accent Borders**:
  * **Cyan Accent** (`#4EC9B0`): Valid, balanced `rx:` query.
  * **Amber Accent** (`#CE9178`): Unbalanced parenthesis while typing (e.g. `Depth: 1`).
  * **Red Border** (`#F44747`): Syntax error or invalid operator.
* **Status Pill**:
  * Rendered below the search bar:
    ```text
    [✓ rx: valid]  Dir: "src"  |  Ext: ".cpp", ".h"  |  Regex: .*[/\\]src[/\\] .*\.(cpp|h)$
    ```

---

## 10. Test & Verification Matrix

### 10.1 `rx_matcher_tests` Suites
1. **Lexer & Parsing Tests**:
   * Tokenization of bare words (`cpp`), quoted strings (`"Program Files"`), parentheses.
   * Delimiter handling without requiring quotes.
2. **Lowering & Escaping Tests**:
   * Metacharacter escaping (`foo.bar` $\rightarrow$ `foo\.bar`, `[a]` $\rightarrow$ `\[a\]`).
   * Aliases (`and` $\equiv$ `seq` $\equiv$ `:`).
   * All `NamedClass` outputs (`space`, `blank`, `xdigit`, `any-char`).
3. **Path Composition Tests**:
   * Standalone `(name ...)` vs composed `(path (name ...) (ext ...))`.
   * Cross-platform directory matching across `/` and `\` separators.
   * `exclude-dir` lookahead placement.
4. **Safety & Guardrail Tests**:
   * Rejection of nested quantifiers: `(* (+ a))`, `(+ (* a))`, `(? (? a))`.
   * Rejection of patterns exceeding 1,000 bytes.
   * Rejection of nesting depth $> 32$.
   * Rejection of general negation `(not (seq ...))`.
5. **Prefilter Tests**:
   * Verification of harvested literal substrings forwarding to `SimdSubstringHints`.

### 10.2 Verification Command
```bash
./scripts/build_tests_macos.sh --target rx_matcher_tests
./scripts/build_tests_macos.sh --smoke
```

---

## 11. Future Considerations & Roadmap (V2+)

The following capabilities are deliberately out-of-scope for the V1 release to maintain safety, predictability, and fast time-to-market. They are recorded here for prioritized future iterations:

### 11.1 General Lookahead Negation (Pattern-Level `not`)
* **Current V1 Scope**: `not` is strictly confined to character classes (e.g. `(not (any "0-9"))` $\rightarrow$ `[^0-9]`), while path exclusions are handled structurally via `(exclude-dir ...)`.
* **Future Extension**: Support general negation over arbitrary sub-expressions, such as `(seq "log_" (not (seq "test" (* any))) ".txt")`.
* **Prerequisites & Guardrails**:
  * Formulate safe lowering rules to ECMAScript negative lookaheads `(?!...)`.
  * Implement static AST analysis to guarantee no catastrophic backtracking occurs when negative lookaheads are combined with outer wildcards (`*`, `+`).
  * Ensure emitted lookahead patterns cannot escape their intended sub-expression boundaries.

### 11.2 Trigram Inverted Index Gate Integration
* **Current V1 Scope**: Acceleration relies on zero-copy SIMD Boyer-Moore literal prefiltering (`SimdSubstringHints`) extracted via the recursive "must-appear" AST visitor (§6).
* **Future Extension**: Connect `rx:` AST literals directly to FindHelper's permanent Roaring Bitmap Trigram Index (`specs/2026-09-29_FILENAME_TRIGRAM_ROARING_INDEX_SPEC.md`).
* **Prerequisites & Guardrails**:
  * Implement an exact Trigram harvester that derives mandatory 3-character n-grams from top-level sequences without false-positive or false-negative drops.
  * Execute differential gate guard tests (`specs/2026-10-10_DIFFERENTIAL_GATE_GUARD_TEST_SPEC.md`) and verify zero regression on the 706k-path benchmark suite (`./scripts/run_search_benchmark.sh`) before enabling.

### 11.3 Dual-Target Lowering to 64-bit Bitmask NFA (`PathPattern`)
* **Current V1 Scope**: All `rx:` queries lower to ECMAScript `std::regex`.
* **Future Extension**: For simple `rx` patterns that only use glob-equivalent constructs (e.g. `(path (dir src) (ext cpp))`), compile directly to `path_pattern::CompiledPathPattern` instead of `std::regex`.
* **Benefits**: Leverages FindHelper's ultra-fast 64-bit bitmask NFA (`StateMask` register matching), bringing match overhead down to $< 0.1\,\mu\text{s}$ per path.

