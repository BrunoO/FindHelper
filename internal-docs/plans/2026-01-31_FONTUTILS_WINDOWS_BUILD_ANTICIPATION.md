# FontUtils Deduplication – Windows Build Anticipation

**Date:** 2026-01-31  
**Context:** Shared `FontUtilsCommon` and migrated `FontUtils_win.cpp`; this doc lists possible Windows-specific build issues and mitigations.

---

## 1. Windows.h and min/max macros

**Risk:** `Logger.h` includes `<windows.h>` when `_WIN32` is defined. Windows.h defines `min` and `max` as macros, which break `std::min` and `std::max`.

**Mitigation:**
- The Windows app target already defines **NOMINMAX** (and WIN32_LEAN_AND_MEAN) in `CMakeLists.txt` (e.g. lines 254–256). So when `FontUtilsCommon.cpp` is built as part of `find_helper` on Windows, the min/max macros are not defined.
- `FontUtilsCommon.cpp` does **not** use `std::min` or `std::max`. If you add them later, use **(std::min)** and **(std::max)** per AGENTS document so the code stays safe if NOMINMAX is ever removed.

**Action:** None required; just avoid using `std::min`/`std::max` without parentheses in FontUtilsCommon (or keep NOMINMAX on the target).

---

## 2. PGO (Profile-Guided Optimization)

**Risk:** New source `FontUtilsCommon.cpp` might be built with different PGO flags than the rest of the app, leading to LNK1269 (mismatched PGO object files).

**Mitigation:**
- `FontUtilsCommon.cpp` is in the same `APP_SOURCES` as the rest of the Windows app. It uses the same target (`find_helper`) and therefore the same `target_compile_options` and `target_link_options`, including PGO (/GL, /Gy, /LTCG:PGINSTRUMENT or /LTCG:PGOPTIMIZE).
- No per-file PGO overrides were added; the file is treated like any other app source.

**Action:** None. If you ever add a separate target or per-file options for FontUtilsCommon, ensure they match the main app’s PGO phase (instrument vs optimize) and compiler flags.

---

## 3. Include order and Logger.h pulling Windows headers

**Risk:** Including `Logger.h` before system headers can trigger include-order linters (e.g. readability-include-order, cpp:S954). On Windows, Logger.h pulls in `<windows.h>`, `<direct.h>`, etc.

**Mitigation:**
- In `FontUtilsCommon.cpp`, **system includes** (`<cfloat>`, `<string>`) are placed **before** project includes (`imgui.h`, `platform/EmbeddedFont_FontAwesome.h`, `utils/Logger.h`), matching the standard C++ include order required by AGENTS document and Sonar/clang-tidy.

**Action:** Keep system includes before project includes in FontUtilsCommon.cpp; no change needed if that order is already in place.

---

## 4. Unsafe C string functions (MSVC C4996)

**Risk:** Use of `strncpy`, `strcpy`, `strcat`, etc. triggers MSVC warnings and Sonar rules.

**Mitigation:**
- `FontUtilsCommon.cpp` does not use those functions. Path checks use `path != nullptr && path[0] != '\0'`; log messages use `std::string` concatenation.

**Action:** None. If you add string copying into fixed buffers, use `strcpy_safe`/`strcat_safe` from `StringUtils.h` per AGENTS document.

---

## 5. Forward declaration (class vs struct) and FontUtils_win.h

**Risk:** AGENTS document warns about C4099 when a type is forward-declared as `class` but defined as `struct` (or vice versa).

**Mitigation:**
- `FontUtils_win.h` uses `const AppSettings &`; `AppSettings` is forward-declared in the platform header (e.g. as `struct AppSettings` in `core/Settings.h`). No change was made to type kinds in this refactor.
- Shared API uses only ImGui and standard types; no project struct/class forward declarations in FontUtilsCommon.

**Action:** None unless you add new forward declarations; then keep `struct`/`class` consistent with the definition.

---

## 6. Lambda captures in templates (MSVC)

**Risk:** Implicit lambda captures (`[&]`, `[=]`) in template code can cause MSVC errors (C2062, C2059, C2143).

**Mitigation:**
- FontUtilsCommon and FontUtils_win do not introduce template code with lambdas; no new lambdas in template scope.

**Action:** None. If you add lambdas inside template functions, use explicit capture lists per AGENTS document.

---

## 7. Narrowing (unsigned int → int) in embedded font tables

**Risk:** Embedded font headers expose `unsigned int` for `*_compressed_size`; structs use `int size`; initializing without a cast can trigger narrowing warnings/errors (e.g. on macOS/Clang we already saw this).

**Mitigation:**
- All three platforms (Win/Linux/mac) use **`static_cast<int>(..._compressed_size)`** when filling embedded font tables. ImGui’s `AddFontFromMemoryCompressedTTF` takes `int`, so this is correct and consistent.

**Action:** None. Keep using `static_cast<int>` for any new embedded font size initializers on Windows.

---

## 8. Path literals and encoding (Windows)

**Risk:** Non-ASCII paths or wrong escaping could break font loading on Windows.

**Mitigation:**
- `FontUtils_win.cpp` uses narrow string literals with backslashes: `"C:\\Windows\\Fonts\\consola.ttf"`, etc. These are correct for the current ImGui/Windows usage. The app target uses UNICODE/_UNICODE; font path resolution remains narrow-string based and consistent with existing code.

**Action:** None. If you switch to wide paths or UTF-8 file APIs, update path handling and document encoding assumptions.

---

## 9. Build verification checklist (Windows)

Before considering the Windows build stable after FontUtils changes:

1. **Clean build (no PGO):**  
   `cmake -S . -B build -G "Visual Studio 17 2022" -A x64` then build `find_helper`. No LNK or compile errors.

2. **PGO Phase 1 (instrument):**  
   With `ENABLE_PGO=ON`, build Release, run the app to generate .pgc, merge to .pgd. No LNK1269/LNK1266.

3. **PGO Phase 2 (optimize):**  
   Reconfigure with existing .pgd, build Release again. Linker uses /LTCG:PGOPTIMIZE /USEPROFILE; no LNK1269.

4. **Warnings:**  
   Build with /W4 (or project default). No new warnings in `FontUtilsCommon.cpp` or `FontUtils_win.cpp`.

5. **Runtime:**  
   Run the app on Windows, change font family/size in Settings; confirm embedded and system fonts (e.g. Consolas, Segoe UI, Arial) load and FontAwesome icons render.

---

## 10. Summary

| Issue area           | Status / mitigation                                      |
|----------------------|----------------------------------------------------------|
| min/max macros       | NOMINMAX on target; no std::min/max in FontUtilsCommon  |
| PGO                  | Same target as rest of app; no extra config              |
| Include order        | System includes before project in FontUtilsCommon.cpp    |
| C4996 / string APIs  | No unsafe C string functions used                        |
| Forward decl (C4099)  | No new forward declarations in FontUtils code           |
| Lambda/template      | No new template lambdas                                  |
| Narrowing            | static_cast<int> for embedded font sizes                 |
| Paths / encoding      | Existing narrow literals; no change                      |

No code changes are strictly required for the current implementation; the above are anticipations and verification steps for the Windows build.
