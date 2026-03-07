# 2026-02-02 — Docs Reorganization: Internal vs Public (Open-Source Ready)

**Goal:** Reorganize documentation into two main trees so the app can be released as open-source **without disclosing the full details of historical work**. Contributors get everything they need in `docs/`; internal and historical material stays in `internal-docs/`.

---

## 1. Principle: Two Audiences

| Audience | Folder | Purpose | In public repo? |
|----------|--------|---------|------------------|
| **Contributors** (build, contribute, understand design) | `docs/` | How to build, code standards, architecture, design decisions, security model, contributing | **Yes** |
| **You** (maintainer, history, internal process) | `internal-docs/` | Historical work, reviews over time, prompts, internal plans, detailed analyses, rationale | **No** (or optional) |

**Rule of thumb:**  
- **“Would a new contributor need this to build, code, or understand the system?”** → `docs/`  
- **“Is this my working history, internal review, or narrative of how we got here?”** → `internal-docs/`

---

## 2. Proposed Layout

### 2.1 Public: `docs/` (contributor-facing only)

Keep only what contributors need. Suggested structure:

```
docs/
├── README.md                    # Entry point: what the app is, link to build/contributing
├── CONTRIBUTING.md              # (optional) How to contribute, link to standards & guides
│
├── guides/                      # How to build and develop
│   ├── building/
│   │   ├── BUILDING_ON_LINUX.md
│   │   ├── MACOS_BUILD_INSTRUCTIONS.md
│   │   ├── PGO_SETUP.md
│   │   ├── CODE_SIGNING_EXPLAINED.md
│   │   └── WINDOWS_BUILD_ISSUES_FONTAWESOME.md  # if useful for contributors
│   ├── development/             # Dev workflow (e.g. clang-tidy, logging)
│   └── testing/                 # How to run tests, coverage
│
├── design/                      # Current architecture and design (no history)
│   ├── ARCHITECTURE_COMPONENT_BASED.md
│   ├── IMGUI_IMMEDIATE_MODE_PARADIGM.md
│   ├── ISEARCHABLE_INDEX_DESIGN.md
│   ├── LAZY_ATTRIBUTE_LOADER_DESIGN.md
│   ├── PARALLEL_SEARCH_ENGINE_DESIGN.md
│   ├── STREAMING_SEARCH_RESULTS_DESIGN.md
│   ├── STRING_POOL_DESIGN.md
│   ├── INTERLEAVED_LOAD_BALANCING_STRATEGY.md
│   ├── DIRECTORY_MANAGER_DESIGN.md
│   └── (other current design docs)
│
├── standards/                   # Coding standards (referenced by AGENTS document)
│   ├── CXX17_NAMING_CONVENTIONS.md
│   └── PARAMETER_NAME_PROPOSALS.md   # optional
│
├── platform/                    # Platform-specific notes for contributors
│   ├── linux/
│   └── windows/
│
├── security/                    # Public security model
│   └── SECURITY_MODEL.md
│
├── analysis/                    # Only contributor-relevant analysis (minimal)
│   ├── CLANG_TIDY_GUIDE.md      # How to use clang-tidy (if kept)
│   ├── CLANG_TIDY_CLASSIFICATION.md
│   └── 2026-02-01_CLANG_TIDY_VS_SONAR_AVOID_DOUBLE_WORK.md  # avoid duplicate work
│
└── plans/                       # Only high-level, non-sensitive plans (optional)
    └── production/
        └── PRODUCTION_READINESS_CHECKLIST.md   # if you want it public
```

**Remove from public `docs/` (move to `internal-docs/` or drop from repo):**

- All dated “how we got here” narratives and internal reviews.
- Prompts, task prompts, and internal orchestration notes.
- Detailed historical analyses (deduplication, Sonar progress, fix rationales, etc.).
- Archive and Historical folders in their entirety for public view.

So in practice:

- **Keep in `docs/`:** guides, design (current), standards, security, platform, and a small analysis subset (tooling/how-to).
- **Move out of `docs/`:** Historical, archive, review, prompts, most of analysis, most of plans, fixes, futures, deduplication, bugs, research (or keep only a short “further reading” subset in docs if needed).

### 2.2 Internal: `internal-docs/` (for you only)

Everything that documents **your** process, history, and internal decisions:

```
internal-docs/
├── README.md                    # "Internal maintainer docs – not for contributors"
│
├── Historical/                  # Current docs/Historical/ → move here as-is
├── archive/                     # Current docs/archive/ → move here
├── review/                      # All dated review snapshots (docs/review/)
├── prompts/                    # All prompt templates and task prompts
│
├── analysis/                    # Detailed analyses (deduplication, Sonar, performance deep-dives)
│   ├── duplication/
│   ├── code-quality/
│   ├── performance/
│   └── (other analysis subdirs)
│
├── plans/                      # Internal plans (features, refactoring, production details)
│   ├── features/
│   ├── refactoring/
│   ├── production/
│   └── deduplication/
│
├── fixes/                      # Bug fix summaries, buffer overflow plans, security fix notes
├── futures/                    # Futures implementation reviews
├── research/                   # MFT, streaming, hot folders, etc.
├── optimization/               # Internal optimization notes
├── bugs/
├── deduplication/
├── ui-ux/                      # Internal UI/UX notes
├── boost/
├── examples/                   # If internal-only
└── (root-level dated docs)     # e.g. 2026-01-30_BENCHMARKING_SYSTEM_OPTIONS.md, etc.
```

You can keep the same internal structure you have today under `docs/`, just under `internal-docs/` instead.

---

## 3. Mapping: Current `docs/` → `docs/` vs `internal-docs/`

