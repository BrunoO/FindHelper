# Why Open Sonar Issues Persist Despite Rules — Guardrail Gaps

**Date:** 2026-02-03  
**Context:** Analysis of why SonarQube open issues are created despite AGENTS document rules and constraints. **Update:** Sonar MCP was retried successfully; real open issues were fetched and used to refine gaps and next steps.

---

## 0. Real Open Issues (as of 2026-02-03)

Fetched via Sonar MCP `search_sonar_issues_in_projects` for project `BrunoO_USN_WINDOWS`. **Total open: 10 issues** (500 issues returned in API, 490 CLOSED, 10 OPEN).

| Rule | Count | Severity | Message (summary) | Files |
|------|-------|----------|-------------------|--------|
| **cpp:S125** | 9 | MAJOR | Remove the commented out code. | IndexBuilder.h (1), SearchTypes.h (3), Settings.h (4), MftMetadataReader.h (1) |
| **cpp:S3229** | 1 | MINOR | Field "index_build_state_" is initialized after field "deferred_index_build_start_". | Application.cpp |

**Why these two rules?**

- **S125 (Sections of code should not be commented out):** Sonar flags commented-out code as a maintainability smell; AGENTS document has **no** rule or checklist item for "remove commented-out code." So these issues exist **because the rule was never in our guardrails**.
- **S3229 (Members should be initialized in the order they are declared):** Constructor initializer list order must match member declaration order (C++ requirement and C.47). AGENTS document documents in-class initializers (S3230) and constructor initializer lists but **does not** document "initializer list order must match declaration order." So this issue exists **because the rule was never in our guardrails**.

---

## 1. Why Open Sonar Issues Are Created Despite Our Rules

### 1.1 Two Pipelines, One Feedback Loop

| Where it runs        | Tool       | When developers see it     |
|----------------------|------------|----------------------------|
| Local / pre-commit   | clang-tidy | Before commit              |
| CI / after push      | SonarCloud | After push (or manual run) |

**Effect:** Code is written and committed against clang-tidy and AGENTS document. Sonar runs later on the same code. Issues that only Sonar reports (or that Sonar interprets differently) appear as "new" open issues even when the author followed documented rules.

### 1.2 Rules With No Local Enforcement

Several Sonar rules are documented in AGENTS document but have **no automated local check**:

| Sonar rule | AGENTS document coverage | Local enforcement |
|------------|--------------------|-------------------|
| **S3972** (`} if (` on one line) | Full subsection, examples | None (no clang-tidy equivalent) |
| **S3230** (in-class initializers) | Documented | clang-tidy `modernize-use-default-member-init` helps but scope/interpretation can differ |
| **S6004** (init-statement in `if`) | Documented, with "when NOT to apply" | No direct clang-tidy equivalent; easy to miss when not in "clean clang-tidy" scope |
| **S954** (include order) | Documented | `readability-include-order` in .clang-tidy; can be disabled or not run on all files |
| **S3806** (lowercase `<windows.h>`) | Documented | No dedicated clang-tidy check |

So **documentation exists, but enforcement is only on Sonar**. Until Sonar runs, these violations are invisible locally.

### 1.3 Scope of Work vs. Full Rule Set

Typical workflow:

- Task: "Fix clang-tidy in file X" or "Refactor Y for S107."
- Developer fixes only what the task (or clang-tidy) asks for.
- Sonar then reports other issues in the same file (e.g. S3972, S6004, S5350) that were out of scope.

So issues are "created" in the sense that **the same commit that fixed A can leave B and C open** — not because the developer broke a rule, but because B and C were never in the acceptance criteria for that change.

### 1.4 Justified Exceptions and NOSONAR

Some open issues are intentional:

- **NOSONAR** (e.g. UIRenderer.cpp S5350 for `MetricsWindow::Render`): Design requires non-const; issue may stay "open" until NOSONAR is applied and Sonar re-run.
- **Test code** (e.g. TestHelpers.h S134, FileIndexSearchStrategyTests S1181): Exceptions or nesting may be intentional; fixing or documenting (e.g. NOSONAR) can lag.
- **CTAD** (e.g. ParallelSearchEngineTests S6012): Simple fix, but must be done and re-analyzed.

So part of "open issues" is **delay between deciding the exception and re-running Sonar**, not missing guardrails.

### 1.5 Clang-Tidy and Sonar Interpret Differently

From `docs/analysis/2026-02-01_CLANG_TIDY_VS_SONAR_AVOID_DOUBLE_WORK.md`:

- Same idea, different rules: e.g. const ref (S995/S5350 vs `readability-non-const-parameter`). One tool can be satisfied and the other still report.
- Clang-tidy might not run on every file (e.g. globs, exclude lists), so Sonar sees files that were never checked locally.
- Suppressions (NOLINT) silence clang-tidy but do not affect Sonar; Sonar can still open an issue.

So **alignment between tools is partial**; satisfying one does not guarantee the other.

### 1.6 Rules Not in AGENTS document at All (Real Open Issues)

The **current 10 open issues** are all from two rules that **are not mentioned in AGENTS document**:

