# Specification: Responses-API Provider (`openai-responses`)

**Feature Name:** OpenAI Responses-protocol adapter for Search-Config Generation
**Status:** Draft Specification (future implementation)
**Date:** 2026-10-03
**Target Platforms:** macOS, Windows, Linux
**Relevant Components:** `src/api/AiProvider/OpenAiCompatibleProvider.*`, `src/api/AiProvider/LlmHttp.*`, `src/api/AiProvider/ProviderFactory.*`, `src/api/AiProvider/AiTypes.h`, `src/core/Settings.*`, `tests/AiProviderTests.cpp`
**Prior Art:** `specs/2026-10-02_LLM_PROVIDER_ABSTRACTION_SPEC.md` (the `openai-compatible` chat provider this mirrors); Inference API docs https://opencode.ai/v2/docs/console/inference/

---

## 1. Executive Summary & Motivation

The provider abstraction supports Gemini and OpenAI **Chat Completions**. The GPT/Grok/Muse models on the Console Inference gateway live on the **Responses** API (`.../inference/openai/v1/responses`) — a different envelope. Attempting them today yields wrong-protocol 4xx. This spec adds a sibling `ResponsesApiProvider` reusing transport, auth, retry, and UI, differing only in request/response envelope.

Verified live 2026-10-03: `inference/` serves free Chat models to third parties; legacy `zen/go/v1` gates them (403 FreeTierError) and mandates `x-opencode-session`. Validate the shapes below with curl before coding:

```bash
curl -X POST "https://opencode.ai/inference/openai/v1/responses" \
  -H "Authorization: Bearer $OPENCODE_API_KEY" \
  -H "Content-Type: application/json" \
  -d '{"model":"gpt-6-luna","input":"Say hi"}'
```

---

## 2. Protocol Contract

| | Chat Completions (exists) | Responses (new) |
|---|---|---|
| Path | `{base}/chat/completions` | `{base}/responses` |
| Request | `{"model","messages":[{"role":"user","content"}]}` | `{"model","input":"<prompt>"}` (plain string suffices) |
| Reply text | `choices[0].message.content` | `output[]` → `type=="message"` → `content[]` → `type=="output_text"` → `text`, concatenated |
| Auth/session/retry/UI | Bearer + `x-opencode-session`, 3 attempts | identical — shared |

Extractor algorithm:

```
on response body:
  if top-level "error" → terminal "API error: <message>" (same as chat)
  text = ""
  refusal = ""
  for item in response.output[] (must be array, else terminal "missing output"):
    if item.type != "message": continue            # skip reasoning, tool calls
    for part in item.content[]:
      if part.type == "output_text" and part.text is string: text += part.text
      if part.type == "refusal" and part.text is string: refusal = part.text (capped)
  if text non-empty → {true, text}
  else terminal error (include refusal text when present, capped)
```

Lenient fallback (tolerance for shape drift): a `content[]` part with a string `text` but no `type` is accepted as text. Unknown item types are skipped, never fatal.

---

## 3. Requirements

### 3.1 Functional
- FR1: New `providerId` `openai-responses` (constant `kProviderOpenAiResponses`), selected the same way via `aiProvider.providerId`; `baseUrl` reused (default `https://opencode.ai/inference/openai/v1`, path `/responses` via new `kDefaultOpenAiResponsesPath`).
- FR2: **Explicit model required** for responses (no silent default): empty model → terminal error naming example models (`gpt-6-luna`, `grok-4.7`). Rationale: every responses-path model is billed; guessing one spends the user's quota.
- FR3: Same `SearchConfig` JSON contract and common `ParseSearchConfigJson` tail as chat; same sticky error panel, status-bar display name (model string), timeout setting, and keyless-auth behavior.
- FR4: `Settings.cpp` `LoadAiProviderSettings` allowlist gains the new id; unknown ids still fall back to `gemini`.

### 3.2 Non-functional / Constraints
- Same project rules as the chat provider (C++17, `(std::min)`/`(std::max)`, `scoped_lock` where locking, `explicit`, `[[nodiscard]]`, `string_view` params, braced returns, explicit lambda captures, no `new`/`delete`/C-casts, init-statement `if (init; cond)`, same-line NOSONAR/NOLINT only).
- No duplication: retry loop, backoff, classification, FreeTier annotation, and HTTP POST are written once (see §4).
- Existing `OpenAiCompatibleProvider::IsRetriableError/RetryDelayMs` test call-sites keep compiling (thin forwarders if the implementation moves).

---

## 4. Architecture Overview

L1/L2 unchanged from the provider-abstraction spec: desktop app → opencode gateway → `SearchConfig` → `GuiState`. L3 adds one component and one shared unit:

- `OpenAiShared.h/.cpp` (new, in `AI_PROVIDER_SOURCES`): hosts the moved-once items —
  `kMaxAttempts/kRetryBaseDelayMs/kRetryMaxDelayMs`, `AnnotateFreeTierError`,
  `IsRetriableError`, `RetryDelayMs`, plus a generic core:
  ```cpp
  using ResponseExtractor = std::function<std::pair<bool,std::string>(std::string_view)>;
  struct RequestSpec { std::string url; std::vector<std::string> headers; std::string body; int timeout_seconds; };
  AiResult PostWithRetry(const RequestSpec& request, ResponseExtractor extract_text);
  ```
  (`PostWithRetry` = today's loop + `TryRequestOnce` shape, extractor-injected; `"(after N attempts)"` suffix preserved.)
- `ResponsesApiProvider : ILlmProvider` (new): `BuildResponsesBody(prompt, model)`, `EndpointUrl` (base + `/responses`), `ExtractResponsesText` (§2 algorithm), headers/session via the same helpers. `GenerateSearchConfigAsync` = validate → build `RequestSpec` → `PostWithRetry(spec, ExtractResponsesText)` → `ParseSearchConfigJson`.
- `OpenAiCompatibleProvider` refactored to call `PostWithRetry` with its existing extractor; behavior (including log lines) unchanged; its `IsRetriableError/RetryDelayMs` statics forward to the shared free functions so `AiProviderTests.cpp` needs no edits for them.
- `ProviderFactory`: `CreateProvider/CreateProviderForConfig/IsKnownProviderId` handle the new id; `ResolveProviderConfig` maps it through and does **not** inject a default model; `DisplayName` returns model or `"OpenAI Responses"`.

Data flow (responses): `description_input` → shared `BuildSearchConfigPrompt` → `{"model","input"}` → `LlmHttp::CallLlmHttpPost` → `ExtractResponsesText` → shared `ParseSearchConfigJson` → UI commit. Threading/async identical to chat (per-attempt timeout, worker-thread sleep, UI-thread commit).

---

## 5. User Stories

| ID | P | Summary | Gherkin |
|----|---|---------|---------|
| US1 | P0 | Responses happy path | Given `openai-responses` + model `gpt-6-luna` and a fixture reply with two `output_text` parts, When extraction runs, Then concatenated text parses to `SearchConfig` |
| US2 | P0 | Reasoning/tool items skipped | Given `output[]` mixing `reasoning` + `message` + `tool_call` items, When extraction runs, Then only message `output_text` is returned |
| US3 | P0 | Refusal is terminal and visible | Given a refusal part and no text, When extraction runs, Then terminal error containing the refusal text (sticky panel, Copy works) |
| US4 | P0 | Missing model fails fast, no network | Given empty model, When Generate is called, Then terminal error naming example models and no HTTP traffic |
| US5 | P1 | Retry/auth parity | Given 429/5xx then success, When Generate runs, Then succeeds after retry; Given 401/403, Then fails fast with shared hint text |
| US6 | P1 | No chat regression | Given the existing suite, When the full build runs, Then all chat/gemini/settings/gui tests still pass unmodified |

---

## 6. Acceptance Criteria

| Story | Criterion | Measurable check |
|-------|-----------|------------------|
| US1–US3 | Extractor correctness | New doctests: concat, skip-non-message, refusal, missing-`output`, top-level `error` object, lenient typeless part; all pass |
| US4 | Guard | Doctest with empty model asserts error text mentions example models; HTTP layer unreachable (no network in CI either way) |
| US5 | Shared core | Existing retry tests pass via forwarders; one new test posts 503-fixture→success through `PostWithRetry` if a fake transport seam is added, else covered by classification tests |
| US | Quality | `./scripts/build_tests_macos.sh` green; `pre-commit-clang-tidy.sh` 0 new; `find_class_struct_mismatches.py` clean; no new Sonar issues; no duplication (single retry/extract core) |
| US | Live smoke (manual, key required) | `gpt-6-luna` (or cheapest available responses model) returns a config end-to-end; sticky panel shows any failure verbatim |

---

## 7. Task Breakdown

| Phase | Task | Deps | Est. | Notes / tests |
|-------|------|------|------|---------------|
| P0 | Extract `OpenAiShared.h/.cpp` from chat provider (move, no behavior change) | — | 2h | Chat/gemini/settings suites green unmodified; wire into `AI_PROVIDER_SOURCES` + platform `LLM_HTTP_SRC` already present |
| P1 | `ResponsesApiProvider` + factory/settings/DisplayName wiring | P0 | 3h | `providerId` allowlist, explicit-model guard, endpoint `/responses`; no UI changes |
| P2 | Extractor + policy tests (§6) | P1 | 2h | Add to `tests/AiProviderTests.cpp` (already in `test_targets.txt`); fixtures inline, no network |
| P3 | Live curl validation + help/whats-new note | P2 | 1h | Confirm shape against real endpoint first (the §1 curl); `SearchHelpWindow.cpp` bullet on ship per repo rule |

Total ~8h. Each phase shippable; chat behavior frozen by its untouched tests.

---

## 8. Risks & Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Response-shape drift (new part types) | Parse fail | Lenient fallback (§2) + skip-unknown; log raw on failure as today |
| Billed-model footgun via default | Surprise spend | FR2: explicit model required; error names examples |
| Grok reasoning bloat in `output[]` | Slow/wrong extract | Skip non-message items; 8KB Details cap already bounds display |
| Upstream 5xx (seen live: `server_error`) | False failure | Shared retry covers it; exhaustion message states attempts |
| Scope creep (streaming, tools) | Delay | Out of scope: non-streaming `input`-string only; note as follow-up |

---

## 9. Validation & Handoff

- Review checklist per repo rules (`AGENTS.md`, strict constraints, naming table, `(std::min/max)`, init-statements, same-line suppressions, no platform-`#ifdef` edits, DRY constants in `settings_defaults`/`ai_provider_defaults`).
- Implement per §7; after each phase run `./scripts/build_tests_macos.sh --no-asan`, `scripts/pre-commit-clang-tidy.sh`, `find_class_struct_mismatches.py`; rebuild the `./build` Release bundle the user runs (`cmake --build build --target find_helper` — the test script does not cover that dir) and smoke-test with a real key.
- Handoff prompt must cite this spec + `specs/2026-10-02_LLM_PROVIDER_ABSTRACTION_SPEC.md` as sources of truth.
