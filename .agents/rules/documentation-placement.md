---
description: Where to put docs (docs/ vs internal-docs/ vs specs/) and when to update the index
globs: "**/*.md,docs/**/*.md,internal-docs/**/*.md,specs/**/*.md"
alwaysApply: false
paths:
  - "**/*.md"
  - "docs/**/*.md"
  - "internal-docs/**/*.md"
  - "specs/**/*.md"
---

# Documentation placement

When creating or moving documentation, use the correct folder and keep the index up to date.

## Folders

| Folder | Use for |
|--------|--------|
| **docs/** | External contributors: build guides, architecture, standards, platform notes. Must be stable and public. |
| **internal-docs/** | Maintainer-only: prompts, dated analysis, plans, review notes, historical/archive. |
| **specs/** | Formal feature specs (specification-driven development). |

**Decision:** “Would a first-time external contributor need this to build or contribute?” → **Yes** = `docs/`, **No** = `internal-docs/` (or `specs/` for feature specs).

## Index

When you add, move, or rename documents under `docs/`, update **`docs/DOCUMENTATION_INDEX.md`** (paths, status, category).

**Reference:** `docs/plans/production/PRODUCTION_READINESS_CHECKLIST.md` (Phase 10: Documentation Organization); AGENTS.md § Documentation Placement Rules.
