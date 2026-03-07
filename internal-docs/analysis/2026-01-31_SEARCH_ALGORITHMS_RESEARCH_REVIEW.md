# Search & Grep Algorithms: Research Review

**Date:** 2026-01-31  
**Perspective:** Super-experienced researcher in search/grep algorithms (ripgrep, grep, ag, ugrep, Boyer-Moore, SIMD, multi-pattern, etc.)

This document reviews the current search implementation and identifies **high-impact ideas** you may still be missing: algorithmic, data-structure, and hot-path improvements that are standard in state-of-the-art search tools.

---

## 1. What You Already Do Well

- **SoA + contiguous path storage:** Cache-friendly, parallel-friendly, pre-parsed offsets (filename_start, extension_start). Matches modern “scan a big buffer” design.
- **AVX2 substring search:** First-character scan + FullCompare, with runtime detection and Linux `target("avx2")`. Good for long-enough, ASCII text.
- **Pattern routing:** Substring / Glob / PathPattern / StdRegex with detection and fast paths (literal → SimpleRegex → regex). Reduces unnecessary heavy work.
- **Extension filter:** O(1) hash set lookup; pre-lowercased set for case-insensitive. Sensible.
- **Reverse comparison** for long patterns (>5 chars): Fail fast at the end of the pattern. Nice touch.
- **Prefix fast path:** “Starts with” is checked before AVX2. Good for common queries.
- **Pre-created matchers per thread:** No pattern re-compilation in the hot loop.

---

## 2. Brilliant Ideas You’re Still Not (Fully) Using

### 2.1 Case-insensitive substring: avoid full ToLower copies (high impact, easy fix)

**Where:** `SearchPatternUtils.h` – `MatchPattern()` for `PatternType::Substring` when `case_sensitive == false`:

```cpp
// Current (expensive):
std::string text_lower = ToLower(text);   // O(|text|) allocation + copy
std::string pattern_lower = ToLower(pattern);  // O(|pattern|) allocation + copy
return string_search::ContainsSubstring(text_lower, pattern_lower);
```

**Problem:** Two full string allocations and copies per call. For “*.cpp” over 1M paths you do 1M+ path-length allocations. You already have `ContainsSubstringI(text, pattern)`, which is case-insensitive **without** copying the whole text: it uses AVX2, prefix check, reverse compare, and scalar fallback on the original views.

**Idea:** Use the existing case-insensitive API and avoid building lowercased copies when the type allows:

- For `std::string_view` text: call `string_search::ContainsSubstringI(text, pattern)` directly. No allocations.
- For `const char*` text: wrap in `std::string_view` and call `ContainsSubstringI` (or keep `StrStrCaseInsensitive` which already has an AVX2 path and no full-text copy).

**Recommendation:** In `MatchPattern`, for Substring + case-insensitive, replace the `ToLower(text)` / `ToLower(pattern)` + `ContainsSubstring` path with a single call to `ContainsSubstringI(text, pattern)` (with appropriate `string_view` from `text`). Same for any `CreateMatcherImplSubstring`-style path that currently lowercases the whole string. This is a **one-line conceptual change** and removes a major allocation hot spot.

---

### 2.2 Extension filter: avoid building `std::string` for case-insensitive lookup (medium impact)

**Where:** `SearchPatternUtils.h` – `ExtensionMatches()` when `case_sensitive == false`:

```cpp
std::string ext_key;
for (char c : ext_view) {
  ext_key.push_back(ToLowerChar(...));
}
return (extension_set.find(ext_key) != extension_set.end());
```

**Problem:** For every path you build a small string (typically 2–5 chars) for the extension and do a hash lookup. With millions of paths this is millions of small allocations and hashes. Extensions are short and bounded (e.g. ≤ 16 chars in practice).

**Ideas:**

