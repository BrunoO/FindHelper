---
description: Keep crash-fix work focused and evidence-driven
globs: "**/*.cpp,**/*.h,**/*.hpp,**/*.cc,**/*.cxx,**/*.md,**/*.yml,**/*.yaml"
alwaysApply: true
paths:
  - "**/*.cpp"
  - "**/*.h"
  - "**/*.hpp"
  - "**/*.cc"
  - "**/*.cxx"
  - "**/*.md"
  - "**/*.yml"
  - "**/*.yaml"
---

# Crash Triage Discipline

- For active crash investigations, keep each commit tied to one hypothesis and one concrete risk.
- Prefer minimal targeted fixes before broad refactors.
- Separate unrelated CI/tooling/chore changes from crash-fix branches unless blocking.
- After each fix, record outcome (repro still fails/passes, stack/dump change, new evidence).
- If adding diagnostic infrastructure (e.g., crash dumps), treat it as baseline reliability work.

## Commit Hygiene

- Commit message should state: fault model + safety mechanism added.
- Avoid mixed-purpose commits during incident response.
