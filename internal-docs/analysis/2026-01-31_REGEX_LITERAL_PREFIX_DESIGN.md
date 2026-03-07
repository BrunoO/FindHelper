# Regex literal-prefix extraction: where it fits

**Date:** 2026-01-31  
**Context:** Priority 4 from [2026-01-31_SEARCH_ALGORITHMS_RESEARCH_REVIEW.md](2026-01-31_SEARCH_ALGORITHMS_RESEARCH_REVIEW.md).

This note describes how **regex literal-prefix extraction** would plug into the current search flow without changing the rest of the logic.

---

## 1. Current flow for `rs:` (StdRegex)

### 1.1 One-off match: `MatchPattern()`

**File:** `src/search/SearchPatternUtils.h`

- `DetectPatternType(pattern)` → `PatternType::StdRegex` when pattern starts with `"rs:"`.
- `ExtractPattern(pattern)` strips the prefix → `regex_pattern`.
- Call: `std_regex_utils::RegexMatchStrict(regex_pattern, text, case_sensitive)`.

**File:** `src/utils/StdRegexUtils.h` – `RegexMatchStrict()`

- Compiles regex via `GetCompiledRegex(pattern, case_sensitive)` (cached).
- Runs `regex_match` or `regex_search` on the full text (with `RequiresFullMatch(pattern)`).
- No literal check: every call runs the full regex.

### 1.2 Hot path: matcher used in parallel search

**File:** `src/search/SearchPatternUtils.h` – `CreateMatcherImplStdRegex()`

- Called when building filename/path matchers for `PatternType::StdRegex`.
- Compiles regex once: `GetCache().GetRegex(regex_pattern, case_sensitive)`.
- Returns a lambda that, for each text:
  1. Converts text to `std::string` (e.g. `Traits::ConvertToStdString(text)`).
  2. Calls `regex_match(text_str, *compiled_regex)` or `regex_search(...)`.

So for every path in the index we: convert to string (if needed) and run the full regex. There is no “quick reject” using a literal prefix.

---

## 2. Where literal-prefix extraction fits

Idea: for many `rs:` patterns there is a **literal prefix** that must appear in the text for the regex to match (e.g. `^src/` → `"src/"`, `\\.(cpp|hpp)$` after `.*` has no prefix at *start*; `^project/.*\\.cpp` → `"project/"`). If we can compute that prefix once, we can:

1. **Reject quickly:** if the text does not contain the literal prefix, skip the regex.
2. **Run regex only on candidates:** when the prefix is found, run the full regex as today.

So the change is **add a pre-check** before running the regex; the rest of the logic (pattern type, rs: handling, compilation, cache, full match vs search) stays the same.

---

## 3. Integration points

### 3.1 New helper: extract literal prefix from regex pattern

**Place:** `src/utils/StdRegexUtils.h` (in `detail::` or as a small public helper).

**Signature (conceptual):**

```cpp
// Returns the longest literal prefix of the regex pattern, or empty if none.
// - Skip leading ^ (anchor).
// - Then append characters until the first regex metacharacter that can vary
//   (e.g. ., *, +, ?, [, (, {, |, or unescaped \ for escapes).
// - Backslash-escaped character (e.g. \.) counts as literal and extends the prefix.
// Minimum useful length is 2–4; caller can require prefix.size() >= 4.
std::string_view ExtractLiteralPrefix(std::string_view pattern);
```

**Rules (sketch):**

