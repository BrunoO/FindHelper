# Clang-Tidy: Reduce Work Per File — Options Exploration

**Date:** 2026-02-19

This document explores four ways to reduce the amount of work clang-tidy does per file: narrower checks, disabling the analyzer family, header filtering, and avoiding wildcard-enabling on upgrades. For each, we summarize **current state**, **options**, and **recommendations**.

---

## 1. Avoid `-checks=*` or a Very Broad Glob

### Current state

- **`.clang-tidy`** uses a broad glob: `Checks: >-\n  *,\n  -abseil-*,\n  -altera-*,\n  ...` (enable everything, then disable specific families).
- Scripts (**run_clang_tidy.sh**, **pre-commit-clang-tidy.sh**, **analyze_clang_tidy_warnings.py**) do **not** pass `-checks=` on the command line; they rely entirely on the config file.
- Effect: every check that isn’t explicitly disabled is enabled, including any **new checks added in future LLVM releases** when the tool is upgraded.

### Why it matters

- More checks ⇒ more work per translation unit and longer runs.
- A single glob like `*` makes behavior dependent on the clang-tidy version (new checks appear automatically and can change behavior or add noise).

### Options

| Approach | Pros | Cons |
|---------|------|------|
| **A. Keep `*` and current disables** | No config churn; status quo. | Maximum work per file; behavior drifts on LLVM upgrade. |
| **B. Explicit family list (no `*`)** | Only the families you care about run; predictable; often faster. | Config is longer; you must add new families explicitly. |
| **C. Small set of globs** | e.g. `bugprone-*,cppcoreguidelines-*,performance-*,readability-*,modernize-*,misc-*` — still broad but no `*`, so you control what’s in scope. | New checks inside those families still appear on upgrade. |

### Recommendation

- **Short term:** Keep current config if you don’t want to change behavior yet.
- **Medium term:** Prefer **B or C** — drop `*` and enable only the families you need (e.g. the list in C). That reduces work and avoids surprise new checks on upgrade. Align the list with **docs/analysis/CLANG_TIDY_CLASSIFICATION.md** and SonarQube mapping in `.clang-tidy` (bugprone, cppcoreguidelines, performance, readability, modernize, misc). Do **not** include `clang-analyzer-*` in that list unless you rely on it (see section 2).

---

## 2. Disable the Analyzer Family Unless You Need It

### Current state

- Only **five** analyzer checks are explicitly disabled:
  - `clang-analyzer-apiModeling.google.GTest`
  - `clang-analyzer-optin.mpi.MPI-Checker`
  - `clang-analyzer-webkit.NoUncountedMemberChecker`
  - `clang-analyzer-webkit.RefCntblBaseVirtualDtor`
  - `clang-analyzer-webkit.UncountedLambdaCapturesChecker`
- Because the config uses `*`, **all other `clang-analyzer-*` checks are enabled** (e.g. core, security, unix, deadcode, nullability, etc.).
- The comment in `.clang-tidy` says “Static analysis similar to SonarQube” but does not require the analyzer for current policy.

### Why it matters

- The **clang-analyzer-*** checks are among the most expensive: they perform deeper static analysis (e.g. path-sensitive, symbolic execution–style). Disabling the whole family often gives a large speedup with no config change beyond one line.

### Options

| Approach | Pros | Cons |
|---------|------|------|
| **A. Add `-clang-analyzer-*`** | One-line change; big reduction in work per file; same behavior as today for the five already disabled. | You stop getting any analyzer findings in normal runs. |
| **B. Keep current (only disable the five)** | No change; you keep all other analyzer checks. | Higher cost per file; slower full runs and pre-commit. |
| **C. Re-enable analyzer only for audits** | Use a separate config or `-checks=` override when doing security/reliability audits. | Requires documenting when/how to run with analyzer. |

### Recommendation

- **Add `-clang-analyzer-*`** to the `Checks` list in `.clang-tidy` unless the project explicitly relies on analyzer findings in CI/pre-commit. Document in the “Disabled Checks Rationale” section that the analyzer family is disabled for performance and can be re-enabled (e.g. via a second config or one-off `-checks=...,clang-analyzer-*`) for periodic audits.
- If you add it, you can remove the five individual `-clang-analyzer-...` lines to avoid redundancy (optional cleanup).

---

## 3. Exclude Heavy or Low-Value Headers (HeaderFilterRegex)

### Current state

- **HeaderFilterRegex** is set to: `'^(?!external/).*\.(h|hpp|cpp)$'`.
  - **Excludes:** paths under `external/` (third-party code).
  - **Includes:** all other `.h`, `.hpp`, `.cpp` (e.g. `src/`, `tests/`, any other in-repo code).
- Scripts exclude `external/` and generated files (e.g. `Embedded*`) when selecting files to analyze; they do not pass a separate `--header-filter` on the command line (config is used).

### How header filtering works

