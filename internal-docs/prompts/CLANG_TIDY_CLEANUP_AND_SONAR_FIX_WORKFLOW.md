# Clang-Tidy Cleanup and Sonar Fix Workflow

**Purpose:** Step-by-step instructions for an AI agent to clean clang-tidy warnings (goal: 0 warnings) and fix resulting Sonar issues, with strict constraints and SonarCloud polling.

**Reference:** Follow the constraints in `docs/prompts/AGENT_STRICT_CONSTRAINTS.md` for all code and verification steps.

---

## Full workflow (execute in order)

### Step 1: Commit and push (if not already done)

- If there are uncommitted changes, commit them with a clear message and push to the remote.
- If the working tree is clean and already pushed, skip this step.

---

### Step 2: Pick the next target file and clean clang-tidy warnings

- **Goal:** Reach 0 clang-tidy warnings in the codebase.
- **Action:** Choose the next file to clean (e.g. a file that still has clang-tidy warnings, in an order you or the project defines).
- **Constraints (from `docs/prompts/AGENT_STRICT_CONSTRAINTS.md`):**
  - Do not introduce new SonarQube or clang-tidy violations; fix the underlying issue rather than suppressing.
  - Apply preferred style so one fix satisfies both tools where rules overlap: in-class initializers for default members (Sonar S3230); `const T&` for read-only parameters (Sonar S995/S5350); do not put `} if (` on one line — use a new line or `else if` (Sonar S3972). See AGENTS.md "Clang-Tidy and SonarQube Alignment".
  - If you must suppress: use `// NOSONAR` only as a last resort, on the **same line** as the flagged code; use `// NOLINT` for clang-tidy only, and only for checks that are **enabled** in `.clang-tidy`.
  - No extra allocations in hot paths, no new O(n) or worse work in critical loops, no unnecessary copies; prefer `std::string_view`, `reserve()`, and existing patterns.
  - Do not duplicate logic; extract and reuse (DRY).
  - Do not modify code inside `#ifdef _WIN32` / `__APPLE__` / `__linux__` to make it “cross-platform”; use platform-agnostic abstractions if needed.
  - Keep all `#include` directives in the top block; use lowercase `<windows.h>`.
- Clean the chosen file of clang-tidy warnings while respecting the above.

---

### Step 3: Run tests

- Run the project’s test script (e.g. on macOS: `scripts/build_tests_macos.sh`) to ensure the build and tests pass.
- Do not run raw build commands (cmake, make, clang++, etc.) unless the task explicitly requires it; use the project scripts when the task says to run tests.

---

### Step 4: Commit and push the cleanup

- Commit the clang-tidy cleanup changes with a clear message and push to the remote.

---

### Step 5: Fix Sonar issues resulting from the changes

SonarCloud may need a few minutes to analyze the push. To get the **current open issues** (and avoid guessing when analysis is done), use the Sonar MCP and poll as follows.

#### 5.1 Get the list of open issues (polling)

1. **Call the Sonar MCP tool**  
   Use the tool **`search_sonar_issues_in_projects`** (Sonar MCP server: `user-sonarqube-cloud`) with:
   - `projects`: `["BrunoO_USN_WINDOWS"]`
   - `ps`: `200` (or larger if the project has many issues)

2. **Poll interval**  
   Wait **45 seconds** between two consecutive calls.  
   Do not call more often than once every 30 seconds.

3. **Stability condition**  
   Treat the analysis result as **stable** when, for **two consecutive** calls (each after the 45 s wait):
   - The **total number of issues** returned is the same, and  
   - The **list of issue keys** (the `key` field of each issue) is identical (same set of keys; order may be ignored).  
   When both hold, treat analysis as complete and use this response as the current issues.

4. **Timeout**  
   Stop polling after **5 minutes** (e.g. after about 6–7 calls) even if the result is not stable. Use the **last** response as the current issues.

5. **What to use**  
   From the stable (or last) response:
   - Use the **`issues`** array.
   - Filter to issues with **`status` == `"OPEN"`** if the API returns non-open issues.
   - Use this list as the **current open Sonar issues** to fix (especially those that relate to the file(s) you just changed).

#### 5.2 Fix the issues

- Fix any open Sonar issues that result from or relate to the changes you made (and optionally other open issues in scope).
- Respect the same constraints as in Step 2 (no new violations, preferred style, no performance/duplication/platform regressions). See `docs/prompts/AGENT_STRICT_CONSTRAINTS.md`.

#### 5.3 Verify and commit

- Run tests again to ensure nothing is broken.
- Commit and push the Sonar fixes.

---

## Verification (before considering the task complete)

Confirm:

1. No new Sonar issues for the changed files.
2. No new clang-tidy warnings for the changed files (or any new warnings are explicitly accepted).
3. Preferred style applied so both Sonar and clang-tidy are satisfied where rules overlap.
4. No new performance regressions, duplication, or inappropriate changes inside platform `#ifdef` blocks.

See the “Verification” and “Elegance checklist” sections in `docs/prompts/AGENT_STRICT_CONSTRAINTS.md`.
