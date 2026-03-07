# ImGui Test Engine – Helper Extraction Study

**Date:** 2026-02-23

This document studies whether extracting helpers from `ImGuiTestEngineTests.cpp` would reduce code duplication and complexity, and recommends concrete extractions.

---

## 1. Current duplication and complexity

### 1.1 UI window tests (help, settings, search_help, metrics)

**Pattern repeated in four tests (~35 lines each):**

1. `ctx->SetRef(kMainWindowTitle); ctx->Yield();`
2. **Precondition** (3 of 4): `if (!ctx->ItemExists(button_ref)) { IM_ERRORF("Precondition failed: ..."); IM_CHECK(ctx->ItemExists(button_ref)); }`  
   Metrics: `if (!ctx->ItemExists(...)) return;` (skip)
3. `ctx->ItemClick(button_ref);`
4. **Wait loop** (identical):  
   `for (int attempt = 0; attempt < kMaxWindowWaitAttempts; ++attempt) { ctx->Yield(); win = ctx->GetWindowByRef(window_title); if (!win) win = ctx->GetWindowByRef("//$FOCUSED"); if (win && std::strcmp(win->Name, window_title) == 0 && !win->Hidden) break; win = nullptr; }`
5. **Assert window** (identical):  
   `if (win == nullptr || win->Hidden) IM_ERRORF(error_msg); IM_CHECK(win != nullptr); IM_CHECK(!win->Hidden);`
6. **Teardown** (identical):  
   `ctx->SetRef(win); ctx->WindowClose(""); ctx->Yield(); ctx->Yield();`

**Duplication:** ~25 lines of logic repeated 4 times (wait loop + assert + teardown). Precondition block repeated 3 times (with one variant for skip).

**Complexity:** Each test is a long lambda; AGENTS.md “Keep Lambdas Short (cpp:S1188)” suggests extracting to named functions when > ~20 lines.

---

### 1.2 Regression / load_balancing / streaming registration

**Pattern:**

- **Regression (6 tests):**  
  `const auto& c = kRegressionCases[i]; RunRegressionTestCase(ctx, c.case_id, c.filename, c.path, c.extensions, c.folders_only, c.expected);`
- **Load balancing (18 tests):** Same plus strategy `"static"` / `"hybrid"` / `"dynamic"`.
- **Streaming (12 tests):** Same plus `stream_partial_results` true/false.

**Duplication:** 36 nearly identical lambdas that only differ by case index and optional strategy/stream. The *logic* is already shared (`RunRegressionTestCase`); the duplication is in the **registration** (one lambda per test).

**Constraint:** ImGui Test Engine expects `TestFunc` to be a function pointer (non-capturing). So we cannot use a single lambda that captures `(case_index, strategy, stream)`; we need either one function per test or a way to bind indices/options at compile time (e.g. templates).

---

## 2. Candidate helpers

### 2.1 UI window tests

| Helper | Purpose | Effect |
|--------|--------|--------|
| **RequireItemExists(ctx, item_ref, precondition_msg)** | Run `ItemExists`; if false, `IM_ERRORF(precondition_msg)` and `IM_CHECK(ctx->ItemExists(item_ref))`. | Removes 4–5 line precondition block from 3 tests. |
| **ImGuiWindow* WaitForFloatingWindowAfterClick(ctx, button_ref, window_title, error_msg)** | Call `ItemClick(button_ref)`, run the wait loop (by title then `//$FOCUSED`, validate `win->Name`), then `IM_ERRORF` + `IM_CHECK` if not found; return `win`. | Single place for wait + validation (~15 lines); each test loses the duplicated loop and assert. |
| **CloseFloatingWindowAndYield(ctx, win)** | `ctx->SetRef(win); ctx->WindowClose(""); ctx->Yield(); ctx->Yield();` | Single place for teardown; each test loses 4 lines. |

**Resulting test shape (example: help_window_open):**

```cpp
t_help->TestFunc = [](ImGuiTestContext* ctx) {
  ctx->SetRef(kMainWindowTitle);
  ctx->Yield();
  RequireItemExists(ctx, kHelpButtonRef, "Precondition failed: Help button \"...\" not found. ...");
  ImGuiWindow* win = WaitForFloatingWindowAfterClick(ctx, kHelpButtonRef, kHelpWindowTitle, kHelpWindowOpenErrorMsg);
  CloseFloatingWindowAndYield(ctx, win);
};
```

**Metrics** stays slightly different (skip when button missing, then same flow):

