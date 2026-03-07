# Agent Impact Analysis – Regression Prevention Prompt

**Date:** 2026-02-19  
**Purpose:** A specialized prompt for coding agents to systematically analyze the impacts of their changes and prevent hidden regressions. Use this in task prompts (e.g. under "Impact Analysis" or "Regression Prevention") or in agent instructions.

---

## Copy-paste block for prompts

```
## Impact Analysis / Regression Prevention

Before and during your changes, perform this impact analysis to avoid hidden regressions.

**Before changing code**
- Identify the full impact surface: list all call sites of the function(s) or type(s) you are modifying (search for the symbol name and for includes of the header). Do not change signatures, semantics, or return values without updating or verifying every caller.
- Trace data flow: who owns the data (caller vs callee), who reads/writes it, and on which thread. If you change ownership or threading, ensure all affected code paths are updated.
- Note platform-specific code: any #ifdef _WIN32, __APPLE__, __linux__ in or around the changed code must stay intentional. Do not alter code inside those blocks to "fix" cross-platform issues; use a platform-agnostic abstraction with separate implementations if you need unified behavior.
- Identify tests: which test files or test cases cover this code? Plan to run those tests (and any integration tests that use this path) after your change.

**During the change**
- Preserve API contracts: if a function has preconditions (e.g. "caller must hold lock", "path must not be empty"), document them and ensure all call sites still satisfy them. If it returns a value that callers rely on (e.g. success/failure), do not change the meaning without updating every caller.
- Preserve thread and ImGui rules: all ImGui calls must be on the main thread; background work must synchronize results back to the main thread. Do not add ImGui calls or UI state updates from background threads.
- Avoid silent behavior changes: do not change the meaning of "success", "failure", or sentinel values (e.g. size 0 vs "not loaded" vs "failed") without updating every place that interprets them. Prefer adding assertions in debug builds for invariants (e.g. "on success, size is never kFileSizeFailed").

**After the change**
- Re-verify call sites: after signature or semantic changes, confirm each call site still compiles and behaves correctly (correct arguments, correct handling of return value or output parameters).
- Re-run relevant tests: run the tests that cover the changed code (e.g. scripts/build_tests_macos.sh on macOS when C++/CMake changed). Fix any new failures before considering the task complete.
- Check for unintended side effects: no new allocations in hot paths unless justified; no new locks or cross-thread access that could deadlock or race; no changes to platform-specific blocks that alter platform intent.
```

---

## Detailed impact analysis checklist

Use this checklist when modifying code that could affect behavior beyond the immediate edit.

| Area | Question |
|------|----------|
| **Call sites** | Have I found every caller of the function/type I changed (including via macros, templates, or other translation units)? Do all call sites still pass correct arguments and handle return values/out parameters correctly? |
| **Data flow** | Who owns the data (caller-owned, callee-owned, shared)? Did I change lifetime or ownership? Are there use-after-free or double-free risks? |
| **Platform** | Did I change or add code inside #ifdef _WIN32 / __APPLE__ / __linux__? If yes, is the platform intent preserved, or did I introduce a platform-agnostic abstraction with separate implementations? |
| **Threading** | Does this code run on the main thread only (e.g. ImGui) or on a background thread? Did I add any ImGui calls or UI updates off the main thread? Did I add or change locks (risk of deadlock or lock ordering)? |
| **Contracts** | Are preconditions and postconditions (including success/failure and sentinel values) still satisfied at every call site? Did I document any new contract in code or comments? |
| **Tests** | Which tests cover this code? Did I run them after my change? Did I add or extend tests for new behavior or previously untested edge cases? |
| **Performance** | Did I add allocations, copies, or extra work in a hot path (e.g. search, index build, result rendering)? If yes, is it justified and documented? |
| **Dependencies** | Did I add or remove includes, forward declarations, or CMake entries? Do all affected targets still build and link correctly on all platforms? |

---

## When to apply

- **Always:** When changing a function signature, return type, or semantic meaning (e.g. what "success" or "0" means).
- **Always:** When touching platform-specific blocks (#ifdef _WIN32, etc.), threading (e.g. ImGui, background workers), or code that has explicit contracts (e.g. lazy loading, MFT, file attributes).
- **Recommended:** When refactoring across multiple files, when adding a new public API or type, or when modifying code that has a history of subtle bugs (see e.g. internal-docs/analysis/2026-02-02_LAZY_ATTRIBUTE_LOADING_REGRESSION_PREVENTION.md).

---

## Integration with existing prompts

- **With AGENT_STRICT_CONSTRAINTS:** Use the "Strict Constraints" block for what not to do (no new Sonar/clang-tidy violations, no platform block changes, etc.). Use this Impact Analysis block for *how* to reason about impact before and during the change. Together they reduce both style regressions and behavioral regressions.
- **With TaskmasterPrompt:** In "Anticipated Pitfalls", add: "Run impact analysis (call sites, data flow, platform, threading, tests) before and after changes; see docs/prompts/AGENT_IMPACT_ANALYSIS_REGRESSION_PREVENTION.md." In "Acceptance Criteria", add: "All call sites of changed symbols verified; relevant tests run and passing."
- **With AGENTS.md:** Add a short line in the "Workflow Summary" or "Questions to Ask Yourself": *"Did I perform impact analysis (call sites, data flow, platform, threading, tests, performance) for this change?"* and link to this document.

---

## Optional: one-line reminder

For minimal prompts or chat:

```
Impact analysis: Before/during change, identify all call sites and data flow; preserve platform #ifdef intent and ImGui main-thread rule; preserve API contracts and sentinel semantics; run relevant tests after change; avoid new hot-path allocations or lock changes without justification. See docs/prompts/AGENT_IMPACT_ANALYSIS_REGRESSION_PREVENTION.md.
```

---

## Project-specific notes

- **Cross-platform:** Main development is on macOS; primary target is Windows; secondary target is Linux. Build and tests are expected on all three. Impact analysis should consider all platforms when changing shared code or build/config.
- **ImGui:** All UI rendering and ImGui calls must happen on the main thread. Background tasks (search, index build, file I/O) must not call ImGui or update UI state directly; use thread-safe state and let the main loop read it.
- **Hot paths:** Search (ParallelSearchEngine, ExtensionMatches, result materialization), index build (InitialIndexPopulator, FileIndex, LazyAttributeLoader), and result table rendering are performance-sensitive. Extra allocations or work there must be justified.
- **Contracts and sentinels:** LazyAttributeLoader and MFT/initial index use "not loaded", "loaded" (including 0), and "failed". Changing how success/failure or size 0 is represented can cause silent regressions; see internal-docs/analysis/2026-02-02_LAZY_ATTRIBUTE_LOADING_REGRESSION_PREVENTION.md.
- **Windows/MSVC:** When editing code that compiles on Windows, follow AGENTS.md (e.g. (std::min)/(std::max), explicit lambda captures in templates, struct/class forward declaration match). Impact analysis does not replace those rules; it adds behavioral and call-site checks.
