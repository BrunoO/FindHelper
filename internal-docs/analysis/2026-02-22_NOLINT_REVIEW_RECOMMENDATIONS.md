# NOLINT Review Recommendations (2026-02-22)

## Scope and Method
This document provides a comprehensive review of all `NOLINT` and `NOLINTNEXTLINE` suppressions in the `src/` and `tests/` directories. The goal is to identify obsolete suppressions (due to disabled checks in `.clang-tidy`) and identify code that could be fixed instead of suppressed.

### Method:
1.  **Extraction**: Parsed `.clang-tidy` to identify enabled and disabled checks.
2.  **Scanning**: Grepped `src/` and `tests/` (excluding `external/`) for all `NOLINT` occurrences.
3.  **Classification**:
    - **Obsolete**: All suppressed checks are disabled in `.clang-tidy`. Recommendation: Remove `NOLINT`.
    - **Worth Fixing**: At least one enabled check can be easily fixed (e.g., `modernize-use-auto`, `readability-redundant-member-init`). Recommendation: Fix code and remove `NOLINT`.
    - **Still Relevant**: Suppressed checks are enabled and the suppression is justified by comments or API/platform constraints. Recommendation: Keep.

## Disabled Checks in .clang-tidy
The following check patterns are currently disabled in `.clang-tidy`. `NOLINT`s only suppressing these are obsolete:

- `abseil-*`
- `altera-*`
- `android-*`
- `boost-use-ranges`
- `boost-use-to-string`
- `clang-analyzer-*`
- `google-readability-avoid-underscore-in-googletest-name`
- `google-upgrade-googletest-case`
- `linuxkernel-*`
- `bugprone-easily-swappable-parameters`
- `cppcoreguidelines-avoid-do-while`
- `cppcoreguidelines-avoid-magic-numbers`
- `cppcoreguidelines-macro-to-enum`
- `cppcoreguidelines-special-member-functions`
- `fuchsia-*`
- `google-readability-namespace-comments`
- `hicpp-special-member-functions`
- `hicpp-uppercase-literal-suffix`
- `llvm-header-guard`
- `llvm-namespace-comment`
- `llvm-prefer-static-over-anonymous-namespace`
- `llvmlibc-*`
- `misc-include-cleaner`
- `misc-use-anonymous-namespace`
- `misc-use-internal-linkage`
- `modernize-macro-to-enum`
- `modernize-use-trailing-return-type`
- `portability-avoid-pragma-once`
- `readability-convert-member-functions-to-static`
- `readability-identifier-length`
- `readability-redundant-inline-specifier`
- `readability-redundant-preprocessor`
- `readability-uppercase-literal-suffix`
- `readability-use-concise-preprocessor-directives`

## Summary

| Classification | Count |
| :--- | :--- |
| Obsolete | 0 |
| Still Relevant | 1636 |
| Worth Fixing | 102 |
| **Total** | **1738** |

Note: 0 items are partially obsolete (contain at least one disabled check but also keep at least one enabled check).

## Complete Audit List

