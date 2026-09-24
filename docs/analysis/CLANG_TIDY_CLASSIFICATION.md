# Clang-Tidy Findings Classification

This document classifies the findings from the initial `clang-tidy` run on the FindHelper project. Findings are categorized based on their impact on reliability, performance, maintainability, and noise level.

## 1. Highly Recommended (Reliability & Security)

These checks identify potential bugs, logic errors, or security risks. Addressing these is a priority for project stability.

| Check Name | Description | Example Finding |
|------------|-------------|-----------------|
| `bugprone-branch-clone` | Detects switch statements or if-else chains with identical branches. | `SizeFilterUtils.cpp`: Switch has 4 consecutive identical branches. |
| `bugprone-empty-catch` | Detects empty catch blocks that swallow exceptions without handling. | `SearchResultUtils.cpp`: Empty catch statement hides potential issues. |
| `bugprone-narrowing-conversions` | Detects narrowing conversions that could lead to data loss. | `MetricsWindow.cpp`: Narrowing conversion from `size_t` to `double`. |
| `bugprone-implicit-widening-of-multiplication-result` | Detects potential overflows before widening. | `GeminiApiUtils.cpp`: Multiplication in `int` before widening to `size_t`. |

## 2. Recommended (Performance & Maintainability)

These checks improve code efficiency or long-term maintainability.

| Check Name | Description | Rationale |
|------------|-------------|-----------|
| `performance-*` | Various performance optimizations (e.g., unnecessary copies). | Essential for a high-performance search engine. |
| `misc-const-correctness` | Suggests `const` for variables that are never modified. | Improves code intent and allows compiler optimizations. |
| `readability-braces-around-statements` | Enforces braces for all control flow statements. | Prevents "dangling else" style bugs and improves readability. |
| `readability-magic-numbers` | Detects unexplained numeric literals. | Improves maintainability by encouraging named constants. |

## 3. Style (Consistency)

These checks ensure a consistent coding style across the project.

| Check Name | Description | Status |
|------------|-------------|--------|
| `readability-identifier-naming` | Enforces naming conventions. | **Needs Adjustment**: Current config forces `KPascalCase` while project uses `kPascalCase`. |
| `modernize-use-equals-default` | Suggests using `= default` for default constructors/destructors. | Standard C++17 practice. |
| `readability-convert-member-functions-to-static` | Identifies methods that don't use `this`. | Helps identify opportunities for refactoring or decoupling. |
| `modernize-avoid-c-arrays` | Suggests `std::array` or `std::vector` instead of C-style arrays. | Safer and more modern alternative. |

## 4. Noise (Ignore or Disable)

These checks are considered too aggressive, incompatible with project style, or produce too many false positives.

| Check Name | Description | Reason for Disabling |
|------------|-------------|----------------------|
| `modernize-use-trailing-return-type` | Forces `auto func() -> type` syntax. | Highly intrusive and not the established project style. |
| `misc-include-cleaner` | Suggests removing unused includes or adding missing ones. | Often unreliable with complex dependencies and produced many false positives. |
| `readability-identifier-length` | Flag short variable names like `i`, `id`, `c`. | Short names are often appropriate for loop counters or well-understood contexts. |
| `bugprone-easily-swappable-parameters` | Warns when adjacent parameters have same type. | High noise in established APIs where parameter order is already set. |

---
*Date: January 16, 2025*
