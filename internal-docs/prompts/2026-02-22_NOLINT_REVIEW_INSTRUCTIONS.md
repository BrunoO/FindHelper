# 2026-02-22 — NOLINT Review Task Instructions

Instructions for a subordinate AI agent to review all `// NOLINT` and `// NOLINTNEXTLINE` usages in the project (excluding `external/`), assess relevance against the current `.clang-tidy` configuration, and produce a detailed list of recommendations. No code changes are to be made; the deliverable is a markdown recommendations document only.

---

## Part 1: Summary

**Mission:** Produce a single, detailed markdown document that lists every NOLINT in project code (`src/`, `tests/`), classifies each as (1) still relevant, (2) obsolete (check disabled in config), or (3) worth fixing (recommend removing NOLINT and changing code), and gives concrete recommendations. The goal is to identify obsolete suppressions and opportunities to fix underlying issues instead of suppressing.

**Scope:** All files under `src/` and `tests/` that contain `NOLINT` or `NOLINTNEXTLINE`. Exclude `external/` and any generated or third-party paths. Do not modify any source files; output is the recommendations document only.

**Constraints:** Use `.clang-tidy` as the single source of truth for which checks are enabled or disabled. Follow AGENTS.md and docs/prompts/AGENT_STRICT_CONSTRAINTS.md for interpretation (e.g. prefer fixing over suppressing). Do not run builds or compile; read-only analysis only.

**References:** `.clang-tidy` (Checks and Disabled Checks Rationale), AGENTS.md, and the project files containing NOLINT.

***

## Part 2: Final Prompt (for subordinate agent)

```markdown
### Mission Context
The project uses `// NOLINT` and `// NOLINTNEXTLINE` to suppress specific clang-tidy checks where the team has decided suppression is appropriate. Over time, the `.clang-tidy` configuration has changed: some checks have been disabled globally, so NOLINTs that only suppress those checks are obsolete and can be removed. Elsewhere, fixing the underlying code (instead of suppressing) may be beneficial. This task is to review every NOLINT in project code, classify it, and produce a detailed recommendations document in markdown—without changing any code.

### Core Objective
Produce one markdown document that (1) lists every NOLINT/NOLINTNEXTLINE in `src/` and `tests/`, (2) for each, states whether it is still relevant, obsolete (check disabled), or worth fixing, and (3) gives a clear recommendation (e.g. remove NOLINT, keep, or fix code with brief rationale).

### Desired Outcome
- A single markdown file (e.g. `docs/analysis/2026-02-22_NOLINT_REVIEW_RECOMMENDATIONS.md`) containing:
  - Brief scope and method (how you determined relevance and recommendations).
  - List of checks disabled in `.clang-tidy` that can make NOLINTs obsolete.
  - Per-file or grouped entries: file path, line number(s), check name(s), current justification, classification (obsolete / still relevant / worth fixing), and recommendation.
  - Summary tables or counts: total NOLINTs, obsolete count, “worth fixing” count, “keep” count.
- No edits to source or config files.

### Visual Workflow (Mermaid)

    ```mermaid
    graph TD
        A[Start] --> B[Load .clang-tidy disabled checks list]
        B --> C[Grep NOLINT in src/ and tests/]
        C --> D[For each NOLINT occurrence]
        D --> E{Check name in disabled list?}
        E -->|Yes| F[Classify: Obsolete; recommend remove NOLINT]
        E -->|No| G{Would fixing code be beneficial?}
        G -->|Yes, low risk| H[Classify: Worth fixing; recommend fix]
        G -->|No or API/platform constraint| I[Classify: Still relevant; recommend keep]
        F --> J[Append to recommendations]
        H --> J
        I --> J
        J --> K{More NOLINTs?}
        K -->|Yes| D
        K -->|No| L[Write summary and counts]
        L --> M[Save markdown document]
        M --> N[End]
    ```

### The Process / Workflow

1. **Extract disabled checks from .clang-tidy**
   - Read the `Checks: >-` block and the "Disabled Checks Rationale" section.
   - Build a set of check names (or patterns) that are explicitly disabled (e.g. `-clang-analyzer-*`, `-readability-identifier-length`). Any NOLINT that only suppresses one or more of these checks is obsolete.