| Current location | Public `docs/` | Internal `internal-docs/` |
|-------------------|-----------------|---------------------------|
| `docs/Historical/` | — | Move entirely |
| `docs/archive/` | — | Move entirely |
| `docs/review/` | — | Move entirely |
| `docs/prompts/` | — | Move entirely |
| `docs/design/` | Keep (current design only) | — |
| `docs/guides/` | Keep | — |
| `docs/standards/` | Keep | — |
| `docs/security/` | Keep (e.g. SECURITY_MODEL.md) | — |
| `docs/platform/` | Keep (contributor-relevant) | — |
| `docs/analysis/` | Keep only: CLANG_TIDY_GUIDE, CLANG_TIDY_CLASSIFICATION, CLANG_TIDY_VS_SONAR | Move rest (duplication, code-quality, performance, refactoring, etc.) |
| `docs/plans/` | Optional: e.g. PRODUCTION_READINESS_CHECKLIST | Move features, refactoring, production details, timeline, etc. |
| `docs/fixes/` | — | Move entirely |
| `docs/futures/` | — | Move entirely |
| `docs/research/` | — | Move entirely |
| `docs/optimization/` | — | Move entirely |
| `docs/bugs/` | — | Move entirely |
| `docs/deduplication/` | — | Move entirely |
| `docs/features/` (FEATURE_IDEAS) | Optional: sanitized “ideas” page | Prefer internal |
| `docs/ui-ux/` | — | Move entirely |
| `docs/boost/` | — | Move entirely |
| `docs/examples/` | Only if useful for contributors | Else internal |
| Root-level dated files (e.g. 2026-01-30_*, 2026-01-31_*) | — | Move to internal-docs |
| `DOCUMENTATION_INDEX.md` | New index for `docs/` only | — |
| `DOCUMENTATION_REORGANIZATION_*.md` | — | Move to internal-docs |

---

## 4. What to Do With AGENTS document and References

- **AGENTS document** stays in the repo root and is part of the **public** repo (contributors and bots use it). It should **only** reference paths under `docs/`, not `internal-docs/`.
- **Update AGENTS document** so that:
  - `docs/BUILDING_ON_LINUX.md` → `docs/guides/building/BUILDING_ON_LINUX.md` (or the single canonical path you choose).
  - `docs/Historical/GEMINI_API_IMGUI_THREADING_ANALYSIS.md` → either:
    - **Option A:** Remove the link and summarize ImGui threading rules in AGENTS document (and optionally in `docs/design/IMGUI_IMMEDIATE_MODE_PARADIGM.md`).
    - **Option B:** Add a short “ImGui threading” section in `docs/design/IMGUI_IMMEDIATE_MODE_PARADIGM.md` and point AGENTS document to that; keep the full historical analysis in `internal-docs/`.
- **README.md** already points to `docs/platform/linux/BUILDING_ON_LINUX.md`; ensure the file actually lives there or update to the canonical path (e.g. `docs/guides/building/BUILDING_ON_LINUX.md`).
- Any link that currently points into `Historical/`, `archive/`, `review/`, `prompts/`, or internal plans should either be removed or replaced with a link to a public doc that summarizes the rule (e.g. design or standards).

Result: **no references to `internal-docs/` in the public tree**; AGENTS document and README stay contributor-focused and stable.

---

## 5. Hiding Internal Docs When Releasing Open-Source

To “not disclose the full details of the historical work” you have two options.

**Option A – Don’t publish `internal-docs/` at all (recommended)**  
- Keep `internal-docs/` only in your private repo or local clone.  
- In the **public** repo, there is no `internal-docs/` directory.  
- No need to .gitignore in the public repo; the folder simply isn’t there.

**Option B – Keep both in one repo but hide internal from the public**  
- In the **public** repo add to `.gitignore`: `internal-docs/`.  
- Your private repo (or a branch you don’t push) does **not** ignore `internal-docs/`.  
- Contributors only see `docs/`; they never see historical or internal content.

Choose one and stick to it so the boundary is clear.

---

## 6. Implementation Order (High Level)

1. **Create `internal-docs/`** at repo root (sibling of `docs/`).
2. **Move** the folders and files listed in the mapping into `internal-docs/` (e.g. `Historical/`, `archive/`, `review/`, `prompts/`, and the parts of `analysis/`, `plans/`, etc. that are internal). Prefer `git mv` to keep history where useful.
3. **Thin public `docs/`**: remove or move root-level dated files and any internal-only content; keep only guides, design, standards, security, platform, and the chosen analysis subset.
4. **Add or update `docs/README.md`** and optionally `docs/DOCUMENTATION_INDEX.md` so they describe only the public `docs/` tree.
5. **Update AGENTS document** (and any other root docs): replace links to moved files with links to `docs/` only; fix BUILDING_ON_LINUX path; replace Historical threading link with design summary or in-AGENTS summary.
6. **If using Option B:** add `internal-docs/` to `.gitignore` in the public repo and document in a short CONTRIBUTING or README note that “maintainer-only docs live in internal-docs and are not published.”

After this, **docs/** is the single place contributors need; **internal-docs/** holds the rest and stays out of the open-source story.

---

## 7. Summary

- **docs/** = public, contributor-facing (build, design, standards, security, platform, minimal analysis).
- **internal-docs/** = your internal and historical material (reviews, prompts, plans, analyses, archive).
- **AGENTS document** and **README** reference only `docs/`; no mention of `internal-docs/` in the public repo.
- **Open-source:** either omit `internal-docs/` from the public repo or .gitignore it so “the full details of the historical work” are not disclosed.

This gives you a clear, repeatable way to keep the app open-source-friendly while keeping historical and internal context for yourself in `internal-docs/`.
