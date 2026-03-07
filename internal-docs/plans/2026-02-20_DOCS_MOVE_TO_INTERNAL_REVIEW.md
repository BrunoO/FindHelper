# 2026-02-20 — Public docs review: files to move to internal

**Status:** Applied (2026-02-20). All moves below have been executed. **Prompts kept public** for contributors; all other categories moved to `internal-docs/`. References in remaining docs and in scripts have been updated to point to `internal-docs/` where applicable.

**Purpose:** Identify files under `docs/` that should be moved to `internal-docs/` (or equivalent) before publishing as open source, so the public repo contains only contributor-facing material.

**Criteria:**
- **Keep public:** Build/test/contribute how-to, coding standards, current architecture and design, security model, production checklist, clang-tidy/sonar alignment, open-source publish checklist.
- **Move to internal:** Point-in-time reviews, internal backlog/prioritization, prompt templates for maintainer workflow, dated analyses and investigations, implementation reports, research and feasibility docs, Sonar/clang-tidy status snapshots.

---

## Summary

| Category | Action | Count (approx.) |
|----------|--------|------------------|
| **review/** | Move entire tree | ~70 files |
| **prompts/** | Move all except optionally AGENT_STRICT_CONSTRAINTS | ~18 files |
| **analysis/** | Move most dated analyses; keep guides + 1–2 reference docs | ~25 move, ~5 keep |
| **plans/** | Move backlog and dated plans; keep production checklist + open-source checklist | ~12 move, ~3 keep |
| **optimization/**, **research/** | Move entire trees | ~3 + 1 |
| **guides/** (dated/one-off) | Move Sonar/build reports, quick fixes, anticipation docs | ~12 files |
| **design/** (dated deep-dives) | Move 2 | 2 |
| **build/**, **platform/** (reviews) | Move build reviews, contributor review docs | ~5 + 1 |
| **internal/** | Already internal; move to repo-root `internal-docs/` or leave under docs | 4 |

---

## 1. Move entire directory trees

### 1.1 `docs/review/` → internal

**Rationale:** All content is point-in-time comprehensive reviews (v1, v2, …), findings summaries, rationale docs, and improvement plans. Useful for maintainer history; not needed for contributors to build, test, or contribute.

**Move:**
- `review/2026-02-01-v1/`
- `review/2026-02-02_MULTIPLATFORM_COHERENCE_REVIEW.md`
- `review/2026-02-06_POST_PROCESSING_MAP_LOOKUPS_OPTIONS.md`
- `review/2026-02-06_REVIEW_FINDINGS_SUMMARY_AND_PRIORITIES.md`
- `review/2026-02-06-v1/`
- `review/2026-02-09-v1/`
- `review/2026-02-14_UX_THEMES_REVIEW.md`
- `review/2026-02-14-v1/`
- `review/2026-02-14-v2/`
- `review/2026-02-18-v1/`
- `review/2026-02-19-v1/`

### 1.2 `docs/optimization/` → internal

**Rationale:** Research and optimization assessments (folder crawl, macOS home index); maintainer-only context.

**Move:**
- `optimization/2026-02-02_FOLDER_CRAWL_REVIEW_AND_OPTIMIZATION_RESEARCH.md`
- `optimization/2026-02-02_MACOS_HOME_INDEX_ASSESSMENT.md`

### 1.3 `docs/research/` → internal

**Rationale:** One-off research (e.g. sprite sheet texture loading); not contributor-facing.

**Move:**
- `research/2026-02-15_SPRITE_SHEET_TEXTURE_LOADING_RESEARCH.md`

### 1.4 `docs/build/` → internal

**Rationale:** Build-error anticipation and review docs; internal process.

**Move:**
- `build/BUILD_ERRORS_ANTICIPATED_AppBootstrap.md`
- `build/REVIEW_Linux_Build_Fixes.md`
- `build/SCRIPTS_BUILD_TESTS_MACOS_ANALYSIS.md`
- `build/WINDOWS_BUILD_ISSUES_FONTAWESOME.md`

---

## 2. `docs/prompts/` — move to internal (with one optional exception)

**Rationale:** Prompt templates for architecture, security, performance, UX, tech-debt, test-strategy, Sonar, etc. are maintainer workflow; contributors don’t need them to contribute.

**Move:**
- `prompts/architecture-review.md`
- `prompts/error-handling-review.md`
- `prompts/master-orchestrator.md`
- `prompts/performance-review.md`
- `prompts/security-review.md`
- `prompts/sonar-check.md`
- `prompts/tech-debt.md`
- `prompts/test-strategy-audit.md`
- `prompts/ux-review.md`
- `prompts/documentation-maintenance.md`
- `prompts/feature-exploration-product-owner.md`
- `prompts/2026-02-14_AGENT_INSTRUCTIONS_UPDATE_CLANG_TIDY_STATUS.md`
- `prompts/2026-02-14_CUSTOM_SIZE_RANGE_FILTER_TASK.md`
- `prompts/2026-02-14_FULL_INDEX_TOTAL_SIZE_ROOT_CAUSE_TASK.md`
- `prompts/AGENT_IMPACT_ANALYSIS_REGRESSION_PREVENTION.md`
- `prompts/CLANG_TIDY_CLEANUP_AND_SONAR_FIX_WORKFLOW.md`
- `prompts/TaskmasterPrompt.md`

**Optional keep (public):**  
- `prompts/AGENT_STRICT_CONSTRAINTS.md` — Copy-paste block for AI agents; can stay if you want contributors and bots to use the same strict constraints. Otherwise move to internal.

---

## 3. `docs/analysis/` — keep a small set; move the rest

**Keep (contributor-facing):**
- `analysis/CLANG_TIDY_GUIDE.md` — How to run/configure clang-tidy
- `analysis/CLANG_TIDY_CLASSIFICATION.md` — Check classification
- `analysis/2026-02-01_CLANG_TIDY_VS_SONAR_AVOID_DOUBLE_WORK.md` — Align clang-tidy and Sonar fixes
- Optionally: `analysis/2026-02/2026-02-13_IMPROVEMENT_OPPORTUNITIES.md` — High-level opportunities (or move and rely on backlog/contributor issues)
- Optionally: `analysis/2026-02/2026-02-13_CODE_REVIEW_POTENTIAL_BUGS.md` — If you want to expose known potential bugs to contributors

**Move to internal (dated analyses, snapshots, investigations, plans):**
- `analysis/2026-02-02_CLANG_TIDY_WARNINGS_ASSESSMENT.md`
- `analysis/2026-02-02_HOT_SEARCH_PATH_OPTIMIZATION_REVIEW.md`
- `analysis/2026-02-02_LAZY_ATTRIBUTE_LOADING_REGRESSION_PREVENTION.md`
- `analysis/2026-02-02_LAZY_SIZE_LOADING_WINDOWS_INVESTIGATION_PLAN.md`
- `analysis/2026-02-02_SEARCH_PERFORMANCE_OPTIMIZATIONS.md`
- `analysis/2026-02-03_SONAR_OPEN_ISSUES_GUARDRAIL_GAPS.md`
- `analysis/2026-02-03_SONAR_OPEN_ISSUES_LIST.md`
- `analysis/2026-02-03_SONARCLOUD_LOC_REDUCTION.md`
- `analysis/2026-02-14_FULL_INDEX_TOTAL_SIZE_ANALYSIS.md`
- `analysis/2026-02-14_SONAR_NINE_ISSUES_INVESTIGATION.md`
- `analysis/2026-02-18_SET_UNORDERED_SET_FAST_LIBS_INVESTIGATION.md`
- `analysis/2026-02-19_CLANG_TIDY_APPROACHES_SUMMARY.md`
- `analysis/2026-02-19_CLANG_TIDY_CACHE_RESEARCH.md`
- `analysis/2026-02-19_CLANG_TIDY_REDUCE_WORK_PER_FILE.md`
- `analysis/2026-02-19_HELP_WINDOW_AS_NORMAL_WINDOW_PLAN.md`
- `analysis/2026-02-19_IMGUI_TEST_ENGINE_EXPLORATION.md`
- `analysis/2026-02-19_MEMORY_IMPACT_FIX_DUPLICATE_PATH_INDEX.md`
- `analysis/2026-02-19_MODIFIED_FILES_INVESTIGATION.md`
- `analysis/2026-02-19_RULES_COMPLIANCE_REVIEW.md`
- `analysis/2026-02-19_UI_TESTING_PLAN_PYTHON_TEST_MODE_MACOS.md`
- `analysis/2026-02-20_CONCURRENCY_MT_UNSAFE_REVIEW.md`
- `analysis/2026-02-20_DRY_CONSTANTS_ANALYSIS.md`
- `analysis/2026-02-20_FILES_WITH_CLANG_TIDY_WARNINGS.md`
- `analysis/2026-02-20_NO_VERIFY_TAGS_REVIEW.md`
- `analysis/2026-02-20_PRODUCTION_READINESS_VERIFICATION.md`
- `analysis/2026-02/2026-02-13_EXPORT_SEARCH_RESULTS_CSV_TASKMASTER.md`
- `analysis/2026-02/2026-02-13_SMART_STATUS_BAR_TOTAL_SIZE_FEASIBILITY.md`
- `analysis/2026-02/2026-02-13_STREAMING_EXPECTED_BEHAVIOR_AND_INVESTIGATION.md`
- `analysis/2026-02/2026-02-14_CLEAR_ALL_STREAMING_MODE_INVESTIGATION.md`
- `analysis/2026-02/2026-02-14_PR91_EXPORT_CSV_DEVELOPMENT_PRACTICES_REVIEW.md`
- `analysis/2026-02/2026-02-14_STREAMING_RESULTS_COMPREHENSIVE_REVIEW.md`

(If you keep `2026-02-13_IMPROVEMENT_OPPORTUNITIES` or `2026-02-13_CODE_REVIEW_POTENTIAL_BUGS` in public, remove them from the move list.)

---

## 4. `docs/plans/` — keep checklists; move backlog and dated plans

**Keep (public):**
- `plans/production/PRODUCTION_READINESS_CHECKLIST.md` — Quick and comprehensive checklist for contributors/maintainers
- `plans/2026-02-20_OPEN_SOURCE_PRE_PUBLISH_CHECKLIST.md` — Publish workflow

**Move to internal:**
- `plans/2026-02-02_PRIORITIZED_REMAINING_WORK_BACKLOG.md` — Internal prioritization and status
- `plans/2026-02-02_SONAR_ZERO_OPEN_ISSUES_PLAN.md`
- `plans/2026-02-02_AI_PROMPT_PROVIDER_CUSTOMIZATION_OPTIONS.md`
- `plans/2026-02-03_LINUX_CRAWL_FOLDER_UI_FIRST_PLAN.md`
- `plans/2026-02-13_DRY_LOAD_BALANCING_STRATEGY_PLAN.md`
- `plans/2026-02-14_CATPPUCCIN_THEME_PROPOSAL.md`
- `plans/2026-02-14_FOUR_ADDITIONAL_UI_THEMES_PLAN.md`
- `plans/2026-02-14_MULTI_SELECT_BULK_COPY_IMPLEMENTATION_PLAN.md`
- `plans/2026-02-18_DUPLICATE_PATH_INDEX_FIX_PLAN.md`
- `plans/production/2026-02-14_THEME_REFACTOR_PRODUCTION_AND_WINDOWS_BUILD.md`
- `plans/production/2026-02-18_PRODUCTION_READINESS_CHECK.md`
- `plans/production/2026-02-19_PRODUCTION_READINESS_AND_WINDOWS_BUILD_ANTICIPATION.md`

---

## 5. `docs/guides/` — keep core how-to; move dated/reports

**Keep (public):**
- `guides/building/BUILDING_ON_LINUX.md`
- `guides/building/MACOS_BUILD_INSTRUCTIONS.md`
- `guides/building/PGO_SETUP.md`
- `guides/building/CODE_SIGNING_EXPLAINED.md`
- `guides/development/INSTRUMENTS_SOURCE_CODE_SETUP.md`
- `guides/development/LOGGING_STANDARDS.md`
- `guides/development/MACOS_PROFILING_GUIDE.md`
- `guides/development/RUNNING_CLANG_TIDY_MACOS.md`
- `guides/testing/WINDOWS_TEST_EXECUTION_GUIDE.md` (if it’s a how-to; else move)

**Move to internal:**
- `guides/2026-02-03_CONFIGURING_LOCAL_SONAR_SCANNER_TO_MATCH_SONARCLOUD.md`
- `guides/building/2026-01-17_PGO_CONFIGURATION_IMPROVEMENTS.md`
- `guides/building/2026-01-17_TEST_BUILD_OPTIMIZATIONS.md`
- `guides/building/BUILD_ISSUES_ANTICIPATION.md`
- `guides/building/D9002_QUICK_FIX.md`
- `guides/building/WINDOWS_BUILD_ISSUES_FONTAWESOME.md`
- `guides/building/WINDOWS_BUILD_PRODUCTION_READINESS_REPORT.md`
- `guides/development/2026-01-17_UPDATING_IMGUI_WINDOWS.md`
- `guides/testing/CODE_COVERAGE_MACOS_RESEARCH.md`
- `guides/testing/COVERAGE_IMPROVEMENT_PLAN.md`
- `guides/testing/COVERAGE_INCLUDING_ALL_FILES.md`
- `guides/testing/COVERAGE_MISSING_FILES_EXPLANATION.md`

---

## 6. `docs/design/` — keep current design; move dated deep-dives

**Keep (public):** All non-dated design docs (IMGUI, STRING_POOL, PARALLEL_SEARCH_ENGINE, ISEARCHABLE_INDEX, LAZY_ATTRIBUTE_LOADER, STREAMING_SEARCH_RESULTS, INTERLEAVED_LOAD_BALANCING, DIRECTORY_MANAGER, ARCHITECTURE_COMPONENT_BASED) and the keyboard-shortcuts spec:
- `design/ARCHITECTURE_COMPONENT_BASED.md`
- `design/DIRECTORY_MANAGER_DESIGN.md`
- `design/IMGUI_IMMEDIATE_MODE_PARADIGM.md`
- `design/INTERLEAVED_LOAD_BALANCING_STRATEGY.md`
- `design/ISEARCHABLE_INDEX_DESIGN.md`
- `design/LAZY_ATTRIBUTE_LOADER_DESIGN.md`
- `design/PARALLEL_SEARCH_ENGINE_DESIGN.md`
- `design/STREAMING_SEARCH_RESULTS_DESIGN.md`
- `design/STRING_POOL_DESIGN.md`
- `design/2026-02-18_RESULTS_TABLE_KEYBOARD_SHORTCUTS.md` — Current feature spec

**Move to internal:**
- `design/2026-02-14_PATH_POOL_LIFECYCLE_AND_RISKS.md` — Deep-dive analysis
- `design/2026-02-16_THREADING_AND_SYNC_MECHANISMS.md` — Detailed analysis

---

## 7. `docs/standards/` — keep all (contributor-facing)

- `standards/CXX17_NAMING_CONVENTIONS.md`
- `standards/PARAMETER_NAME_PROPOSALS.md`
- `standards/2026-02-02_MULTIPLATFORM_COHERENCE_CHECKLIST.md`

No moves.

---

## 8. `docs/security/` — keep all (contributor-facing)

- `security/SECURITY_MODEL.md`
- `security/PRIVILEGE_DROPPING_STATUS.md`

No moves.

---

## 9. `docs/platform/` — keep implementation docs; move review/research

**Keep (public):**
- `platform/linux/LINUX_BUILD_SYSTEM_IMPLEMENTATION.md`
- `platform/linux/LINUX_FILE_OPERATIONS_IMPLEMENTATION.md`
- `platform/linux/LINUX_FILE_SYSTEM_MONITORING_RESEARCH.md` (or move if purely research)
- `platform/linux/LINUX_PREPARATION_REFACTORINGS.md`
- `platform/macos/2026-02-02_MACOS_PLATFORM_NOTES.md`
- `platform/windows/WINDOWS_DEFENDER_RECOGNITION.md`
- `platform/windows/WINLINE_ANALYSIS.md` (or move if internal-only analysis)

**Move to internal:**
- `platform/linux/REVIEW_CONTRIBUTOR_LINUX_BRANCH.md` — Contributor review, internal process

---

## 10. `docs/internal/` — already internal

**Action:** Either move `docs/internal/` to repo-root `internal-docs/` (e.g. `internal-docs/historical/`) for consistency, or leave under `docs/` and exclude `docs/internal/` from the public repo together with `internal-docs/`. Current contents:
- `internal/historical/2026-02-01_MACOS_DRAG_AND_DROP_TASK.md`
- `internal/historical/2026-02-02_COMPETITIVE_RESEARCH_HIGH_VALUE_FEATURES_TASK.md`
- `internal/historical/2026-02-02_HIDE_METRICS_UNLESS_SHOW_METRICS_FLAG_TASK.md`
- `internal/historical/2026-02-02_WINDOWS_NO_ELEVATION_FOLDER_CRAWL_TASK.md`

---

## 11. Root of `docs/`

**Keep:** `DOCUMENTATION_INDEX.md`, `README.md` (and update the index after moves so it only lists remaining public docs).

---

## After moving

1. **Update `docs/DOCUMENTATION_INDEX.md`** — Remove references to moved docs; list only what remains under `docs/`.
2. **Update `README.md`** project structure if it mentions `docs/` layout.
3. **Links:** Search for relative links from remaining public docs to moved files; fix or remove (or accept that they point to internal-only locations if the repo is split).

**Last updated:** 2026-02-20
