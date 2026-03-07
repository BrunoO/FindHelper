# NOLINT / NOSONAR Investigation

**Date:** 2026-01-31  
**Purpose:** Identify dubious NOLINT/NOSONAR comments that could be removed if a better (project-wide) setting exists.

---

## Summary

| Category | Check / rule | Disabled in .clang-tidy? | Action |
|----------|----------------|---------------------------|--------|
| **readability-identifier-length** | clang-tidy | **Yes** (line 61) | Remove NOLINT when it is the **only** suppressed check; when combined with another check, keep only the other check (e.g. `readability-identifier-naming`). |
| **readability-redundant-inline-specifier** | clang-tidy | **Yes** (line 62) | No NOLINTs found in src/tests for this check; nothing to remove. |
| **llvm-header-guard** | clang-tidy | **Yes** (line 48) | One remaining: `resource.h` on `#ifndef RESOURCE_H`. Remove NOLINT; keep comment for .rc compatibility if desired. |
| **clang-diagnostic-error** | Compiler diagnostic | N/A (not a check) | **Keep** NOLINT. Cannot be disabled in .clang-tidy; occurs when headers are not found (e.g. without `-p build`). Script already excludes from counts. |
| **NOSONAR** (SonarQube) | Various cpp:S* | N/A | SonarQube is separate from clang-tidy; no .clang-tidy setting removes Sonar rules. **Keep** NOSONAR where intent is documented. |

---

## 1. NOLINT for checks already disabled in .clang-tidy

These checks are **disabled globally** in `.clang-tidy`, so suppressing them per-line is redundant.

### 1.1 `readability-identifier-length`

- **Disabled in .clang-tidy:** Yes (`-readability-identifier-length`, line 61).
- **Finding:** Many files use `// NOLINT(readability-identifier-length)` or `// NOLINT(readability-identifier-length,readability-identifier-naming)`.
- **Action:**
  - If the NOLINT **only** lists `readability-identifier-length`: **remove the entire NOLINT comment** (the check is off project-wide).
  - If the NOLINT lists **both** `readability-identifier-length` and another check (e.g. `readability-identifier-naming`): **remove only** `readability-identifier-length` from the list and keep the other check, e.g. `// NOLINT(readability-identifier-naming) - ...`.
- **Approx. count:** ~41 lines with NOLINT(readability-identifier-length) only; ~4 lines with it combined with readability-identifier-naming (PathStorage.h, PathOperations.h, PathStorage.cpp).

### 1.2 `readability-redundant-inline-specifier`

- **Disabled in .clang-tidy:** Yes (`-readability-redundant-inline-specifier`, line 62).
- **Finding:** No NOLINTs in `src/` or `tests/` reference this check.
- **Action:** None.

### 1.3 `llvm-header-guard` (remaining after #pragma once cleanup)

- **Disabled in .clang-tidy:** Yes (`-llvm-header-guard`, line 48).
- **Finding:** `src/platform/windows/resource.h` has `#ifndef RESOURCE_H  // NOLINT(llvm-header-guard) - Windows resource files require traditional header guards for .rc file compatibility`.
- **Action:** Remove `// NOLINT(llvm-header-guard) - ...`; optionally keep a plain comment: `// Windows resource files require traditional header guards for .rc file compatibility`.

---

## 2. NOLINT that should stay (no better setting)

### 2.1 `clang-diagnostic-error`

- **What it is:** Compiler diagnostic (e.g. "file not found" for `<array>`, `<string>`, `<atomic>` when clang-tidy runs without a compile_commands.json).
- **Why NOLINT is used:** Header-only or single-file analysis without `-p build` can trigger these; they are environment/tooling limitations, not code defects.
- **Better setting?** No. This is not a clang-tidy “check”; it cannot be turned off in the Checks list. The analysis script already excludes `clang-diagnostic-error` from warning counts.
- **Action:** **Keep** existing NOLINT for `clang-diagnostic-error` where it documents the known limitation.

### 2.2 NOSONAR (SonarQube rules)

- **What it is:** Suppression of SonarQube rules (e.g. cpp:S5028, cpp:S3806, cpp:S117).
- **Better setting?** No. SonarQube is configured in the Sonar project/quality profile, not in .clang-tidy. NOSONAR is the right way to suppress a specific rule at a specific line.
- **Action:** **Keep** NOSONAR comments where the intent is documented; no removal based on .clang-tidy.

### 2.3 Other NOLINTs (checks still enabled)

- **Examples:** `readability-identifier-naming`, `misc-non-private-member-variables-in-classes`, `cppcoreguidelines-pro-type-member-init`, `readability-redundant-member-init`, `cppcoreguidelines-avoid-const-or-ref-data-members`, `cppcoreguidelines-pro-bounds-pointer-arithmetic`, `cppcoreguidelines-init-variables`, `readability-magic-numbers`, `cppcoreguidelines-macro-usage`, `boost-use-ranges`, `performance-enum-size`, etc.
- **Better setting?** Only if we **disable** that check project-wide in .clang-tidy (e.g. like `readability-identifier-length`). That would be a policy decision and might hide real issues elsewhere.
- **Action:** No bulk removal; leave as-is unless we explicitly decide to disable a check globally and then remove its NOLINTs.

---

## 3. Code changes applied (2026-01-31)

1. **Removed** all standalone `// NOLINT(readability-identifier-length) ...` comments in src/ (check is disabled in .clang-tidy). Files: PathStorage.h, PathOperations.h, PathPatternMatcher.cpp, FileIndex.cpp, ResultsTable.cpp, EmptyState.cpp, SearchInputs.cpp, Popups.cpp, FileIndexStorage.cpp, TimeFilterUtils.cpp, PathStorage.cpp, PathOperations.cpp.
2. **Replaced** `// NOLINT(readability-identifier-length,readability-identifier-naming) ...` with `// NOLINT(readability-identifier-naming) ...` in PathStorage.h, PathOperations.h, PathStorage.cpp (drop the disabled check from the list).
3. **Removed** `// NOLINT(llvm-header-guard) ...` from `src/platform/windows/resource.h`; kept the comment "Windows resource files require traditional header guards for .rc file compatibility".

No change to:
- NOLINT(clang-diagnostic-error)
- NOSONAR(...)
- NOLINT for any check that remains **enabled** in .clang-tidy

---

## 4. References

- `.clang-tidy`: disabled checks list (lines 37–63).
- `AGENTS document`: "Modifying .clang-tidy Safely" and quick-ref rule for YAML.
- `docs/analysis/2026-01-31_CLANG_TIDY_STATUS.md`: current baseline and config/tooling notes.
