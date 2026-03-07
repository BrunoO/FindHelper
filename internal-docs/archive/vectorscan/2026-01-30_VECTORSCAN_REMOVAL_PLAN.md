# Plan: Remove VectorScan (Minimize Lines and Maintenance)

**Date:** 2026-01-30  
**Goal:** Remove the VectorScan (Hyperscan) dependency and all `vs:` prefix support. Use a single regex path (std::regex via `rs:`). Minimize lines of code and long-term maintenance cost. **No code changes in this document—plan only.**

---

## Rationale

- **Maintenance:** VectorScan is an optional external dependency (find_package / pkg-config / manual find). Its integration adds ~130 lines in CMake, a `configure_vectorscan_for_test()` used by 15 test targets, and conditional compilation (`USE_VECTORSCAN`) in multiple files.
- **Lines:** Removing VectorScan deletes ~412 lines in `VectorScanUtils.cpp`/`.h`, ~100+ lines in `SearchPatternUtils.h` (VectorScan branch + CreateMatcher branch), ~20 lines in `SearchHelpWindow.cpp`, and ~130+ lines in `CMakeLists.txt` (option block + function + 15 call sites). No VectorScan-specific test file exists; tests only pull it in via object libraries.
- **Behavior:** After removal, `vs:` can be treated as an alias for `rs:` (same std::regex path). Users who used `vs:` get identical semantics via `rs:`; no feature loss except the optional SIMD regex engine.
- **Single regex path:** One implementation (std::regex + StdRegexUtils) reduces surface area, build variability, and platform-specific issues (e.g. VectorScan on Windows/vcpkg).

---

## What Stays (No VectorScan)

- **Regex:** `rs:` prefix → std::regex via `StdRegexUtils` (unchanged).
- **Path patterns:** `pp:` and PathPattern (unchanged).
- **Glob / Substring:** Unchanged.
- **Pattern type detection:** Remove `VectorScan` from the enum and from prefix detection; treat `vs:` as `rs:` (see below) or remove `vs:` and document “use rs: for regex”.

---

## Removal Steps (Order Matters)

### 1. Delete VectorScan implementation

| Action | Item |
|--------|------|
| **Delete file** | `src/utils/VectorScanUtils.h` |
| **Delete file** | `src/utils/VectorScanUtils.cpp` |

**Result:** No VectorScan types or APIs. Build will break until steps 2–4 are done.

---

### 2. Make `vs:` an alias for `rs:` (or remove `vs:`)

**File:** `src/search/SearchPatternUtils.h`

| Action | Detail |
|--------|--------|
| **Remove include** | `#include "utils/VectorScanUtils.h"` |
| **Remove enum value** | From `PatternType`: remove `VectorScan` and its comment. |
| **Prefix detection** | In `DetectPatternType()`: either (A) map `vs:` to `PatternType::StdRegex` (so `vs:` uses the same path as `rs:`), or (B) remove the `vs:` branch and drop support (document “use rs: for regex”). **Recommended: (A)** so existing `vs:` patterns keep working via std::regex. |
| **ExtractPattern** | Keep `vs:` in the prefix check so `vs:pattern` still strips to `pattern`; no change if already handling `vs:`. |
| **MatchPattern** | Remove the entire `case PatternType::VectorScan:` block. If `vs:` is mapped to `StdRegex`, that case is unreachable; delete it. |
| **CreateMatcherImpl** | Remove the entire `case PatternType::VectorScan:` block (including `#ifdef USE_VECTORSCAN` and `#else`). If `vs:` → StdRegex, this case is unreachable; delete it. |
| **Update comments** | Pattern type list: remove “vs: / VectorScan”; document that `vs:` is an alias for `rs:` if (A), or that only `rs:` is for regex if (B). |

**Result:** No references to `vectorscan_utils` or `VectorScan`; `vs:` (if kept) uses the std::regex path only.

---

### 3. Update Search Help UI

**File:** `src/ui/SearchHelpWindow.cpp`

| Action | Detail |
|--------|--------|
| **Remove include** | `#include "utils/VectorScanUtils.h"` |
| **Remove section** | Entire “6. VectorScan Regex (High-Performance)” block (title, `IsRuntimeAvailable()` check, bullet texts, examples, “available / not available” note). |
| **Renumber** | Renumber “7. PathPattern Shorthands…” to “6. PathPattern Shorthands…”. |
| **Optional** | If `vs:` is kept as alias: add one short bullet under the std::regex section: “'vs:' is an alias for 'rs:' (same regex engine).” |

**Result:** UI no longer mentions VectorScan; help stays accurate for `rs:` (and optional `vs:` alias).

---

### 4. Remove VectorScan from CMake

