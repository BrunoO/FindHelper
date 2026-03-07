# Documentation Security Review (2026 Best Practices)

**Date:** 2026-03-07  
**Purpose:** How and why we scan documentation for hidden characters and poisoning/prompt-injection phrases. Aligns with 2025–2026 best practices for securing docs consumed by humans and AI.

## Script

- **Location:** `scripts/review_documentation_security.py`
- **Usage:**
  ```bash
  python3 scripts/review_documentation_security.py [--root <path>] [--verbose] [--json]
  ```
- **Exit code:** 0 if no findings, 1 if any finding (CI-friendly).
- **Scope:** `docs/`, `internal-docs/`, `specs/`, `external/`, and root-level `*.md`. Excludes build artifacts and non-doc dirs under each.

## What It Detects

### 1. Hidden / invisible characters

- **Zero-width and format characters** (e.g. U+200B, U+200C, U+FEFF) that can hide instructions from human reviewers or affect tokenization (see CVE-2025-32711, Unicode TR36).
- **Bidirectional override/isolate** (e.g. U+202E RLO) used in Trojan source / display attacks (CVE-2021-42574).
- **Control characters and null bytes** (path/argument injection).
- **Unicode Tag characters** (U+E0000–U+E007F) used in tag-based injection.

**Exception:** U+200D (ZWJ) is not reported when it appears between emoji (e.g. 👨‍💻), as it is standard in emoji sequences.

### 2. Poisoning / prompt-injection phrases

Patterns that could instruct an AI reading the docs to override instructions, reveal prompts, or bypass safety (OWASP LLM Top 10, MCP06:2025). Examples:

- Instruction override: “ignore previous instructions”, “disregard your instructions”, “new instructions:”
- System override: “system override”, “override system instructions”
- Prompt extraction: “reveal your prompt”, “show your instructions”
- Role override: “you are now”, “pretend you are”, “act as if you”
- Safety bypass: “never refuse”, “bypass safety”, “ignore content policy”
- Concealment: “do not tell the user”, “keep this secret”

Phrases are matched case-insensitively. Some patterns are relaxed to avoid false positives (e.g. “show instructions” only when “show your/the/system instructions”; “hidden instructions” not when “hidden instructions in …” as in security docs describing the threat).

## Best practices (2025–2026) applied

1. **Input sanitization**
   - Strip or normalize invisible characters (NFKC + remove zero-width/format controls) before storing or displaying docs.
   - Use deny-lists for instruction-like phrases in contextual content that may be fed to models.

2. **Detection layers**
   - Character-level: invisible and control characters.
   - Phrase-level: instruction-override and prompt-extraction patterns.
   - Human review: script output is advisory; confirm legitimate vs malicious.

3. **References**
   - Unicode TR36 (Unicode Security Considerations).
   - CVE-2021-42574 (Trojan source / bidirectional override).
   - CVE-2025-32711 (Unicode tag prompt injection).
   - OWASP LLM Top 10, MCP06:2025 – Prompt Injection via Contextual Payloads.
   - Prompt-injection defense guides (e.g. NFKC normalization, deny-lists, structured prompts).

## When to run

- Before merging changes that touch `docs/`, `internal-docs/`, `specs/`, or `external/`.
- In CI: add a job that runs the script and fails on findings.
- After pulling third-party or copied documentation.

## Remediation

- **Hidden characters:** Normalize with NFKC and remove zero-width/format characters; fix or remove control characters and null bytes.
- **Poisoning phrases:** If the phrase is legitimate (e.g. security doc describing “hidden instructions in comments”), no change. If malicious or ambiguous, reword or remove.

## Related

- `scripts/detect_hidden_characters.py` — Broader scan of source and text files (not doc-only).
- `internal-docs/prompts/security-review.md` — Security review prompts (may mention these threats).
