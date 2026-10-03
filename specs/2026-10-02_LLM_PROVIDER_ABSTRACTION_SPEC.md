# Specification: LLM Search-Config Provider Abstraction

**Feature Name:** Generic AI Provider for Search-Config Generation (Gemini + OpenAI-compatible incl. opencode-go)
**Status:** Implemented and live-tested (opencode inference + Ollama) on `feature/llm-provider-abstraction`
**Date:** 2026-10-02
**Target Platforms:** macOS, Windows, Linux
**Relevant Components:** `src/api/GeminiApiUtils.*`, `src/api/GeminiApiHttp*`, `src/ui/SearchInputsGeminiHelpers.*`, `src/gui/WorkflowStates.h`, `src/gui/GuiState.h`, `src/core/Settings.h`, `tests/GeminiApiUtilsTests.cpp`
**Prior Art:** `internal-docs/plans/2026-02-02_AI_PROMPT_PROVIDER_CUSTOMIZATION_OPTIONS.md` (Options 1-4)

---

## 1. Executive Summary & Motivation

The AI Search feature hardcodes Google Gemini (`src/api/GeminiApiUtils.cpp:77`, `src/api/GeminiApiHttp_linux.cpp:45`, `src/api/GeminiApiHttp_win.cpp:156,171`, `src/api/GeminiApiHttp_mac.mm:25`). Only the clipboard copy/paste flow is provider-agnostic.

Goal: make one-click generation work with any provider via a small Strategy/Adapter abstraction, starting with `GeminiProvider` (existing behavior) + `OpenAiCompatibleProvider` (covers `opencode-go`, `opencode-zen`, OpenAI, Ollama). Prompt building (`BuildSearchConfigPrompt`, `src/api/GeminiApiUtils.cpp:160`) and inner-JSON parsing (`ParseSearchConfigJson`, `src/api/GeminiApiUtils.cpp:608`) stay common; only transport (URL/auth/body) and envelope extraction differ.

