# Architecture Review - 2026-02-14

## Executive Summary
- **Health Score**: 5/10
- **Critical Issues**: 0
- **High Issues**: 2
- **Total Findings**: 4
- **Estimated Remediation Effort**: 8 hours

## Findings


### High
1. **Tight Coupling in `Application.cpp`**
   - **Code**: `src/core/Application.cpp`
   - **Debt type**: Architecture Review
   - **Risk**: High coupling makes it hard to swap components or run in different modes (e.g., CLI-only without GUI).
   - **Suggested fix**: Use Interface-based dependency injection for all major components.
   - **Severity**: High
   - **Effort**: 20 hours

2. **Circular Dependencies via ImGui**
   - **Code**: `src/search/SearchTypes.h` and ImGui
   - **Debt type**: Architecture Review
   - **Risk**: Compilation bottlenecks and fragile include structure.
   - **Suggested fix**: Ensure `SearchTypes.h` remains the central header for shared types, decoupled from UI headers.
   - **Severity**: High
   - **Effort**: 4 hours


## Quick Wins
- Decouple SearchTypes from ImGui

## Recommended Actions
1. Implement a proper Event Bus or Mediator to decouple UI from Logic.
2. Continue refactoring towards dependency injection.
