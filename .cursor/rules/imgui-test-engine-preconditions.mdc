---
description: ImGui test engine regression tests — verify Set* was applied before triggering search
globs: "**/ui/ImGui*Test*.cpp,**/ui/*Regression*.cpp,**/ui/*Test*Engine*.cpp"
alwaysApply: false
paths:
  - "**/ui/ImGui*Test*.cpp"
  - "**/ui/*Regression*.cpp"
  - "**/ui/*Test*Engine*.cpp"
---

# ImGui Test Engine regression tests (precondition verification)

When editing regression tests that drive search via `IRegressionTestHook` (e.g. `RunRegressionTestCase` in `ImGuiTestEngineTests.cpp`), ensure **settings/params are verified** before triggering the search. Otherwise a test may run with wrong state and pass or fail for the wrong reason.

## Rule

After each `Set*` call that affects the next search, **assert that the corresponding getter matches** what was set, then trigger the search.

## Examples

- **Load balancing:** After `SetLoadBalancingStrategy(strategy)`, require `GetLoadBalancingStrategy() == strategy` (IM_CHECK) before `TriggerManualSearch()`.
- **Streaming:** After `SetStreamPartialResults(stream)`, require `GetStreamPartialResults() == stream` before triggering search.
- **Search params:** After `SetSearchParams(...)`, require `GetSearchParamFilename()`, `GetSearchParamPath()`, `GetSearchParamExtensions()`, `GetSearchParamFoldersOnly()` to match the values just set before triggering search.

## UI window tests

For Help, Settings, Metrics, Search Syntax: use **exact window title** comparison (`std::strcmp(win->Name, kExactTitle) == 0`). For optional UI (e.g. Metrics button), use a pre-check (e.g. `ItemExists(ref)`) to skip when the control is not available.

**Reference:** `src/ui/ImGuiTestEngineTests.cpp` (`RunRegressionTestCase`), `src/ui/ImGuiTestEngineRegressionHook.h`; AGENTS.md § ImGui Test Engine Regression Tests.