opencode-go reference (verified 2026-10-02, https://opencode.ai/docs/go/; inference paths per https://opencode.ai/v2/docs/console/inference/):
- `POST https://opencode.ai/inference/openai/v1/chat/completions` with `Authorization: Bearer $KEY`, body `{"model":"<id>","messages":[{"role":"user","content":prompt}]}` → `choices[0].message.content`. Auth: console service-account key; `x-opencode-session` stable id required by the gateway.
- `POST https://opencode.ai/inference/openai/v1/responses` for GPT/Grok/Muse-Spark variants; `.../inference/anthropic/v1/messages` for Claude/Qwen/MiniMax — out of scope except via config.
- Gateway differences (verified live 2026-10-03 with key): `inference/` serves free models to third parties without session header; legacy `zen/go/v1` gates free models (403 FreeTierError) and requires `x-opencode-session` (else MissingSessionID). Paid models can be transiently unavailable upstream (`server_error: Model is unavailable`) — retry or switch model.
- Free test models: `space-bunny-free`, `longcat-2.5-preview-free`.

---

## 2. Requirements

### 2.1 Functional
- FR1: User selects provider `gemini` (default, backward compat) or `openai-compatible` (opencode-go/zen, OpenAI, Ollama) in Settings/AI panel; one-click Generate uses it.
- FR2: `GEMINI_API_KEY` keeps working; new `OPENCODE_API_KEY` (or generic `AI_API_KEY`) supported via env var. No raw secrets in `findhelper_settings.json`.
- FR3: Same `SearchConfig` JSON contract (`version` + `search_config` with `path/filename/extensions/folders_only/files_only/case_sensitive/time_filter/size_filter`) regardless of provider.
- FR4: Validation preserved: prompt empty / >100KB (`kMaxPromptSize`, `src/api/GeminiApiUtils.cpp:24`), key empty, timeout 1-300s (`kMin/MaxTimeoutSeconds`), response >1MB (`kMaxResponseSize`, `src/api/GeminiApiHttp.h:13`).
- FR5: Clipboard Copy prompt / Paste JSON flows unchanged.

### 2.2 Non-functional / Constraints
- C++17 only; `(std::min)`/`(std::max)`; `std::scoped_lock`; `explicit` single-arg ctors; `[[nodiscard]]` on key/error getters; `std::string_view` read-only params; `return {…};` braced; explicit lambda captures in templates; `const` correctness.
- No `malloc/free/new/delete`; `std::unique_ptr/std::vector`; no C-casts.
- No edits inside `#ifdef _WIN32` / `__APPLE__` / `__linux__` to unify — new platform-agnostic `CallLlmHttpPlatform(url,headers,body,timeout)` with per-OS `.cpp/.mm` impls reusing WinHTTP/cURL/NSURLSession.
- `GuiState` single-writer rule: new fields extend substate (`FIELD OWNERSHIP MAP`, `src/gui/GuiState.h:26`); `GeminiWorkflowState` → `AiWorkflowState` with alias.
- UI-thread confinement: workers write staging only; `ApplySearchConfigResult` commits on UI thread. Async context struct captured by value.
- DRY: provider defaults in `settings_defaults` (`src/core/Settings.h:10`); no duplicated constants/URLs.
- Quality gates: `scripts/build_tests_macos.sh`, `scripts/pre-commit-clang-tidy.sh`, no new Sonar/clang-tidy issues; `// NOSONAR` same-line only with justification.

---

## 3. User Stories

| ID | P | Summary | Gherkin |
|----|---|---------|---------|
| US1 | P0 | Gemini still works by default | Given `aiProviderId` unset and `GEMINI_API_KEY` set, When user clicks Generate, Then `generativelanguage.googleapis.com` + `x-goog-api-key` path succeeds as before |
| US2 | P0 | opencode-go one-click | Given provider `openai-compatible`, base `https://opencode.ai/inference/openai/v1`, model `space-bunny-free`, `OPENCODE_API_KEY` set, When Generate, Then `POST .../chat/completions` with Bearer auth returns `SearchConfig` applied |
| US3 | P1 | Configurable endpoint/model/key | Given Settings `aiProviderId/aiBaseUrl/aiModel/aiApiKeyEnv`, When Generate, Then factory builds correct provider without code change |
| US4 | P1 | Bad key/network surfaces error | Given invalid key or timeout, When Generate, Then `error_message` shows `HTTP error ...` / `Network error ...`, no crash, future cleaned |
| US5 | P2 | Validation parity | Given empty/>100KB prompt or bad timeout, When Generate, Then same messages as `CallGeminiApiRaw` validation (`src/api/GeminiApiUtils.cpp:55`) |
| US6 | P2 | Clipboard interop | Given any provider, When Copy prompt → external AI → Paste JSON, Then `ParseSearchConfigJson` applies config |

---

## 4. Architecture Overview

L1 System Context: FindHelper desktop app → external LLM gateway (Google `generativelanguage.googleapis.com` or opencode `opencode.ai/inference/openai/v1`) → returns `SearchConfig JSON` → applied to `GuiState::searchCriteria` via `ApplySearchConfig` (`src/gui/GuiState.cpp:142`).

L2 Container: single desktop process; `SearchInputsGeminiHelpers::StartGeminiApiCall` (`src/ui/SearchInputsGeminiHelpers.cpp:64`) → `GenerateSearchConfigAsync` → platform HTTP (WinHTTP/cURL/NSURLSession) → `ParseSearchConfigJson` → UI commit. No new process/DB.

L3 Component (new `src/api/AiProvider/`, namespace `ai_provider`):
- `AiTypes.h`: `using SearchConfig = gemini_api_utils::SearchConfig; struct AiResult{bool success; std::string error_message; SearchConfig search_config;}; struct ProviderConfig{std::string provider_id; std::string base_url; std::string model; std::string api_key_env; int timeout_seconds;};`
- `ILlmProvider.h`: pure interface `BuildRequestBody(prompt)`, `BuildHeaders(key)`, `EndpointUrl(config)`, `ExtractText(response)->pair<bool,string>`, `EnvVarName()`, `ValidateInputs()` reusing `ValidateApiInputs`.
- `GeminiProvider.*`: moves `BuildGeminiRequestBody` (`src/api/GeminiApiHttp.h:47`), `ExtractTextFromCandidates` (`src/api/GeminiApiUtils.cpp:434`), `x-goog-api-key` header verbatim.
- `OpenAiCompatibleProvider.*`: `BuildOpenAiRequestBody(prompt,model)` with `EscapeJsonString`, `Authorization: Bearer`, `ExtractChoicesText` (`choices[0].message.content`, fallback `output_text` for `/responses` documented but not parsed in P2).
- `LlmHttp.h / LlmHttp_{win,linux,mac}.*`: `CallLlmHttpPlatform(url,headers,body,timeout)`; existing `CallGeminiApiHttpPlatform(prompt,key,timeout)` becomes thin wrapper delegating to it (removed in P4).
- `ProviderFactory.*`: `CreateProvider(provider_id)->unique_ptr<ILlmProvider>`, `ResolveConfig(AppSettings+env)->ProviderConfig`.
- `AppSettings` (`src/core/Settings.h:70`): add `aiProviderId="gemini"`, `aiBaseUrl=""`, `aiModel=""`, `aiApiKeyEnv=""` (empty = provider default; camelCase + NOLINT per file convention).
- State: `WorkflowStates.h:33` `struct AiWorkflowState` (same fields, `future<AiResult>`); `using GeminiWorkflowState = AiWorkflowState;` alias; `GuiState.h:192` field type swapped, ownership-map row renamed Gemini→AI workflow.

Data flow: `description_input` → `BuildSearchConfigPrompt(desc)` (unchanged) → `provider->BuildRequestBody/Headers/Url` → `CallLlmHttpPlatform` → `provider->ExtractText` → `ParseSearchConfigJson` (+ `FixPathPatternIfNeeded`) → `ApplySearchConfigResult`.

Patterns: Strategy/Adapter; RAII guards (`WinHttpHandleGuard`, cURL cleanup); `string_view` in, `string` out; in-class init; `strcpy_safe` N/A (no fixed buffers except existing 512B ImGui input kept).

Threading: `std::async(launch::async)` as today (`src/api/GeminiApiUtils.cpp:766`); capture `ProviderConfig+prompt+key` by value; `ProcessGeminiApiResult` (renamed `ProcessAiResult` with wrapper) `future.get()` on UI thread with `try/catch (exception + ...)` + future reset as in `src/ui/SearchInputsGeminiHelpers.cpp:175`.

---

## 5. Acceptance Criteria

| Story | Criterion | Measurable check |
|-------|-----------|------------------|
| US1 | No Gemini regression | Existing `GeminiApiUtilsTests` + `TestHelpers` pass; live Gemini smoke (manual, key set) returns config |
| US2 | opencode-go works | Unit: request body contains `model`+`messages`; extract from fixture `choices[0].message.content` → `ParseSearchConfigJson.success`; manual smoke with `space-bunny-free` |
| US3 | Config drives provider | Set `aiProviderId=openai-compatible`, custom base/model/env → factory unit test asserts URL/headers/body |
| US4 | Errors surfaced, no leak | `HTTP error 401/429` fixture → `success=false`, message prefixed; future reset (mirror `SafeGetAndCleanupFuture`) |
| US5 | Validation parity | `CallGeminiApiRaw`-equivalent empty/oversize/empty-key/timeout cases return same messages via provider `ValidateInputs` |
| US | Quality | `./scripts/build_tests_macos.sh` green; `pre-commit-clang-tidy.sh` 0 new; `find_class_struct_mismatches.py` clean; no new Sonar issues |
| US | DRY/naming | New types PascalCase, fns PascalCase, members `snake_case_`, ns `snake_case`, consts `kPascalCase`; constants only in `settings_defaults`/shared header |

---

## 6. Task Breakdown

| Phase | Task | Deps | Est. | Notes / tests |
|-------|------|------|------|---------------|
| P0 | Extract common: alias `AiResult`, keep `BuildSearchConfigPrompt`/`ParseSearchConfigJson` untouched, add `AiTypes.h` | — | 2h | Build `gui_state_tests`; no behavior change |
| P1 | Generalize HTTP: add `CallLlmHttpPlatform(url,headers,body,timeout)` in 3 OS files reusing bodies; `CallGeminiApiHttpPlatform` delegates | P0 | 4h | Existing network tests unaffected; manual Gemini smoke mac+linux (win via CI) |
| P2 | `GeminiProvider` + `OpenAiCompatibleProvider` + `ProviderFactory` + `ResolveConfig`; `OPENCODE_API_KEY`/`AI_API_KEY` env | P1 | 6h | New `tests/AiProviderTests.cpp` + `scripts/test_targets.txt`; doctest `-tc="*OpenAi*"`; no live calls in CI |
| P3 | `AppSettings.ai*` + persistence + AI panel dropdown + `StartAiCall(state, config)`; keep `StartGeminiApiCall` wrapper | P2 | 5h | `Settings` round-trip test; ImGui manual: switch provider, Generate, error paths |
| P4 | Rename `GeminiWorkflowState→AiWorkflowState`, `ProcessGeminiApiResult→ProcessAiResult` (+ compat wrappers), remove old `GeminiApiHttp*` wrappers, update help/whats-new | P3 | 4h | `grep Gemini` only in `GeminiProvider`; full `./scripts/build_tests_macos.sh`; clang-tidy full |
| P5 | Docs: `docs/` usage (providers, env vars, opencode-go models/quotas); `SearchHelpWindow.cpp` whats-new bullet with commit date | P4 | 1h | Keep last-month window |

Total ~22h. Each phase shippable; flag-gated by `aiProviderId` default `gemini`.

---

## 7. Risks & Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Response-shape drift (`/responses` vs `chat/completions`, Anthropic `/messages`) | Parse fail | P2 supports `chat/completions` only; `ExtractChoicesText` tries `choices[0].message.content` then `output_text`; log raw on fail (`LOG_DEBUG_BUILD` as today) |
| Secret leak in settings/logs | Credential exposure | Env-only keys; never log key; `GetEnvVarString` reuse; settings stores env name, not value |
| MSVC/WinHTTP header/body quirks (wide-string, DWORD casts) | Win-only break | Keep `BuildApiHeaders`/`ReadResponseBody` logic verbatim in generalized fn; existing NOSONAR/NOLINT preserved; CI win build |
| Async lifetime (stack ctx, future leak) | UAF/leak | Capture `ProviderConfig` by value (`[ctx]`); keep `SafeGetAndCleanupFuture` + `WaitForAll…` discipline; no `Cleanup…(false)` before drain |
| Scope creep (all 3 opencode endpoints at once) | Delay | Defer `/responses` + `/messages` to follow-up; config `base_url` allows manual override |
| Sonar S134/S2486/S3972, container `[]` | CI fail | Early returns, no empty catch, range-for, reserve before loops, hoisted scratch buffers |

---

## 8. Validation & Handoff

- Review checklist: C++17, naming table OK, `(std::min/max)`, `scoped_lock`, `explicit`, `string_view`, braced returns, `#endif // _WIN32` comments with platform blocks untouched, no `new/delete`/C-casts, no `[]` in hot paths without invariant, NOLINT same-line only.
- Implement per §6 phases; after each: `./scripts/build_tests_macos.sh --no-asan` (full + `--run-imgui-tests` at P3), `scripts/pre-commit-clang-tidy.sh`, `python3 scripts/find_class_struct_mismatches.py`, `./scripts/fetch_sonar_results.sh --open-only` on touch.
- Handoff prompt must cite: this spec as source of truth + `AGENTS.md` + `internal-docs/prompts/AGENT_STRICT_CONSTRAINTS.md` + `docs/standards/CXX17_NAMING_CONVENTIONS.md`; small steps; invariants from §5 as assertions; threading/failure modes from §4; KISS/YAGNI/DRY/SOLID.
- On ship: bullet in `src/ui/SearchHelpWindow.cpp` Whats-new with `git log -1 --format="%ad" --date=short` date; prune >1 month.