2. **Collect all NOLINT occurrences in project code**
   - Run `grep -rn 'NOLINT' src/ tests/` (or equivalent). Exclude paths under `external/`.
   - For each match, record: file path, line number, full NOLINT text (including check names and comment).

3. **Classify each NOLINT**
   - Parse the check name(s) from the NOLINT (e.g. `NOLINT(check-one,check-two)` or `NOLINTNEXTLINE(...)`).
   - **Obsolete:** Every listed check is in the disabled list (e.g. only `clang-analyzer-deadcode.DeadStores` and that family is disabled). Recommendation: remove the NOLINT line or the NOLINT part of the line; no code change needed for the check.
   - **Still relevant:** At least one check is enabled and the comment justifies suppression (e.g. ImGui API, COM, C API, platform requirement). Recommendation: keep as-is.
   - **Worth fixing:** At least one check is enabled and the underlying issue could be fixed without harming API/platform constraints (e.g. simplify boolean, add init, use structured binding). Recommendation: describe the fix briefly (e.g. "Initialize with `{}`", "Apply De Morgan to simplify") and recommend removing NOLINT after the fix.

4. **Write the recommendations document**
   - Use markdown with clear sections: Scope and method, Disabled checks (obsolete NOLINTs), Per-file or grouped recommendations, Summary tables.
   - For "obsolete" and "worth fixing" entries, be specific (file, line, check name, recommendation).
   - For "keep" entries, you may group by file or by check type and note "keep; reason" briefly.

5. **Add summary**
   - Total NOLINT occurrences reviewed (excluding external/).
   - Count and list of obsolete NOLINTs (recommend remove NOLINT only).
   - Count and list of "worth fixing" (recommend fix code then remove NOLINT).
   - Count of "keep" (no action).

### Anticipated Pitfalls

- **Multiple check names in one NOLINT:** If a NOLINT lists both a disabled check and an enabled check (e.g. `NOLINT(clang-analyzer-*,cppcoreguidelines-init-variables)`), it is not fully obsolete; either remove only the disabled check from the NOLINT or keep if the enabled check still fires.
- **NOLINTNEXTLINE:** The suppression applies to the next line; record the line that is suppressed (next line), not only the line the NOLINT is on.
- **external/ and tests in external:** Do not include files under `external/` in the review; they are third-party. Include only `src/` and `tests/` (project tests).
- **Interpretation of “worth fixing”:** Prefer conservative: if fixing would require large refactors or might break platform/API contracts, classify as "keep" rather than "worth fixing."

### Acceptance Criteria / Verification Steps

- [ ] Every NOLINT in `src/` and `tests/` (excluding external) appears in the recommendations document.
- [ ] Each entry has a classification: obsolete, still relevant, or worth fixing.
- [ ] Obsolete NOLINTs are clearly tied to a check that is disabled in `.clang-tidy`.
- [ ] "Worth fixing" entries include a short, actionable recommendation (what to change in code).
- [ ] Summary counts (total, obsolete, worth fixing, keep) are correct.
- [ ] No source or config files were modified.

### Strict Constraints / Rules to Follow

- **Do not change any code or configuration.** The only deliverable is the markdown recommendations file.
- Use `.clang-tidy` as the authority for which checks are enabled or disabled.
- Do not run build or compile commands.
- Exclude `external/` from the set of files to review.
- When in doubt about "worth fixing," classify as "keep" and state that review is recommended.

### Context and Reference Files

- `.clang-tidy` — enabled/disabled checks and rationale.
- `AGENTS.md` — project rules (NOLINT on same line, prefer fix over suppress).
- `docs/prompts/AGENT_STRICT_CONSTRAINTS.md` — strict constraints (no new violations, same-line suppression).
- All files under `src/` and `tests/` that contain the string `NOLINT`.

### Concluding Statement
Proceed with the task. Produce the single markdown recommendations document and save it under `docs/analysis/` with a date prefix (e.g. `2026-02-22_NOLINT_REVIEW_RECOMMENDATIONS.md`). Do not modify any source or config files.
```
