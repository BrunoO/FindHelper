# Formal Specification: PathPattern Simplification — Keep `**`/`*`/`?`, Drop Regex-Lite Features

**Date:** 2026-11-10  
**Status:** APPROVED FOR IMPLEMENTATION  
**Policy on Breaking Changes:** Breaking changes are explicitly accepted. Backward compatibility with regex-lite features (`[...]`, `+`, `{m,n}`) does not need to be maintained. Users requiring regex, character classes, alternation, or repetition are directed to `rx:` (structured regex, recommended) or `re:` (ECMAScript regex).

---

## 1. Motivation & Goals

`rx:` (structured regex) and `re:` (ECMAScript) now provide comprehensive, robust support for all advanced pattern constructs (character classes, repetition, alternations, lookaheads/exclusions). 

The remaining purpose and value of `pathPattern` (`pp:`) is the **fast glob tier** (`*`, `?`, `**`, `**/`) accelerated by SIMD substring prefilters. Retaining the regex-lite half is redundant, maintenance-heavy, and costly:
- Requires a 272-byte `CharClass` bitmap per atom.
- Allocates up to ~34 KB per compiled pattern on the heap (`Pattern`, `Atom[128]`).
- Forces type-erased heap storage (`cached_pattern_` / `PatternPtr`), custom deleters (`PatternDeleter`), and multiple compilation pipelines.
- Causes subtle parser discrepancies and complex prefilter-skipping logic.

**Goal:** A lean, fast, robust codebase — completely remove the regex-lite half of `pathPattern`, retain the fast glob tier, unify token storage on a clean `std::vector<SimpleToken>`, and provide a unified ~50-line simple-token backtracking matcher for segment semantics (`**/`, `/**`) and patterns exceeding the 64-bit NFA mask.

---

## 2. Scope

### 2.1 In scope
- **Spike (completed):** Verified that the simple NFA does *not* handle `**/` / `/**` segment boundaries (the zero-segment case requires positionally trying the skip), so a backtracking matcher is retained and simplified to `SimpleToken` (no classes/quantifiers). `NeedsSegmentSemantics` is kept.
- **`src/path/PathPatternMatcher.{h,cpp}`**:
  - Delete `Pattern`, `Atom`, `CharClass`, `PatternPtr`, `PatternDeleter`, `CompilePattern`, `MatchAdvancedPathPattern`, `CompileAdvancedPattern`, `CompileSimplePatternAsBacktracking`, `IsRunLastCharOptional`, and `SkipBraceGroupInterior`.
  - Expose `SimpleTokKind` and `SimpleToken` in `PathPatternMatcher.h` (or namespace); replace fixed-size array type-punning (`simple_tokens_storage`, `alignas`, `reinterpret_cast`) with `std::vector<SimpleToken> tokens`.
  - Implement a single, clean, lightweight backtracking matcher `MatchSimpleTokensBacktracking` (~50 lines) that executes both whole-segment double-star (`**/`) skips and patterns with $>64$ tokens.
  - Simplify `AppendPrefilterRuns` to extract safe literals directly via `IsSafeLiteralChar`, without class/brace interior skipping.
  - Simplify `CompileStatus`: retain only `kOk` and `kMovedFrom` (remove `kTooManyAtoms` and `kUnterminatedClass`).
  - Enable `= default` move constructor and assignment on `CompiledPathPattern`.
- **`src/search/SearchPatternUtils.h`**:
  - Update `DetectPatternType` auto-detection to drop `[ ] { }`.
- **`src/search/TrigramQueryPlanner.h`**:
  - Update `HasComplexLeafMetacharacters` to check only anchors (`^$`) instead of `^$[]{}+`, allowing literal brackets and plus signs to produce trigrams.
- **Consumers**:
  - `src/gui/PatternDiagnostics.cpp`: PathPattern compile check succeeds for all input strings (syntax is always valid).
  - `src/ui/SearchHelpWindow.cpp` & `src/ui/SearchHelpWindow.h`: Remove Section 9 ("PathPattern Character Classes and Separators"), update Section 6 ("PathPattern") to document only globs/anchors, and renumber subsequent sections.
  - `src/ui/HelpWindow.cpp`: Update inline help and changelog strings.
  - `README.md`: Update auto-detect flowchart node (drop `[ ] { }`) and feature descriptions.
