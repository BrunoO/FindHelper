# Documentation Index (public)

**Last Updated:** 2026-03-15
**Purpose:** Index of contributor-facing documentation in `docs/`. Internal-only material (analyses, prompt templates, task notes, plans) lives in `internal-docs/`.

---

## Dated reviews (maintainer-only)

Comprehensive review bundles (tech debt, architecture, security, error handling, performance, testing, documentation, UX, feature exploration, clang-tidy) are maintainer-only and live under **`internal-docs/review/`**. For example, the 2026-03-15 review is in `internal-docs/review/2026-03-15/` (summary: `COMPREHENSIVE_REVIEW_SUMMARY_2026-03-15.md`).

---

## For new contributors

| Document | Purpose |
|----------|---------|
| [README.md](../README.md) (project root) | Project overview and build instructions |
| [AGENTS.md](../AGENTS.md) (project root) | Guidelines for AI assistants and coding rules |
| [guides/building/BUILDING_ON_LINUX.md](guides/building/BUILDING_ON_LINUX.md) | Linux build and dependencies |
| [guides/building/MACOS_BUILD_INSTRUCTIONS.md](guides/building/MACOS_BUILD_INSTRUCTIONS.md) | macOS build |
| [guides/building/PGO_SETUP.md](guides/building/PGO_SETUP.md) | Profile-Guided Optimization (Windows) |
| [standards/CXX17_NAMING_CONVENTIONS.md](standards/CXX17_NAMING_CONVENTIONS.md) | Naming conventions for all code |

---

## How the public docs are organized

- **`guides/`** – How to build and develop
  - `guides/building/` – BUILDING_ON_LINUX, MACOS_BUILD_INSTRUCTIONS, PGO_SETUP, CODE_SIGNING_EXPLAINED
  - `guides/development/` – Profiling, clang-tidy, logging standards, Instruments setup
  - `guides/testing/` – WINDOWS_TEST_EXECUTION_GUIDE

- **`design/`** – Current architecture and design
  - IMGUI_IMMEDIATE_MODE_PARADIGM, STRING_POOL_DESIGN, PARALLEL_SEARCH_ENGINE_DESIGN, ISEARCHABLE_INDEX_DESIGN, LAZY_ATTRIBUTE_LOADER_DESIGN, STREAMING_SEARCH_RESULTS_DESIGN, INTERLEAVED_LOAD_BALANCING_STRATEGY, DIRECTORY_MANAGER_DESIGN, ARCHITECTURE_COMPONENT_BASED
  - 2026-02-18_RESULTS_TABLE_KEYBOARD_SHORTCUTS – Results table mark/unmark and navigation shortcuts
  - 2026-03-15_LOCK_ORDERING_AND_CRITICAL_SECTIONS – Lock ordering and rule: no I/O or heavy work inside critical sections

- **`standards/`** – Coding standards
  - CXX17_NAMING_CONVENTIONS, 2026-02-02_MULTIPLATFORM_COHERENCE_CHECKLIST

- **`security/`** – Security model
  - SECURITY_MODEL

- **`platform/`** – Platform-specific
  - `platform/linux/`, `platform/windows/`, `platform/macos/`

- **`analysis/`** – Tooling and quality
  - CLANG_TIDY_GUIDE, CLANG_TIDY_CLASSIFICATION

- **`plans/production/`** – Quality bar
  - PRODUCTION_READINESS_CHECKLIST

---

## Essential reference

| Document | When to use |
|----------|-------------|
| [design/IMGUI_IMMEDIATE_MODE_PARADIGM.md](design/IMGUI_IMMEDIATE_MODE_PARADIGM.md) | All UI work (ImGui is immediate-mode; no widget storage, all ImGui on main thread) |
| [standards/CXX17_NAMING_CONVENTIONS.md](standards/CXX17_NAMING_CONVENTIONS.md) | Writing or refactoring code |
| [plans/production/PRODUCTION_READINESS_CHECKLIST.md](plans/production/PRODUCTION_READINESS_CHECKLIST.md) | Before every commit (Quick) or major features (Comprehensive) |
| [analysis/CLANG_TIDY_GUIDE.md](analysis/CLANG_TIDY_GUIDE.md) | Running or configuring clang-tidy |

---

**Note:** Some analyses, plans, and internal reviews are maintainer-only and are not part of the public documentation set.