- **HeaderFilterRegex** controls **which headers’ diagnostics are reported**. Clang-tidy still **parses** all included files (including system and third-party headers) to build the AST; it does not skip parsing. So:
  - A **tighter** filter (e.g. only `src/` and `tests/`) reduces **output** and avoids reporting in other in-repo or system headers; it does **not** reduce parsing cost.
  - Excluding **external/** avoids reporting issues in third-party code, which is desirable and already in place.

### Options

| Approach | Pros | Cons |
|---------|------|------|
| **A. Keep current regex** | Already excludes `external/`; no change. | Diagnostics can still appear for any non-external header (e.g. system, or other dirs). |
| **B. Restrict to project source only** | e.g. `^(src/|tests/).*\.(h|hpp|cpp)$` — only report in `src/` and `tests/`. Fewer reported lines; clearer that “our” code is in scope. | No reduction in parse time; slightly stricter than today. |
| **C. Use ExcludeHeaderFilterRegex** | If your clang-tidy version supports it, exclude system or other heavy paths from **checking** (may reduce work where implemented). | Need to verify support and behavior in your LLVM version. |

### Recommendation

- **Keep** the current **HeaderFilterRegex**; it already excludes `external/` and is well documented in `.clang-tidy`.
- **Optional:** Tighten to project code only, e.g. `^(src/|tests/).*\.(h|hpp|cpp)$`, if you want diagnostics only in `src/` and `tests/`. This improves clarity and focus, not parse performance.
- **Optional:** In newer clang-tidy, explore **ExcludeHeaderFilterRegex** for system or other heavy header paths if you need to reduce checking work (document the version and behavior once tested).

---

## 4. Don’t Wildcard-Enable New Checks on Every LLVM Upgrade

### Current state

- With `*` in `Checks`, **any new check** added in a new clang-tidy/LLVM release is **enabled automatically** after an upgrade.
- That can both slow runs and change behavior (new warnings, different style suggestions).

### Recommendation

- **Process:** When upgrading LLVM / clang-tidy:
  1. Prefer **not** using `*` (see section 1); use an explicit list of families or a small set of globs.
  2. If you keep `*`, after each upgrade run `clang-tidy -p build --list-checks` (with your `.clang-tidy` loaded) and compare to a baseline (e.g. saved list from previous version) to see new checks.
  3. Decide explicitly whether to disable new checks or adopt them; document in “Disabled Checks Rationale” or a changelog.
- **Documentation:** Add a short note to **docs/analysis/CLANG_TIDY_GUIDE.md** or **AGENTS.md** (e.g. under “Modifying `.clang-tidy` safely”): “On LLVM/clang-tidy upgrade, review new checks (e.g. via --list-checks) and do not blindly rely on `*`; prefer explicit check families to avoid unexpected new warnings and extra cost.”

---

## 5. Run in Parallel

### Current state

- **run_clang_tidy.sh:** Uses `echo "$SOURCE_FILES" | xargs "$CLANG_TIDY_CMD" -p "$COMPILE_DB"` with **no `-P`** — runs clang-tidy **one file at a time** (sequential).
- **analyze_clang_tidy_warnings.py:** Loops over files with `for file_path in cpp_files:` and calls `scan_file()` — **fully sequential**.
- **pre-commit-clang-tidy.sh:** Runs clang-tidy once per staged file in a loop — sequential (acceptable for few files).
- The project does **not** use LLVM’s **run-clang-tidy.py** (which supports `-j N` and is the design-recommended driver for batch analysis).

### Why it matters

- Running N clang-tidy processes in parallel (e.g. one per core) can reduce wall-clock time for full-project runs by roughly a factor of the number of cores (subject to I/O and memory).

### Options

| Approach | Pros | Cons |
|----------|------|------|
| **xargs -P N** | Simple; no new dependency; works with existing pipe (filter script is line-based, so interleaved output is fine). | Need to choose N (e.g. `nproc` or cap). |
| **run-clang-tidy.py -p build -j N** | Official LLVM script; handles parallelism and compile_commands.json; recent versions default to all cores. | Requires script (often in LLVM share dir); not all installs have it. |
| **analyze_clang_tidy_warnings.py: multiprocessing** | Parallel Python; good for CI. | More code; need to merge results. |

### Recommendation

- **run_clang_tidy.sh:** Use **`xargs -P N`** with N = number of cores (e.g. `nproc` on Linux, `sysctl -n hw.ncpu` on macOS, default 4). Add an optional `--jobs N` so users can override. This is the design-recommended way to scale (multiple clang-tidy processes in parallel).
- **analyze_clang_tidy_warnings.py:** Use `concurrent.futures.ProcessPoolExecutor` to run `scan_file()` in parallel; default workers = CPU count (or 4). Optional `--jobs N` to override. **Done.**
- **CI/automation:** Same idea — drive multiple clang-tidy processes (e.g. `ninja -j` if using CMAKE_CXX_CLANG_TIDY, or `xargs -P`, or CodeChecker). Document in CLANG_TIDY_GUIDE.md.

---

## Summary Table

| Lever | Current state | Recommended change |
|-------|----------------|---------------------|
| **Broad glob** | `*` with many `-foo` disables | Prefer explicit families (e.g. bugprone-*, cppcoreguidelines-*, performance-*, readability-*, modernize-*, misc-*) and drop `*`. |
| **Analyzer** | Only 5 analyzer checks disabled; rest enabled | Add `-clang-analyzer-*` unless you rely on analyzer in CI/pre-commit. ✅ Done. |
| **Header filter** | `^(?!external/).*\.(h|hpp|cpp)$` | Keep; optionally restrict to `^(src/|tests/).*\.(h|hpp|cpp)$`. |
| **Upgrade policy** | Not documented | Document: review new checks on upgrade; avoid blind `*`. |
| **Parallel** | Sequential (xargs without -P; Python loop) | Use `xargs -P N` in run_clang_tidy.sh; ProcessPoolExecutor in analyze_clang_tidy_warnings.py; optional --jobs. **Done.** |

---

## References

- `.clang-tidy` (Checks, HeaderFilterRegex, Disabled Checks Rationale).
- **docs/analysis/CLANG_TIDY_GUIDE.md** — usage and verification commands.
- **docs/analysis/CLANG_TIDY_CLASSIFICATION.md** — which checks the project cares about.
- LLVM: HeaderFilterRegex vs ExcludeHeaderFilterRegex (exclude headers from checking).
- **docs/analysis/2026-02-19_CLANG_TIDY_CACHE_RESEARCH.md** — caching (complementary to reducing work per file).
