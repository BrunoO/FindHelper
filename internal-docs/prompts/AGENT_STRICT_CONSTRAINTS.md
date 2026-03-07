# Agent Strict Constraints (Refined Prompt)

**Purpose:** A concise, high-impact list of non-negotiable constraints for AI agents so they follow instructions precisely and do not degrade code quality. Use this block in task prompts (e.g. under "Strict Constraints / Rules to Follow") or in agent instructions.

---

## Copy-paste block for prompts

```
## Strict Constraints / Rules to Follow

**Code quality**
- Do not introduce new SonarQube or clang-tidy violations. If a change would trigger a rule, fix the underlying issue (refactor, simplify, or align with project standards) instead of suppressing it.
- Apply preferred style so one fix satisfies both tools where rules overlap: use in-class initializers for default member values (Sonar S3230); use const T& for read-only parameters (Sonar S995/S5350); do not put "} if (" on one line — put the if on a new line or use else if (Sonar S3972). See AGENTS.md "Clang-Tidy and SonarQube Alignment".
- If you must add a suppression, use // NOSONAR only as a last resort, and place it exactly on the same line as the flagged code (e.g. the line with the cast, the loop, or the parameter). Never put // NOSONAR on a separate line above or below the issue.
- Do not add // NOLINT for checks that are already disabled in .clang-tidy (e.g. readability-identifier-length). Use NOSONAR for SonarQube only; use NOLINT for clang-tidy only.

**Performance**
- Do not introduce performance penalties: no extra allocations in hot paths, no new O(n) or worse work in loops already in critical paths, no unnecessary copies of large data. Prefer std::string_view, reserve(), and existing patterns used elsewhere in the codebase.

**Duplication**
- Do not duplicate logic. If the same or very similar code appears in more than one place, extract a single shared function or helper and reuse it (DRY). Flag existing duplication only if the current task scope allows; otherwise leave a short comment for a follow-up.

**Platform and Windows**
- Do not modify code inside #ifdef _WIN32, #ifdef __APPLE__, #ifdef __linux__ to make it "cross-platform" (e.g. changing backslashes to forward slashes). That code is intentionally platform-specific. To make behavior cross-platform, refactor via a platform-agnostic abstraction with separate implementations.
- When editing code that compiles on Windows: use (std::min) and (std::max) with parentheses; use explicit lambda captures in template functions (no [&] or [=]); match forward declarations to the definition (struct vs class). See AGENTS.md.

**Includes**
- Keep all #include directives in the top block of the file. Do not add includes in the middle of the file. Use lowercase <windows.h> for portability.

**Build and verification**
- Do not run build or compile commands (cmake, make, clang++, etc.) unless the task explicitly requires it. Use the project's scripts (e.g. scripts/build_tests_macos.sh on macOS) when the task specifies running tests.

**Verification**
- Before considering the task complete, confirm: (1) no new Sonar issues for the changed files, (2) no new clang-tidy warnings for the changed files (or that any new warnings are explicitly accepted by the task), (3) preferred style applied so both Sonar and clang-tidy are satisfied where rules overlap, (4) no new loops or allocations in performance-sensitive paths unless justified, (5) no new copy-pasted blocks that could be a shared helper, (6) no changes inside platform-specific #ifdef blocks that alter platform intent.

**Elegance checklist**
- Minimal: Is this the smallest change that achieves the goal?
- Clear: Can someone understand intent from names and structure without reading the whole function?
- Stable: Are existing callers and platform-specific blocks left intact unless they are the direct target?
- Focused: Does each new function/class have one clear responsibility?
- Concrete first: Is any new abstraction justified by actual duplication or complexity (not by imagined future use)?
```

---

## Elegance checklist (detailed)

When writing or reviewing a change, apply this checklist so code stays minimal, clear, and maintainable:

| Check | Question |
|-------|----------|
| **Minimal** | Is this the smallest change that achieves the goal? |
| **Clear** | Can someone understand intent from names and structure without reading the whole function? |
| **Stable** | Are existing callers and platform-specific blocks left intact unless they are the direct target? |
| **Focused** | Does each new function/class have one clear responsibility? |
| **Concrete first** | Is any new abstraction justified by actual duplication or complexity (not by imagined future use)? |

These align with AGENTS.md (SOLID, DRY, KISS, YAGNI, single responsibility). Use the checklist during implementation and before considering the task complete.

---

## Why this is more impactful

| Original idea | Refinement |
|--------------|------------|
| "Don't introduce Sonar violations" | Explicit: fix the cause first; use // NOSONAR only as last resort, and only on the same line as the issue. Now also: do not introduce clang-tidy violations; apply preferred style so one fix satisfies both tools (in-class init, const ref, no "} if (" on one line). |
| "No performance penalties" | Concrete: no extra allocations in hot paths, no worse complexity in critical loops, no unnecessary copies; points to existing patterns (string_view, reserve()). |
| "No code duplication" | Actionable: extract and reuse (DRY); if out of scope, comment for follow-up instead of leaving duplicated code. |
| "NOSONAR on the line with the issue" | Preserved and stated as a hard rule with an example. |
| "Verification" | Strengthened: confirm no new Sonar and no new clang-tidy warnings; confirm preferred style applied so both tools are satisfied; then the existing checklist (performance, duplication, platform). |

---

## Why these extra constraints were added

Based on project docs, AGENTS.md, and recurring issues:

