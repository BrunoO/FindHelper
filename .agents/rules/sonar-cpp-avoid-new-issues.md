---
description: Avoid introducing new SonarQube (and clang-tidy) issues in C++ code
globs: "**/*.cpp,**/*.h,**/*.hpp"
alwaysApply: false
paths:
  - "**/*.cpp"
  - "**/*.h"
  - "**/*.hpp"
---

# Do not introduce Sonar / clang-tidy issues

When adding or modifying C++ code, follow project rules so new SonarQube and clang-tidy issues are not introduced.

## Before committing

- The pre-commit hook runs clang-tidy on **all staged files** and blocks the commit on
  any warning. Do not bypass with `--no-verify`: fix the warning or add a same-line
  NOLINT with justification (see `.agents/rules/suppressions-same-line.md`). When the
  hook flags pre-existing warnings in files you merely touched, fix them too (Boy Scout
  rule) — partial clean-ups of a staged file still count as new issues in review.
- Run **`scripts/fetch_sonar_results.sh --open-only`** for current open issues; avoid
  adding the same rule types. Note SonarCloud's snapshot is usually older than HEAD;
  verify each finding in the current code before fixing (many are already fixed but
  not yet re-scanned).
- Run **clang-tidy** on changed files: `scripts/clang-tidy-wrapper.sh -p . <files...>` or
  `scripts/pre-commit-clang-tidy.sh` / `scripts/run_clang_tidy.sh`.

## Sonar vs clang-tidy

- **`// NOSONAR(rule-id)` and NOLINT are clang-tidy-side only for NOLINT**; Sonar only
  reads `// NOSONAR(rule-id)` markers. They silence different tools — an NOLINT does
  NOT hide the issue from Sonar, and a NOSONAR does not silence clang-tidy. When a
  check fires in both tools, both markers are needed (comma-separate multiple NOLINT
  checks in one `// NOLINT(a,b)`).
- Sonar computes Cognitive Complexity independently: the
  `// NOLINTNEXTLINE(readability-function-cognitive-complexity)` comment is NOT enough
  to close cpp:S3776. Refactor (extract helpers) instead.

## Reference

- **Full Sonar/quality rules:** `docs/standards/SONAR_CPP_RULES_REFERENCE.md`
- **Quick checklist:** AGENTS.md § Quick Reference
- **Const / init-statement:** `.agents/rules/const-correctness-cpp.md`, `.agents/rules/cxx17-init-statement.md`

## Critical patterns

1. **Nesting (cpp:S134):** Max 3 levels. Use early returns and extracted functions; no deep `if`/`for` chains.
2. **Exceptions (cpp:S2486):** No empty `catch` blocks. Log and/or rethrow.
3. **Const:** Use `const T&` for read-only parameters; mark non-mutating members `const` (Sonar S995/S5350, clang-tidy `readability-non-const-parameter`).
4. **Parameters (cpp:S107):** ≤7 parameters; group into structs if more.
5. **Arrays:** Prefer `std::array` / `std::vector` over C-style arrays (cpp:S5945).
6. **Complexity (cpp:S3776):** Cognitive complexity ≤25 per function; extract helpers
   when over the limit — target the largest loop/phase in a big function first.

When in doubt, open the reference doc or AGENTS.md Quick Reference and apply the rule that matches your change.
