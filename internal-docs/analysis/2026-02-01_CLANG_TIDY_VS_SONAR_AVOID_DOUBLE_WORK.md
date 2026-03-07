commi# Why Clang-Tidy Fixes Introduced Sonar Issues — and How to Avoid Double Work

**Date:** 2026-02-01  
**Context:** Cleaning clang-tidy warnings (e.g. in SearchWorker, LoadBalancingStrategy, SearchHelpWindow) led to 27 new/open SonarQube issues. This doc explains the cause and how to prevent it.

---

## 1. Why This Happened

### Two separate pipelines, two rule sets

| Aspect | Clang-tidy | SonarQube |
|--------|------------|-----------|
| **When it runs** | Locally: pre-commit hook, `analyze_clang_tidy_warnings.py`, `run_clang_tidy.sh` | After push: SonarCloud (or `run_sonar_scanner.sh`) |
| **What developers see first** | Clang-tidy warnings | Sonar issues only after analysis on the server |
| **Rule set** | `.clang-tidy` (readability, modernize, etc.) | Sonar C++ rules (S995, S3230, S3972, S6004, …) |

So the workflow was:

1. **Goal:** “Clean clang-tidy warnings.”
2. **Action:** Fix or suppress only what clang-tidy reports (e.g. naming, init-statements, lock types).
3. **Result:** Code passes clang-tidy and pre-commit.
4. **Later:** Sonar runs on the same code and reports issues that clang-tidy never checked (or checks differently).

So “double work” is: fix for tool A, then fix again for tool B.

### Why the same edit can satisfy one tool and upset the other

- **Different rules for the same idea**  
  Both tools care about “good” style, but with different rules and wording. A change that clears a clang-tidy warning can still violate a Sonar rule (or the other way around).

- **Only one tool was in the loop**  
  Tasks were “clean clang-tidy” only. Sonar was not part of the acceptance criteria for those changes, so Sonar-only issues were never considered until after push.

- **Gaps and overlaps**  
  Some Sonar rules have no (or no direct) clang-tidy equivalent. Fixes aimed only at clang-tidy never address those Sonar rules, so they appear as “new” Sonar issues even though the code was just “cleaned” for clang-tidy.

---

## 2. Concrete Examples (from the 27 Sonar issues)

| Sonar rule | What Sonar wants | Why clang-tidy didn’t prevent it |
|------------|------------------|-----------------------------------|
| **S3230** | In-class initializers instead of constructor initializer list for default values | Clang-tidy doesn’t enforce this the same way; we fixed other clang-tidy warnings and left ctor initializer lists as-is. |
| **S3972** | `} if (` → put `if` on new line or use `else if` | Formatting/consistency rule; clang-tidy doesn’t have an equivalent “brace + if on same line” check. |
| **S6004** | Use init-statement in `if` when variable is only used there | Clang-tidy has init-statement checks, but they may be disabled or different; we didn’t change every such `if` when cleaning clang-tidy. |
| **S995 / S5350** | `const T&` for read-only parameters/variables | Clang-tidy has `readability-non-const-parameter`; if it wasn’t run on that code path or was suppressed, Sonar still reported. |
| **S5827** | Use `auto` instead of redundant type | Similar to clang-tidy’s `modernize-use-auto`; again, not applied everywhere clang-tidy was “cleaned,” or check not enabled. |

So: **fixing “clang-tidy” did not mean “fixing everything Sonar cares about.”** We only fixed what clang-tidy reported, in the files we touched.

---

## 3. How to Avoid Double Work

### 3.1 Single checklist: “Satisfy both” before commit

When changing code that will be analyzed by **both** tools:

- After making a fix, ask: “Could this introduce a Sonar issue even if clang-tidy is happy?”
- Prefer **one change that satisfies both** (e.g. add `const` to a parameter → good for clang-tidy and S995/S5350).
- Keep a short “Sonar vs clang-tidy” mapping (see below) so the same fix is chosen the first time.

### 3.2 Align rules and docs