- **cpp:S125** (9 issues): "Remove the commented out code." — We have no guideline or checklist item for removing commented-out code. Internal fix plans listed S125, but it was never added to the main guardrails.
- **cpp:S3229** (1 issue): "Members should be initialized in the order they are declared." — We document in-class initializers (S3230) and constructor style but not "initializer list order must match declaration order" (C.47 / CERT OOP53-CPP).

So these issues were not created "despite" our rules; they exist **because these rules were never part of our documented guardrails**. Adding them to AGENTS document and the checklist (and, where possible, to clang-tidy or a pre-commit check) would prevent new occurrences.

---

## 2. What Is Missing in Our Guardrails

### 2.1 No Sonar (or Sonar-Equivalent) Before Push

- **Gap:** "Done" is defined as: clang-tidy clean + pre-commit pass. Sonar is not a gate for merge.
- **Result:** Open Sonar issues are discovered only after push (or manual scanner run).
- **Improvement:** Treat "no new open Sonar issues in changed files" as part of done (e.g. run `scripts/run_sonar_scanner.sh` on branch before merge, or add a CI step that fails on new issues in the diff).

### 2.2 Sonar-Only Rules Are Documentation-Only

- **Gap:** S3972, S3806, and partially S6004/S3230 have no local automated check. They rely on human checklist and Sonar.
- **Result:** These violations are found only when Sonar runs.
- **Improvement:**  
  - Option A: Run Sonar (or Sonar scanner) locally before push.  
  - Option B: Add a small script or pre-commit check that mimics the highest-impact Sonar-only rules (e.g. `} if (` on one line, include order, lowercase `<windows.h>`).

### 2.3 Checklist Is Manual and Not Tied to Sonar Rules

- **Gap:** AGENTS document "Questions to Ask Yourself" and Quick Reference are manual. There is no mapping "if you changed X, re-check Sonar rules Y and Z."
- **Result:** Easy to forget Sonar-only or rarely-hit rules when committing.
- **Improvement:** Add a short "Sonar rules most often missed" subsection (e.g. S3972, S6004, S3230, S5350, S954, S3806) with one-line reminders and link to this doc. Optionally, a script that lists Sonar rule keys and points to AGENTS document sections.

### 2.4 Not All Sonar Rules in the Checklist Are in the Quick Reference Table

- **Gap:** S108 (empty compound statement) is mentioned in AGENTS document (exception handling) but not in the Quick Reference table. Internal plans reference S5827, S1905, S3609, S1481, S125 — these may not all be in the main checklist.
- **Result:** Some rules are "documented somewhere" but not in the single place developers use before commit.
- **Improvement:** Audit Sonar rules that appear in internal plans and fix plans; add any missing ones to the Quick Reference and "Questions to Ask Yourself" so one checklist covers all rules we care about.

### 2.5 Task Prompts Don’t Require "No New Sonar Issues"

- **Gap:** Prompts like "clean clang-tidy in X" or "refactor Y" do not explicitly require "verify no new Sonar issues in the same files."
- **Result:** Tasks are closed when clang-tidy is clean; Sonar regressions are found later.
- **Improvement:** In task templates and prompts (e.g. in `docs/prompts/`), add: "After your change, ensure no **new** Sonar issues in the modified files (run scanner or check Sonar UI)."

### 2.6 Pre-Commit Runs Clang-Tidy Only

- **Gap:** Pre-commit runs clang-tidy (e.g. `scripts/pre-commit-clang-tidy.sh`); it does not run Sonar scanner or any Sonar-equivalent checks.
- **Result:** Developers get immediate feedback only for clang-tidy; Sonar feedback is delayed.
- **Improvement:** If running the full Sonar scanner in pre-commit is too slow, consider a lightweight "Sonar-equivalent" check (e.g. grep/script for `} if (`, include order, or a small subset of rules) so at least the most common Sonar-only issues are caught before commit.

### 2.7 No Single Source of Truth for "Which Sonar Rules We Care About"

- **Gap:** Sonar rules are scattered across AGENTS document, CLANG_TIDY_VS_SONAR doc, .clang-tidy comments, and various fix plans. There is no single list of "Sonar rule key → AGENTS document section → clang-tidy equivalent (if any)."
- **Result:** Hard to ensure every rule we care about has both documentation and, where possible, local enforcement.
- **Improvement:** Maintain a single mapping table (e.g. in this doc or in AGENTS document): rule key, short description, AGENTS document section, clang-tidy check if any, and "enforced by" (pre-commit / Sonar / manual).

### 2.8 S125 (Commented-Out Code) Not in AGENTS document — Confirmed by Real Open Issues

- **Gap:** Sonar rule **cpp:S125** ("Sections of code should not be commented out") is **not** mentioned in AGENTS document. All 9 open S125 issues (IndexBuilder.h, SearchTypes.h, Settings.h, MftMetadataReader.h) exist because we never documented or enforced this rule.
- **Result:** Commented-out code is left in place; Sonar reports MAJOR issues after push.
- **Improvement:** Add a short guideline in AGENTS document: "Remove commented-out code; use version control to retrieve if needed. Sonar S125." Add to Quick Reference and "Questions to Ask Yourself." Consider enabling a clang-tidy or script check for commented-out code if available.