**File:** `CMakeLists.txt`

| Action | Detail |
|--------|--------|
| **Remove VectorScan block** | Delete the entire “--- VectorScan Integration ---” section: `set(USE_VECTORSCAN ...)`, `set_property(...)`, the `if(USE_VECTORSCAN STREQUAL "AUTO" OR ...)` block (find_package, pkg-config, manual find_library/find_path, target_compile_definitions, target_link_libraries, target_include_directories, messages, FATAL_ERROR when ON and not found). Ends at the `else()` that prints “VectorScan: Disabled”. **Do not** remove the “--- Compiler Options ---” or following sections. |
| **Remove source from targets** | Remove `src/utils/VectorScanUtils.cpp` from: main app `APP_SOURCES` (Windows and any other platform blocks), `test_utils_obj`, and any other target that lists it (e.g. `find_helper` if separate). Search for `VectorScanUtils` to find all. |
| **Remove helper and all call sites** | Delete the `function(configure_vectorscan_for_test ...)` definition and every `configure_vectorscan_for_test(...)` call (15 test targets). Remove comments that say “Configure VectorScan” or “VectorScanUtils” for those targets. |
| **PGO / optional** | If any PGO or toolchain block mentions VectorScan or `USE_VECTORSCAN`, remove those references. |

**Result:** No VectorScan option, no VectorScan library linking, no `USE_VECTORSCAN` definition; fewer test-config branches.

---

### 5. Tests: no test code changes

- No dedicated VectorScan test file exists. Tests that used `test_utils_obj` or `test_search_obj` (which included `VectorScanUtils.cpp`) will simply stop linking VectorScan after step 4.
- **Action:** Run the full test suite after step 4; fix any failures (e.g. tests that assumed VectorScan availability or `vs:`-specific behavior). If `vs:` is an alias for `rs:`, existing `rs:` regex tests cover the behavior.

---

### 6. Documentation and references

| Action | Detail |
|--------|--------|
| **Move VectorScan-specific docs** | Create `docs/vectorscan/` and move there (for historical reference): e.g. `docs/analysis/performance/2026-01-17_VECTORSCAN_*.md`, `docs/bugs/2026-01-17_VECTORSCAN_*.md`, `docs/guides/development/VECTORSCAN_BEST_PRACTICES.md`, `docs/investigation/VECTORSCAN_PATTERN_NOT_MATCHING_INVESTIGATION.md`, `docs/plans/2026-01-17_VECTORSCAN_INTEGRATION_PLAN.md`, and any other docs that are primarily about VectorScan. Optionally move `HYPERSCAN_VECTORSCAN_BEST_PRACTICES.md` from repo root to `docs/vectorscan/`. |
| **Update other docs** | In docs that only mention VectorScan in passing (e.g. architecture, performance review, CMake audit): replace or remove the VectorScan sentence so the doc stays accurate and maintenance is minimal. No need to rewrite entire sections. |
| **.clang-tidy / analysis** | Remove or adjust any line that excludes or references `external/vectorscan/` (there is no such subdirectory; VectorScan is system/vcpkg). |

**Result:** VectorScan is documented only in an archive folder; main docs and configs no longer depend on it.

---

## Summary: Line and Maintenance Impact

| Area | Approx. change |
|------|----------------|
| `VectorScanUtils.cpp` / `.h` | **-412 lines** (deleted) |
| `SearchPatternUtils.h` | **-~100 lines** (VectorScan enum, branch in DetectPatternType, MatchPattern case, CreateMatcherImpl case, comments) |
| `SearchHelpWindow.cpp` | **-~20 lines** (section + include) |
| `CMakeLists.txt` | **-~130+ lines** (VectorScan option block, `configure_vectorscan_for_test` body, 15 call sites, comments) |
| Tests | 0 lines (no VectorScan test file; build-only removal) |
| Docs | Moved to `docs/vectorscan/`; short edits elsewhere |

**Total:** Roughly **660+ lines removed** and one fewer optional dependency and build path. Single regex implementation (std::regex) reduces maintenance and platform variability.

---

## Checklist Before Committing

- [ ] No `#include "utils/VectorScanUtils.h"` or `vectorscan_utils::` left in `src/`.
- [ ] No `USE_VECTORSCAN` or `VECTORSCAN_AVAILABLE` in CMake or source.
- [ ] `vs:` either removed or documented as alias for `rs:` and implemented via StdRegex path only.
- [ ] All tests pass (macOS: `scripts/build_tests_macos.sh`; Windows/Linux as per project).
- [ ] Search Help and any user-facing docs no longer promise “VectorScan” or “vs:” as a separate engine; if `vs:` is kept, it is clearly an alias for `rs:`.
