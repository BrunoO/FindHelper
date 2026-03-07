# Feature Exploration Review - 2026-02-14

## Executive Summary
- **Health Score**: 8/10
- **Critical Issues**: 0
- **High Issues**: 0
- **Total Findings**: 3
- **Estimated Remediation Effort**: 6 hours

## Findings


### Medium
1. **AI-Assisted Search (Gemini)**
   - **Code**: `src/api/GeminiApiUtils.cpp`
   - **Debt type**: Product Strategy
   - **Risk**: Dependency on external API and network; privacy concerns for local file indexing.
   - **Suggested fix**: Make it strictly opt-in and clarify what data is sent (only the query).
   - **Severity**: Medium
   - **Effort**: 4 hours

### Low
1. **Linux USN Monitoring equivalent**
   - **Code**: `src/platform/linux/`
   - **Debt type**: Product Strategy
   - **Risk**: Feature parity gap between Windows and Linux.
   - **Suggested fix**: Explore `inotify` or `fanotify` for Linux real-time monitoring.
   - **Severity**: Low
   - **Effort**: 40 hours


## Quick Wins
- Clarify Gemini API privacy.

## Recommended Actions
1. Research Linux real-time monitoring alternatives.
2. Improve AI query suggestions.
