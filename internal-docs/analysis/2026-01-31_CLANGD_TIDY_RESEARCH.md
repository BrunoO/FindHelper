# Research: Using clangd-tidy for Faster Analysis

**Date:** 2026-01-31  
**Source:** [clangd-tidy](https://github.com/lljbash/clangd-tidy) (GitHub)  
**Purpose:** Assess whether we can use clangd-tidy as a faster alternative to clang-tidy while keeping consistent warning measurement.

---

## 1. What is clangd-tidy?

[clangd-tidy](https://github.com/lljbash/clangd-tidy) is a **wrapper around clangd** (the language server) that runs clang-tidy–style diagnostics via clangd and collects the output. It is designed as a **drop-in replacement** for clang-tidy in scripts and CI.

- **Install:** `pip install clangd-tidy`
- **Usage:** `clangd-tidy -p build src/file1.cpp src/file2.cpp ...`
- **Extra tool:** `clangd-tidy-diff` runs on modified files / changed lines only (e.g. `git diff -U0 HEAD^^..HEAD | clangd-tidy-diff -p build`).

---

## 2. Pros for Our Project

| Benefit | Detail |
|--------|--------|
| **Speed** | Author reports **>10× faster** than clang-tidy; clangd only builds AST from headers (does not run all checks on every included header). |
| **Same config** | clangd reads `.clang-tidy` from the project; our existing config is used. |
| **Compile DB** | `-p build` (default `build`) matches our `analyze_clang_tidy_warnings.py` and pre-commit. |
| **Headers** | Can check headers even if not in `compile_commands.json`. |
| **Deduplication** | Diagnostics grouped by file; no duplicate warnings from the same header in multiple TUs. |
| **Optional format** | `-f` runs clang-format checks as well. |
| **Diff mode** | `clangd-tidy-diff` fits “only changed lines” workflows (e.g. pre-commit or incremental lint). |

---

## 3. Cons and Risks

| Issue | Detail |
|-------|--------|
| **No `--fix`** | clangd-tidy does not support `--fix`. Use editor code actions (clangd) or keep running clang-tidy when applying fixes. |
| **Checks disabled in clangd** | clangd **always disables** a small set of checks ([TidyProvider.cpp](https://github.com/llvm/llvm-project/blob/main/clang-tools-extra/clangd/TidyProvider.cpp)): `misc-include-cleaner`, `llvm-header-guard`, `modernize-macro-to-enum`, `cppcoreguidelines-macro-to-enum`, `bugprone-use-after-move`, `hicpp-invalid-access-moved`, `bugprone-unchecked-optional-access`. We already disable `llvm-header-guard` in `.clang-tidy`; the rest are either unused or minor for us. |
| **Slow checks in clangd** | clangd uses a [fast-check filter](https://github.com/llvm/llvm-project/blob/main/clang-tools-extra/clangd/TidyFastChecks.inc). **`misc-const-correctness` is SLOW (67% parse regression)** and may be disabled or restricted when “fast only” is used. We have **12** such warnings; they might not appear in clangd-tidy output. |
| **Different scope** | clang-tidy runs checks on the current file and all included headers (suppressing system headers). clangd only runs on the current file’s AST and may skip parsing function bodies in included headers, so **diagnostics in header bodies can be missed**. |
| **Output format** | Output may differ from clang-tidy (e.g. format, ordering). Our `analyze_clang_tidy_warnings.py` parses `warning:` and `[check-name]`; we’d need to confirm or adapt parsing for clangd-tidy. |
| **Known discrepancies** | The repo documents issues [#7](https://github.com/lljbash/clangd-tidy/issues/7), [#15](https://github.com/lljbash/clangd-tidy/issues/15), [#16](https://github.com/lljbash/clangd-tidy/issues/16) vs clang-tidy. |

---

## 4. Our High-Value Checks vs clangd

From LLVM’s `TidyFastChecks.inc` and `TidyProvider.cpp`:

| Check | In clangd | Note |
|-------|-----------|------|
| **readability-identifier-naming** | ✓ FAST | Runs in clangd. |
| **bugprone-empty-catch** | ✓ FAST | Runs in clangd. |
| **modernize-use-nodiscard** | ✓ FAST | Runs in clangd. |
| **cppcoreguidelines-pro-type-member-init** | ✓ FAST | Runs in clangd. |
| **readability-implicit-bool-conversion** | ✓ FAST | Runs in clangd. |
| **misc-const-correctness** | ✗ **SLOW (67%)** | May be disabled or limited in clangd; our 12 warnings might not show. |
| **llvm-header-guard** | Disabled in clangd | We already disable it in `.clang-tidy`. |

So **most** of our high-value checks are available in clangd; the main gap is **misc-const-correctness** (and possibly other slow checks depending on clangd configuration).

---

## 5. Recommendation

### Option A: Use clangd-tidy as an optional fast path (recommended)

- **Keep clang-tidy as the source of truth** for:
  - Baseline counts and status doc (e.g. `2026-01-31_CLANG_TIDY_STATUS.md`).
  - Pre-commit (or run both and compare if desired).
  - Any `--fix` usage.
- **Add clangd-tidy as an optional, faster check** for:
  - **CI:** Run `clangd-tidy -p build $(find src -name '*.cpp' -o -name '*.h')` and fail on diagnostics; much faster than per-file clang-tidy.
  - **Local “quick check”:** e.g. `clangd-tidy -p build src/path/to/file.cpp` before committing.
  - **Diff-only:** `git diff -U0 main -- '*.cpp' '*.h' | clangd-tidy-diff -p build` to only report issues on changed lines.
- **Accept** that clangd-tidy may report **fewer** warnings (e.g. no `misc-const-correctness`, or fewer from headers). Do **not** replace the official baseline with clangd-tidy counts; keep baseline from `analyze_clang_tidy_warnings.py` (clang-tidy).

### Option B: Full replacement for CI

- **Possible** if we are willing to:
  - Drop strict comparability with current baseline (e.g. ignore the ~12 misc-const-correctness in “fast” runs).
  - Parse clangd-tidy output in our script (or use its exit code only).
  - Keep running clang-tidy locally or in a separate job when we need `--fix` or exact clang-tidy counts.

### What to do next

1. **Try it once:**  
   `pip install clangd-tidy` then e.g.  
   `clangd-tidy -p build src/utils/Logger.h src/search/SearchWorker.cpp`  
   and compare output and runtime vs  
   `clang-tidy -p build --config-file=.clang-tidy src/utils/Logger.h src/search/SearchWorker.cpp`.
2. **Compare counts:** Run `analyze_clang_tidy_warnings.py` (clang-tidy) on a subset of files, then run clangd-tidy on the same list and diff the number of diagnostics (and check names). Expect clangd-tidy to be faster but possibly fewer (e.g. no misc-const-correctness).
3. **Pre-commit / diff:** Experiment with `clangd-tidy-diff` on a branch and see if “diagnostics only on changed lines” is acceptable for pre-commit or a separate CI step.
4. **Document:** If we adopt clangd-tidy, add a short note in `docs/analysis/` and/or `scripts/README_PRE_COMMIT.md` that:
   - clang-tidy remains the reference for baseline and `--fix`.
   - clangd-tidy is the optional fast path and may report fewer checks (e.g. misc-const-correctness).

---

## 6. References

- [clangd-tidy README](https://github.com/lljbash/clangd-tidy?tab=readme-ov-file)
- [clangd TidyProvider.cpp (disabled checks)](https://github.com/llvm/llvm-project/blob/main/clang-tools-extra/clangd/TidyProvider.cpp)
- [clangd TidyFastChecks.inc (fast vs slow)](https://github.com/llvm/llvm-project/blob/main/clang-tools-extra/clangd/TidyFastChecks.inc)
- [Why clang-tidy in clangd is faster (Stack Overflow)](https://stackoverflow.com/questions/76531831/why-is-clang-tidy-in-clangd-so-much-faster-than-run-clang-tidy-itself)
- Our status: `docs/analysis/2026-01-31_CLANG_TIDY_STATUS.md`  
- Our script: `scripts/analyze_clang_tidy_warnings.py`
