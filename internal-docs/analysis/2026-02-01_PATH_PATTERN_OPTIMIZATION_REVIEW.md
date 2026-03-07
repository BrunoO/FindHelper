# Path Pattern Implementation – Expert Optimization Review

**Date:** 2026-02-01  
**Scope:** `PathPatternMatcher.cpp/.h`, hot path usage in `ParallelSearchEngine`, `SearchPatternUtils`  
**Focus:** Search/grep-style algorithm optimizations currently missed

---

## Executive Summary

The path pattern implementation is already strong: literal-only fast path, required-substring pre-filter, DFA when possible, NFA fallback, and advanced Pattern for char classes/quantifiers. This review identifies **high-impact** and **medium-impact** optimizations that reuse existing project infrastructure (e.g. AVX2 string search) and classic algorithm improvements (length bounds, sparse NFA iteration, CharClass packing, multiple substrings).

---

## 1. Use Existing AVX2 Substring Search for Required-Substring "Contains"

**Current behavior:**  
`CheckRequiredSubstringContains` uses:
- Case-sensitive: `path.find(required_substring) != npos` (libc `strstr`-style).
- Case-insensitive: `std::search` with a lambda that lowercases both sides per comparison.

**Gap:**  
The project already has `string_search::ContainsSubstring` and `ContainsSubstringI` in `StringSearch.h`, which use AVX2 for longer strings (e.g. text ≥ 32 bytes, pattern ≥ 4 bytes). PathPatternMatcher does **not** use them; it uses its own scalar path.

**Optimization:**  
In `CheckRequiredSubstringContains`, delegate to:
- Case-sensitive: `string_search::ContainsSubstring(path, required_substring)` when `path.size()` and `required_substring.size()` are above a small threshold (e.g. 16 and 4).
- Case-insensitive: `string_search::ContainsSubstringI(path, required_substring)` under the same threshold.

For short path/needle, keep the current scalar logic to avoid AVX2 call overhead.

**Impact:** High for patterns like `**/some_long_folder_name/**/*.cpp` where the required substring is “in the middle” and paths are long. The required-substring check runs on every candidate path; AVX2 can cut cost significantly on large indexes.

**Implementation note:**  
Add `#include "utils/StringSearch.h"` in `PathPatternMatcher.cpp` and branch in `CheckRequiredSubstringContains`: if sizes are above threshold, call `ContainsSubstring` / `ContainsSubstringI`, else keep existing `find` / `std::search`. No new dependencies; reuse existing, tested code.

---

## 2. Path Length Bounds (Min/Max Match Length)

**Current behavior:**  
Full match is implicit `^pattern$`. There is no length-based rejection before running DFA/NFA.

**Gap:**  
For simple (non-advanced) patterns we can compute from the token list:
- **Min length:** sum of literal lengths + number of `?` (each consumes 1) + 0 for `*`/`**` (they can consume 0).
- **Max length:** unbounded if `*` or `**` is present; otherwise sum of literal lengths + number of `?`.

If `path.size()` is outside `[min_len, max_len]`, the path cannot match. This is a single integer comparison before running the matcher.

**Optimization:**  
At compile time (e.g. in `CompileSimplePattern` after `BuildSimpleTokens`):
- Compute `min_match_length` and `max_match_length` from the simple tokens (use `std::numeric_limits<size_t>::max()` for “unbounded” when `*` or `**` exists).
- Store them in `CompiledPathPattern` (e.g. `size_t min_match_length`, `bool has_unbounded_max` or `max_match_length`).

In `PathPatternMatches` (before DFA/NFA), if `path.size() < min_match_length` return false; if max is bounded and `path.size() > max_match_length` return false.

**Impact:** Medium. Cheap rejection for very short or (when max is bounded) very long paths, especially for literal-heavy or `?`-only patterns. No change to match semantics.

---

## 3. Sparse NFA Active-Set Iteration (Trailing Zeros)

**Current behavior:**  
`StepSimpleNfaOptimized` loops over all token indices:

