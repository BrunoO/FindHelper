# SonarCloud Lines of Code Reduction (Free Tier)

**Date:** 2026-02-03  
**Context:** SonarCloud free tier limits private repos to **50,000 lines of code**. This doc records scope reductions and dead-code findings to stay under the limit.

---

## 1. Scope changes (implemented)

**File:** `sonar-project.properties`

Exclusions are aligned with SonarCloud project settings (cloud configuration) and the free-tier 50k LOC limit:

| Exclusion                | Approx. lines | Reason |
|--------------------------|---------------|--------|
| `external/**`            | (all)         | Third-party code; must not be counted or analyzed. |
| `tests/**`               | ~10,300       | Test code; reduces ncloc and focuses analysis on production code. |
| `**/EmbeddedFont_*.cpp`  | ~7,000        | Embedded font data (byte arrays), not analyzable logic. |
| `scripts/**`             | ~1,000        | Build/helper scripts; match cloud scope. |
| `docs/**`                | (markdown etc.) | Documentation; match cloud scope. |

**Rough ncloc before:** src ~46k + tests ~10k + scripts ~1k → ~57k (over 50k).  
**After:** src (excluding embedded fonts) ~39k → under 50k.

---

## 2. Dead-code survey (no large removals)

- **ParallelSearchEngine deprecated helpers:** `CalculateChunkBytes` and `RecordThreadTiming` are still used by `LoadBalancingStrategy.cpp`. Removing them would require refactoring call sites to use `SearchStatisticsCollector` directly (~60 lines removed from the header, small gain). Left as-is; can be done later if needed.
- **WorkStealingStrategy fallback:** The `#else` stub when `FAST_LIBS_BOOST` is not defined is required for the interface; not dead code.
- **BuildFullPath / PathBuilder:** Both `BuildFullPath` and `BuildFullPathWithLogging` are used; no removal.
- **MftMetadataReader:** Documented as optional (“can be completely removed if MFT reading proves not beneficial”); removing it would be a feature/design decision, not a pure LOC cleanup.
- No large `#if 0` or commented-out blocks found; remaining S125-style comments are NOLINT/NOSONAR on the same line as code.

---

## 3. Summary

- **Done:** Exclude `tests/**` and `**/EmbeddedFont_*.cpp` in `sonar-project.properties` to bring analyzed LOC under the free-tier limit.
- **Optional later:** Refactor `LoadBalancingStrategy` to use `SearchStatisticsCollector` only and delete the deprecated wrappers in `ParallelSearchEngine` for a small LOC reduction.
- **Reference:** SonarCloud [pricing](https://www.sonarcloud.io/about/pricing) — free tier 50k LOC for private projects. `external/**`, `scripts/**`, and `docs/**` are excluded so local analysis matches the cloud configuration.