- **Tests**:
  - `tests/PathPatternMatcherTests.cpp`: Delete test cases for classes, quantifiers, too-many-atoms, and unterminated class. Keep and verify `**`/`*`/`?`, segment semantics (`**/*.cpp`), $>64$-token fallback, prefilters, highlighting, and move semantics.
  - `tests/PathPatternBenchmark.cpp`: Update benchmark patterns (replace `[0-9]` with glob wildcards).
  - `tests/GeminiApiUtilsTests.cpp`: Clean up subcase names in `ValidatePathPatternFormat - Complex Patterns`.
- **Docs**:
  - `docs/design/PARALLEL_SEARCH_ENGINE_DESIGN.md`: Update matcher list.

### 2.2 Out of scope (NOT touched)
- Glob tier single-segment (`*`/`?`) and `TryGlobShapeMatcher`.
- `re:` (ECMAScript) and `rx:` (structured regex) matchers.
- `fz:` (fuzzy) matcher.
- Substring matcher and SIMD substring prefilter engine.
- Filename trigram index (`FilenameTrigramIndex`) and index storage.
- `src/api/GeminiApiUtils.cpp`: `BuildSearchConfigPrompt` already instructs the model to use plain text, glob, or `re:`; no prompt changes needed.

---

## 3. New Grammar (post-simplification)

`pathPattern` (`pp:`) supports exactly:

- **Literals:** Ordinary characters match themselves.
- **Separators:** `/` and `\` are interchangeable (either matches either).
- **Wildcards:**
  - `?` — exactly one non-separator character.
  - `*` — zero or more non-separator characters (within one segment).
  - `**` — zero or more characters, including separators.
  - `**/` (double-star-slash) — zero or more whole directory segments (boundary kept; e.g. `**/*.cpp` matches `main.cpp` and `src/main.cpp`).
  - `/**` (slash-double-star) — zero or more trailing directory segments (e.g. `src/**` matches `src` and `src/sub`).
- **Anchors:**
  - `^` — path start.
  - `$` — path end.
- By default (no anchors) the pattern is treated as whole-path matching (`^pattern$`).

### Behavior for Removed Constructs
- `[`, `]`, `{`, `}`, `+` are treated as **ordinary literal characters**.
- For example, `pp:file[0-9].txt` matches the literal filename `file[0-9].txt`.
- Users needing character classes or quantifiers must use `rx:` (recommended) or `re:`.

---

## 4. Auto-Detection (`DetectPatternType`)

In `src/search/SearchPatternUtils.h`:

**Before:**
```cpp
if ((!pattern.empty() && (pattern.front() == '^' || pattern.back() == '$')) ||
    pattern.find("**") != std::string_view::npos ||
    pattern.find('[') != std::string_view::npos ||
    pattern.find(']') != std::string_view::npos ||
    pattern.find('{') != std::string_view::npos ||
    pattern.find('}') != std::string_view::npos) {
  return PatternType::PathPattern;
}
```

**After:**
```cpp
if ((!pattern.empty() && (pattern.front() == '^' || pattern.back() == '$')) ||
    pattern.find("**") != std::string_view::npos) {
  return PatternType::PathPattern;
}
```

Patterns containing `[ ] { } +` without `**`, `^`, `$`, or explicit prefix now fall through to `PatternType::Glob` (if `*` or `?` are present) or `PatternType::Substring`.

---

## 5. Architectural Design & Implementation

### 5.1 Token Representation & Storage (`PathPatternMatcher.h`)

**Spike result (c):** The simple NFA does *not* handle `**/` / `/**` segment boundaries (the zero-segment case requires positionally trying the skip), so a backtracking matcher is retained. `NeedsSegmentSemantics` (already present in the codebase) is kept as-is.

`SimpleTokKind` and `SimpleToken` stay **file-local in the `.cpp`** (they are an implementation detail; exposing them is not required by this simplification). The fixed inline storage is retained for the hot path:

In `CompiledPathPattern`:
- Keep `simple_tokens_storage[64*2]` + `simple_token_count` + `epsilon_mask` for the NFA path (≤64 tokens, zero heap allocation, cache-local).
- Add `std::vector<SimpleToken> overflow_tokens;` used **only** by the backtracking matcher when `simple_token_count > kMaxSimpleTokens` (cold path; heap allocation acceptable).
- Add `bool needs_backtracking = false;` flag (set when `NeedsSegmentSemantics(pattern)` is true OR token count exceeds the mask).
- Delete `kMaxPatternAtoms`, `cached_pattern_`, `PatternPtr`, `PatternDeleter`, `uses_advanced`.
- `CompileStatus` retains only `kOk` and `kMovedFrom`.
- Enable `= default` for move constructor and move assignment. Keep copy deleted.

**Storage decision (a):** Inline fixed array for the hot NFA path; `std::vector` only for the cold overflow/backtracking case. This avoids a heap allocation and pointer indirection on every matched path while still eliminating the `Pattern`/`Atom`/`CharClass` machinery. (If simplicity is preferred over this, adopt the vector everywhere but add a benchmark gate — see §7.)

### 5.2 Match Engines & Dispatch (`PathPatternMatcher.cpp`)

`NeedsSegmentSemantics` (already present) is kept unchanged:
```cpp
bool NeedsSegmentSemantics(std::string_view pattern) {
  if (pattern.find("**/") != npos || pattern.find("**\\") != npos) return true;
  if (pattern.size() >= 3) {
    auto tail = pattern.substr(pattern.size() - 3);
    if (tail == "/**" || tail == "\\**") return true;
  }
  return false;
}
```

Compilation in `CompilePathPattern()`:
1. Extract anchors `^` and `$`.
2. Check for all-pass (`**`, empty).
3. Build tokens into `compiled.simple_tokens_storage` (up to `kMaxSimpleTokens`) via `BuildSimpleTokens(pattern_without_anchors)`. On overflow, set `needs_backtracking = true` and store the full token list in `overflow_tokens`.
4. Determine execution engine:
   - If `!NeedsSegmentSemantics(pattern_without_anchors) && token_count <= kMaxSimpleTokens`:
     - `compiled.needs_backtracking = false;`
     - Compute `compiled.epsilon_mask = ComputeEpsilonMask(...)`.
   - Else:
     - `compiled.needs_backtracking = true;` (segment semantics `**/`, `/**`, or >64 tokens).
5. Extract SIMD prefilter substrings (`ExtractRequiredSubstring`, `CollectExtraRequiredSubstrings`).

Matching in `PathPatternMatches()`:
```cpp
if (compiled.needs_backtracking) {
  return MatchSimpleTokensBacktracking(compiled, path);
}
return MatchSimplePathPattern(compiled, path); // Bitmask NFA
```

### 5.3 Lightweight Simple-Token Backtracking Matcher (~50 lines)

**Guard requirement (d):** The matcher must be either (i) iterative with an explicit stack, or (ii) recursive with a hard iteration cap (e.g. 10,000 steps) that returns `false` on exceed (fail closed). The `kStarAny` backtrack loop is O(n) per star per match; acceptable on this cold path but must be bounded. Prefer iterative.

Implement `MatchSimpleTokensBacktracking`:
```cpp
bool MatchTokensFrom(const std::vector<SimpleToken>& tokens, size_t tok_idx,
                     std::string_view path, size_t path_idx, bool case_insensitive) {
  if (tok_idx == tokens.size()) {
    return path_idx == path.size();
  }

  const SimpleToken& tok = tokens[tok_idx];

  // Zero-segment skip for **/ (DoubleStarSlash):
  if (tok.kind == SimpleTokKind::kStarAny && tok_idx + 1 < tokens.size() &&
      tokens[tok_idx + 1].kind == SimpleTokKind::kSeparator) {
    if (MatchTokensFrom(tokens, tok_idx + 2, path, path_idx, case_insensitive)) {
      return true;
    }
  }

  // Zero-segment skip for trailing /** (TrailingSlashDoubleStar):
  if (tok.kind == SimpleTokKind::kSeparator && tok_idx + 1 < tokens.size() &&
      tokens[tok_idx + 1].kind == SimpleTokKind::kStarAny && tok_idx + 2 == tokens.size()) {
    if (path_idx == path.size()) {
      return true;
    }
  }

  switch (tok.kind) {
    case SimpleTokKind::kLiteral:
      if (path_idx < path.size() &&
          (case_insensitive ? ToLowerChar(path[path_idx]) == tok.literal
                            : path[path_idx] == tok.literal)) {
        return MatchTokensFrom(tokens, tok_idx + 1, path, path_idx + 1, case_insensitive);
      }
      return false;

    case SimpleTokKind::kSeparator:
      if (path_idx < path.size() && (path[path_idx] == '/' || path[path_idx] == '\\')) {
        return MatchTokensFrom(tokens, tok_idx + 1, path, path_idx + 1, case_insensitive);
      }
      return false;

    case SimpleTokKind::kAnyNonSep:
      if (path_idx < path.size() && path[path_idx] != '/' && path[path_idx] != '\\') {
        return MatchTokensFrom(tokens, tok_idx + 1, path, path_idx + 1, case_insensitive);
      }
      return false;

    case SimpleTokKind::kStarNonSep: {
      // Greedily advance within the segment, then backtrack
      size_t end = path_idx;
      while (end < path.size() && path[end] != '/' && path[end] != '\\') {
        ++end;
      }
      for (size_t p = end; p >= path_idx; --p) {
        if (MatchTokensFrom(tokens, tok_idx + 1, path, p, case_insensitive)) {
          return true;
        }
        if (p == path_idx) break;
      }
      return false;
    }

    case SimpleTokKind::kStarAny: {
      // Greedily advance across all characters, then backtrack
      for (size_t p = path.size(); p >= path_idx; --p) {
        if (MatchTokensFrom(tokens, tok_idx + 1, path, p, case_insensitive)) {
          return true;
        }
        if (p == path_idx) break;
      }
      return false;
    }
  }
  return false;
}
```

### 5.4 Prefilter Extraction Simplification

In `PathPatternMatcher.cpp`:
- Delete `IsRunLastCharOptional` and `SkipBraceGroupInterior`.
- `AppendPrefilterRuns`: Simplify to scan the pattern linearly using `IsSafeLiteralChar`. Wildcards and separators break runs; all safe literal runs are extracted directly without character-class or quantifier guards.

### 5.5 Trigram Query Planner (`src/search/TrigramQueryPlanner.h`)

Update `HasComplexLeafMetacharacters`:
```cpp
[[nodiscard]] inline bool HasComplexLeafMetacharacters(std::string_view leaf) {
  return leaf.find_first_of("^$") != std::string_view::npos;
}
```
Brackets (`[ ]`), braces (`{ }`), and `+` are now literal characters and will no longer prevent leaf trigram planning.

---

## 6. Consumer Updates

1. **`src/gui/PatternDiagnostics.cpp`**:
   - `CheckQuery` for PathPattern still calls `CompilePathPattern`. Since `CompileStatus` only produces `kOk` and `kMovedFrom`, PathPattern queries will not fail compilation.
2. **`src/ui/SearchHelpWindow.cpp` & `SearchHelpWindow.h`**:
   - Remove Section 9 ("PathPattern Character Classes and Separators").
   - Update Section 6 ("PathPattern") to document `**`, `*`, `?`, and anchors. Note that `[...]` and `{...}` are literals and recommend `rx:` / `re:`.
   - Renumber Section 10 ("Mark Mode") to Section 9.
3. **`src/ui/HelpWindow.cpp`**:
   - Update changelog and help strings referring to `[0-9]` or character classes.
4. **`README.md`**:
   - Update auto-detect flowchart node from `Contains ** ^ $ [ ] { }` to `Contains ** ^ $`.
   - Update feature description to emphasize fast globbing and refer classes/repetition to `rx:`.

---

## 7. Acceptance Criteria

1. `src/path/PathPatternMatcher.h` and `.cpp` no longer contain `CharClass`, `Atom`, `Pattern`, `cached_pattern_`, `PatternPtr`, `PatternDeleter`, `uses_advanced`, `IsRunLastCharOptional`, or `SkipBraceGroupInterior`.
2. `CompiledPathPattern` uses `std::vector<SimpleToken>` for token storage; move constructor and move assignment are `= default`. No `reinterpret_cast` type-punning remains.
3. `CompileStatus` contains only `kOk` and `kMovedFrom`.
4. Segment semantics (`**/*.cpp` matching `main.cpp`, `src/**/*.cpp` matching `src/main.cpp`) and long patterns ($>64$ tokens) pass all tests via `MatchSimpleTokensBacktracking`.
5. `DetectPatternType` auto-detects `PathPattern` only on `**`, `^`, or `$`.
6. Full test suite (`./scripts/build_tests_macos.sh`) passes with 0 failures and no AddressSanitizer/UBSanitizer issues.
7. Documentation and Search Help accurately reflect the simplified grammar without references to character classes or repetition in `pp:`.
8. All existing `**/` boundary tests (`PathPatternMatcherTests.cpp`, the "double star slash preserves segment boundary" case) pass against the new matcher.
9. `path_pattern_benchmark` shows no regression >5% on the 7 representative patterns vs. a recorded baseline.
10. Backtracking matcher is iterative (or guarded by an explicit step cap) and cannot hang on pathological input.

---

## 8. Breaking Change Notice (for release notes)

> **Breaking Change:** `pp:` / PathPattern no longer supports regex-lite character classes (`[abc]`, `[a-z]`, `[^a]`) or repetition quantifiers (`+`, `{n}`, `{m,n}`). These characters are now treated as exact literals. Auto-detection of PathPattern now triggers solely on `**`, `^`, or `$`. Users requiring character classes, repetition, or exclusions should use `rx:` (recommended structured regex) or `re:` (ECMAScript regex).