```cpp
for (std::size_t i = 0; i < n; ++i) {
  if (!(active & Bit(i))) continue;
  // ...
}
```

So we always do O(n) iterations per input character, even when only a few bits of `active` are set.

**Gap:**  
When the active set is sparse (e.g. after a literal mismatch or in patterns with many `*`), most iterations are no-ops. Iterating only the set bits is a standard optimization.

**Optimization:**  
Iterate only indices where `active` has a bit set, using count-trailing-zeros:

```cpp
StateMask remaining = active;
while (remaining != 0) {
  const std::size_t i = static_cast<std::size_t>(__builtin_ctzll(remaining));
  // ... use tokens[i] ...
  remaining &= (remaining - 1);  // clear lowest set bit
}
```

Use `#ifdef _MSC_VER` / `_BitScanForward64` on Windows for portability. Then build `next` from these indices and apply epsilon closure as today.

**Impact:** Medium. Helps when token count is large and active states are sparse (e.g. patterns with many `*` or long literal runs). No semantic change.

---

## 4. CharClass Bitmap Packing

**Current behavior:**  
`CharClass` uses `std::array<bool, 256>` (typically 256 bytes). Lookup is `bitmap[uc]`.

**Gap:**  
256 bytes can evict other hot data from cache when many paths are matched. A packed bitmap (e.g. 4 × `uint64_t` or 8 × `uint32_t`) uses 32 bytes and allows a single cache line for the class. Lookup: `(bits[c / 64] >> (c % 64)) & 1`.

**Optimization:**  
Replace `std::array<bool, 256>` with e.g. `std::array<std::uint64_t, 4>` and:
- Set bit: `bits[c / 64] |= (uint64_t(1) << (c % 64));`
- Test: `(bits[c / 64] >> (c % 64)) & 1` (with same negate/has_any logic as now).

**Impact:** Medium for advanced patterns that use character classes; reduces footprint and can improve cache behavior when matching many paths. Requires updating `AddCharToClass` and `CharClass::Matches`.

---

## 5. Literal-Only Case-Insensitive: Pre-Lowercase Pattern Once

**Current behavior:**  
`MatchLiteralOnlyPath` for case-insensitive does:

```cpp
return std::equal(path.begin(), path.end(), compiled.pattern_string.begin(),
                  [](char a, char b) {
                    return ToLowerChar(...a) == ToLowerChar(...b);
                  });
```

So we lowercase **every character of the pattern** on **every** match. The pattern is fixed for the lifetime of the compiled pattern.

**Optimization:**  
For literal-only + case-insensitive, store a lowercased copy of the pattern at compile time (e.g. `std::string pattern_string_lower` in `CompiledPathPattern`, populated only when `is_literal_only && case_insensitive`). In `MatchLiteralOnlyPath`, compare `ToLower(path[i])` only to `pattern_string_lower[i]`, so we never lowercase the pattern at match time.

**Impact:** Small–medium. Only affects literal-only, case-insensitive patterns, but removes redundant work in a tight loop.

---

## 6. Multiple Required Substrings (AND of Literals)

**Current behavior:**  
`ExtractRequiredSubstring` returns the **single longest** literal run (e.g. from `**/src/**/*.cpp` we might get `src` or `.cpp` depending on how runs are broken). We only use one required substring for fast rejection.

**Gap:**  
Patterns like `**/src/**/*.cpp` have two useful literals: `src` and `.cpp`. A path that has `.cpp` but not `src` (or the reverse) cannot match. Rejecting such paths with two cheap “contains” checks avoids running the full DFA/NFA.

**Optimization:**  
Extend extraction to return a small set of **non-overlapping** required literals (e.g. up to 2–3), with a minimum length (e.g. ≥ 3) and optionally “must be prefix” / “must be suffix” for the first/last. In `PathPatternMatches`, before DFA/NFA, require **all** of these substrings to be present (prefix/suffix/contains as today). If any is missing, return false.

