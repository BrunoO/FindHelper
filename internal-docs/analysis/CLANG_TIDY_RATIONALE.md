# Rationale for Clang-Tidy Configuration Adjustments

This document explains the rationale behind the updates made to the `.clang-tidy` configuration.

## 1. Goal: Zero SonarQube Issues
The primary driver for the configuration is alignment with SonarQube rules. We've ensured that all checks that map to SonarQube reliability, security, and maintainability rules are enabled.

## 2. Noise Reduction

### Disabling `misc-include-cleaner`
While technically useful, `misc-include-cleaner` proved to be highly unstable and noisy in this environment. It frequently suggested removing necessary includes (due to template usage or indirect dependencies) or adding includes that were already transitively present. Disabling it significantly reduces the warning count while focusing on more critical issues.

### Disabling `modernize-use-trailing-return-type`
Trailing return types (`auto func() -> type`) are a modern C++ feature, but their forced adoption is intrusive and contradicts the established style of the FindHelper codebase. We've disabled this check to avoid thousands of stylistic warnings that don't improve code quality.

## 3. Style Alignment

### Naming Conventions
The previous configuration forced `KPascalCase` for constants (e.g., `KMaxPromptSize`). However, the project's established style (as seen in `src/api/GeminiApiUtils.cpp` and documented in `AGENTS.md`) is `kPascalCase`. We've updated the configuration to expect the `k` prefix with `CamelCase` for:
- `GlobalConstant`
- `Constant`
- `ConstantMember`
- `StaticConstant`

This alignment ensures that `clang-tidy` validates the *actual* project style rather than a generic one.

## 4. Stability and Environment
We observed segmentation faults in `clang-tidy` (v18.1.3) when processing certain files. We've documented workarounds (e.g., `-nostdinc++`) and focused on a subset of checks that are both high-value and stable.

---
*Date: January 16, 2025*