- **AGENTS document / coding standards**  
  State the preferred style once (e.g. “use in-class initializers for default member values,” “read-only parameters are `const T&`,” “put `if` on its own line after `}`”). Then:
  - Configure clang-tidy to match where possible.
  - Rely on Sonar for the rest; no second, conflicting style.

  **Implemented:** AGENTS document now has a **"Clang-Tidy and SonarQube Alignment"** section (before Quick Reference) that states the three preferred styles and points to this doc and `.clang-tidy`. The **"If/Else Formatting (cpp:S3972)"** subsection documents the Sonar-only rule with examples.

- **Enable overlapping clang-tidy checks**  
  Where Sonar and clang-tidy cover the same idea, enable the clang-tidy check so both are satisfied by the same fix, e.g.:
  - `readability-non-const-parameter` ↔ S995/S5350  
  - Init-statement / in-class init ↔ S6004/S3230 (if available and not too noisy)  
  - `modernize-use-auto` ↔ S5827  

  **Implemented:** `.clang-tidy` already enables these via `*` (readability-*, modernize-*). The Sonar mapping comment block in `.clang-tidy` now explicitly lists S3972 (no equivalent) and S5827 (modernize-use-auto).

- **Document Sonar-only rules**  
  In the fix plan or AGENTS document, list Sonar rules that have **no** clang-tidy equivalent (e.g. S3972). For those, either:
  - Add a short guideline (“no `} if (` on one line”) and enforce in review, or  
  - Run Sonar (or a subset) locally so they’re caught before push.

  **Implemented:** S3972 is documented in AGENTS document under "Clang-Tidy and SonarQube Alignment" with a full "If/Else Formatting (cpp:S3972)" subsection (examples, when to apply, enforcement). The Quick Reference table includes a row for S3972.

### 3.3 Run Sonar (or a proxy) before push

- **Option A:** Run `run_sonar_scanner.sh` (or CI Sonar step) on a branch before merging, and treat open Sonar issues as part of “done” for that branch.
- **Option B:** Maintain a small “Sonar-equivalent” checklist (e.g. in a script or pre-commit) that catches the most common issues (S3230, S3972, S6004, const ref) so they’re fixed in the same pass as clang-tidy.

Either way, **both** tools are in the loop before “clean clang-tidy” is considered complete.

### 3.4 Task prompts: “Clang-tidy and Sonar”

When creating tasks like “clean clang-tidy in file X”:

- Add: “After fixing clang-tidy, ensure no **new** Sonar issues in the same files (check Sonar UI or run scanner).”
- For shared rules (const ref, init-statement, in-class init), state: “Apply project style so both clang-tidy and Sonar are satisfied.”

That way, “clean clang-tidy” implicitly means “don’t introduce Sonar regressions.”

---

## 4. Quick reference: Sonar ↔ clang-tidy (overlap)

| Sonar rule | Clang-tidy equivalent / note | Suggested approach |
|------------|-----------------------------|---------------------|
| S995, S5350 (const ref) | `readability-non-const-parameter` | Enable; one fix satisfies both. |
| S6004 (init-statement in `if`) | Init-statement / scope checks | Align style in AGENTS document; fix for both in one pass. |
| S3230 (in-class initializers) | No direct equivalent | Add to AGENTS document; when touching ctors, apply in-class init so Sonar is happy. |
| S3972 (`} if (` formatting) | None | Add guideline “new line or `else if`”; catch in review or local Sonar. |
| S5827 (use `auto`) | `modernize-use-auto` | Enable where acceptable; one fix for both. |

---

## 5. Summary

- **Why double work:** Two pipelines (clang-tidy local, Sonar after push) and two rule sets. We only fixed what clang-tidy reported, so Sonar-only or differently-interpreted rules showed up later.
- **How to avoid it:** (1) Prefer one change that satisfies both tools. (2) Align AGENTS document and .clang-tidy with Sonar where possible. (3) Run or mirror Sonar before considering “clang-tidy clean” done. (4) In task prompts, require “no new Sonar issues” after clang-tidy fixes.

Using this, “clean clang-tidy” and “no new Sonar issues” become one pass instead of two.