```cpp
if (!ctx->ItemExists(kMetricsButtonRef)) return;
ImGuiWindow* win = WaitForFloatingWindowAfterClick(ctx, kMetricsButtonRef, kMetricsWindowTitle, kMetricsWindowOpenErrorMsg);
CloseFloatingWindowAndYield(ctx, win);
```

**Relevance:** High. Removes ~80 lines of duplicated logic, shortens lambdas (S1188), and keeps behavior and error messages unchanged.

---

### 2.2 Smoke test

The smoke test is short and already uses a single precondition check. **No helper extraction recommended**; adding a “RequireMainWindowVisible(ctx)” would only hide a few lines and would not remove meaningful duplication.

---

### 2.3 RunRegressionTestCase

`RunRegressionTestCase` is already the single place for regression logic (wait index, check size, set/verify strategy and streaming, set/verify params, trigger search, wait complete, two yields, assert count). It is long but coherent; splitting it further would scatter related steps. The existing NOLINT for cognitive complexity is acceptable for “test orchestration.” **No further split recommended.**

---

### 2.4 Regression / load_balancing / streaming registration (templates)

**Idea:** Replace 36 lambdas with template functions so each test is registered with a function pointer to a template instantiation.

**Regression (6 tests):**

```cpp
template<int CaseIdx>
static void RegressionTestFunc(ImGuiTestContext* ctx) {
  const auto& c = kRegressionCases[CaseIdx];
  RunRegressionTestCase(ctx, c.case_id, c.filename, c.path, c.extensions, c.folders_only, c.expected);
}
// Registration:
ImGuiTest* r0 = IM_REGISTER_TEST(engine, "regression", kRegressionCases[0].case_id);
r0->TestFunc = &RegressionTestFunc<0>;
// ... r1..r5
```

**Load balancing (18 tests):** Same idea with a second template parameter for strategy. Non-type template parameter must be a constant with linkage, e.g.:

```cpp
static constexpr const char* kStrategyStatic = "static";
static constexpr const char* kStrategyHybrid = "hybrid";
static constexpr const char* kStrategyDynamic = "dynamic";

template<int CaseIdx, const char* Strategy>
static void LoadBalancingTestFunc(ImGuiTestContext* ctx) {
  const auto& c = kRegressionCases[CaseIdx];
  RunRegressionTestCase(ctx, c.case_id, c.filename, c.path, c.extensions, c.folders_only, c.expected, Strategy);
}
// lb_s_0->TestFunc = &LoadBalancingTestFunc<0, kStrategyStatic>; etc.
```

**Streaming (12 tests):** Template with `bool StreamOn`:

```cpp
template<int CaseIdx, bool StreamOn>
static void StreamingTestFunc(ImGuiTestContext* ctx) {
  const auto& c = kRegressionCases[CaseIdx];
  RunRegressionTestCase(ctx, c.case_id, c.filename, c.path, c.extensions, c.folders_only, c.expected, {}, StreamOn);
}
```

**Relevance:** Medium. Pros: removes 36 nearly identical lambda bodies; one template per category. Cons: less obvious at a glance which case index maps to which test name; relies on non-type template params (strategy pointer); some projects prefer explicit lambdas for test readability. **Recommendation:** Optional refactor. If we do it, do it after the UI-window helpers so the file is not changed in two big steps at once.

---

## 3. Summary and recommendations

| Area | Extract? | Helpers / approach |
|------|----------|--------------------|
| **UI window tests** | **Yes** | `RequireItemExists`, `WaitForFloatingWindowAfterClick`, `CloseFloatingWindowAndYield`. Reduces duplication and lambda length (S1188). |
| **Smoke test** | No | Already short; no meaningful duplication. |
| **RunRegressionTestCase** | No | Already single place for regression flow; splitting would scatter steps. |
| **Regression/LB/streaming registration** | **Optional** | Template functions `RegressionTestFunc<I>`, `LoadBalancingTestFunc<I, Strategy>`, `StreamingTestFunc<I, StreamOn>` to replace 36 lambdas. |

**Suggested order:** Implement the three UI window helpers first; then consider template-based registration for regression/load_balancing/streaming if we want to reduce registration boilerplate further.

---

## 4. Files to touch

- **ImGuiTestEngineTests.cpp:** Add the three static helpers (near the top, after `WaitForSearchComplete`); refactor the four UI window tests to use them; optionally add template test functions and refactor regression/lb/streaming registration.
- **docs/analysis/2026-02-23_IMGUI_TEST_ENGINE_PRECONDITIONS.md:** No change required; precondition behavior and guarantees stay the same.
