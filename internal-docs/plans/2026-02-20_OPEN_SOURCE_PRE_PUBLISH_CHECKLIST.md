# 2026-02-20 — Open-source pre-publish checklist

**Purpose:** Items to fix or decide before publishing this project as public open source on GitHub.

---

## Critical (must address)

### 1. Exclude `internal-docs/` from the public repo

- **Status:** `internal-docs/` is **tracked** and would be published if you push the repo as-is.
- **Risk:** Internal plans, historical reviews, and personal paths (e.g. `/Users/brunoorsier/...`) would be public.
- **Action:** Use the following workflow (or the equivalent in your private/maintainer docs):
  - Create an **orphan branch** (e.g. `public`) with no history.
  - Run `git rm -rf --cached internal-docs` (and optionally add `internal-docs/` to `.gitignore` on that branch).
  - Push only that branch to the new public remote.
- **Alternative:** If you prefer a single repo: add `internal-docs/` to `.gitignore`, then `git rm -rf --cached internal-docs` and commit; from then on `internal-docs` stays local-only. (History will still show that those files existed in past commits.)

### 2. Remove or generalize personal paths in **public** `docs/`

These files under `docs/` contain the path `/Users/brunoorsier/dev/USN_WINDOWS`. If `internal-docs` is excluded, these are the only places that expose your username; replacing with a placeholder improves privacy and makes docs work for any clone.

| File | Suggested change |
|------|------------------|
| `docs/guides/building/MACOS_BUILD_INSTRUCTIONS.md` | Use `$(pwd)`, `$PROJECT_ROOT`, or `</path/to/USN_WINDOWS>` |
| `docs/guides/development/INSTRUMENTS_SOURCE_CODE_SETUP.md` | Same placeholder for source paths |
| `docs/guides/development/RUNNING_CLANG_TIDY_MACOS.md` | Same placeholder |

### 3. Broken references to `internal-docs/` in public `docs/`

**Status:** Fixed. The following files previously linked or referred to `internal-docs/`; those references have been removed or rephrased (e.g. “see project maintainer”, or generic wording) so there are no broken links once `internal-docs/` is excluded from the public repo.

**Files updated (no remaining internal-docs links):**

- `docs/DOCUMENTATION_INDEX.md`
- `docs/plans/production/PRODUCTION_READINESS_CHECKLIST.md`
- (Other listed files have been moved to `internal-docs/`; see `internal-docs/plans/2026-02-20_DOCS_MOVE_TO_INTERNAL_REVIEW.md`.)

---

## Recommended (improves clarity and safety)

### 4. Add a root `SECURITY.md` (GitHub security policy)

- **Status:** No `SECURITY.md` at repo root. GitHub uses this for “Security” tab and reporting.
- **Action:** Add a short `SECURITY.md` describing how to report vulnerabilities (e.g. open a private Security Advisory or contact you). You can base it on `docs/security/SECURITY_MODEL.md` for context and add a “Reporting” section.

### 5. Optional: `CONTRIBUTING.md` and `CODE_OF_CONDUCT.md`

- **Status:** None at project root (only in `external/` submodules).
- **Action:** Optional but helpful: add a brief `CONTRIBUTING.md` (how to build, test, submit changes) and, if you want, a `CODE_OF_CONDUCT.md` (e.g. Contributor Covenant). README already has good build/test instructions; CONTRIBUTING can link to them and add PR expectations.

### 6. Document optional AI feature (Gemini API)

- **Status:** README does not mention the optional AI-assisted search or `GEMINI_API_KEY`.
- **Action:** Optional: add one short paragraph or “Optional features” section stating that AI search can be enabled via the `GEMINI_API_KEY` environment variable (no key required for core search). Reduces confusion for contributors who discover the Gemini-related code.

### 7. Fix `.gitignore` typo

- **Status:** Line 138: `*.pyoduplo_report.txt` — likely a typo (e.g. intended `duplo_report.*` or similar; `*.pyc` is already on line 137).
- **Action:** Correct or remove the erroneous pattern so it matches your intended ignores.

---

## Already in good shape

- **LICENSE:** MIT, Copyright (c) 2026 Bruno Orsier — present and clear.
- **README.md:** Clear project description, build instructions, submodules, tests, structure. Notes that maintainer-only docs are not published.
- **CREDITS.md:** Third-party libraries and licenses documented.
- **Secrets:** No hardcoded API keys; Gemini uses `GEMINI_API_KEY` from environment only.
- **CI:** `.github/workflows/build.yml` builds and tests on Windows, macOS, and Linux (manual dispatch; can enable push/PR if desired).
- **AGENTS.md:** No maintainer-only path references; safe for public repo.

---

## Quick decision list

| Item | Decision |
|------|----------|
| Use orphan-branch workflow to exclude internal-docs and history? | Yes / No |
| Replace `/Users/brunoorsier/dev/USN_WINDOWS` in docs with placeholder? | Yes / No |
| Clean or remove internal-docs references in public docs? | Yes / No |
| Add SECURITY.md? | Yes / No |
| Add CONTRIBUTING.md / CODE_OF_CONDUCT? | Yes / No |
| Mention GEMINI_API_KEY in README? | Yes / No |
| Fix .gitignore line 138? | Yes / No |

---

**Last updated:** 2026-02-20