- **Sonar and clang-tidy alignment:** Cleaning clang-tidy warnings without applying Sonar-aligned style led to new Sonar issues (S3230, S3972, S995, etc.). The constraint to "apply preferred style so one fix satisfies both" and the explicit rules (in-class init, const ref, no "} if (" on one line) prevent double work. See docs/analysis/2026-02-01_CLANG_TIDY_VS_SONAR_AVOID_DOUBLE_WORK.md and AGENTS.md "Clang-Tidy and SonarQube Alignment".
- **NOLINT vs NOSONAR:** NOLINT investigation showed redundant NOLINT for checks already disabled in .clang-tidy (e.g. readability-identifier-length). NOSONAR is for SonarQube only; NOLINT for clang-tidy only.
- **Platform-specific code:** AGENTS.md stresses that code inside `#ifdef _WIN32` / `__APPLE__` / `__linux__` is intentional; agents often "fix" it (e.g. backslashes → slashes) and break the build.
- **Windows/MSVC:** Production and Windows-build docs explicitly check (std::min)/(std::max), explicit lambda captures in templates, and forward declaration struct/class match. These are frequent causes of Windows build failures.
- **Includes:** Sonar S954 and AGENTS.md require all includes at the top; agents sometimes add includes mid-file. Lowercase `<windows.h>` is required for portability (S3806).
- **Build:** AGENTS.md marks "Never run build commands" as CRITICAL; agents should use project scripts when tests are required, not invoke cmake/make directly unless the task says so.
- **Verification:** Requiring "no new clang-tidy warnings" and "preferred style applied" ensures agents do not leave one tool clean and the other failing. Both must be satisfied before the task is complete.
- **Additional table:** Float `F`, `std::string_view`, C++17, ImGui popups, strcpy_safe, CMake, and .clang-tidy come from user rules and AGENTS.md; they are optional in the main block but useful to paste when the task touches those areas.

---

## Where to use

- **Taskmaster / task prompts:** Paste the "Copy-paste block" under **Strict Constraints / Rules to Follow** (or **Anticipated Pitfalls** if you want to stress what not to do).
- **AGENTS.md:** The project already has detailed Sonar, performance, and DRY guidance; this block is a short, agent-facing summary. You can add a line in AGENTS.md such as: *"When generating task prompts, include the Strict Constraints from docs/prompts/AGENT_STRICT_CONSTRAINTS.md."*
- **Cursor rules:** Use the same bullet points in a project or global rule so every agent run sees them.

---

## Additional constraints to consider (by task type)

Add these when the task touches the relevant area:

| When the task involves… | Add this constraint |
|---------------------------|----------------------|
| **Clang-tidy or Sonar cleanup** | Apply preferred style so both tools are satisfied: in-class initializers for default members (S3230); const T& for read-only parameters (S995/S5350); never put "} if (" on one line — use new line or else if (S3972). See AGENTS.md "Clang-Tidy and SonarQube Alignment". |
| **Float literals** | Use `F` suffix (e.g. `3.14F`), not `f`, to satisfy clang-tidy. |
| **String parameters** | Prefer `std::string_view` for read-only string parameters instead of `const std::string&`. Avoid explicit `std::string(view)` construction when the target already accepts `string_view` or has a C++17 `string_view` assignment operator — pass the view directly or use `.assign(view)`. |
| **`string_view` lifetime** | Never construct a `std::string_view` from a ternary where one branch produces a temporary `std::string` — the view dangles immediately. Use a named `static` fallback string instead. |
| **`const` locals** | Declare all non-mutating local variables `const` (IDs returned from lookups, path strings, bool results, lock guards). `misc-const-correctness` enforces this. |
| **`static` locals in loops** | Declare `static` local variables before the loop body, never inside it (SonarQube S3010 / clang-tidy style). |
| **C++ features** | Use only C++17 features; no older, no newer. The `llvm-use-ranges` clang-tidy check recommends `std::ranges::` algorithms which require C++20 — suppress with `// NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20` rather than applying the suggestion. |
| **Test files** | `tests/.clang-tidy` suppresses `cert-err33-c`, `readability-function-cognitive-complexity`, `readability-magic-numbers`, and `readability-identifier-naming` — do not add per-line NOLINTs for these in test files. |
| **ImGui popups** | `OpenPopup` and `BeginPopupModal` must be in the same window context; call `SetNextWindowPos` every frame before `BeginPopupModal`; close button outside `CollapsingHeader` with `SetItemDefaultFocus`. See AGENTS.md ImGui Popup Management. |
| **Fixed-size buffers / C APIs** | Use `strcpy_safe` / `strcat_safe` from `StringUtils.h`, not `strncpy`/`strcpy`/`strcat`. |
| **CMake / new source files** | Follow existing patterns in CMakeLists.txt; respect PGO and conditional blocks. See AGENTS.md "Modifying CMakeLists.txt Safely". |
| **.clang-tidy** | Do not put inline `#` comments on the same line as a check name in the Checks value; put rationale in a separate comment block. |

---

## Optional: one-line reminder

If you need a single line for chat or a minimal prompt:

```
Strict constraints: no new Sonar or clang-tidy violations (fix first; if suppression needed, // NOSONAR on the same line only). Apply preferred style so both tools are satisfied: in-class init, const ref, no "} if (" on one line. No performance regressions (no extra allocations/copies in hot paths); no duplication (extract and reuse). Do not change code inside platform #ifdef blocks; use (std::min)/(std::max) and explicit lambda captures for Windows. Do not run build unless the task requires it. Verify: no new Sonar/clang-tidy issues, preferred style applied, then performance/duplication/platform.
```
