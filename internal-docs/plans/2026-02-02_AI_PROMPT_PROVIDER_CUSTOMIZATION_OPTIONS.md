# AI Prompt Provider Customization Options

**Date:** 2026-02-02  
**Context:** The AI Search feature currently hardcodes a call to the Google Gemini API. This document outlines options to make it customizable for people using other models (OpenAI, Claude, Ollama, etc.).

---

## Current State

- **Flow:** User types a natural-language description → "Generate Search Config" → app calls Gemini API (hardcoded URL + model) → parses JSON response → applies `SearchConfig`.
- **Hardcoded pieces:**
  - **URL/model:** `https://generativelanguage.googleapis.com/v1beta/models/gemini-1.5-flash:generateContent` (Win/Linux) or `gemini-flash-latest` (mac).
  - **Auth:** `x-goog-api-key` header, API key from env var `GEMINI_API_KEY`.
  - **Request body:** Gemini format `{"contents":[{"parts":[{"text":"..."}]}]}`.
  - **Response parsing:** Expects Gemini structure `candidates[].content.parts[].text` containing our JSON.
- **Already flexible:** "Copy prompt" / "Paste from clipboard" let users use any AI (Copilot, ChatGPT, etc.); only the **one-click "Generate with API"** path is Gemini-specific.

---

## Option 1: Configurable Endpoint (URL + Model + Key)

**Idea:** Keep a single HTTP path but make base URL, model, and API key configurable (settings or env vars).

- **Config (examples):**
  - `AI_PROVIDER_BASE_URL` (e.g. `https://generativelanguage.googleapis.com/v1beta`) or full URL override.
  - `AI_MODEL` (e.g. `gemini-1.5-flash`, `gpt-4o`, or empty to use a default).
  - `AI_API_KEY` (or keep `GEMINI_API_KEY` as default for backward compatibility).
- **Request/response:** Either keep Gemini-style request/response and document that "custom endpoint must speak Gemini-compatible API", or add a single "response_format" (e.g. `gemini` vs `openai`) and a small adapter in the HTTP layer to normalize the response before existing `ParseSearchConfigJson()`.
- **Pros:** Minimal code change; works for any HTTP API that matches (or can be adapted to) the chosen format.  
- **Cons:** Only supports APIs that match that contract; different providers (OpenAI, Anthropic) use different request/response shapes, so one "format" won’t fit all without adapters.

**Best for:** Users who want to switch Gemini model or use a Gemini-compatible proxy, with minimal implementation cost.

---

## Option 2: Provider Abstraction (Strategy / Adapter Pattern)

**Idea:** Introduce an abstraction (e.g. `ILlmSearchConfigProvider` or `SearchConfigProvider`) with a single method such as `GenerateSearchConfigAsync(prompt, api_key, timeout) -> future<LlmSearchConfigResult>`. Implement one adapter per provider.

- **Adapters:** `GeminiProvider`, `OpenAIProvider`, `OllamaProvider`, etc. Each knows:
  - Endpoint URL and model ID.
  - Auth (header name and value: e.g. `x-goog-api-key`, `Authorization: Bearer ...`).
  - Request body format and how to extract the JSON from the response (e.g. Gemini `candidates[0].content.parts[0].text`, OpenAI `choices[0].message.content`).
- **Configuration:** Settings or env: provider id (e.g. `gemini`, `openai`, `ollama`) plus provider-specific key/URL/model (in settings file or env vars).
- **Pros:** Clean, extensible; supports different request/response formats per provider; easy to add new providers.  
- **Cons:** More code and ongoing maintenance for each provider.

**Best for:** Supporting several first-class providers (Gemini, OpenAI, Ollama, etc.) with a maintainable design.

---

## Option 3: External Script or Webhook

**Idea:** App does not call a specific LLM API. Instead, it invokes a user-configured command or HTTP endpoint with the prompt and reads the result (stdout or response body) as our SearchConfig JSON.

- **Config (examples):**
  - **Script:** `AI_SEARCH_SCRIPT` = path to executable; app passes prompt via stdin or argument, reads JSON from stdout.
  - **Webhook:** `AI_SEARCH_WEBHOOK_URL` (+ optional API key header); app POSTs prompt, parses JSON from response body.
- **Contract:** Input = prompt (text); output = our existing SearchConfig JSON (or a wrapper that contains it). Parsing can reuse `ParseSearchConfigJson()` if the script/webhook returns that JSON directly.
- **Pros:** Maximum flexibility; user can call any model (local Ollama, ChatGPT, Claude, custom API) without app changes.  
- **Cons:** Security and validation (script path, webhook URL); platform-dependent paths; harder to give good error messages; need to define timeout and error handling.

**Best for:** Power users who want to plug in any tool or service without waiting for built-in provider support.

---

## Option 4: Rely on "Copy Prompt" / "Paste from Clipboard" Only

**Idea:** Do not add more provider logic in-app. Document that for non-Gemini models, users should use "Copy prompt" → paste into their preferred AI → copy the JSON back → "Paste from clipboard".

- **Change:** Optionally rename UI/strings from "Gemini" to "AI" and keep the single "Generate with API" button as a convenience for Gemini (or for a single configurable endpoint as in Option 1).
- **Pros:** No extra code; already works for any model.  
- **Cons:** No one-click experience for non-Gemini users; they must leave the app to paste and copy.

**Best for:** Keeping the codebase simple while still supporting any model via the existing clipboard flow.

---

## Recommendation Summary

| Goal | Suggested option |
|------|-------------------|
| Quick win, same UX, support different Gemini models or one Gemini-like API | **Option 1** (configurable endpoint + optional response adapter). |
| Support multiple providers (Gemini, OpenAI, Ollama, etc.) with clear structure | **Option 2** (provider abstraction). |
| Maximum flexibility without adding provider-specific code | **Option 3** (script/webhook). |
| Minimal change, accept that one-click is Gemini-only | **Option 4** (document clipboard flow). |

A practical path is **Option 1** first (configurable URL/model/key, possibly one response format flag), then evolve toward **Option 2** if you add more providers. Keeping **Option 4** in the docs ensures users know they can already use any model via the clipboard.

---

## Implementation Notes (if pursuing Option 1 or 2)

- **Settings:** Add fields to `AppSettings` (and persist in JSON) for provider id, base URL, model, API key (or reference to env var name). Consider not storing raw API key in settings and only using env vars for secrets.
- **API key:** Prefer env var for the key (e.g. `GEMINI_API_KEY` or `AI_API_KEY`); optional override in settings for power users.
- **Response parsing:** Current `ParseSearchConfigJson()` expects our JSON; it can accept either raw JSON or text extracted from provider-specific wrappers (Gemini `candidates[].content.parts[].text`, OpenAI `choices[].message.content`, etc.). Option 2 would do this extraction inside each adapter before calling a common parser.
- **Naming:** Renaming `GeminiApiUtils` / `GeminiApiHttp*` to something provider-neutral (e.g. `LlmSearchConfigApi` or `SearchConfigProvider`) would reduce confusion when adding more providers; can be done when introducing the abstraction in Option 2.
