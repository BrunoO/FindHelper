---
description: NOSONAR and NOLINT must be on the same line as the flagged code; fix root cause first
globs: "**/*.cpp,**/*.h,**/*.hpp"
alwaysApply: false
paths:
  - "**/*.cpp"
  - "**/*.h"
  - "**/*.hpp"
---

# Suppressions: same line and last resort

When Sonar or clang-tidy flags code and you cannot fix the root cause, use suppressions **only as a last resort** and **on the same line** as the flagged code.

## Same line only

- **NOSONAR:** Place `// NOSONAR(rule-id)` (or `// NOSONAR(cpp:S1234)`) on the **same line** as the code that triggers the finding. Sonar only recognizes it on that line.
- **NOLINT:** Place `// NOLINT(check-name)` on the **same line** as the flagged code. Use only for checks that are **enabled** in `.clang-tidy`; do not add NOLINT for checks already disabled globally.

```cpp
// ❌ WRONG — suppression on separate line
// NOSONAR(cpp:S5350)
void Render(MetricsWindow* window);

// ✅ CORRECT — same line
void Render(MetricsWindow* window);  // NOSONAR(cpp:S5350) - MetricsWindow::Render requires non-const; see design
```

## Fix first, suppress last

1. Prefer fixing the underlying issue (refactor, const, extract function, etc.) so the finding goes away.
2. If fixing is impractical or would break a required design, add the suppression on the **same line** with a short justification.

**Reference:** AGENTS.md; `internal-docs/prompts/AGENT_STRICT_CONSTRAINTS.md`; user rule: "when adding // NOSONAR comments, write them on the same line as the SonarQube issue to disabled."
