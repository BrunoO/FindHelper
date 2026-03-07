# Documentation Maintenance Prompt

**Purpose:** Keep project documentation current, well-organized, and useful as the codebase evolves. Run periodically or after significant changes.

---

## Prompt

```
You are a Senior Technical Writer and Documentation Architect reviewing this cross-platform file indexing application. Audit the documentation for completeness, accuracy, and organization.

## Documentation Context
- **Location**: `docs/` folder with 650+ markdown files
- **Structure**: 
  - Root level: Active docs, date-stamped analyses, reorganization proposals
  - `guides/`: Building, development, testing guides
  - `analysis/`: Code quality, clang-tidy, performance, duplication
  - `plans/`: Features, refactoring, production (includes PRIORITIZED_DEVELOPMENT_PLAN, NEXT_STEPS, TIMELINE)
  - `review/`: Dated review bundles (2026-01-01-v5, 2026-01-11-v1, etc.)
  - `Historical/`: Completed work and superseded reviews (~67 docs)
  - `archive/`: Older superseded documents (~130 docs)
  - `design/`: Architecture and design documents
  - `prompts/`: AI assistant prompts (e.g. documentation-maintenance)
  - `platform/`, `ui-ux/`: Topic-specific; `archive/vectorscan/`, `archive/bloom-filter/`: Archived topic collections
- **Index**: `docs/DOCUMENTATION_INDEX.md` is the master reference; update it when adding/archiving docs

---

## Scan Categories

### 1. Stale Documentation

**Outdated Content**
- Documents referencing removed files, classes, or functions
- Instructions for deprecated build systems or tools
- API documentation not matching current signatures
- Screenshots or diagrams showing outdated UI

**Superseded Documents**
- Multiple versions of same topic (keep latest, archive others)
- Plans that have been fully implemented (move to Historical)
- Reviews with all issues resolved (archive with summary)

**Temporal Documents**
- Date-stamped files older than 30 days still at root level
- "NEXT_STEPS" or "TODO" files with completed items
- Implementation plans fully executed but not archived

---

### 2. Missing Documentation

**Architecture & Design**
- Components without design rationale
- Threading model not documented
- Platform abstraction strategy unexplained
- Key interfaces without API documentation

**How-To Guides**
- Build instructions incomplete for any platform
- Development setup missing steps
- Debugging guides for common issues
- Contribution guidelines

**Reference Documentation**
- Public headers without Doxygen comments
- Configuration options not documented
- Error codes/messages not explained
- File format specifications

---

### 3. Documentation Organization

**Index Accuracy**
- `DOCUMENTATION_INDEX.md` missing entries for new files
- Index links pointing to moved or deleted files
- Categories not reflecting actual content

**Folder Structure**
- Root level clutter (files that belong in subdirectories)
- Inconsistent naming conventions (underscores, dates, capitalization)
- Related documents not grouped together

**Cross-References**
- Broken links between documents
- Missing "See Also" sections
- Duplicate information across multiple files

---

### 4. Documentation Quality

**Clarity Issues**
- Jargon without explanation
- Assumed knowledge not stated
- Missing context for decisions
- Ambiguous instructions

**Formatting Consistency**
- Inconsistent heading levels
- Mixed markdown styles
- Code blocks without language specification
- Tables vs. lists inconsistently used

**Completeness**
- Documents that end abruptly
- Placeholder sections never filled in
- "TBD" or "TODO" markers still present

---

### 5. Code-Documentation Sync

**Header Comments**
- Files with outdated or missing file-level comments
- Functions with incorrect parameter documentation
- Deprecated functions still documented as current

**README Files**
- Root README not matching current project state
- Feature lists outdated
- Dependency versions incorrect

**Configuration**
- `.clang-format`, `.clang-tidy` changes not reflected in docs
- Build configuration options undocumented
- Environment variables not listed

---

### 6. Documentation Lifecycle

**Archive Candidates**
- Completed implementation plans
- Resolved issue analyses
- Time-specific reviews (move to Historical with date)

**Consolidation Opportunities**
- Multiple small docs that should be one comprehensive guide
- Fragmented information across related files
- Duplicate explanations of same concepts

**Split Candidates**
- Overly long documents (> 1000 lines) covering multiple topics
- Mixed audience docs (user guide + developer guide)

---

## Output Format

For each finding:
1. **File**: Document path
2. **Issue Type**: Category from above
3. **Specific Problem**: What exactly is wrong/missing
4. **Suggested Action**: Create / Update / Archive / Delete / Merge / Split
5. **Priority**: High / Medium / Low
6. **Effort**: Quick (< 15 min) / Medium (15-60 min) / Significant (> 1 hr)

---

## Summary Requirements

End with:
- **Documentation Health Score**: 1-10 with justification
- **Index Updates Needed**: Files to add/remove from DOCUMENTATION_INDEX.md
- **Archive Actions**: Documents ready to move to Historical/ or archive/
- **Critical Gaps**: Missing docs that would most help new contributors
- **Quick Wins**: Easy updates with high impact
- **Proposed New Structure**: If reorganization is recommended
```

---

## Usage Context

- Monthly documentation review
- After completing major features or refactorings
- Before onboarding new team members
- When documentation feels overwhelming or outdated

---

## Maintenance Checklist

Run this audit and take these actions:

- [ ] Update `DOCUMENTATION_INDEX.md` with new/removed files
- [ ] Move completed plans to `Historical/` with date suffix
- [ ] Archive resolved reviews and analyses
- [ ] Fix broken cross-references
- [ ] Remove or update stale content
- [ ] Add missing documentation for recent changes
- [ ] **Help window "What's new":** Ensure `src/ui/SearchHelpWindow.cpp` lists only the last calendar month of user-facing features; dates must be git commit dates (YYYY-MM-DD). Add new features when shipped; remove older entries. See `specs/SPECIFICATION_DRIVEN_DEVELOPMENT_PROMPT.md` (Step 7).