- Start at index 0. If pattern starts with `^`, start at 1.
- For each position: if it’s `\` and not end, treat next char as literal and advance by 2; else if it’s a literal (e.g. alphanumeric, `/`, `_`, etc.), append and advance; else stop (metacharacter: `.`, `*`, `+`, `?`, `[`, `(`, `{`, `|`, etc.).
- Return the substring from start to the current position (possibly empty).

So:

- `^src/.*\\.cpp` → skip `^`, then take `s`,`r`,`c`,`/` → `"src/"`.
- `.*\\.(cpp|hpp)$` → first char `.` is metacharacter → `""`.
- `^project/[a-z]+\\.cpp` → after `^` take `project/` → `"project/"`.
- `rs:^main\\.(cpp|h)$` (after stripping `rs:`) → `^main\\.` → after `^` take `m`,`a`,`i`,`n`, then `\\.` → literal dot → `"main."`.

No need to parse the full regex; a single left-to-right scan is enough for a conservative literal prefix.

### 3.2 Use in `RegexMatchStrict` (one-off match)

**File:** `src/utils/StdRegexUtils.h` – inside `RegexMatchStrict()`:

**Current:** compile → run regex on full text.

**With literal prefix:**

1. `literal_prefix = detail::ExtractLiteralPrefix(pattern)` (or equivalent).
2. If `literal_prefix.size() >= kMinLiteralPrefixLength` (e.g. 4):
   - If `case_sensitive`: if `!string_search::ContainsSubstring(text, literal_prefix)` return `false`.
   - Else: if `!string_search::ContainsSubstringI(text, literal_prefix)` return `false`.
3. Then: compile and run regex as today (no change to compilation or to full-match vs search).

So we only add an early-exit branch; we do not change when or how we call `regex_match` / `regex_search`.

### 3.3 Use in `CreateMatcherImplStdRegex` (hot path)

**File:** `src/search/SearchPatternUtils.h` – inside `CreateMatcherImplStdRegex()`:

**Current:** lambda captures `compiled_regex`, `requires_full_match`; for each text it converts to string and runs regex.

**With literal prefix:**

1. After extracting `regex_pattern` and before `GetRegex()` (or after):
   - `literal_prefix = std_regex_utils::ExtractLiteralPrefix(regex_pattern)` (returns a string view; we need to store a string so the lambda can own it).
   - Store it as `std::string literal_prefix_str(literal_prefix)` (or only if `literal_prefix.size() >= kMinLiteralPrefixLength`).
2. Lambda captures `literal_prefix_str` (or empty string when not used).
3. In the lambda, before converting text to string and running regex:
   - If `!literal_prefix_str.empty()`:
     - If case_sensitive: if `!string_search::ContainsSubstring(text_view, literal_prefix_str)` return `false`.
     - Else: if `!string_search::ContainsSubstringI(text_view, literal_prefix_str)` return `false`.
   - Then: same as today – convert to string (if needed) and run `regex_match` / `regex_search`.

So the matcher’s contract (same inputs, same result) is unchanged; we only add a fast path that skips regex when the text clearly cannot match.

---

## 4. Flow diagram (hot path)

```
CreateMatcherImplStdRegex(pattern, case_sensitive)
  │
  ├─ regex_pattern = ExtractPattern(pattern)   // strip "rs:"
  ├─ literal_prefix = ExtractLiteralPrefix(regex_pattern)
  ├─ if literal_prefix.size() >= 4  →  literal_prefix_str = std::string(literal_prefix)
  │   else                            →  literal_prefix_str = ""
  ├─ compiled_regex = GetCache().GetRegex(regex_pattern, case_sensitive)
  ├─ requires_full_match = RequiresFullMatch(regex_pattern)
  │
  └─ return lambda [literal_prefix_str, compiled_regex, requires_full_match, case_sensitive](text) {
        if (!literal_prefix_str.empty()) {
          if (case_sensitive) {
            if (!ContainsSubstring(text_view, literal_prefix_str)) return false;
          } else {
            if (!ContainsSubstringI(text_view, literal_prefix_str)) return false;
          }
        }
        text_str = ConvertToStdString(text);
        return requires_full_match ? regex_match(text_str, *compiled_regex)
                                   : regex_search(text_str, *compiled_regex);
      }
```

So literal-prefix extraction fits as a **single extra step at matcher creation** and a **single pre-check in the match lambda** (and similarly one pre-check in `RegexMatchStrict`). All existing logic (pattern type, rs:, cache, full match vs search, case sensitivity) stays in place.

---

## 5. Edge cases and limits

- **No prefix:** patterns like `.*\\.cpp` or `^.*foo` yield an empty prefix; we never use the fast path and behave exactly as today.
- **Short prefix:** e.g. 1–3 chars; we can require `literal_prefix.size() >= 4` (or 2) so the substring check is cheap and useful.
- **Prefix longer than pattern:** extraction stops at the first metacharacter, so the prefix is always a contiguous substring of the pattern; no out-of-bounds.
- **Unicode:** current substring/regex are byte-oriented; literal prefix can be byte-oriented the same way. No change to encoding contract.

---

## 6. Summary

| Current step | Change |
|--------------|--------|
| DetectPatternType / rs: | Unchanged. |
| ExtractPattern (strip rs:) | Unchanged. |
| RegexMatchStrict | Add: if ExtractLiteralPrefix(…) length ≥ N, run ContainsSubstring/ContainsSubstringI; if false return false. Then run regex as now. |
| CreateMatcherImplStdRegex | Add: compute and store literal prefix string; in lambda, if non-empty, run ContainsSubstring/ContainsSubstringI; if false return false. Then run regex as now. |
| GetRegex / cache, regex_match vs regex_search | Unchanged. |

So “regex literal-prefix extraction” fits as a **thin optional fast path** in front of the existing regex logic, both for one-off `MatchPattern` and for the StdRegex matcher used in the parallel search hot path.

---

## 7. PathPattern: same idea, already there

**Yes, the same idea is relevant for PathPattern** – and PathPattern **already implements it**.

**PathPatternMatcher.h / PathPatternMatcher.cpp:**

- At **compile time** (`CompilePathPattern`): the implementation extracts a **required substring** from the pattern (`req_substr_info.substring`) and sets:
  - `has_required_substring`
  - `required_substring_is_prefix` / `required_substring_is_suffix` (or general “contains”).
- At **match time** (`PathPatternMatches`): if `has_required_substring`, it runs **CheckRequiredSubstringForMatch** before the full match:
  - Prefix → path must start with the substring.
  - Suffix → path must end with the substring.
  - Else → path must contain the substring (ContainsSubstring-style check).

So PathPattern already does “extract a required literal (or literal run) from the pattern and reject quickly if the path doesn’t contain it.” The regex path does **not** have this yet; adding literal-prefix extraction to regex is the missing piece.