1. **Stack buffer + string_view for lookup:** Use a small stack buffer (e.g. `char ext_buf[32]`), fill with lowercased extension, then build a `std::string` only for the hash set lookup (or use a set that accepts a key from a buffer). Reduces heap churn; still one hash lookup.
2. **Linear scan over a tiny set:** If `extension_set.size()` is small (e.g. 1–5), a linear scan with inline case-insensitive compare (no allocation) can be faster than hashing a newly built string. Example: for each candidate extension in the set, compare with `ext_view` character-by-character (lowercased). No `std::string` for the path’s extension.
3. **Precomputed “extension in set” for common sizes:** For very small sets (e.g. 1 extension like `"cpp"`), a single inline comparison (length check + loop) avoids set lookup and allocation entirely.

**Recommendation:** At least use a small fixed buffer for the lowercased extension and avoid repeated heap allocation. If `extension_set.size() <= N` (e.g. 4 or 8), consider a branch that does a linear scan with inline compare instead of building a key and hashing.

---

### 2.3 Boyer-Moore / Sunday-style skip (medium impact for long patterns)

**Where:** `StringSearch.h` / `StringSearchAVX2.cpp` – when pattern length is large (e.g. > 32 bytes).

**Current:** You scan 32-byte chunks, find candidate positions by first character, then `FullCompare`. You still advance by 1 after a mismatch in the scalar tail and effectively by 1 when the only match in a chunk fails. So for long patterns you do O(n) candidate positions in the worst case.

**Idea:** Bad-character skip (Boyer-Moore–Horspool or Sunday): precompute a table “if I see character c at the current alignment, I can shift by at least k.” Then on mismatch you advance by `max(1, skip[haystack[i + pattern_len]])` (Sunday) or the BM bad-character shift. For long patterns this gives sub-linear average behavior and much better worst case than naive scan.

**Already in your docs:** `docs/archive/AVX2_NEXT_STEPS_DETAILED.md` describes a Boyer-Moore + AVX2 hybrid (Step 5). That’s the right direction.

**Recommendation:** For pattern length above a threshold (e.g. 32 or 64), add a fast path that builds a small bad-character (or Sunday) table and uses it to skip. Keep using AVX2 for the first-character scan and for `FullCompare` at candidate positions. This gives the “brilliant” win for long patterns (e.g. long path segments) without changing short-pattern behavior.

---

### 2.4 Multi-pattern substring (medium impact for “many literals” queries)

**Where:** Today you have one filename matcher and one path matcher; each is a single pattern (or regex/glob). So “match any of these literals” (e.g. `.cpp` or `.h` in the filename) is not a single multi-pattern scan.

**Idea:** When the user query is effectively “pattern A OR pattern B OR …” and all are plain substrings (or literal after regex extraction), use one of:

- **Aho-Corasick:** One automaton for all literals; single pass over the text. Classic.
- **SIMD multi-pattern:** e.g. 2–4 short patterns in parallel (different registers or lanes). You already have AVX2; checking 2–4 first bytes in parallel and then confirming with a small state machine or comparisons is feasible for a small set of literals.
- **Literal extraction from regex:** If the pattern is a regex that starts with a literal prefix, search for that literal first (with your fast substring path) and run the regex only on candidate regions. ripgrep/ugrep do this.

**Already in your docs:** `AVX2_NEXT_STEPS_DETAILED.md` Step 4 discusses multi-pattern AVX2 for extension filtering.

**Recommendation:** The highest leverage is “regex with leading literal”: detect a literal prefix, use your existing substring (or AVX2) to find candidate positions, then run regex only on those. For “extension in {cpp, h, hpp}” as separate checks, a small Aho-Corasick or a 2–4 pattern SIMD path could replace multiple substring calls; implement after the easier wins (2.1, 2.2).

---

### 2.5 “Last character” or “first character” filter (lower impact, optional)

**Idea:** Before loading the full path or running the full matcher, check a single character (e.g. last character of the path = last character of the pattern, or first character of the filename). If it can’t match, skip the path. This only helps when such a check is very cheap (e.g. you store or can quickly get “last char” or “first char” per path) and when it rejects a large fraction of paths. With SoA you don’t store per-path first/last char; computing it requires at least looking at the path. So this is a micro-optimization unless you add a tiny “first char” / “last char” array per entry (one byte per path). Reasonable to defer.