| File | Line | Checks | Classification | Recommendation |
| :--- | :--- | :--- | :--- | :--- |
| `src/api/GeminiApiHttp.h` | 3 | `clang-diagnostic-error` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/api/GeminiApiHttp_linux.cpp` | 53 | `readability-non-const-parameter` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/api/GeminiApiHttp_linux.cpp` | 84 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/api/GeminiApiHttp_linux.cpp` | 87 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/api/GeminiApiHttp_linux.cpp` | 89 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/api/GeminiApiHttp_linux.cpp` | 96 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/api/GeminiApiHttp_linux.cpp` | 98 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/api/GeminiApiHttp_linux.cpp` | 100 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/api/GeminiApiHttp_linux.cpp` | 102 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/api/GeminiApiHttp_linux.cpp` | 104 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/api/GeminiApiHttp_linux.cpp` | 106 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/api/GeminiApiHttp_linux.cpp` | 109 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/api/GeminiApiHttp_linux.cpp` | 111 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/api/GeminiApiHttp_linux.cpp` | 113 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/api/GeminiApiHttp_linux.cpp` | 115 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/api/GeminiApiHttp_linux.cpp` | 121 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/api/GeminiApiUtils.cpp` | 19 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/api/GeminiApiUtils.cpp` | 42 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/api/GeminiApiUtils.cpp` | 117 | `cppcoreguidelines-init-variables, concurrency-mt-unsafe` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/api/GeminiApiUtils.cpp` | 167 | `readability-magic-numbers` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/api/GeminiApiUtils.cpp` | 168 | `readability-magic-numbers` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/api/GeminiApiUtils.cpp` | 169 | `readability-math-missing-parentheses` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/api/GeminiApiUtils.cpp` | 430 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/api/GeminiApiUtils.cpp` | 792 | `bugprone-exception-escape` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/api/GeminiApiUtils.h` | 3 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/core/AppBootstrapCommon.h` | 43 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/AppBootstrapCommon.h` | 152 | `bugprone-unused-local-non-trivial-variable` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/AppBootstrapCommon.h` | 236 | `readability-non-const-parameter` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/AppBootstrapCommon.h` | 239 | `readability-non-const-parameter` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/AppBootstrapResultBase.h` | 46 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/core/AppBootstrapResultBase.h` | 48 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/core/AppBootstrapResultBase.h` | 50 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/core/AppBootstrapResultBase.h` | 52 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/core/AppBootstrapResultBase.h` | 54 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/core/AppBootstrapResultBase.h` | 56 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/core/AppBootstrapResultBase.h` | 58 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/core/AppBootstrapResultBase.h` | 61 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/core/AppBootstrapResultBase.h` | 65 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/core/Application.cpp` | 56 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/core/Application.cpp` | 58 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/core/Application.cpp` | 89 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/Application.cpp` | 104 | `llvm-include-order` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/Application.cpp` | 108 | `llvm-include-order` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/Application.cpp` | 109 | `llvm-include-order` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/Application.cpp` | 110 | `llvm-include-order` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/Application.cpp` | 162 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/Application.cpp` | 218 | `bugprone-empty-catch` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/Application.cpp` | 230 | `bugprone-empty-catch` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/Application.cpp` | 360 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/Application.cpp` | 364 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/core/Application.cpp` | 412 | `readability-function-cognitive-complexity` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/Application.cpp` | 603 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/Application.cpp` | 652 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/core/Application.cpp` | 661 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/Application.cpp` | 693 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/Application.h` | 229 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/core/Application.h` | 230 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/core/Application.h` | 263 | `readability-redundant-member-init` | Worth Fixing | Worth fixing; address readability-redundant-member-init and remove NOLINT. |
| `src/core/Application.h` | 265 | `readability-redundant-member-init` | Worth Fixing | Worth fixing; address readability-redundant-member-init and remove NOLINT. |
| `src/core/Application.h` | 270 | `readability-redundant-member-init` | Worth Fixing | Worth fixing; address readability-redundant-member-init and remove NOLINT. |
| `src/core/Application.h` | 288 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/core/ApplicationLogic.cpp` | 101 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/core/ApplicationLogic.cpp` | 136 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/core/CommandLineArgs.cpp` | 88 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/CommandLineArgs.cpp` | 109 | `cert-err34-c` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/CommandLineArgs.cpp` | 124 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/core/CommandLineArgs.cpp` | 125 | `cert-err34-c, cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/CommandLineArgs.cpp` | 203 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/IndexBuildState.h` | 17 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/core/IndexBuildState.h` | 20 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/core/IndexBuildState.h` | 22 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/core/IndexBuildState.h` | 24 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/core/IndexBuildState.h` | 26 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/core/IndexBuildState.h` | 28 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/core/IndexBuildState.h` | 32 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/core/IndexBuildState.h` | 34 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/core/IndexBuildState.h` | 36 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/core/IndexBuildState.h` | 38 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/core/IndexBuildState.h` | 43 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/core/IndexBuildState.h` | 47 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/core/IndexBuildState.h` | 49 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/core/IndexBuilder.h` | 37 | `cppcoreguidelines-pro-type-member-init, hicpp-member-init` | Worth Fixing | Worth fixing; address cppcoreguidelines-pro-type-member-init, hicpp-member-init and remove NOLINT. |
| `src/core/IndexBuilder.h` | 97 | `google-objc-function-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/IndexBuilder.h` | 102 | `google-objc-function-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/Settings.cpp` | 57 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/Settings.cpp` | 77 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/core/Settings.cpp` | 79 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/core/Settings.cpp` | 548 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/Settings.cpp` | 599 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/Settings.h` | 34 | `misc-non-private-member-variables-in-classes, readability-identifier-naming, readability-redundant-member-init` | Worth Fixing | Worth fixing; address readability-redundant-member-init and remove NOLINT. |
| `src/core/Settings.h` | 35 | `misc-non-private-member-variables-in-classes, readability-identifier-naming, readability-redundant-member-init` | Worth Fixing | Worth fixing; address readability-redundant-member-init and remove NOLINT. |
| `src/core/Settings.h` | 36 | `misc-non-private-member-variables-in-classes, readability-identifier-naming, readability-redundant-member-init` | Worth Fixing | Worth fixing; address readability-redundant-member-init and remove NOLINT. |
| `src/core/Settings.h` | 37 | `misc-non-private-member-variables-in-classes, readability-identifier-naming, readability-redundant-member-init` | Worth Fixing | Worth fixing; address readability-redundant-member-init and remove NOLINT. |
| `src/core/Settings.h` | 38 | `misc-non-private-member-variables-in-classes, readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/Settings.h` | 39 | `misc-non-private-member-variables-in-classes, readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/Settings.h` | 40 | `misc-non-private-member-variables-in-classes, readability-identifier-naming, readability-redundant-member-init` | Worth Fixing | Worth fixing; address readability-redundant-member-init and remove NOLINT. |
| `src/core/Settings.h` | 41 | `misc-non-private-member-variables-in-classes, readability-identifier-naming, readability-redundant-member-init` | Worth Fixing | Worth fixing; address readability-redundant-member-init and remove NOLINT. |
| `src/core/Settings.h` | 42 | `misc-non-private-member-variables-in-classes, readability-identifier-naming, readability-redundant-member-init` | Worth Fixing | Worth fixing; address readability-redundant-member-init and remove NOLINT. |
| `src/core/Settings.h` | 47 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/Settings.h` | 48 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/Settings.h` | 49 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/Settings.h` | 59 | `readability-identifier-naming, readability-redundant-string-init` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/Settings.h` | 63 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/Settings.h` | 66 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/Settings.h` | 70 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/Settings.h` | 74 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/Settings.h` | 75 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/Settings.h` | 78 | `readability-identifier-naming, readability-redundant-member-init` | Worth Fixing | Worth fixing; address readability-redundant-member-init and remove NOLINT. |
| `src/core/Settings.h` | 82 | `readability-identifier-naming, readability-redundant-member-init` | Worth Fixing | Worth fixing; address readability-redundant-member-init and remove NOLINT. |
| `src/core/Settings.h` | 90 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/Settings.h` | 97 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/Settings.h` | 106 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/Settings.h` | 115 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/Settings.h` | 124 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/Settings.h` | 131 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/Settings.h` | 145 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/Settings.h` | 150 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/Settings.h` | 155 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/Settings.h` | 164 | `readability-identifier-naming, readability-redundant-member-init` | Worth Fixing | Worth fixing; address readability-redundant-member-init and remove NOLINT. |
| `src/core/Version.h` | 19 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/core/main_common.h` | 153 | `google-objc-function-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/core/main_common.h` | 175 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/crawler/FolderCrawler.cpp` | 43 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/crawler/FolderCrawler.cpp` | 68 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/crawler/FolderCrawler.cpp` | 69 | `readability-function-cognitive-complexity` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/crawler/FolderCrawler.cpp` | 211 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/crawler/FolderCrawler.cpp` | 218 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/crawler/FolderCrawler.cpp` | 284 | `hicpp-named-parameter, readability-named-parameter` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/crawler/FolderCrawler.cpp` | 300 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/crawler/FolderCrawler.cpp` | 330 | `hicpp-named-parameter, readability-named-parameter` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/crawler/FolderCrawler.cpp` | 334 | `hicpp-named-parameter, readability-named-parameter` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/crawler/FolderCrawler.cpp` | 351 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/crawler/FolderCrawler.cpp` | 427 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/crawler/FolderCrawler.cpp` | 429 | `readability-non-const-parameter` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/crawler/FolderCrawler.cpp` | 456 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/crawler/FolderCrawler.cpp` | 550 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/crawler/FolderCrawler.cpp` | 577 | `concurrency-mt-unsafe` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/crawler/FolderCrawler.cpp` | 579 | `cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/crawler/FolderCrawler.cpp` | 584 | `cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/crawler/FolderCrawler.cpp` | 596 | `cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/crawler/FolderCrawler.cpp` | 604 | `hicpp-signed-bitwise` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/crawler/FolderCrawler.cpp` | 605 | `hicpp-signed-bitwise` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/crawler/FolderCrawler.cpp` | 617 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/crawler/FolderCrawler.cpp` | 641 | `concurrency-mt-unsafe` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/crawler/FolderCrawler.h` | 3 | `clang-diagnostic-error` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/crawler/FolderCrawler.h` | 84 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/crawler/FolderCrawler.h` | 96 | `readability-avoid-const-params-in-decls` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/crawler/FolderCrawler.h` | 107 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/crawler/FolderCrawler.h` | 108 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/crawler/FolderCrawler.h` | 111 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/crawler/FolderCrawler.h` | 112 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/crawler/FolderCrawler.h` | 115 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/crawler/FolderCrawler.h` | 116 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/crawler/FolderCrawler.h` | 117 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/crawler/FolderCrawler.h` | 118 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/crawler/FolderCrawler.h` | 119 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/crawler/FolderCrawler.h` | 122 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/crawler/FolderCrawler.h` | 123 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/crawler/FolderCrawler.h` | 124 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/crawler/FolderCrawlerIndexBuilder.cpp` | 37 | `readability-function-cognitive-complexity` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/crawler/FolderCrawlerIndexBuilder.cpp` | 45 | `misc-const-correctness` | Worth Fixing | Worth fixing; address misc-const-correctness and remove NOLINT. |
| `src/crawler/FolderCrawlerIndexBuilder.cpp` | 59 | `readability-function-cognitive-complexity` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/crawler/FolderCrawlerIndexBuilder.cpp` | 126 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/crawler/FolderCrawlerIndexBuilder.cpp` | 127 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/crawler/FolderCrawlerIndexBuilder.cpp` | 128 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/crawler/FolderCrawlerIndexBuilder.cpp` | 129 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/crawler/FolderCrawlerIndexBuilder.cpp` | 130 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/crawler/IndexOperations.h` | 3 | `clang-diagnostic-error` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/crawler/IndexOperations.h` | 136 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/crawler/IndexOperations.h` | 137 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/crawler/IndexOperations.h` | 138 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/crawler/IndexOperations.h` | 139 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/filters/SizeFilter.h` | 12 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/filters/SizeFilterUtils.cpp` | 131 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/filters/TimeFilter.h` | 12 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/filters/TimeFilterUtils.cpp` | 253 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/filters/TimeFilterUtils.cpp` | 255 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/filters/TimeFilterUtils.cpp` | 269 | `cppcoreguidelines-pro-type-member-init, hicpp-member-init` | Worth Fixing | Worth fixing; address cppcoreguidelines-pro-type-member-init, hicpp-member-init and remove NOLINT. |
| `src/filters/TimeFilterUtils.cpp` | 321 | `readability-magic-numbers` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/filters/TimeFilterUtils.cpp` | 349 | `readability-magic-numbers` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/gui/GuiState.cpp` | 63 | `bugprone-empty-catch` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/gui/GuiState.cpp` | 79 | `bugprone-empty-catch` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/gui/GuiState.h` | 3 | `clang-diagnostic-error` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/gui/GuiState.h` | 26 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 28 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 31 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 58 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 62 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 64 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 66 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 68 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 70 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 73 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 75 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 81 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 83 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 86 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 89 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 91 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 93 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 95 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 98 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 100 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 102 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 104 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 107 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 109 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 112 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 114 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 120 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/gui/GuiState.h` | 121 | `misc-non-private-member-variables-in-classes, readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/gui/GuiState.h` | 122 | `misc-non-private-member-variables-in-classes, readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/gui/GuiState.h` | 124 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 126 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 129 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 131 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 133 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 135 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 156 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 158 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 160 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 162 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 164 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 166 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 168 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 170 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 173 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 175 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 179 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 181 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 186 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 189 | `misc-non-private-member-variables-in-classes, readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/gui/GuiState.h` | 195 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 198 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 202 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 207 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 211 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 213 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 215 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 219 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 221 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 225 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 229 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 231 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 235 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 238 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 242 | `misc-non-private-member-variables-in-classes, readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/gui/GuiState.h` | 243 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 245 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 247 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 251 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 254 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 256 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 259 | `misc-non-private-member-variables-in-classes, readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/gui/GuiState.h` | 261 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 263 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 265 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 267 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 272 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 274 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 276 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 280 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 282 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 286 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 288 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 293 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 295 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 297 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 299 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 301 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 306 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 308 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 313 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 315 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 317 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 319 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 321 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 323 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/GuiState.h` | 325 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/gui/ImGuiUtils.h` | 21 | `readability-math-missing-parentheses` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/gui/UIActions.h` | 15 | `clang-diagnostic-error` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/imgui_config.h` | 14 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndex.cpp` | 26 | `cppcoreguidelines-pro-type-member-init, hicpp-member-init` | Worth Fixing | Worth fixing; address cppcoreguidelines-pro-type-member-init, hicpp-member-init and remove NOLINT. |
| `src/index/FileIndex.cpp` | 163 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndex.cpp` | 164 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndex.cpp` | 173 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndex.cpp` | 202 | `hicpp-named-parameter, readability-named-parameter` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndex.cpp` | 380 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndex.cpp` | 384 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndex.cpp` | 439 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndex.h` | 97 | `readability-avoid-const-params-in-decls` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndex.h` | 119 | `cppcoreguidelines-missing-std-forward` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndex.h` | 132 | `cppcoreguidelines-missing-std-forward` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndex.h` | 139 | `bugprone-branch-clone` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndex.h` | 163 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndex.h` | 183 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndex.h` | 191 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndex.h` | 206 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndex.h` | 217 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndex.h` | 392 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndex.h` | 395 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndex.h` | 451 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndex.h` | 455 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndex.h` | 458 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndex.h` | 468 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndex.h` | 469 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndex.h` | 473 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndex.h` | 475 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndex.h` | 477 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndex.h` | 482 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndex.h` | 486 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndex.h` | 490 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndex.h` | 493 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndex.h` | 496 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndex.h` | 498 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndexMaintenance.cpp` | 94 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndexMaintenance.h` | 98 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndexMaintenance.h` | 99 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndexMaintenance.h` | 100 | `readability-identifier-naming, readability-redundant-member-init` | Worth Fixing | Worth fixing; address readability-redundant-member-init and remove NOLINT. |
| `src/index/FileIndexMaintenance.h` | 101 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndexMaintenance.h` | 102 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndexMaintenance.h` | 103 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndexStorage.cpp` | 150 | `cppcoreguidelines-init-variables, misc-const-correctness` | Worth Fixing | Worth fixing; address misc-const-correctness and remove NOLINT. |
| `src/index/FileIndexStorage.cpp` | 158 | `cppcoreguidelines-init-variables, misc-const-correctness` | Worth Fixing | Worth fixing; address misc-const-correctness and remove NOLINT. |
| `src/index/FileIndexStorage.h` | 45 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndexStorage.h` | 48 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndexStorage.h` | 49 | `readability-identifier-naming, readability-redundant-member-init` | Worth Fixing | Worth fixing; address readability-redundant-member-init and remove NOLINT. |
| `src/index/FileIndexStorage.h` | 50 | `readability-identifier-naming, readability-redundant-member-init` | Worth Fixing | Worth fixing; address readability-redundant-member-init and remove NOLINT. |
| `src/index/FileIndexStorage.h` | 121 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndexStorage.h` | 122 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/FileIndexStorage.h` | 126 | `cppcoreguidelines-avoid-const-or-ref-data-members, readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/IndexFromFilePopulator.cpp` | 49 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/IndexFromFilePopulator.h` | 13 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/InitialIndexPopulator.cpp` | 45 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/InitialIndexPopulator.cpp` | 88 | `misc-const-correctness` | Worth Fixing | Worth fixing; address misc-const-correctness and remove NOLINT. |
| `src/index/InitialIndexPopulator.cpp` | 89 | `misc-const-correctness` | Worth Fixing | Worth fixing; address misc-const-correctness and remove NOLINT. |
| `src/index/InitialIndexPopulator.cpp` | 202 | `readability-non-const-parameter` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/InitialIndexPopulator.cpp` | 215 | `misc-const-correctness` | Worth Fixing | Worth fixing; address misc-const-correctness and remove NOLINT. |
| `src/index/InitialIndexPopulator.cpp` | 230 | `misc-const-correctness` | Worth Fixing | Worth fixing; address misc-const-correctness and remove NOLINT. |
| `src/index/InitialIndexPopulator.cpp` | 250 | `misc-const-correctness` | Worth Fixing | Worth fixing; address misc-const-correctness and remove NOLINT. |
| `src/index/InitialIndexPopulator.cpp` | 254 | `misc-const-correctness` | Worth Fixing | Worth fixing; address misc-const-correctness and remove NOLINT. |
| `src/index/InitialIndexPopulator.cpp` | 257 | `misc-const-correctness` | Worth Fixing | Worth fixing; address misc-const-correctness and remove NOLINT. |
| `src/index/LazyAttributeLoader.cpp` | 41 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/index/LazyAttributeLoader.cpp` | 43 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/index/LazyAttributeLoader.cpp` | 45 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/index/LazyAttributeLoader.cpp` | 395 | `bugprone-empty-catch` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/LazyAttributeLoader.h` | 64 | `cppcoreguidelines-avoid-const-or-ref-data-members, readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/LazyAttributeLoader.h` | 65 | `cppcoreguidelines-avoid-const-or-ref-data-members, readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/LazyAttributeLoader.h` | 66 | `cppcoreguidelines-avoid-const-or-ref-data-members, readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/LazyValue.h` | 33 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/index/LazyValue.h` | 68 | `google-explicit-constructor, hicpp-explicit-conversions` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/LazyValue.h` | 83 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/index/PathRecomputer.h` | 46 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/index/PathRecomputer.h` | 48 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/index/PathRecomputer.h` | 50 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/index/mft/MftMetadataReader.cpp` | 27 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/index/mft/MftMetadataReader.cpp` | 55 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/index/mft/MftMetadataReader.cpp` | 69 | `cppcoreguidelines-avoid-c-arrays, hicpp-avoid-c-arrays, modernize-avoid-c-arrays` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/mft/MftMetadataReader.cpp` | 77 | `cppcoreguidelines-avoid-c-arrays, hicpp-avoid-c-arrays, modernize-avoid-c-arrays` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/mft/MftMetadataReader.cpp` | 98 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/index/mft/MftMetadataReader.cpp` | 108 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/index/mft/MftMetadataReader.cpp` | 111 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/index/mft/MftMetadataReader.cpp` | 128 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/index/mft/MftMetadataReader.cpp` | 131 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/index/mft/MftMetadataReader.cpp` | 135 | `cppcoreguidelines-avoid-c-arrays, hicpp-avoid-c-arrays, modernize-avoid-c-arrays` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/mft/MftMetadataReader.cpp` | 137 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/index/mft/MftMetadataReader.cpp` | 148 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/index/mft/MftMetadataReader.cpp` | 191 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/index/mft/MftMetadataReader.cpp` | 192 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/index/mft/MftMetadataReader.cpp` | 210 | `readability-non-const-parameter` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/mft/MftMetadataReader.cpp` | 219 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/index/mft/MftMetadataReader.cpp` | 221 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/mft/MftMetadataReader.cpp` | 263 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/mft/MftMetadataReader.cpp` | 305 | `readability-non-const-parameter` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/mft/MftMetadataReader.cpp` | 308 | `cppcoreguidelines-pro-type-reinterpret-cast` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/mft/MftMetadataReader.cpp` | 326 | `cppcoreguidelines-pro-type-reinterpret-cast, cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/mft/MftMetadataReader.cpp` | 330 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/mft/MftMetadataReader.cpp` | 335 | `cppcoreguidelines-pro-type-reinterpret-cast` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/mft/MftMetadataReader.cpp` | 349 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/index/mft/MftMetadataReader.cpp` | 360 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/index/mft/MftMetadataReader.cpp` | 377 | `cppcoreguidelines-pro-type-reinterpret-cast` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/mft/MftMetadataReader.cpp` | 387 | `cppcoreguidelines-pro-type-reinterpret-cast` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/mft/MftMetadataReader.cpp` | 404 | `cppcoreguidelines-pro-type-reinterpret-cast` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/mft/MftMetadataReader.cpp` | 411 | `cppcoreguidelines-pro-type-reinterpret-cast` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/mft/MftMetadataReader.cpp` | 439 | `cppcoreguidelines-pro-type-reinterpret-cast, cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/mft/MftMetadataReader.cpp` | 445 | `cppcoreguidelines-pro-type-reinterpret-cast` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/mft/MftMetadataReader.cpp` | 488 | `cppcoreguidelines-pro-type-reinterpret-cast, cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/mft/MftMetadataReader.cpp` | 495 | `cppcoreguidelines-pro-type-reinterpret-cast` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/mft/MftMetadataReader.cpp` | 508 | `misc-const-correctness` | Worth Fixing | Worth fixing; address misc-const-correctness and remove NOLINT. |
| `src/index/mft/MftMetadataReader.cpp` | 518 | `cppcoreguidelines-pro-type-reinterpret-cast, cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/mft/MftMetadataReader.cpp` | 522 | `cppcoreguidelines-pro-type-reinterpret-cast` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/mft/MftMetadataReader.cpp` | 525 | `cppcoreguidelines-pro-type-reinterpret-cast, cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/mft/MftMetadataReader.cpp` | 534 | `cppcoreguidelines-pro-type-reinterpret-cast, cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/mft/MftMetadataReader.h` | 54 | `cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-type-const-cast, cppcoreguidelines-pro-type-reinterpret-cast` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/index/mft/MftMetadataReader.h` | 58 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/index/mft/MftMetadataReader.h` | 61 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/DirectoryResolver.h` | 86 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/DirectoryResolver.h` | 88 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/DirectoryResolver.h` | 90 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathBuilder.cpp` | 75 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathOperations.cpp` | 18 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathOperations.h` | 36 | `readability-redundant-member-init` | Worth Fixing | Worth fixing; address readability-redundant-member-init and remove NOLINT. |
| `src/path/PathOperations.h` | 37 | `readability-redundant-member-init` | Worth Fixing | Worth fixing; address readability-redundant-member-init and remove NOLINT. |
| `src/path/PathOperations.h` | 38 | `readability-redundant-member-init` | Worth Fixing | Worth fixing; address readability-redundant-member-init and remove NOLINT. |
| `src/path/PathOperations.h` | 39 | `readability-redundant-member-init` | Worth Fixing | Worth fixing; address readability-redundant-member-init and remove NOLINT. |
| `src/path/PathOperations.h` | 68 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathOperations.h` | 127 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathOperations.h` | 160 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathPatternMatcher.cpp` | 54 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.cpp` | 56 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.cpp` | 59 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.cpp` | 65 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathPatternMatcher.cpp` | 70 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.cpp` | 72 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathPatternMatcher.cpp` | 73 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathPatternMatcher.cpp` | 74 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathPatternMatcher.cpp` | 75 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathPatternMatcher.cpp` | 76 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathPatternMatcher.cpp` | 77 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathPatternMatcher.cpp` | 78 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathPatternMatcher.cpp` | 82 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.cpp` | 84 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.cpp` | 86 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.cpp` | 90 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.cpp` | 92 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.cpp` | 118 | `cppcoreguidelines-owning-memory` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathPatternMatcher.cpp` | 156 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.cpp` | 158 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathPatternMatcher.cpp` | 159 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathPatternMatcher.cpp` | 160 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathPatternMatcher.cpp` | 161 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathPatternMatcher.cpp` | 162 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathPatternMatcher.cpp` | 205 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathPatternMatcher.cpp` | 259 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.cpp` | 300 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.cpp` | 346 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.cpp` | 380 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathPatternMatcher.cpp` | 394 | `bugprone-branch-clone` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathPatternMatcher.cpp` | 402 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.cpp` | 427 | `cppcoreguidelines-avoid-c-arrays, hicpp-avoid-c-arrays, modernize-avoid-c-arrays` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathPatternMatcher.cpp` | 497 | `cppcoreguidelines-avoid-c-arrays, hicpp-avoid-c-arrays, modernize-avoid-c-arrays` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathPatternMatcher.cpp` | 658 | `readability-math-missing-parentheses` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathPatternMatcher.cpp` | 694 | `misc-const-correctness` | Worth Fixing | Worth fixing; address misc-const-correctness and remove NOLINT. |
| `src/path/PathPatternMatcher.cpp` | 695 | `misc-const-correctness` | Worth Fixing | Worth fixing; address misc-const-correctness and remove NOLINT. |
| `src/path/PathPatternMatcher.cpp` | 696 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathPatternMatcher.cpp` | 701 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.cpp` | 711 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.cpp` | 716 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathPatternMatcher.cpp` | 721 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.cpp` | 722 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.cpp` | 760 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.cpp` | 835 | `bugprone-signed-char-misuse` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathPatternMatcher.cpp` | 855 | `bugprone-signed-char-misuse` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathPatternMatcher.cpp` | 917 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.cpp` | 975 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.cpp` | 993 | `misc-const-correctness` | Worth Fixing | Worth fixing; address misc-const-correctness and remove NOLINT. |
| `src/path/PathPatternMatcher.cpp` | 1004 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.cpp` | 1026 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.cpp` | 1181 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.cpp` | 1184 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathPatternMatcher.cpp` | 1190 | `misc-const-correctness` | Worth Fixing | Worth fixing; address misc-const-correctness and remove NOLINT. |
| `src/path/PathPatternMatcher.cpp` | 1298 | `misc-const-correctness` | Worth Fixing | Worth fixing; address misc-const-correctness and remove NOLINT. |
| `src/path/PathPatternMatcher.cpp` | 1300 | `misc-const-correctness` | Worth Fixing | Worth fixing; address misc-const-correctness and remove NOLINT. |
| `src/path/PathPatternMatcher.cpp` | 1363 | `misc-const-correctness` | Worth Fixing | Worth fixing; address misc-const-correctness and remove NOLINT. |
| `src/path/PathPatternMatcher.cpp` | 1364 | `misc-const-correctness` | Worth Fixing | Worth fixing; address misc-const-correctness and remove NOLINT. |
| `src/path/PathPatternMatcher.cpp` | 1454 | `cppcoreguidelines-pro-type-reinterpret-cast` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathPatternMatcher.h` | 26 | `performance-enum-size` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathPatternMatcher.h` | 27 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathPatternMatcher.h` | 28 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathPatternMatcher.h` | 40 | `cppcoreguidelines-pro-type-member-init, hicpp-member-init` | Worth Fixing | Worth fixing; address cppcoreguidelines-pro-type-member-init, hicpp-member-init and remove NOLINT. |
| `src/path/PathPatternMatcher.h` | 42 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.h` | 44 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.h` | 46 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.h` | 50 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.h` | 55 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.h` | 68 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.h` | 69 | `readability-identifier-naming, cppcoreguidelines-avoid-c-arrays, hicpp-avoid-c-arrays, modernize-avoid-c-arrays` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathPatternMatcher.h` | 70 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.h` | 71 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathPatternMatcher.h` | 72 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.h` | 73 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathPatternMatcher.h` | 74 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.h` | 75 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathPatternMatcher.h` | 80 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.h` | 83 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathPatternMatcher.h` | 84 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.h` | 87 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.h` | 89 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.h` | 90 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathPatternMatcher.h` | 96 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.h` | 97 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathPatternMatcher.h` | 98 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.h` | 100 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.h` | 106 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.h` | 108 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.h` | 113 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathPatternMatcher.h` | 117 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathStorage.cpp` | 6 | `hicpp-use-equals-default, modernize-use-equals-default, cppcoreguidelines-pro-type-member-init, hicpp-member-init` | Worth Fixing | Worth fixing; address modernize-use-equals-default, cppcoreguidelines-pro-type-member-init, hicpp-member-init and remove NOLINT. |
| `src/path/PathStorage.cpp` | 18 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathStorage.cpp` | 63 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathStorage.cpp` | 70 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathStorage.cpp` | 71 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathStorage.cpp` | 74 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathStorage.cpp` | 81 | `readability-magic-numbers` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathStorage.cpp` | 91 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathStorage.cpp` | 97 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathStorage.cpp` | 157 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathStorage.cpp` | 167 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathStorage.cpp` | 183 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathStorage.cpp` | 197 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathStorage.cpp` | 338 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathStorage.cpp` | 343 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathStorage.cpp` | 348 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathStorage.h` | 52 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathStorage.h` | 54 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathStorage.h` | 56 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathStorage.h` | 58 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathStorage.h` | 60 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathStorage.h` | 62 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathStorage.h` | 64 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathStorage.h` | 66 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathStorage.h` | 93 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathStorage.h` | 99 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathStorage.h` | 114 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathStorage.h` | 115 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathStorage.h` | 117 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathStorage.h` | 127 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathStorage.h` | 167 | `readability-redundant-member-init` | Worth Fixing | Worth fixing; address readability-redundant-member-init and remove NOLINT. |
| `src/path/PathStorage.h` | 168 | `readability-redundant-member-init` | Worth Fixing | Worth fixing; address readability-redundant-member-init and remove NOLINT. |
| `src/path/PathStorage.h` | 169 | `readability-redundant-member-init` | Worth Fixing | Worth fixing; address readability-redundant-member-init and remove NOLINT. |
| `src/path/PathStorage.h` | 170 | `readability-redundant-member-init` | Worth Fixing | Worth fixing; address readability-redundant-member-init and remove NOLINT. |
| `src/path/PathStorage.h` | 227 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathStorage.h` | 228 | `readability-redundant-member-init` | Worth Fixing | Worth fixing; address readability-redundant-member-init and remove NOLINT. |
| `src/path/PathStorage.h` | 229 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathStorage.h` | 230 | `readability-redundant-member-init` | Worth Fixing | Worth fixing; address readability-redundant-member-init and remove NOLINT. |
| `src/path/PathStorage.h` | 231 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathStorage.h` | 232 | `readability-redundant-member-init` | Worth Fixing | Worth fixing; address readability-redundant-member-init and remove NOLINT. |
| `src/path/PathStorage.h` | 233 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathStorage.h` | 234 | `readability-redundant-member-init` | Worth Fixing | Worth fixing; address readability-redundant-member-init and remove NOLINT. |
| `src/path/PathStorage.h` | 251 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathStorage.h` | 252 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathStorage.h` | 253 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathStorage.h` | 254 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathStorage.h` | 255 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathStorage.h` | 256 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathStorage.h` | 257 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathStorage.h` | 260 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathStorage.h` | 263 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathStorage.h` | 264 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/path/PathUtils.cpp` | 106 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/path/PathUtils.cpp` | 107 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/EmbeddedFont_Cousine.cpp` | 4 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/platform/EmbeddedFont_Cousine.cpp` | 6 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/platform/EmbeddedFont_Cousine.h` | 18 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/platform/EmbeddedFont_Cousine.h` | 20 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/platform/EmbeddedFont_FontAwesome.cpp` | 3 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/platform/EmbeddedFont_FontAwesome.cpp` | 5 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/platform/EmbeddedFont_FontAwesome.h` | 19 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/EmbeddedFont_FontAwesome.h` | 20 | `readability-identifier-naming, cppcoreguidelines-avoid-c-arrays, hicpp-avoid-c-arrays, modernize-avoid-c-arrays` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/EmbeddedFont_FontAwesome.h` | 24 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/EmbeddedFont_FontAwesome.h` | 25 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/EmbeddedFont_Karla.cpp` | 4 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/platform/EmbeddedFont_Karla.cpp` | 6 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/platform/EmbeddedFont_Karla.h` | 18 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/platform/EmbeddedFont_Karla.h` | 20 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/platform/EmbeddedFont_RobotoMedium.cpp` | 4 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/platform/EmbeddedFont_RobotoMedium.cpp` | 6 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/platform/EmbeddedFont_RobotoMedium.h` | 18 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/platform/EmbeddedFont_RobotoMedium.h` | 20 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/platform/FontUtilsCommon.cpp` | 34 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/platform/FontUtilsCommon.cpp` | 37 | `cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/FontUtilsCommon.cpp` | 41 | `cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/FontUtilsCommon.cpp` | 51 | `misc-unused-parameters` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/FontUtilsCommon.cpp` | 87 | `misc-const-correctness` | Worth Fixing | Worth fixing; address misc-const-correctness and remove NOLINT. |
| `src/platform/linux/AppBootstrap_linux.h` | 49 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/platform/linux/FileOperations_linux.cpp` | 111 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/platform/linux/FileOperations_linux.cpp` | 116 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/platform/linux/FileOperations_linux.cpp` | 145 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/linux/FileOperations_linux.cpp` | 244 | `cppcoreguidelines-init-variables, concurrency-mt-unsafe` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/linux/FileOperations_linux.cpp` | 249 | `cppcoreguidelines-init-variables, concurrency-mt-unsafe` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/linux/OpenGLManager.cpp` | 28 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/platform/linux/OpenGLManager.cpp` | 68 | `cppcoreguidelines-pro-type-reinterpret-cast` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/linux/OpenGLManager.h` | 48 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/linux/OpenGLManager.h` | 49 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/linux/ThreadUtils_linux.cpp` | 9 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/macos/AppBootstrap_mac.h` | 52 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/platform/macos/FontUtils_mac.h` | 41 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/platform/macos/FontUtils_mac.h` | 57 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/platform/macos/MetalManager.h` | 60 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/macos/MetalManager.h` | 61 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/macos/MetalManager.h` | 62 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/macos/MetalManager.h` | 65 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/macos/MetalManager.h` | 66 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/macos/MetalManager.h` | 67 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/macos/MetalManager.h` | 70 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/macos/ThreadUtils_mac.cpp` | 11 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/macos/ThreadUtils_mac.cpp` | 21 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/platform/windows/AppBootstrap_win.h` | 54 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/platform/windows/DirectXManager.h` | 116 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/windows/DirectXManager.h` | 117 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/windows/DirectXManager.h` | 118 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/windows/DirectXManager.h` | 119 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/windows/DragDropUtils.cpp` | 70 | `misc-non-private-member-variables-in-classes, readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/windows/DragDropUtils.cpp` | 74 | `misc-non-private-member-variables-in-classes, readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/windows/DragDropUtils.cpp` | 90 | `cppcoreguidelines-pro-type-member-init, hicpp-member-init` | Worth Fixing | Worth fixing; address cppcoreguidelines-pro-type-member-init, hicpp-member-init and remove NOLINT. |
| `src/platform/windows/DragDropUtils.cpp` | 106 | `misc-non-private-member-variables-in-classes, readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/windows/DragDropUtils.cpp` | 109 | `misc-non-private-member-variables-in-classes, readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/windows/DragDropUtils.cpp` | 114 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/platform/windows/DragDropUtils.cpp` | 163 | `misc-non-private-member-variables-in-classes, readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/windows/DragDropUtils.cpp` | 164 | `misc-non-private-member-variables-in-classes, readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/windows/DragDropUtils.cpp` | 172 | `cppcoreguidelines-pro-type-member-init, hicpp-member-init` | Worth Fixing | Worth fixing; address cppcoreguidelines-pro-type-member-init, hicpp-member-init and remove NOLINT. |
| `src/platform/windows/DragDropUtils.cpp` | 195 | `misc-non-private-member-variables-in-classes, readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/windows/DragDropUtils.cpp` | 198 | `misc-non-private-member-variables-in-classes, readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/windows/DragDropUtils.cpp` | 282 | `misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/windows/DragDropUtils.cpp` | 286 | `cppcoreguidelines-pro-type-member-init, hicpp-member-init` | Worth Fixing | Worth fixing; address cppcoreguidelines-pro-type-member-init, hicpp-member-init and remove NOLINT. |
| `src/platform/windows/DragDropUtils.cpp` | 304 | `misc-non-private-member-variables-in-classes, readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/windows/DragDropUtils.cpp` | 307 | `misc-non-private-member-variables-in-classes, readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/windows/DragDropUtils.cpp` | 391 | `cppcoreguidelines-owning-memory` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/windows/DragDropUtils.cpp` | 392 | `cppcoreguidelines-owning-memory` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/windows/DragDropUtils.cpp` | 410 | `bugprone-unused-local-non-trivial-variable` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/windows/ShellContextUtils.cpp` | 128 | `misc-const-correctness` | Worth Fixing | Worth fixing; address misc-const-correctness and remove NOLINT. |
| `src/platform/windows/ThreadUtils_win.cpp` | 14 | `readability-identifier-naming, readability-function-cognitive-complexity` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/windows/ThreadUtils_win.cpp` | 22 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/windows/ThreadUtils_win.cpp` | 23 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/windows/ThreadUtils_win.cpp` | 27 | `readability-implicit-bool-conversion` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/windows/ThreadUtils_win.cpp` | 36 | `bugprone-branch-clone` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/windows/ThreadUtils_win.cpp` | 49 | `readability-implicit-bool-conversion` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/windows/ThreadUtils_win.cpp` | 51 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/windows/ThreadUtils_win.cpp` | 53 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/windows/ThreadUtils_win.cpp` | 59 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/windows/ThreadUtils_win.cpp` | 60 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/windows/ThreadUtils_win.cpp` | 62 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/windows/ThreadUtils_win.cpp` | 68 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/platform/windows/resource.h` | 8 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/search/ParallelSearchEngine.cpp` | 24 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/search/ParallelSearchEngine.cpp` | 114 | `bugprone-exception-escape` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.cpp` | 345 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.cpp` | 354 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.cpp` | 370 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.cpp` | 376 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.cpp` | 410 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.cpp` | 438 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.cpp` | 441 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.cpp` | 456 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.cpp` | 457 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.cpp` | 466 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.h` | 29 | `readability-magic-numbers` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.h` | 75 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.h` | 149 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.h` | 177 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.h` | 210 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.h` | 211 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.h` | 212 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.h` | 239 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.h` | 240 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.h` | 243 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.h` | 244 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.h` | 245 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.h` | 246 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.h` | 274 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/search/ParallelSearchEngine.h` | 292 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.h` | 317 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.h` | 323 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.h` | 327 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.h` | 342 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.h` | 345 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.h` | 349 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.h` | 350 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.h` | 351 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.h` | 368 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.h` | 413 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.h` | 416 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.h` | 420 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.h` | 437 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.h` | 463 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.h` | 469 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.h` | 470 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.h` | 472 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.h` | 473 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.h` | 477 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.h` | 494 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.h` | 497 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.h` | 499 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.h` | 500 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.h` | 511 | `readability-function-cognitive-complexity` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.h` | 512 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.h` | 559 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/ParallelSearchEngine.h` | 564 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchContext.h` | 43 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/search/SearchContext.h` | 44 | `readability-redundant-member-init` | Worth Fixing | Worth fixing; address readability-redundant-member-init and remove NOLINT. |
| `src/search/SearchContext.h` | 45 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/search/SearchContext.h` | 46 | `readability-redundant-member-init` | Worth Fixing | Worth fixing; address readability-redundant-member-init and remove NOLINT. |
| `src/search/SearchContext.h` | 47 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/search/SearchContext.h` | 48 | `readability-redundant-member-init` | Worth Fixing | Worth fixing; address readability-redundant-member-init and remove NOLINT. |
| `src/search/SearchContext.h` | 49 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/search/SearchContext.h` | 50 | `readability-redundant-member-init` | Worth Fixing | Worth fixing; address readability-redundant-member-init and remove NOLINT. |
| `src/search/SearchContext.h` | 54 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/search/SearchContext.h` | 56 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/search/SearchContext.h` | 60 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/search/SearchContext.h` | 62 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/search/SearchContext.h` | 64 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/search/SearchContext.h` | 68 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/search/SearchContext.h` | 69 | `readability-redundant-member-init` | Worth Fixing | Worth fixing; address readability-redundant-member-init and remove NOLINT. |
| `src/search/SearchContext.h` | 70 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/search/SearchContext.h` | 71 | `readability-redundant-member-init` | Worth Fixing | Worth fixing; address readability-redundant-member-init and remove NOLINT. |
| `src/search/SearchContext.h` | 75 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/search/SearchContext.h` | 77 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/search/SearchContext.h` | 81 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/search/SearchContext.h` | 83 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/search/SearchContext.h` | 84 | `readability-magic-numbers` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchContext.h` | 85 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/search/SearchContext.h` | 86 | `readability-magic-numbers` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchContext.h` | 88 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/search/SearchContext.h` | 91 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/search/SearchContext.h` | 181 | `readability-magic-numbers` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchContext.h` | 182 | `readability-magic-numbers` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchContext.h` | 183 | `readability-magic-numbers` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchContext.h` | 184 | `readability-magic-numbers` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchContext.h` | 188 | `clang-diagnostic-unused-parameter` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchController.cpp` | 92 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/search/SearchController.cpp` | 156 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchController.cpp` | 164 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchController.cpp` | 180 | `cppcoreguidelines-rvalue-reference-param-not-moved` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchController.cpp` | 200 | `bugprone-use-after-move, hicpp-invalid-access-moved` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchController.cpp` | 247 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchController.cpp` | 256 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchController.cpp` | 264 | `bugprone-suspicious-stringview-data-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchController.cpp` | 356 | `cppcoreguidelines-rvalue-reference-param-not-moved` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchController.cpp` | 374 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchController.cpp` | 398 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchController.cpp` | 445 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchController.cpp` | 467 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchController.cpp` | 512 | `misc-non-private-member-variables-in-classes, readability-identifier-naming, readability-redundant-member-init` | Worth Fixing | Worth fixing; address readability-redundant-member-init and remove NOLINT. |
| `src/search/SearchController.cpp` | 513 | `misc-non-private-member-variables-in-classes, readability-identifier-naming, readability-redundant-member-init` | Worth Fixing | Worth fixing; address readability-redundant-member-init and remove NOLINT. |
| `src/search/SearchController.cpp` | 514 | `misc-non-private-member-variables-in-classes, readability-redundant-member-init` | Worth Fixing | Worth fixing; address readability-redundant-member-init and remove NOLINT. |
| `src/search/SearchController.cpp` | 515 | `misc-non-private-member-variables-in-classes, readability-identifier-naming, readability-redundant-member-init` | Worth Fixing | Worth fixing; address readability-redundant-member-init and remove NOLINT. |
| `src/search/SearchController.cpp` | 625 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchController.cpp` | 687 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchInputField.h` | 32 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchInputField.h` | 110 | `google-explicit-constructor, hicpp-explicit-conversions` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchPatternUtils.h` | 27 | `performance-enum-size` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchPatternUtils.h` | 55 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchPatternUtils.h` | 56 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchPatternUtils.h` | 59 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchPatternUtils.h` | 63 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchPatternUtils.h` | 98 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchPatternUtils.h` | 151 | `bugprone-suspicious-stringview-data-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchPatternUtils.h` | 155 | `bugprone-suspicious-stringview-data-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchPatternUtils.h` | 229 | `bugprone-exception-escape` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchPatternUtils.h` | 233 | `bugprone-exception-escape` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchPatternUtils.h` | 244 | `bugprone-exception-escape` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchPatternUtils.h` | 249 | `bugprone-exception-escape` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchPatternUtils.h` | 256 | `bugprone-exception-escape` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchPatternUtils.h` | 261 | `bugprone-exception-escape` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchPatternUtils.h` | 274 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchPatternUtils.h` | 300 | `readability-identifier-naming, cppcoreguidelines-avoid-non-const-global-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchPatternUtils.h` | 309 | `readability-identifier-naming, cppcoreguidelines-avoid-non-const-global-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchPatternUtils.h` | 324 | `readability-identifier-naming, cppcoreguidelines-avoid-non-const-global-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchPatternUtils.h` | 353 | `readability-container-contains` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchPatternUtils.h` | 356 | `readability-container-contains` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchPatternUtils.h` | 364 | `misc-const-correctness` | Worth Fixing | Worth fixing; address misc-const-correctness and remove NOLINT. |
| `src/search/SearchPatternUtils.h` | 366 | `cppcoreguidelines-pro-bounds-constant-array-index` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchPatternUtils.h` | 370 | `readability-container-contains` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchResultUtils.cpp` | 281 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/search/SearchResultUtils.cpp` | 292 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchResultUtils.h` | 201 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/search/SearchResultsService.cpp` | 57 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchStatisticsCollector.cpp` | 12 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchStatisticsCollector.cpp` | 13 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchStatisticsCollector.cpp` | 14 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchStatisticsCollector.cpp` | 23 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchStatisticsCollector.cpp` | 26 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchStatisticsCollector.cpp` | 27 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchStatisticsCollector.h` | 45 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchStatisticsCollector.h` | 46 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchStatisticsCollector.h` | 47 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchStatisticsCollector.h` | 70 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchStatisticsCollector.h` | 71 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchStatisticsCollector.h` | 74 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchStatisticsCollector.h` | 75 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchStatisticsCollector.h` | 76 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchStatisticsCollector.h` | 77 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchThreadPool.cpp` | 90 | `cppcoreguidelines-pro-type-member-init, hicpp-member-init` | Worth Fixing | Worth fixing; address cppcoreguidelines-pro-type-member-init, hicpp-member-init and remove NOLINT. |
| `src/search/SearchThreadPool.h` | 50 | `cppcoreguidelines-missing-std-forward` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchThreadPool.h` | 94 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchThreadPool.h` | 97 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchThreadPool.h` | 100 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchThreadPool.h` | 101 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchThreadPool.h` | 102 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchThreadPoolManager.cpp` | 12 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/search/SearchThreadPoolManager.cpp` | 44 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/search/SearchThreadPoolManager.cpp` | 74 | `readability-function-cognitive-complexity` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchThreadPoolManager.h` | 52 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 29 | `performance-enum-size` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 47 | `cppcoreguidelines-pro-type-member-init, hicpp-member-init` | Worth Fixing | Worth fixing; address cppcoreguidelines-pro-type-member-init, hicpp-member-init and remove NOLINT. |
| `src/search/SearchTypes.h` | 48 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 49 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 50 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 54 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 55 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 56 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 57 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 58 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 59 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 60 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 61 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 62 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 67 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 68 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 69 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 70 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 71 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 72 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 73 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 74 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 75 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 78 | `cppcoreguidelines-pro-type-member-init, hicpp-member-init` | Worth Fixing | Worth fixing; address cppcoreguidelines-pro-type-member-init, hicpp-member-init and remove NOLINT. |
| `src/search/SearchTypes.h` | 79 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 109 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 110 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 112 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 113 | `readability-identifier-naming, misc-non-private-member-variables-in-classes, cppcoreguidelines-pro-type-member-init, hicpp-member-init` | Worth Fixing | Worth fixing; address cppcoreguidelines-pro-type-member-init, hicpp-member-init and remove NOLINT. |
| `src/search/SearchTypes.h` | 114 | `readability-identifier-naming, misc-non-private-member-variables-in-classes, cppcoreguidelines-pro-type-member-init, hicpp-member-init` | Worth Fixing | Worth fixing; address cppcoreguidelines-pro-type-member-init, hicpp-member-init and remove NOLINT. |
| `src/search/SearchTypes.h` | 115 | `readability-identifier-naming, misc-non-private-member-variables-in-classes, cppcoreguidelines-pro-type-member-init, hicpp-member-init` | Worth Fixing | Worth fixing; address cppcoreguidelines-pro-type-member-init, hicpp-member-init and remove NOLINT. |
| `src/search/SearchTypes.h` | 117 | `readability-identifier-naming, misc-non-private-member-variables-in-classes, cppcoreguidelines-pro-type-member-init, hicpp-member-init` | Worth Fixing | Worth fixing; address cppcoreguidelines-pro-type-member-init, hicpp-member-init and remove NOLINT. |
| `src/search/SearchTypes.h` | 118 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 119 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 120 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 123 | `cppcoreguidelines-pro-type-member-init, hicpp-member-init` | Worth Fixing | Worth fixing; address cppcoreguidelines-pro-type-member-init, hicpp-member-init and remove NOLINT. |
| `src/search/SearchTypes.h` | 124 | `readability-identifier-naming, misc-non-private-member-variables-in-classes, cppcoreguidelines-pro-type-member-init, hicpp-member-init` | Worth Fixing | Worth fixing; address cppcoreguidelines-pro-type-member-init, hicpp-member-init and remove NOLINT. |
| `src/search/SearchTypes.h` | 125 | `readability-identifier-naming, misc-non-private-member-variables-in-classes, cppcoreguidelines-pro-type-member-init, hicpp-member-init` | Worth Fixing | Worth fixing; address cppcoreguidelines-pro-type-member-init, hicpp-member-init and remove NOLINT. |
| `src/search/SearchTypes.h` | 126 | `readability-identifier-naming, misc-non-private-member-variables-in-classes, cppcoreguidelines-pro-type-member-init, hicpp-member-init` | Worth Fixing | Worth fixing; address cppcoreguidelines-pro-type-member-init, hicpp-member-init and remove NOLINT. |
| `src/search/SearchTypes.h` | 127 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 128 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 134 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 135 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 138 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 139 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 140 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 141 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 144 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 145 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 146 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 147 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 165 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 166 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 167 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 168 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 169 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 170 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 171 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 172 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 173 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchTypes.h` | 174 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchWorker.cpp` | 25 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/search/SearchWorker.cpp` | 47 | `bugprone-implicit-widening-of-multiplication-result` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchWorker.cpp` | 84 | `bugprone-unused-local-non-trivial-variable, readability-redundant-string-init` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchWorker.cpp` | 142 | `bugprone-unused-local-non-trivial-variable` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchWorker.cpp` | 143 | `bugprone-unused-local-non-trivial-variable` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchWorker.cpp` | 144 | `bugprone-unused-local-non-trivial-variable` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchWorker.cpp` | 147 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchWorker.cpp` | 285 | `cppcoreguidelines-avoid-non-const-global-variables, readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchWorker.cpp` | 305 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchWorker.cpp` | 340 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/search/SearchWorker.cpp` | 442 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/search/SearchWorker.cpp` | 488 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/search/SearchWorker.cpp` | 504 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchWorker.cpp` | 548 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/search/SearchWorker.cpp` | 556 | `misc-const-correctness` | Worth Fixing | Worth fixing; address misc-const-correctness and remove NOLINT. |
| `src/search/SearchWorker.cpp` | 566 | `misc-const-correctness` | Worth Fixing | Worth fixing; address misc-const-correctness and remove NOLINT. |
| `src/search/SearchWorker.cpp` | 579 | `misc-const-correctness` | Worth Fixing | Worth fixing; address misc-const-correctness and remove NOLINT. |
| `src/search/SearchWorker.cpp` | 663 | `bugprone-branch-clone` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchWorker.h` | 18 | `misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchWorker.h` | 35 | `cppcoreguidelines-avoid-non-const-global-variables, readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchWorker.h` | 146 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchWorker.h` | 147 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchWorker.h` | 148 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchWorker.h` | 149 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchWorker.h` | 158 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchWorker.h` | 159 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchWorker.h` | 160 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchWorker.h` | 161 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchWorker.h` | 162 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchWorker.h` | 165 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchWorker.h` | 166 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchWorker.h` | 169 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchWorker.h` | 170 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchWorker.h` | 171 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchWorker.h` | 176 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchWorker.h` | 180 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/SearchWorker.h` | 183 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/StreamingResultsCollector.cpp` | 5 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/search/StreamingResultsCollector.cpp` | 20 | `hicpp-move-const-arg, performance-move-const-arg` | Worth Fixing | Worth fixing; address performance-move-const-arg and remove NOLINT. |
| `src/search/StreamingResultsCollector.cpp` | 119 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/search/StreamingResultsCollector.h` | 81 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/StreamingResultsCollector.h` | 82 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/StreamingResultsCollector.h` | 84 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/StreamingResultsCollector.h` | 85 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/StreamingResultsCollector.h` | 86 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/StreamingResultsCollector.h` | 87 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/StreamingResultsCollector.h` | 89 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/StreamingResultsCollector.h` | 91 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/StreamingResultsCollector.h` | 92 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/StreamingResultsCollector.h` | 93 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/search/StreamingResultsCollector.h` | 94 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/EmptyState.cpp` | 29 | `readability-static-definition-in-anonymous-namespace` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/EmptyState.cpp` | 41 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/EmptyState.cpp` | 108 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/EmptyState.cpp` | 214 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/EmptyState.cpp` | 227 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/EmptyState.cpp` | 248 | `performance-no-automatic-move` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/EmptyState.cpp` | 295 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/EmptyState.cpp` | 579 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/EmptyState.cpp` | 590 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/EmptyState.cpp` | 597 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/EmptyState.cpp` | 601 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/EmptyState.cpp` | 607 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/EmptyState.cpp` | 620 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/FilterPanel.cpp` | 235 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/FilterPanel.cpp` | 259 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/FilterPanel.cpp` | 271 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/FilterPanel.cpp` | 294 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/FilterPanel.cpp` | 333 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/FilterPanel.cpp` | 415 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/FilterPanel.cpp` | 446 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/FilterPanel.cpp` | 448 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/FilterPanel.cpp` | 496 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/FilterPanel.cpp` | 497 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/FilterPanel.cpp` | 498 | `bugprone-branch-clone` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/FilterPanel.cpp` | 532 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/FilterPanel.cpp` | 561 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/FilterPanel.cpp` | 587 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/FilterPanel.cpp` | 597 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/FilterPanel.cpp` | 602 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/FolderBrowser.cpp` | 60 | `concurrency-mt-unsafe, cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/FolderBrowser.cpp` | 104 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/FolderBrowser.cpp` | 106 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/FolderBrowser.cpp` | 130 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/FolderBrowser.cpp` | 134 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/FolderBrowser.cpp` | 148 | `readability-implicit-bool-conversion` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/FolderBrowser.cpp` | 214 | `cppcoreguidelines-pro-type-member-init, hicpp-member-init` | Worth Fixing | Worth fixing; address cppcoreguidelines-pro-type-member-init, hicpp-member-init and remove NOLINT. |
| `src/ui/FolderBrowser.h` | 15 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/FolderBrowser.h` | 102 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/FolderBrowser.h` | 103 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/FolderBrowser.h` | 104 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/FolderBrowser.h` | 105 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/FolderBrowser.h` | 106 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/FolderBrowser.h` | 107 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/FolderBrowser.h` | 108 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/FolderBrowser.h` | 109 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/HelpWindow.cpp` | 46 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/HelpWindow.cpp` | 47 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/HelpWindow.cpp` | 48 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/HelpWindow.cpp` | 49 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/HelpWindow.cpp` | 51 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/HelpWindow.cpp` | 52 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/HelpWindow.cpp` | 53 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/HelpWindow.cpp` | 55 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/HelpWindow.cpp` | 56 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/HelpWindow.cpp` | 58 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/HelpWindow.cpp` | 60 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/HelpWindow.cpp` | 62 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/HelpWindow.cpp` | 63 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/HelpWindow.cpp` | 65 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/HelpWindow.cpp` | 67 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/HelpWindow.cpp` | 69 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/HelpWindow.cpp` | 70 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/HelpWindow.cpp` | 72 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/HelpWindow.cpp` | 73 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/HelpWindow.cpp` | 75 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/HelpWindow.cpp` | 76 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/HelpWindow.cpp` | 78 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/HelpWindow.cpp` | 80 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/HelpWindow.cpp` | 82 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/HelpWindow.cpp` | 88 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/HelpWindow.cpp` | 93 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/HelpWindow.cpp` | 96 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/HelpWindow.cpp` | 99 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/HelpWindow.cpp` | 102 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/HelpWindow.cpp` | 104 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/HelpWindow.cpp` | 107 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/HelpWindow.cpp` | 110 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/HelpWindow.cpp` | 115 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/HelpWindow.cpp` | 117 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/HelpWindow.cpp` | 119 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/HelpWindow.cpp` | 123 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/HelpWindow.cpp` | 126 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/HelpWindow.cpp` | 129 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/HelpWindow.cpp` | 134 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/HelpWindow.cpp` | 137 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/HelpWindow.cpp` | 140 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/HelpWindow.cpp` | 144 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/HelpWindow.cpp` | 147 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/HelpWindow.cpp` | 150 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/HelpWindow.cpp` | 152 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/HelpWindow.cpp` | 154 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/HelpWindow.cpp` | 156 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/HelpWindow.cpp` | 158 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/HelpWindow.cpp` | 161 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/HelpWindow.cpp` | 163 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/HelpWindow.cpp` | 166 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/HelpWindow.cpp` | 168 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/HelpWindow.cpp` | 171 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/HelpWindow.cpp` | 174 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/HelpWindow.cpp` | 179 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/HelpWindow.cpp` | 181 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/HelpWindow.cpp` | 183 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/HelpWindow.cpp` | 185 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/HelpWindow.cpp` | 187 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/HelpWindow.cpp` | 189 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/HelpWindow.cpp` | 191 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/HelpWindow.cpp` | 193 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/HelpWindow.cpp` | 195 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/HelpWindow.cpp` | 197 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/HelpWindow.cpp` | 199 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/HelpWindow.cpp` | 201 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/IconsFontAwesome.h` | 21 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/IconsFontAwesome.h` | 22 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/IconsFontAwesome.h` | 23 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/IconsFontAwesome.h` | 24 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/IconsFontAwesome.h` | 25 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/IconsFontAwesome.h` | 26 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/IconsFontAwesome.h` | 27 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/IconsFontAwesome.h` | 28 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/IconsFontAwesome.h` | 29 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/IconsFontAwesome.h` | 30 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/IconsFontAwesome.h` | 31 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/IconsFontAwesome.h` | 32 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/IconsFontAwesome.h` | 33 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/IconsFontAwesome.h` | 34 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/IconsFontAwesome.h` | 35 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/IconsFontAwesome.h` | 36 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/IconsFontAwesome.h` | 37 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/IconsFontAwesome.h` | 38 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/IconsFontAwesome.h` | 39 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/IconsFontAwesome.h` | 40 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/IconsFontAwesome.h` | 41 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/IconsFontAwesome.h` | 42 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/IconsFontAwesome.h` | 43 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/IconsFontAwesome.h` | 44 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/IconsFontAwesome.h` | 45 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/IconsFontAwesome.h` | 47 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/IconsFontAwesome.h` | 48 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/IconsFontAwesome.h` | 49 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/IconsFontAwesome.h` | 50 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/IconsFontAwesome.h` | 51 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/IconsFontAwesome.h` | 52 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/IconsFontAwesome.h` | 53 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/IconsFontAwesome.h` | 54 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/IconsFontAwesome.h` | 55 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/IconsFontAwesome.h` | 56 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/ImGuiTestEngineTests.cpp` | 29 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/ImGuiTestEngineTests.cpp` | 39 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/ImGuiTestEngineTests.cpp` | 53 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/ImGuiTestEngineTests.cpp` | 60 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/ImGuiTestEngineTests.cpp` | 71 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/ImGuiTestEngineTests.cpp` | 82 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/ImGuiTestEngineTests.cpp` | 84 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/ImGuiTestEngineTests.cpp` | 103 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/ImGuiTestEngineTests.cpp` | 106 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/MetricsWindow.cpp` | 33 | `cert-dcl50-cpp, cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 34 | `cppcoreguidelines-init-variables, cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 100 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 102 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 104 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 109 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 116 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 129 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 137 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 140 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 143 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 146 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 151 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 158 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 161 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 163 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 181 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 183 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 187 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 191 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 193 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 196 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 213 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 217 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 225 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 234 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 244 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 257 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 267 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 277 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 331 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 334 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 343 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 351 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 355 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 368 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 371 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 376 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 382 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 386 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 390 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 397 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 399 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 401 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 405 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 411 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 419 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 433 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 465 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 470 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 481 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 496 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 500 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/MetricsWindow.cpp` | 502 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/Popups.cpp` | 36 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 38 | `readability-math-missing-parentheses` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/Popups.cpp` | 39 | `readability-math-missing-parentheses` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/Popups.cpp` | 77 | `misc-const-correctness` | Worth Fixing | Worth fixing; address misc-const-correctness and remove NOLINT. |
| `src/ui/Popups.cpp` | 108 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 122 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 125 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 140 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 163 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 168 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 176 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 196 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 215 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 217 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 219 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 222 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 225 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 228 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 230 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 232 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 234 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 237 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 260 | `hicpp-signed-bitwise` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/Popups.cpp` | 268 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 284 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 286 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 293 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 309 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 313 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 321 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 325 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 335 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 338 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 344 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 352 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 362 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 365 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 377 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 394 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 439 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 479 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 503 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 506 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 526 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 530 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 566 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 587 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 607 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 637 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 651 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 659 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 665 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Popups.cpp` | 667 | `bugprone-narrowing-conversions, cppcoreguidelines-narrowing-conversions` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/Popups.cpp` | 674 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/ResultsTable.cpp` | 63 | `readability-non-const-parameter` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/ResultsTable.cpp` | 70 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/ResultsTable.cpp` | 73 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/ResultsTable.cpp` | 132 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/ResultsTable.cpp` | 136 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/ResultsTable.cpp` | 137 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/ResultsTable.cpp` | 138 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/ResultsTable.cpp` | 139 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/ResultsTable.cpp` | 156 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/ResultsTable.cpp` | 158 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/ResultsTable.cpp` | 159 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/ResultsTable.cpp` | 164 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/ResultsTable.cpp` | 165 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/ResultsTable.cpp` | 166 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/ResultsTable.cpp` | 167 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/ResultsTable.cpp` | 168 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/ResultsTable.cpp` | 176 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/ResultsTable.cpp` | 177 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/ResultsTable.cpp` | 178 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/ResultsTable.cpp` | 183 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/ResultsTable.cpp` | 184 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/ResultsTable.cpp` | 185 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/ResultsTable.cpp` | 186 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/ResultsTable.cpp` | 187 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/ResultsTable.cpp` | 326 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/ResultsTable.cpp` | 403 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/ResultsTable.cpp` | 476 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/ResultsTable.cpp` | 546 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/ResultsTable.cpp` | 625 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/ResultsTable.cpp` | 674 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/ResultsTable.cpp` | 683 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/ResultsTable.cpp` | 684 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/ResultsTable.cpp` | 848 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/ResultsTable.cpp` | 849 | `cppcoreguidelines-pro-type-const-cast` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/ResultsTable.cpp` | 957 | `misc-const-correctness` | Worth Fixing | Worth fixing; address misc-const-correctness and remove NOLINT. |
| `src/ui/ResultsTable.cpp` | 979 | `misc-const-correctness` | Worth Fixing | Worth fixing; address misc-const-correctness and remove NOLINT. |
| `src/ui/ResultsTable.cpp` | 1000 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/ResultsTable.cpp` | 1241 | `hicpp-signed-bitwise` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchHelpWindow.cpp` | 37 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchHelpWindow.cpp` | 38 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchHelpWindow.cpp` | 41 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchHelpWindow.cpp` | 42 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchHelpWindow.cpp` | 43 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchHelpWindow.cpp` | 46 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchHelpWindow.cpp` | 47 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchHelpWindow.cpp` | 48 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchHelpWindow.cpp` | 51 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchHelpWindow.cpp` | 52 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchHelpWindow.cpp` | 53 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchHelpWindow.cpp` | 54 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchHelpWindow.cpp` | 57 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchHelpWindow.cpp` | 58 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchHelpWindow.cpp` | 59 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchHelpWindow.cpp` | 62 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchHelpWindow.cpp` | 63 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchHelpWindow.cpp` | 64 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchHelpWindow.cpp` | 66 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchHelpWindow.cpp` | 69 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchHelpWindow.cpp` | 74 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchHelpWindow.cpp` | 75 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchHelpWindow.cpp` | 77 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchHelpWindow.cpp` | 79 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchHelpWindow.cpp` | 83 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchHelpWindow.cpp` | 84 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchHelpWindow.cpp` | 85 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchHelpWindow.cpp` | 86 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchHelpWindow.cpp` | 87 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchHelpWindow.cpp` | 92 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchHelpWindow.cpp` | 93 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchHelpWindow.cpp` | 94 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchHelpWindow.cpp` | 95 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchHelpWindow.cpp` | 96 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchHelpWindow.cpp` | 97 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchHelpWindow.cpp` | 100 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchHelpWindow.cpp` | 101 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchHelpWindow.cpp` | 102 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchHelpWindow.cpp` | 103 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchHelpWindow.cpp` | 104 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchInputs.cpp` | 120 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchInputs.cpp` | 121 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchInputs.cpp` | 122 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchInputs.cpp` | 123 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchInputs.cpp` | 124 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchInputs.cpp` | 126 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchInputs.cpp` | 127 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchInputs.cpp` | 128 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchInputs.cpp` | 130 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchInputs.cpp` | 175 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchInputs.cpp` | 197 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchInputs.cpp` | 199 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchInputs.cpp` | 201 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchInputs.cpp` | 219 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/SearchInputs.cpp` | 267 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchInputs.cpp` | 268 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchInputs.cpp` | 290 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchInputs.cpp` | 291 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchInputs.cpp` | 292 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchInputs.cpp` | 293 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchInputs.cpp` | 340 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchInputs.cpp` | 353 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchInputs.cpp` | 363 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchInputs.cpp` | 374 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchInputs.cpp` | 385 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchInputs.cpp` | 404 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchInputs.cpp` | 407 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchInputs.cpp` | 432 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchInputs.cpp` | 463 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchInputs.cpp` | 486 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/SearchInputsGeminiHelpers.cpp` | 34 | `cppcoreguidelines-avoid-non-const-global-variables, readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SearchInputsGeminiHelpers.cpp` | 70 | `readability-magic-numbers` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 44 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 45 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 46 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 47 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 50 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 51 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 52 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 53 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 54 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 57 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 58 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 111 | `cppcoreguidelines-pro-bounds-constant-array-index` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 118 | `cppcoreguidelines-pro-bounds-constant-array-index` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 133 | `cppcoreguidelines-pro-bounds-constant-array-index` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 141 | `cppcoreguidelines-pro-bounds-constant-array-index` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 170 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 183 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 189 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 199 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 207 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 251 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 258 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 271 | `readability-use-std-min-max` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 275 | `readability-use-std-min-max` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 281 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 302 | `readability-use-std-min-max` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 307 | `readability-use-std-min-max` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 313 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 340 | `readability-use-std-min-max` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 345 | `readability-use-std-min-max` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 350 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 367 | `misc-const-correctness` | Worth Fixing | Worth fixing; address misc-const-correctness and remove NOLINT. |
| `src/ui/SettingsWindow.cpp` | 406 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 436 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 439 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 453 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 490 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 518 | `readability-magic-numbers` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 528 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 531 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 545 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 551 | `readability-magic-numbers` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 561 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 563 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 577 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 593 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 597 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 602 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 625 | `bugprone-empty-catch` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 627 | `bugprone-empty-catch` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 629 | `bugprone-empty-catch` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 631 | `bugprone-empty-catch` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 644 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 680 | `misc-unused-parameters` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 689 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 691 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 692 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 701 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 708 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 722 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 729 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 730 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 739 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 812 | `readability-math-missing-parentheses, readability-magic-numbers` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 816 | `readability-math-missing-parentheses, readability-magic-numbers` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/SettingsWindow.cpp` | 820 | `readability-magic-numbers` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/StatusBar.cpp` | 206 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/StatusBar.cpp` | 210 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/StatusBar.cpp` | 212 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/StatusBar.cpp` | 216 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/StatusBar.cpp` | 218 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/StatusBar.cpp` | 227 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/StatusBar.cpp` | 243 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/StatusBar.cpp` | 276 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/StatusBar.cpp` | 287 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/StatusBar.cpp` | 295 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/StatusBar.cpp` | 297 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/StatusBar.cpp` | 301 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/StatusBar.cpp` | 315 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/StatusBar.cpp` | 318 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/StatusBar.cpp` | 321 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/StatusBar.cpp` | 324 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/StatusBar.cpp` | 327 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/StatusBar.cpp` | 343 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/StatusBar.cpp` | 347 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/StatusBar.cpp` | 350 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/StatusBar.cpp` | 360 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/StatusBar.cpp` | 363 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/StatusBar.cpp` | 374 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/StatusBar.cpp` | 376 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/StatusBar.cpp` | 404 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/StatusBar.cpp` | 406 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/StatusBar.cpp` | 409 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/StatusBar.cpp` | 414 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/StatusBar.cpp` | 416 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/StoppingState.cpp` | 38 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/Theme.cpp` | 11 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Theme.cpp` | 31 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Theme.cpp` | 35 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Theme.cpp` | 56 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Theme.cpp` | 60 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Theme.cpp` | 81 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Theme.cpp` | 83 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Theme.cpp` | 218 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Theme.cpp` | 267 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Theme.cpp` | 280 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Theme.cpp` | 287 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/Theme.h` | 19 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/Theme.h` | 20 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/Theme.h` | 21 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/Theme.h` | 24 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/Theme.h` | 25 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/Theme.h` | 26 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/Theme.h` | 29 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/Theme.h` | 32 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/Theme.h` | 33 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/Theme.h` | 34 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/Theme.h` | 37 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/Theme.h` | 38 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/Theme.h` | 39 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/Theme.h` | 40 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/Theme.h` | 43 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/Theme.h` | 44 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/Theme.h` | 45 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/Theme.h` | 48 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/Theme.h` | 56 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/Theme.h` | 57 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/Theme.h` | 58 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/Theme.h` | 59 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/UIRenderer.cpp` | 230 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/UIRenderer.cpp` | 294 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/UIRenderer.h` | 42 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/UIRenderer.h` | 44 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/UIRenderer.h` | 46 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/UIRenderer.h` | 47 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/UIRenderer.h` | 48 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/UIRenderer.h` | 50 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/UIRenderer.h` | 53 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/UIRenderer.h` | 54 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/UIRenderer.h` | 65 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/UIRenderer.h` | 67 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/UIRenderer.h` | 69 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/UIRenderer.h` | 70 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/UIRenderer.h` | 72 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/UIRenderer.h` | 74 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/UIRenderer.h` | 75 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/UiStyleGuards.h` | 20 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/ui/UiStyleGuards.h` | 32 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/ui/UiStyleGuards.h` | 53 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/usn/UsnMonitor.cpp` | 41 | `readability-identifier-naming, cppcoreguidelines-avoid-non-const-global-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/usn/UsnMonitor.cpp` | 53 | `readability-identifier-naming, cppcoreguidelines-avoid-non-const-global-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/usn/UsnMonitor.cpp` | 91 | `cppcoreguidelines-pro-type-reinterpret-cast` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/usn/UsnMonitor.cpp` | 106 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/usn/UsnMonitor.cpp` | 205 | `bugprone-empty-catch` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/usn/UsnMonitor.cpp` | 207 | `bugprone-empty-catch` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/usn/UsnMonitor.cpp` | 215 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/usn/UsnMonitor.cpp` | 230 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/usn/UsnMonitor.cpp` | 268 | `misc-const-correctness` | Worth Fixing | Worth fixing; address misc-const-correctness and remove NOLINT. |
| `src/usn/UsnMonitor.cpp` | 296 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/usn/UsnMonitor.cpp` | 335 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/usn/UsnMonitor.cpp` | 362 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/usn/UsnMonitor.cpp` | 363 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/usn/UsnMonitor.cpp` | 377 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/usn/UsnMonitor.cpp` | 412 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/usn/UsnMonitor.cpp` | 557 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/usn/UsnMonitor.cpp` | 566 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/usn/UsnMonitor.cpp` | 572 | `misc-const-correctness` | Worth Fixing | Worth fixing; address misc-const-correctness and remove NOLINT. |
| `src/usn/UsnMonitor.cpp` | 575 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/usn/UsnMonitor.cpp` | 647 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/usn/UsnMonitor.cpp` | 651 | `misc-const-correctness` | Worth Fixing | Worth fixing; address misc-const-correctness and remove NOLINT. |
| `src/usn/UsnMonitor.cpp` | 681 | `bugprone-empty-catch` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/usn/UsnMonitor.cpp` | 686 | `bugprone-empty-catch` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/usn/UsnMonitor.cpp` | 694 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/usn/UsnMonitor.cpp` | 740 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/usn/UsnMonitor.cpp` | 785 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/usn/UsnMonitor.cpp` | 788 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/usn/UsnMonitor.cpp` | 789 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/usn/UsnMonitor.cpp` | 790 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/usn/UsnMonitor.cpp` | 819 | `bugprone-empty-catch` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/usn/UsnMonitor.cpp` | 825 | `bugprone-empty-catch` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/usn/UsnMonitor.h` | 517 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/usn/UsnMonitor.h` | 568 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/usn/UsnRecordUtils.cpp` | 49 | `cppcoreguidelines-pro-type-const-cast` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/AsyncUtils.h` | 22 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/utils/CpuFeatures.cpp` | 20 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/utils/CpuFeatures.cpp` | 22 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/utils/CpuFeatures.cpp` | 26 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/utils/CpuFeatures.cpp` | 28 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/utils/CpuFeatures.cpp` | 30 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/utils/CpuFeatures.cpp` | 32 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/utils/CpuFeatures.cpp` | 34 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/utils/CpuFeatures.cpp` | 38 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/utils/CpuFeatures.cpp` | 40 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/utils/CpuFeatures.cpp` | 42 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/utils/CpuFeatures.cpp` | 44 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/utils/CpuFeatures.cpp` | 46 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/utils/CpuFeatures.cpp` | 51 | `readability-identifier-naming, cppcoreguidelines-avoid-c-arrays, hicpp-avoid-c-arrays, modernize-avoid-c-arrays` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/CpuFeatures.cpp` | 86 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/utils/CpuFeatures.cpp` | 89 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/utils/CpuFeatures.cpp` | 97 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/utils/CpuFeatures.cpp` | 102 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/utils/CpuFeatures.cpp` | 103 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/utils/CpuFeatures.cpp` | 106 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/utils/CpuFeatures.cpp` | 114 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/utils/CpuFeatures.cpp` | 129 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/CpuFeatures.cpp` | 139 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/utils/CpuFeatures.cpp` | 141 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/utils/CpuFeatures.cpp` | 147 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/utils/CpuFeatures.cpp` | 150 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/utils/CpuFeatures.cpp` | 152 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/utils/ExceptionHandling.cpp` | 38 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/ExceptionHandling.h` | 95 | `cppcoreguidelines-avoid-non-const-global-variables, readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/FileSystemUtils.h` | 3 | `clang-diagnostic-error` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/FileSystemUtils.h` | 102 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/FileSystemUtils.h` | 103 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/FileSystemUtils.h` | 197 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/FileSystemUtils.h` | 348 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/utils/FileSystemUtils.h` | 349 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/FileSystemUtils.h` | 357 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/utils/FileSystemUtils.h` | 358 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/FileSystemUtils.h` | 365 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/utils/FileSystemUtils.h` | 366 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/FileSystemUtils.h` | 396 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/utils/FileSystemUtils.h` | 420 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/utils/FileTimeTypes.h` | 23 | `cert-err58-cpp` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/FileTimeTypes.h` | 24 | `cert-err58-cpp` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/HashMapAliases.h` | 22 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/HashMapAliases.h` | 25 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/HashMapAliases.h` | 34 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/HashMapAliases.h` | 37 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/HashMapAliases.h` | 43 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/HashMapAliases.h` | 46 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/HashMapAliases.h` | 53 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/HashMapAliases.h` | 56 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LightweightCallable.h` | 26 | `cppcoreguidelines-pro-type-member-init, hicpp-member-init` | Worth Fixing | Worth fixing; address cppcoreguidelines-pro-type-member-init, hicpp-member-init and remove NOLINT. |
| `src/utils/LightweightCallable.h` | 38 | `google-explicit-constructor, hicpp-explicit-conversions, cppcoreguidelines-pro-type-member-init, hicpp-member-init` | Worth Fixing | Worth fixing; address cppcoreguidelines-pro-type-member-init, hicpp-member-init and remove NOLINT. |
| `src/utils/LightweightCallable.h` | 56 | `cppcoreguidelines-pro-type-member-init, hicpp-member-init` | Worth Fixing | Worth fixing; address cppcoreguidelines-pro-type-member-init, hicpp-member-init and remove NOLINT. |
| `src/utils/LightweightCallable.h` | 59 | `cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LightweightCallable.h` | 73 | `cppcoreguidelines-pro-type-member-init, hicpp-member-init` | Worth Fixing | Worth fixing; address cppcoreguidelines-pro-type-member-init, hicpp-member-init and remove NOLINT. |
| `src/utils/LightweightCallable.h` | 76 | `cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LightweightCallable.h` | 208 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/utils/LightweightCallable.h` | 212 | `misc-non-private-member-variables-in-classes, readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LightweightCallable.h` | 213 | `misc-non-private-member-variables-in-classes, readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LightweightCallable.h` | 214 | `misc-non-private-member-variables-in-classes, readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LightweightCallable.h` | 215 | `misc-non-private-member-variables-in-classes, readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 72 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 73 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 74 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 110 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 111 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 112 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 113 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 114 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 115 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 116 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 117 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 118 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 119 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 120 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 134 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/utils/LoadBalancingStrategy.cpp` | 190 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 191 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 192 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 193 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 194 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 195 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 204 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/utils/LoadBalancingStrategy.cpp` | 239 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 240 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 241 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 242 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 243 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 244 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 245 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 246 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 247 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 248 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 255 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 256 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 257 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 258 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 267 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/utils/LoadBalancingStrategy.cpp` | 306 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 335 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 336 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 337 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 338 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 339 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 340 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 349 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/utils/LoadBalancingStrategy.cpp` | 382 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 383 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 384 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 385 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 386 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 387 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 388 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 389 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 390 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 391 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 392 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 401 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/utils/LoadBalancingStrategy.cpp` | 427 | `misc-const-correctness` | Worth Fixing | Worth fixing; address misc-const-correctness and remove NOLINT. |
| `src/utils/LoadBalancingStrategy.cpp` | 447 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 448 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 449 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 450 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 451 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 452 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 453 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 454 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 455 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 456 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 457 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 466 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/utils/LoadBalancingStrategy.cpp` | 473 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 505 | `misc-const-correctness` | Worth Fixing | Worth fixing; address misc-const-correctness and remove NOLINT. |
| `src/utils/LoadBalancingStrategy.cpp` | 526 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 527 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 528 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 529 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 530 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 531 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 532 | `cppcoreguidelines-avoid-const-or-ref-data-members` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 533 | `readability-identifier-naming, misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 542 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/utils/LoadBalancingStrategy.cpp` | 555 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 702 | `bugprone-exception-escape` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 796 | `bugprone-exception-escape` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 898 | `bugprone-exception-escape` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 966 | `bugprone-exception-escape` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 1104 | `hicpp-named-parameter, readability-named-parameter` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 1123 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/LoadBalancingStrategy.cpp` | 1144 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 56 | `performance-enum-size` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 57 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 58 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 59 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 60 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 61 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 129 | `cppcoreguidelines-pro-type-reinterpret-cast` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 145 | `readability-make-member-function-const` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 170 | `readability-make-member-function-const` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 230 | `bugprone-empty-catch` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 360 | `bugprone-empty-catch` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 364 | `bugprone-empty-catch` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 393 | `cppcoreguidelines-init-variables, cppcoreguidelines-pro-bounds-pointer-arithmetic, concurrency-mt-unsafe` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 398 | `cppcoreguidelines-init-variables, cppcoreguidelines-pro-bounds-pointer-arithmetic, concurrency-mt-unsafe` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 446 | `readability-make-member-function-const` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 472 | `cert-err33-c` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 477 | `cppcoreguidelines-pro-type-vararg, hicpp-vararg` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 478 | `cert-err33-c` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 495 | `bugprone-empty-catch` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 527 | `cppcoreguidelines-avoid-c-arrays, hicpp-avoid-c-arrays, modernize-avoid-c-arrays` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 528 | `cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 530 | `cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 533 | `cppcoreguidelines-avoid-c-arrays, hicpp-avoid-c-arrays, modernize-avoid-c-arrays` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 543 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 544 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 545 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 547 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 549 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 551 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 552 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 553 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 554 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 562 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 563 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 564 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 565 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 566 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 569 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 570 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 571 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 572 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 573 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 581 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 582 | `misc-const-correctness` | Worth Fixing | Worth fixing; address misc-const-correctness and remove NOLINT. |
| `src/utils/Logger.h` | 583 | `bugprone-macro-parentheses` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 584 | `bugprone-macro-parentheses` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 588 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 589 | `misc-const-correctness` | Worth Fixing | Worth fixing; address misc-const-correctness and remove NOLINT. |
| `src/utils/Logger.h` | 590 | `bugprone-macro-parentheses` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 598 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 599 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 600 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 601 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 602 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 605 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 606 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 607 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 610 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 611 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 612 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 613 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 614 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 617 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 618 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 619 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 633 | `hicpp-named-parameter, readability-named-parameter` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 659 | `bugprone-empty-catch` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 664 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/Logger.h` | 665 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/RegexAliases.h` | 21 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/RegexAliases.h` | 22 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/RegexAliases.h` | 25 | `misc-unused-alias-decls` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/RegexAliases.h` | 29 | `readability-identifier-naming, cppcoreguidelines-missing-std-forward` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/RegexAliases.h` | 34 | `readability-identifier-naming, cppcoreguidelines-missing-std-forward` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/RegexAliases.h` | 44 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/RegexAliases.h` | 45 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/RegexAliases.h` | 48 | `misc-unused-alias-decls` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/RegexAliases.h` | 52 | `readability-identifier-naming, cppcoreguidelines-missing-std-forward` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/RegexAliases.h` | 57 | `readability-identifier-naming, cppcoreguidelines-missing-std-forward` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/SimpleRegex.h` | 47 | `misc-no-recursion` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/SimpleRegex.h` | 113 | `misc-no-recursion` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/SimpleRegex.h` | 133 | `misc-no-recursion` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/SimpleRegex.h` | 170 | `misc-no-recursion` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/SimpleRegex.h` | 191 | `misc-no-recursion` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/StdRegexUtils.h` | 39 | `misc-non-private-member-variables-in-classes, readability-redundant-member-init` | Worth Fixing | Worth fixing; address readability-redundant-member-init and remove NOLINT. |
| `src/utils/StdRegexUtils.h` | 40 | `misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/StdRegexUtils.h` | 47 | `hicpp-exception-baseclass` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/StdRegexUtils.h` | 49 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/utils/StdRegexUtils.h` | 54 | `hicpp-signed-bitwise` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/StdRegexUtils.h` | 118 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/StdRegexUtils.h` | 286 | `misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/StdRegexUtils.h` | 287 | `misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/StdRegexUtils.h` | 288 | `misc-non-private-member-variables-in-classes` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/StringSearch.h` | 13 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/StringSearch.h` | 16 | `cppcoreguidelines-macro-usage` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/StringSearch.h` | 243 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/StringSearch.h` | 249 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/StringSearch.h` | 250 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/StringUtils.h` | 33 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/StringUtils.h` | 40 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/StringUtils.h` | 53 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/StringUtils.h` | 64 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/StringUtils.h` | 65 | `cppcoreguidelines-pro-bounds-pointer-arithmetic` | Still Relevant | Keep; suppressed checks are still enabled. |
| `src/utils/SystemIdleDetector.cpp` | 144 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `src/utils/ThreadPool.cpp` | 41 | `cppcoreguidelines-pro-type-member-init, hicpp-member-init` | Worth Fixing | Worth fixing; address cppcoreguidelines-pro-type-member-init, hicpp-member-init and remove NOLINT. |
| `src/utils/ThreadPool.cpp` | 76 | `misc-const-correctness` | Worth Fixing | Worth fixing; address misc-const-correctness and remove NOLINT. |
| `src/utils/ThreadPool.h` | 23 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `tests/GeminiApiUtilsTests.cpp` | 12 | `readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `tests/GeminiApiUtilsTests.cpp` | 407 | `cppcoreguidelines-init-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `tests/SearchBenchmark.cpp` | 170 | `cppcoreguidelines-avoid-non-const-global-variables, readability-identifier-naming` | Still Relevant | Keep; suppressed checks are still enabled. |
| `tests/SearchBenchmark.cpp` | 194 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `tests/SearchBenchmark.cpp` | 223 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `tests/SearchBenchmark.cpp` | 233 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `tests/SearchBenchmark.cpp` | 275 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `tests/SearchBenchmark.cpp` | 329 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `tests/SearchBenchmark.cpp` | 344 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `tests/SearchBenchmark.cpp` | 347 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `tests/SearchBenchmark.cpp` | 352 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `tests/SearchBenchmark.cpp` | 358 | `(all)` | Still Relevant | Keep; generic NOLINT should be reviewed to specify checks or remove if possible. |
| `tests/SearchContextTests.cpp` | 19 | `readability-identifier-naming, cppcoreguidelines-avoid-non-const-global-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `tests/SearchContextTests.cpp` | 103 | `readability-identifier-naming, cppcoreguidelines-avoid-non-const-global-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `tests/SearchContextTests.cpp` | 149 | `readability-identifier-naming, cppcoreguidelines-avoid-non-const-global-variables` | Still Relevant | Keep; suppressed checks are still enabled. |
| `tests/SettingsTests.cpp` | 577 | `concurrency-mt-unsafe` | Still Relevant | Keep; suppressed checks are still enabled. |
| `tests/SettingsTests.cpp` | 598 | `concurrency-mt-unsafe` | Still Relevant | Keep; suppressed checks are still enabled. |
| `tests/SettingsTests.cpp` | 601 | `concurrency-mt-unsafe` | Still Relevant | Keep; suppressed checks are still enabled. |
| `tests/SettingsTests.cpp` | 615 | `concurrency-mt-unsafe` | Still Relevant | Keep; suppressed checks are still enabled. |