**Impact:** Medium for patterns with multiple literal islands (e.g. folder + extension). Slightly more work at compile time and one extra branch per literal per path; can significantly reduce full matcher calls.

---

## 7. DFA State Table Density (uint8_t When Possible)

**Current behavior:**  
DFA transitions are `std::uint16_t` (state indices). Table size is `state_count * 256 * sizeof(uint16_t)`.

**Gap:**  
If `state_count <= 256`, we could store transitions as `uint8_t`, halving table size and improving cache utilization.

**Optimization:**  
When building the DFA, if `out.state_count <= 256`, allocate a `uint8_t[]` table and store `static_cast<uint8_t>(next_state)`. In `MatchDfa`, use the same index calculation but read `uint8_t` and use it as the next state. If `state_count > 256`, keep the current uint16_t table (or fall back to NFA).

**Impact:** Low–medium. Helps when DFA is used and state count is small; reduces memory and can improve cache behavior.

---

## 8. Suffix / Extension Fast Path

**Current behavior:**  
For `required_substring_is_suffix` we use `CheckRequiredSubstringSuffix` (length check + compare). For patterns that are effectively “ends with .ext”, this is already a good path.

**Possible refinement:**  
If the required suffix is short (e.g. ≤ 8 bytes) and case-sensitive, a single 8-byte load (with proper alignment/safety) could compare the path tail to the suffix in one go. This is a micro-optimization and may not beat a tuned `compare`; measure before adopting.

---

## 9. Avoid Redundant Work in PathPatternMatches Dispatch

**Current behavior:**  
Dispatch order is: literal-only → required substring → simple (DFA/NFA) → advanced. This is already good (cheapest checks first).

**Minor improvement:**  
For literal-only patterns we already skip required-substring extraction (or we could skip setting it). Ensure we do not compute or check required substring when `is_literal_only` is true, so the hot path has one less branch. (Currently we only set required substring when not literal-only in practice; worth confirming and documenting.)

---

## 10. Summary Table

| # | Optimization | Impact | Effort | Reuse |
|---|----------------|--------|--------|--------|
| 1 | AVX2 for required-substring “contains” | High | Low | StringSearch.h |
| 2 | Min/max path length bounds | Medium | Low | New fields + compile-time computation |
| 3 | Sparse NFA iteration (ctz) | Medium | Low | Portable ctz/BSF |
| 4 | CharClass packed bitmap | Medium | Medium | In-place refactor |
| 5 | Literal-only CI: pre-lowercase pattern | Small–Medium | Low | One extra string in compiled |
| 6 | Multiple required substrings | Medium | Medium | Extract + AND in match |
| 7 | DFA uint8_t when state_count ≤ 256 | Low–Medium | Low | Conditional table type/size |
| 8 | Suffix 8-byte load (optional) | Low | Low | Measure first |

---

## Recommended Order of Implementation

1. **#1 (AVX2 for “contains”)** – Reuse existing, high impact, low risk.  
2. **#2 (Length bounds)** – Simple, clear win for length-mismatched paths.  
3. **#5 (Literal-only CI pre-lowercase)** – Quick change, no new concepts.  
4. **#3 (Sparse NFA)** – Standard algorithm improvement; keep NOLINT/sonar notes if needed.  
5. **#6 (Multiple required substrings)** – Better rejection for multi-literal patterns.  
6. **#7 (DFA uint8_t)** – Small code change, measurable on large DFAs.  
7. **#4 (CharClass packing)** – Refactor when touching advanced pattern code.

---

## References

- `src/path/PathPatternMatcher.cpp` – Compilation, DFA/NFA, required substring, literal-only.
- `src/path/PathPatternMatcher.h` – `CompiledPathPattern` layout and constants.
- `src/utils/StringSearch.h` – `ContainsSubstring`, `ContainsSubstringI`, AVX2 routing.
- `src/search/ParallelSearchEngine.cpp` – Hot path: `ProcessChunkRangeIds`, `CheckMatchers`, path matcher usage.
