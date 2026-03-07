# Clang-Tidy Cache Research: ctcache and CMake Integration

**Date:** 2026-02-19

## Summary

**Yes, we can speed up clang-tidy using a cache.** A cache wrapper (e.g. **ctcache**) reuses analysis results when the same translation unit is re-analyzed with the same flags and config, giving order-of-magnitude speedups on incremental runs. Integration is possible in two ways:

1. **Script-based (recommended first step):** Use the cache as the “clang-tidy” command in `run_clang_tidy.sh` and `pre-commit-clang-tidy.sh`. No CMake change; every current lint flow benefits.
2. **CMake-based:** Set `CMAKE_CXX_CLANG_TIDY` to the cache wrapper so that “linting builds” (e.g. a dedicated build dir with clang-tidy enabled) benefit from the cache. This only applies if the project adopts build-time linting.

---

## What is ctcache?

**ctcache** (matus-chochlik/ctcache) is a cache for clang-tidy results:

- **Behavior:** Wraps `clang-tidy` and caches successful runs. For a given (command line + config + source) it stores a hash; on hit it returns the cached result without running clang-tidy.
- **Hash inputs:** Command-line arguments, relevant config files, and source content (so header changes invalidate the cache as expected).
- **Modes:** Local (directory on disk) or client/server (HTTP/Redis/S3/GCS). Local is enough for single-machine speedup.
- **Install:** Python 3.7+, installable via pip or from repo:
  - `pip install ctcache` (if published), or
  - `pip install git+https://github.com/matus-chochlik/ctcache.git`
  - Entry point: `clang-tidy-cache` (invokes `ctcache.clang_tidy_cache:main`).

Key env vars (client):

| Variable | Purpose |
|----------|---------|
| `CTCACHE_SAVE_OUTPUT` | Must be set for the cache to store results (otherwise only disables running clang-tidy on hit). |
| `CTCACHE_DIR` | Local cache directory. Default is under temp (may be cleared on reboot); set to a persistent path for stable cache. |
| `CTCACHE_DISABLE` | Set to disable cache and always run clang-tidy. |
| `CTCACHE_CLANG_TIDY` | Path to real `clang-tidy` if the wrapper is not invoked with it as first argument. |

Usage pattern: the “clang-tidy” command becomes a wrapper that runs:

```bash
clang-tidy-cache /path/to/real/clang-tidy "$@"
```

(or the repo’s shell script that calls the Python module with the same idea). So any script or CMake flow that invokes “clang-tidy” will automatically use the cache when they use this wrapper.

---

## Current Project Usage

- **CMake:** `CMAKE_EXPORT_COMPILE_COMMANDS` is ON; **`CMAKE_CXX_CLANG_TIDY` is not set.** Clang-tidy is not run as part of the build.
- **Scripts:**
  - `scripts/run_clang_tidy.sh`: runs clang-tidy on all project sources via `xargs` and a single chosen `CLANG_TIDY_CMD` (currently `clang-tidy-wrapper.sh` if present, else system `clang-tidy`).
  - `scripts/pre-commit-clang-tidy.sh`: runs clang-tidy on staged files only.
- **Wrapper:** `scripts/clang-tidy-wrapper.sh` exists for macOS (fixes SDK/include paths); it does not implement caching.

So today, **all** clang-tidy runs go through these scripts; there is no separate “linting build” that uses CMake to run clang-tidy per file.

---

## Integration Options

### Option 1: Script-Only Integration (recommended first)

**Idea:** Make the command used by both scripts be the cache wrapper. No CMake changes; every existing lint path (full run + pre-commit) benefits.

**Steps:**

1. Install ctcache (e.g. `pip install ctcache` or from git).
2. Add a small wrapper script (e.g. `scripts/clang-tidy-cached.sh`) that:
   - Sets `CTCACHE_SAVE_OUTPUT=1` (and optionally `CTCACHE_DIR` to a persistent dir, e.g. `.ctcache` in project root or `$HOME/.cache/ctcache`).
   - Invokes `clang-tidy-cache "$REAL_CLANG_TIDY" "$@"`, where `REAL_CLANG_TIDY` is the current logic (existing `clang-tidy-wrapper.sh` or system clang-tidy).