### 2.9 S3229 (Member Initializer Order) Not in AGENTS document — Confirmed by Real Open Issues

- **Gap:** Sonar rule **cpp:S3229** ("Members should be initialized in the order they are declared") is **not** mentioned in AGENTS document. The single open S3229 issue (Application.cpp: index_build_state_ vs deferred_index_build_start_) exists because we never documented initializer-list order (C.47 / CERT OOP53-CPP).
- **Result:** Constructor initializer lists can be written in declaration order by habit, but reordering members or adding new ones can break order without any local check.
- **Improvement:** Add a short guideline in AGENTS document: "Constructor initializer list order must match member declaration order. Sonar S3229; see C++ Core Guidelines C.47." Add to Quick Reference. Clang-tidy has checks for member-initializer order; enable if available.

---

## 3. Recommended Next Steps

**Immediate (from real open issues):**

1. **Add S125 and S3229 to AGENTS document**  
   - **S125:** Add guideline "Remove commented-out code; use version control to retrieve if needed" and a row in the Sonar Code Quality / Quick Reference section. Add checklist question: "Did I remove or justify any commented-out code? (S125)"  
   - **S3229:** Add guideline "Constructor initializer list order must match member declaration order (C.47)" and a row in Quick Reference. Add checklist question: "Did I keep initializer list order matching declaration order? (S3229)"

2. **Fix the 10 current open issues**  
   - **S125 (9):** Remove commented-out code in IndexBuilder.h (1), SearchTypes.h (3), Settings.h (4), MftMetadataReader.h (1), or add justified NOSONAR on the same line with a short comment.  
   - **S3229 (1):** In Application.cpp, reorder the constructor initializer list so it matches member declaration order (index_build_state_ vs deferred_index_build_start_); fix declaration order or initializer list as needed.

3. **Map S125 and S3229 to clang-tidy (if available)**  
   Check whether clang-tidy has a check for commented-out code or member-initializer order; add to `.clang-tidy` and document in the Sonar rule mapping comment block.

**Workflow and guardrails:**

4. **Add "Sonar before merge" to workflow**  
   Require running the Sonar scanner (or CI Sonar step) on the branch and addressing new open issues (or justified NOSONAR) before merge.

5. **Document Sonar rules we enforce in one place**  
   In AGENTS document or this doc, keep a "Sonar rules we enforce" list that includes S125, S3229, S3972, S3806, and others, with one-line reminder and link to full section.

6. **Align full checklist with all Sonar rules we care about**  
   Audit rules referenced in plans and AGENTS document; ensure S108, S125, S3229, S5827, etc. are in Quick Reference and "Questions to Ask Yourself" so one checklist covers them.

7. **Optional: lightweight pre-commit Sonar check**  
   Script for high-impact patterns: `} if (`, commented-out code (S125), includes at top, lowercase `<windows.h>`.

8. **Task/prompt template**  
   Add to relevant prompts: "After your change, ensure no new Sonar issues in modified files (run scanner or check Sonar UI)."

**Local scanner vs SonarCloud:**

10. **Use the same configuration for local and SonarCloud**  
    Analysis scope and C++/Python settings are in `sonar-project.properties`; the scanner script no longer overrides them. See **`docs/guides/2026-02-03_CONFIGURING_LOCAL_SONAR_SCANNER_TO_MATCH_SONARCLOUD.md`** for why local and cloud can differ and how to align them (single source of truth, branch, compile_commands.json, quality profile).

**Ongoing:**

9. **Use Sonar MCP periodically**  
   Run `search_sonar_issues_in_projects` (or `scripts/get_sonarqube_issues.sh --open-only`) to refresh open-issue counts and rule distribution; add any newly appearing rules to AGENTS document and the checklist.

---

## 4. Summary

| Cause of open issues | Guardrail gap |
|----------------------|----------------|
| **S125, S3229 not in AGENTS document** (real open issues) | Rules never documented; 10/10 current open issues |
| Sonar runs after push; developers don’t see it before commit | No Sonar (or equivalent) in pre-push / merge gate |
| S3972, S3230, S6004, S954, S3806 have no or partial local check | Sonar-only rules are documentation-only |
| Tasks and prompts don’t require "no new Sonar issues" | Checklist and task scope don’t include Sonar |
| Different interpretation between clang-tidy and Sonar | Two pipelines; alignment only partial |
| NOSONAR or test exceptions not yet applied/verified | Delay between decision and re-run; not a missing rule |
| Some rules (S108, S125, S3229, etc.) not in main checklist | Not all Sonar rules in Quick Reference / single checklist |
| No single mapping of rules → docs → enforcement | Hard to verify guardrail coverage for every rule |

**As of 2026-02-03:** All 10 open Sonar issues are from two rules (S125, S3229) that are not in AGENTS document. Adding them to the guardrails and fixing the 10 issues will bring open issues to zero; then the workflow and checklist improvements above will reduce future regressions.
