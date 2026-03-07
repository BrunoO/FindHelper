# 2026-01-31 PathPatternMatcher.cpp:933 – cpp:S3776 (Complexity 47) Fix Plan

## Objective

Reduce cognitive complexity of `MatchFrom` (line 933) from 47 to under 25 (Sonar limit) without introducing:
- New Sonar violations
- Performance penalties (no extra call in hot path where avoidable; use `static inline` for helpers)
- Code duplication
- NOSONAR except on the exact line of the issue when justified

## Current Structure of MatchFrom (lines 933–1043)

1. **Base case:** `if (atom_index == pattern.atom_count) return path_index == path.size();`
2. **Current atom:** `const Atom& atom = pattern.atoms[atom_index];`
3. **Empty kStar block:** `if (atom.kind == AtomKind::kStar) { }`
4. **Unbounded branch (lines 949–1014):** `if (atom.IsUnbounded()) { ... return false; }`
   - Satisfy min_count (while loop)
   - Greedy pass for max_consumed (while loop with greedy_done)
   - Backtracking for-loop trying `MatchFrom(atom_index + 1, ...)` from max to min consumed
5. **Bounded branch (lines 1016–1042):**
   - While loop to satisfy min_count
   - While loop to extend to max_count, trying `MatchFrom` at each step
   - Final `MatchFrom` attempt and `return false`

## Strategy: Extract Two Helpers (Same TU, Inline)

### 1. MatchFromUnbounded (new, before MatchFrom)

- **Signature:** `static inline bool MatchFromUnbounded(const Pattern& pattern, unsigned atom_index, std::string_view path, unsigned path_index)`
- **Content:** Move the entire current `if (atom.IsUnbounded()) { ... }` body (lines 950–1014) into this function. Inside, use `const Atom& atom = pattern.atoms[atom_index];` and keep all logic unchanged.
- **Recursion:** Calls `MatchFrom(pattern, atom_index + 1, path, current_index)` — so `MatchFrom` must be declared before this helper. Use a forward declaration: `bool MatchFrom(const Pattern& pattern, unsigned atom_index, std::string_view path, unsigned path_index);` before the helpers.
- **Placement:** Define after forward declaration of `MatchFrom`, before `MatchFrom` definition.
- **Complexity:** Unbounded helper will still have non-trivial complexity (loops + recursion). If Sonar flags it, consider sub-extracting only the backtracking for-loop into a small helper (e.g. `TryUnboundedBacktrack`) so both stay under 25.

### 2. MatchFromBounded (new, before MatchFrom)

- **Signature:** `static inline bool MatchFromBounded(const Pattern& pattern, unsigned atom_index, std::string_view path, unsigned path_index)`
- **Content:** Move the bounded-repetition block (lines 1016–1042): two while loops and the final `MatchFrom` attempt. Use `const Atom& atom = pattern.atoms[atom_index];` at the start.
- **Recursion:** Calls `MatchFrom(pattern, atom_index + 1, path, current_index)` (and `MatchFrom(..., current_index)` at the end). Same forward declaration as above.
- **Placement:** Define after `MatchFromUnbounded`, before `MatchFrom`.

### 3. MatchFrom (refactored)

- **After refactor:**
  - Base case (unchanged).
  - `const Atom& atom = pattern.atoms[atom_index];`
  - `if (atom.kind == AtomKind::kStar) { }` (unchanged, empty).
  - `if (atom.IsUnbounded()) return MatchFromUnbounded(pattern, atom_index, path, path_index);`
  - `return MatchFromBounded(pattern, atom_index, path, path_index);`
- **Remove:** The existing NOLINT for `readability-function-cognitive-complexity` on line 932 once complexity is under 25.

## Order of Definitions in File

1. Forward declaration: `bool MatchFrom(const Pattern& pattern, unsigned atom_index, std::string_view path, unsigned path_index);` (in `path_pattern` namespace, near other MatchFrom/MatchAtomOnce declarations if any, or immediately before the new helpers).
2. `static inline bool MatchFromUnbounded(...)` — full unbounded body.
3. `static inline bool MatchFromBounded(...)` — full bounded body.
4. `bool MatchFrom(...)` — thin dispatcher as above.

## Performance and NOSONAR

- **Performance:** Helpers are `static inline` in the same .cpp as `MatchFrom`. They are in the recursive hot path; inlining keeps the call graph equivalent to the current single function, so no extra call overhead when optimized.
- **NOSONAR:** Do not add NOSONAR for S3776 on `MatchFrom` after the refactor. If a helper still exceeds complexity 25, reduce it by further extraction (e.g. backtrack loop only) rather than suppressing. If any other rule (e.g. S924, S134) is triggered on a specific line and cannot be fixed without harming readability/correctness, use `// NOSONAR(rule)` on that exact line only.

## Verification

- Run `scripts/build_tests_macos.sh` (or project test script).
- Run `path_pattern_matcher_tests` and `path_pattern_integration_tests` (and any test that uses path pattern matching).
- Optionally run search benchmark with path-pattern config to confirm no performance regression.
- Re-run Sonar (or fetch results) and confirm cpp:S3776 is resolved for PathPatternMatcher.cpp line 933 and no new issues appear.

## Summary Checklist

- [x] Add forward declaration of `MatchFrom` before helpers.
- [x] Add `MatchFromUnbounded` with unbounded block; keep logic identical.
- [x] Add `MatchFromBounded` with bounded block; keep logic identical.
- [x] Refactor `MatchFrom` to 4-branch dispatcher; remove complexity NOLINT.
- [x] Ensure no new Sonar issues, no duplication, no performance regression (removed redundant `static` per Sonar; helpers are `inline` in anonymous namespace).
- [x] Use NOSONAR only on the exact line of an issue if unavoidable.