3. In `run_clang_tidy.sh`: prefer this cached wrapper as `CLANG_TIDY_CMD` when ctcache is available (e.g. `command -v clang-tidy-cache`), otherwise keep current behavior.
4. In `pre-commit-clang-tidy.sh`: use the same cached wrapper when available so pre-commit runs are also cached.

**Result:** Second runs (and pre-commit on unchanged files) become much faster for files that haven’t changed. No change to CMake or build process.

### Option 2: CMake `CMAKE_CXX_CLANG_TIDY` (optional, for “lint builds”)

**Idea:** Some teams run a dedicated “lint” build where the C++ compiler invocation is replaced by clang-tidy (via `CMAKE_CXX_CLANG_TIDY`). Every compile in that build is then a clang-tidy run. Using a cache wrapper there gives the same order-of-magnitude benefit for repeated lint builds.

**Steps:**

1. Add an option, e.g. `ENABLE_CLANG_TIDY_LINT_BUILD` (or use a separate build dir with `-DCMAKE_CXX_CLANG_TIDY=...`).
2. When enabled:
   - `find_program(CLANG_TIDY_CACHE NAMES clang-tidy-cache)` (and optionally `find_program(CLANG_TIDY NAMES clang-tidy)`).
   - If cache found: `set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_CACHE};${CLANG_TIDY}")` (semicolon-separated list: wrapper + real clang-tidy).
   - If not found: `set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY}")` (plain clang-tidy).
3. Document that for cache effectiveness, `CTCACHE_DIR` (and `CTCACHE_SAVE_OUTPUT`) should be set when configuring/building (e.g. in CI or developer env).

**Caveats:**

- The project does **not** currently use a lint build; this only helps if you introduce one (e.g. `cmake -B build-lint -DENABLE_CLANG_TIDY_LINT_BUILD=ON` and then `cmake --build build-lint`).
- Lint builds are usually run with `-j 1` when using `-fix` to avoid races on shared headers; without `-fix`, parallel builds are possible but each “compile” is a full clang-tidy run.

**Conclusion:** Script integration (Option 1) gives immediate benefit with no workflow change. CMake integration (Option 2) is useful only if you add a lint-build workflow; it can then use the same cache wrapper for consistency.

---

## Recommendation

1. **Introduce ctcache via scripts first**
   - Add `scripts/clang-tidy-cached.sh` that calls `clang-tidy-cache` with the existing “real” clang-tidy (e.g. current wrapper or system binary).
   - Use a persistent `CTCACHE_DIR` (e.g. `.ctcache` in project root, or under `$HOME/.cache`) and add it to `.gitignore`.
   - In `run_clang_tidy.sh` and `pre-commit-clang-tidy.sh`, prefer the cached wrapper when `clang-tidy-cache` is available.
2. **Optional: CMake integration**
   - Only if the project adopts a “linting build” (e.g. a target or build dir that uses `CMAKE_CXX_CLANG_TIDY`). Then set `CMAKE_CXX_CLANG_TIDY` to the same cache wrapper so that build-time linting benefits from the cache without changing developer commands.

This keeps a single cache (ctcache) and reuses it for both script-based and, if desired, CMake-based linting.

---

## References

- ctcache: https://github.com/matus-chochlik/ctcache (README, env vars, wrapper script, pyproject.toml).
- CMake: `CMAKE_CXX_CLANG_TIDY` (CMake 3.6+), semicolon-separated list; often used as `clang-tidy;-fix` or with a wrapper as first element.
- Stack Overflow: “Is it possible to accelerate clang-tidy using ccache or similar?” (ccache does not cache clang-tidy; dedicated wrappers like ctcache do).
- Alternative: ejfitzgerald/clang-tidy-cache (Go), freedick/cltcache; ctcache is widely referenced and Python-based, which fits the existing scripts.
