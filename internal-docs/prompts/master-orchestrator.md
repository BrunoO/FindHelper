# Master Review Orchestrator

**Purpose:** Execute all specialized review prompts sequentially and generate time-stamped review reports in `docs/`.

---

## Orchestration Prompt

```
You are a Code Quality Orchestrator. Execute a comprehensive codebase review using the specialized prompts in `docs/prompts/`. For each review, generate a time-stamped report file.

## Instructions

Execute the following reviews IN ORDER. For each:
1. Read the prompt file from `docs/prompts/`
2. Execute the review following the prompt's instructions
3. Save the output to `docs/` with the naming pattern: `{REVIEW_TYPE}_{YYYY-MM-DD}.md`
4. Include a summary section at the end linking to all generated reports

---

## Review Sequence

### Phase 1: Code Quality
| Order | Prompt File | Output File |
|-------|-------------|-------------|
| 1 | `docs/prompts/tech-debt.md` | `docs/TECH_DEBT_REVIEW_{DATE}.md` |
| 2 | `docs/prompts/architecture-review.md` | `docs/ARCHITECTURE_REVIEW_{DATE}.md` |

### Phase 2: Reliability & Security
| Order | Prompt File | Output File |
|-------|-------------|-------------|
| 3 | `docs/prompts/security-review.md` | `docs/SECURITY_REVIEW_{DATE}.md` |
| 4 | `docs/prompts/error-handling-review.md` | `docs/ERROR_HANDLING_REVIEW_{DATE}.md` |

### Phase 3: Performance & Testing
| Order | Prompt File | Output File |
|-------|-------------|-------------|
| 5 | `docs/prompts/performance-review.md` | `docs/PERFORMANCE_REVIEW_{DATE}.md` |
| 6 | `docs/prompts/test-strategy-audit.md` | `docs/TEST_STRATEGY_REVIEW_{DATE}.md` |

### Phase 4: Documentation
| Order | Prompt File | Output File |
|-------|-------------|-------------|
| 7 | `docs/prompts/documentation-maintenance.md` | `docs/DOCUMENTATION_REVIEW_{DATE}.md` |

### Phase 5: User Experience
| Order | Prompt File | Output File |
|-------|-------------|-------------|
| 8 | `docs/prompts/ux-review.md` | `docs/UX_REVIEW_{DATE}.md` |

### Phase 6: Product Strategy & Competitive Analysis
| Order | Prompt File | Output File |
|-------|-------------|-------------|
| 9 | `docs/prompts/feature-exploration-product-owner.md` | `docs/FEATURE_EXPLORATION_REVIEW_{DATE}.md` |

### Phase 7: Static Analysis (clang-tidy warnings)
| Order | Prompt File | Output File |
|-------|-------------|-------------|
| 10 | `docs/prompts/2026-02-14_AGENT_INSTRUCTIONS_UPDATE_CLANG_TIDY_STATUS.md` | `docs/CLANG_TIDY_WARNINGS_REVIEW_{DATE}.md` |

---

## Report Format

Each generated report MUST follow this structure:

```markdown
# {Review Type} Review - {YYYY-MM-DD}

## Executive Summary
- **Health Score**: X/10
- **Critical Issues**: N
- **High Issues**: N
- **Total Findings**: N
- **Estimated Remediation Effort**: X hours

## Findings

### Critical
[List critical findings with details per prompt format]

### High
[List high findings]

### Medium
[List medium findings]

### Low
[List low findings]

## Quick Wins
[Top 3-5 easy fixes with high impact]

## Recommended Actions
[Prioritized remediation plan]
```

---

## Final Summary Report

After completing all reviews, generate a master summary:

**File**: `docs/COMPREHENSIVE_REVIEW_SUMMARY_{DATE}.md`

```markdown
# Comprehensive Code Review Summary - {YYYY-MM-DD}

## Overall Health

| Review Area | Score | Critical | High | Medium | Low |
|-------------|-------|----------|------|--------|-----|
| Tech Debt | X/10 | N | N | N | N |
| Architecture | X/10 | N | N | N | N |
| Security | X/10 | N | N | N | N |
| Error Handling | X/10 | N | N | N | N |
| Performance | X/10 | N | N | N | N |
| Testing | X/10 | N | N | N | N |
| Documentation | X/10 | N | N | N | N |
| User Experience | X/10 | N | N | N | N |
| Product Strategy | X/10 | N | N | N | N |
| **Overall** | **X/10** | **N** | **N** | **N** | **N** |

## Top Priority Items
[Top 10 issues across all reviews, ranked by impact]

## Generated Reports
- [Tech Debt Review](./TECH_DEBT_REVIEW_{DATE}.md)
- [Architecture Review](./ARCHITECTURE_REVIEW_{DATE}.md)
- [Security Review](./SECURITY_REVIEW_{DATE}.md)
- [Error Handling Review](./ERROR_HANDLING_REVIEW_{DATE}.md)
- [Performance Review](./PERFORMANCE_REVIEW_{DATE}.md)
- [Test Strategy Review](./TEST_STRATEGY_REVIEW_{DATE}.md)
- [Documentation Review](./DOCUMENTATION_REVIEW_{DATE}.md)
- [UX Review](./UX_REVIEW_{DATE}.md)
 - [Feature Exploration Review](./FEATURE_EXPLORATION_REVIEW_{DATE}.md)
- [Clang-Tidy Warnings Review](./CLANG_TIDY_WARNINGS_REVIEW_{DATE}.md)

## Next Review Date
Recommended: {DATE + 30 days}
```

---

## Execution Options

### Full Review (All 10 steps)
Run the complete sequence as described above (Phases 1–7, including clang-tidy status update).

### Quick Review (Code Quality Only)
Run only:
1. `tech-debt.md`
2. `architecture-review.md`

### Security Focus
Run only:
1. `security-review.md`
2. `error-handling-review.md`

### Pre-Release Review
Run in order:
1. `test-strategy-audit.md`
2. `security-review.md`
3. `error-handling-review.md`
4. `documentation-maintenance.md`
5. `ux-review.md`

### UX Focus
Run only:
1. `ux-review.md`

---

## Post-Review Actions

After generating reports:
1. Update `docs/DOCUMENTATION_INDEX.md` with new review files
2. Create GitHub issues for critical/high findings (optional)
3. Move previous reviews to `docs/Historical/` if superseded
4. Schedule next review date
5. **Phase 7:** Ensure `docs/CLANG_TIDY_WARNINGS_REVIEW_{DATE}.md` was created per `docs/prompts/2026-02-14_AGENT_INSTRUCTIONS_UPDATE_CLANG_TIDY_STATUS.md`
```

---

## Usage

To execute a full review:
```
Run a comprehensive code review using the master orchestrator prompt in docs/prompts/master-orchestrator.md
```

To execute a specific subset:
```
Run a pre-release review using only the security, error handling, testing, and documentation prompts from docs/prompts/
```