---

### 2.6 Glob: limit backtracking (correctness / robustness)

**Where:** `SimpleRegex.h` – `GlobMatchImpl` for `*`: “try rest of pattern with every suffix of text” (recursive). For patterns with multiple `*` and long text this can be exponential in the worst case.

**Idea:** Same as regex: cap recursion depth or total steps, or convert to a bounded dynamic programming / NFA simulation so runtime is O(|pattern| * |text|). Prevents DoS and rare pathologically slow searches.

**Recommendation:** Add a max recursion depth or a step counter for glob matching (and optionally for SimpleRegex), and fall back to “no match” or to a safer implementation when the limit is exceeded.

---

### 2.7 Regex: literal prefix extraction (high impact for “rs:” patterns)

**Where:** When the user uses `rs:some_literal_rest` or a regex that starts with a long literal, you currently run the full regex over the text.

**Idea:** Extract a literal prefix from the regex (e.g. “abc” from “abc.*def”). Search for that literal with your fast substring (or AVX2) path; only run the regex on windows where the literal was found. ripgrep and ugrep do this and get large speedups on many real-world patterns.

**Recommendation:** Add a “literal prefix” extraction step for `rs:` (and optionally for other regex use). If the prefix is long enough (e.g. ≥ 4 bytes), run `ContainsSubstring`/`ContainsSubstringI` first and invoke the regex only at those offsets. This can be done without changing the regex engine itself.

---

### 2.8 Order of checks in the hot loop (already good; one tweak)

**Current order:** deleted → folders_only → extension → pattern. Good: cheapest filters first. Extension uses a hash set (and currently builds a key for case-insensitive). The only change suggested is to make the extension check cheaper (see 2.2) so that “extension filter + substring” remains clearly cheaper than “run path matcher.”

---

## 3. Summary: What to Do First

| Priority | Idea | Effort | Impact |
|----------|------|--------|--------|
| **1** | **Substring case-insensitive:** Use `ContainsSubstringI` in `MatchPattern` (and matcher creation) instead of `ToLower(text)` + `ToLower(pattern)` + `ContainsSubstring`. | Low | High (removes millions of allocations per search). |
| **2** | **Extension case-insensitive:** Avoid building `std::string` for every path; use small buffer or linear scan for small sets. | Low–Medium | Medium. |
| **3** | **Boyer-Moore / Sunday skip:** For long patterns, add bad-character table and skip in AVX2 path. | Medium | Medium (long patterns). |
| **4** | **Regex literal prefix:** Extract leading literal from regex; use substring search first, regex only on candidates. | Medium | High for “rs:” with literal prefix. |
| **5** | **Multi-pattern (Aho-Corasick or SIMD):** For “any of these literals” (e.g. extensions). | Medium–High | Medium. |
| **6** | **Glob recursion limit:** Prevent worst-case exponential time. | Low | Correctness / robustness. |

---

## 4. References (conceptual)

- **Boyer-Moore / Horspool / Sunday:** Bad-character skip tables; sub-linear average case for long patterns.
- **Aho-Corasick:** Multi-pattern exact match in one pass.
- **ripgrep:** Literal extraction, SIMD, lazy DFA, memory maps.
- **ugrep:** Similar ideas; multi-pattern and regex optimizations.
- **Your own:** `docs/archive/AVX2_NEXT_STEPS_DETAILED.md` (Boyer-Moore + AVX2, multi-pattern) – still relevant; the biggest missing piece in code is the case-insensitive substring allocation (2.1) and extension key allocation (2.2).

---

**Bottom line:** You already use SoA, AVX2, pattern routing, and pre-parsed offsets well. The two “brilliant” low-hanging fruits are: **(1) stop allocating full lowercased copies for case-insensitive substring** by using `ContainsSubstringI`, and **(2) stop allocating a new string for every extension lookup** by using a small buffer or small-set linear scan. After that, Boyer-Moore-style skip and regex literal-prefix extraction give the next big wins.
