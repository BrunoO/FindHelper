# PGO Windows Implementation Verification

**Date:** 2026-02-20

## Summary

The Windows PGO (Profile-Guided Optimization) implementation in `CMakeLists.txt` has been reviewed for **correctness** and **optimality**. It is **correct** and follows MSVC best practices. A few small improvements are recommended and applied where noted below.

---

## 1. Architecture Overview

| Phase | Trigger | Compiler | Linker | Output |
|-------|---------|----------|--------|--------|
| **Phase 1** | `ENABLE_PGO=ON`, no `.pgd` file | `/GL /Gy` | `/LTCG:PGINSTRUMENT /GENPROFILE` | Instrumented `.exe`, generates `.pgc` when run |
| **Phase 2** | `ENABLE_PGO=ON`, `FindHelper.pgd` exists | `/GL /Gy` | `/LTCG:PGOPTIMIZE /USEPROFILE /PGD:...` | Optimized `.exe` using profile |
| **No PGO** | `ENABLE_PGO=OFF` | `/Ob2 /Oi /Ot /Oy /GL /arch:SSE2` | `/LTCG /OPT:REF /OPT:ICF` | Standard optimized build |

Phase is chosen at **configure time** via `if(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/Release/FindHelper.pgd")`. No build-time detection.

---

## 2. Correctness Verification

### 2.1 Main executable (`find_helper`)

- **Phase 1**
  - Compiler: `/GL /Gy` only in Release, via `$<$<AND:$<CONFIG:Release>,$<BOOL:${ENABLE_PGO}>>:...>` — correct; avoids D9002 in Debug.
  - Linker: `/LTCG:PGINSTRUMENT /GENPROFILE` only in Release — correct; `/GENPROFILE` is linker-only.
  - Definitions: `_GENPROFILE` in Release when PGO — correct for optional runtime detection.

- **Phase 2**
  - Compiler: `/GL /Gy` in Release — correct.
  - Linker: `/LTCG:PGOPTIMIZE /USEPROFILE /PGD:${PGD_FILE_PATH}` — correct; full path avoids PG0188 if link working directory differs.
  - Definitions: `_USEPROFILE` in Release — correct.
  - Validation: Empty `.pgd` is detected and a warning is printed — correct.

### 2.2 FreeType (subdirectory)

- Gets same PGO compiler flags as main app (`/GL /Gy`) when PGO is on — **required** to avoid LNK1269 when linking with `find_helper`.
- Uses **standard** linker flags (`/LTCG /OPT:REF /OPT:ICF`), not `/LTCG:PGINSTRUMENT` or `/LTCG:PGOPTIMIZE` — **correct**; only the main executable should use PGO linker options to avoid LNK1266 and duplicate profile handling.

### 2.3 Object library `test_fileindex_obj`

- When `ENABLE_PGO=ON`: compiled with `/GL /Gy` in Release — correct; matches main app and avoids LNK1269 for any target linking these objects.
- No `/GENPROFILE` or `/USEPROFILE` on the object library — correct; PGO link options apply only to the final executable.

### 2.4 Test and benchmark targets

- All targets that link `test_fileindex_obj` (or otherwise share code with the main executable) get:
  - When PGO: `/GL /Gy` in Release so object format matches.
  - **Unconditional** `/LTCG /OPT:REF /OPT:ICF` in Release so the linker is started with LTCG from the beginning (avoids “restarting link with /LTCG” warning and is consistent).
- They do **not** use `/LTCG:PGINSTRUMENT` or `/LTCG:PGOPTIMIZE` — correct; tests/benchmarks do not produce or consume the app’s `.pgd`.

### 2.5 `settings_tests` (shared sources with main app)

- Uses Phase 1 vs Phase 2 only for status messages; both branches apply the same flags: `/GL /Gy` (when PGO) and `/LTCG /OPT:REF /OPT:ICF` in Release.
- Unconditional `/LTCG` is also set earlier for this target — redundant but harmless.

### 2.6 Standard (non-PGO) Release build

- When `ENABLE_PGO=OFF`: `find_helper` gets `/Ob2 /Oi /Ot /Oy /GL /arch:SSE2` and linker `/LTCG /OPT:REF /OPT:ICF` — correct and optimal for non-PGO.

---

## 3. Optimality

- **Compiler:** `/GL` (whole-program) and `/Gy` (function-level linking) are the right MSVC options for PGO and LTCG.
- **Linker:** `/LTCG:PGINSTRUMENT` + `/GENPROFILE` (Phase 1) and `/LTCG:PGOPTIMIZE` + `/USEPROFILE` (Phase 2) match the documented two-phase PGO workflow.
- **Dead code:** `/OPT:REF` and `/OPT:ICF` are used in all Release link lines — good for size and clarity.
- **Verbosity:** `/VERBOSE` was intentionally removed from the PGO link line to reduce log noise; can be re-enabled for debugging.

---

## 4. Improvements Applied

1. **Phase 2 `/PGD` path**  
   Use the full path for the profile database so the linker finds it regardless of working directory:
   - Use `PGD_FILE_PATH` (already set to `"${CMAKE_CURRENT_BINARY_DIR}/Release/FindHelper.pgd"`) in the Phase 2 linker option: `/PGD:${PGD_FILE_PATH}`.
   - Prevents PG0188 when the link is invoked from a different directory (e.g. some IDEs or scripts).

2. **`search_benchmark`**  
   This target links `test_fileindex_obj`. For consistency with other such targets and to avoid the “restarting link with /LTCG” warning when PGO is on, add unconditional `/LTCG /OPT:REF /OPT:ICF` for Release (same as other test/benchmark targets that use `test_fileindex_obj`).

---

## 5. Documentation and Workflow

- **PGO_SETUP.md:** Describes manual workflow (instrumented build → run → merge `.pgc` → reconfigure → optimized build); consistent with current implementation.
- **Configure command:** Documented as `cmake -S . -B build -DENABLE_PGO=ON -DCMAKE_BUILD_TYPE=Release`; required for multi-config so PGO flags apply only in Release and D9002 is avoided.
- **Merge command:** `pgomgr /merge /summary /detail FindHelper*.pgc FindHelper.pgd` in `build\Release` is correct and recommended for validation.

---

## 6. Checklist

| Item | Status |
|------|--------|
| Phase detection at configure time | OK |
| Phase 1 compiler/linker flags | OK |
| Phase 2 compiler/linker flags | OK |
| `/PGD` explicit (and full path recommended) | OK (improvement applied) |
| Empty `.pgd` check | OK |
| FreeType same PGO compiler, standard link | OK |
| `test_fileindex_obj` /GL when PGO, no PGO link opts | OK |
| Test/benchmark targets: /GL when PGO, unconditional /LTCG | OK |
| No PGO link options on test/benchmark executables | OK |
| Standard Release path when PGO off | OK |
| search_benchmark /LTCG for Release | OK (improvement applied) |

---

## 7. References

- `CMakeLists.txt` (Windows/PGO block ~908–1106, test PGO ~1450–2966).
- `docs/guides/building/PGO_SETUP.md`
- `internal-docs/guides/building/2026-01-17_PGO_CONFIGURATION_IMPROVEMENTS.md`
- MSVC: `/PGD`, `/USEPROFILE`, `/GENPROFILE`, `/LTCG` (PGINSTRUMENT, PGOPTIMIZE).
