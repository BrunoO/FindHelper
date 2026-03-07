# SonarQube Open Issues – Fix Plan (Refreshed)

**Date:** 2026-02-01  
**Source:** SonarQube Cloud (project BrunoO_USN_WINDOWS)  
**Last refreshed:** 2026-02-01  
**Total open issues:** **27** (per SonarQube UI — use UI count as authoritative; API search may differ by branch/status/pagination)

This plan outlines a path to **0 Sonar issues** quickly, following AGENTS document and `docs/prompts/AGENT_STRICT_CONSTRAINTS.md` (fix root cause first; NOSONAR only as last resort, on the same line).

---

## 1. Input: 27 open issues (SonarQube UI)

Use the **SonarQube UI** (project BrunoO_USN_WINDOWS, Issues, status Open) as the source of truth for the current list. When refreshing this plan:

1. Open the project in SonarQube Cloud and note the exact **open issue count** and **branch** (e.g. `main`).
2. Optionally run MCP `search_sonar_issues_in_projects` with the same **branch** and **status OPEN** to get machine-readable list; if the API total differs from the UI, trust the UI count (27).
3. Update the sections below with the actual 27 issues (by rule, file, severity) so the fix plan targets the real set.

---

## 2. Summary by severity (update from UI)

| Severity  | Count (update from UI) |
|-----------|-------------------------|
| CRITICAL  | ?                       |
| MAJOR     | ?                       |
| MINOR     | ?                       |
| **Total** | **27**                  |

---

## 3. Rules and files (fill from UI)

From the SonarQube UI, list the **27 issues** by rule and by file, for example:

**By rule (example; replace with actual):**

| Rule     | Count | Fix strategy (AGENTS document) |
|----------|-------|---------------------------|
| (fill)   |       | Mechanical or refactor    |

**By file (example; replace with actual):**

| File     | Count |
|----------|-------|
| (fill)   |       |

---

## 4. Path to 0 – recommended order (for ~27 issues)

1. **Mechanical first:** Init-statement (S6004), in-class initializers (S3230), const ref (S995/S5350), scoped_lock (S5997/S6012), merge nested if (S1066), unused param (S1172), redundant type/cast (S5827/S1905), nodiscard (S3609/S1481), empty statement (S125). Fix root cause; no NOSONAR unless necessary, on the same line.

2. **Refactors:** Nesting (S134), cognitive complexity (S3776), exception handling (S1181/S2738), too many parameters (S107), C-style array (S5945), if/else format (S3972).

3. **Remaining:** Any other rule — fix root cause first; NOSONAR only as last resort, on the same line.

---

## 5. Execution tips

- **Batch by rule** for mechanical fixes; **by file** for refactors.
- After each batch: run `scripts/build_tests_macos.sh`; re-check Sonar UI to confirm open count decreases.
- Follow `docs/prompts/AGENT_STRICT_CONSTRAINTS.md`: no new violations; NOSONAR only when fixing is impractical, on the same line.

---

## 6. Checklist (update from UI; tick as done)

- [ ] List 27 issues from UI (rule, file, line, message).
- [ ] Fix mechanical issues (S6004, S3230, S995/S5350, S5997/S6012, S1066, S1172, etc.).
- [ ] Fix refactors (S134, S3776, S1181/S2738, S107, S5945, S3972).
- [ ] Fix remaining rules.
- [ ] Sonar UI: 0 open issues.

---

## 7. References

- **AGENTS document:** Include order, init-statement, in-class initializers, const correctness, exception handling, nesting (S134), cognitive complexity (S3776), NOSONAR placement.  
- **Strict constraints:** `docs/prompts/AGENT_STRICT_CONSTRAINTS.md`  
- **SonarQube:** Project BrunoO_USN_WINDOWS. Use the **UI** (Issues, Open) for the authoritative count (27). MCP `search_sonar_issues_in_projects` can list issues; if its total differs from the UI, trust the UI.
