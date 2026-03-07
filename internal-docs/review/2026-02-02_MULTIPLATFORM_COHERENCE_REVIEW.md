# Multi-Platform Coherence Review (Checklist-Based)

**Date:** 2026-02-02  
**Scope:** Codebase reviewed against [2026-02-02_MULTIPLATFORM_COHERENCE_CHECKLIST.md](../standards/2026-02-02_MULTIPLATFORM_COHERENCE_CHECKLIST.md).  
**Sources checked:** `src/` (platform, api, core, ui, utils, path, crawler, search, usn, filters), `CMakeLists.txt`, `docs/platform/`, `scripts/`.

---

## 1. File and Directory Naming

| Check | Result | Notes |
|-------|--------|-------|
| Platform directories under `src/platform/` | ✅ Pass | `windows/`, `macos/`, `linux/` present and used consistently. |
| Triplet suffix (`_win` / `_mac` / `_linux`) | ✅ Pass | AppBootstrap, FontUtils, FileOperations, StringUtils, ThreadUtils, main, GeminiApiHttp all follow the pattern. |
| Single-OS-only files unsuffixed | ✅ Pass | e.g. `DirectXManager.cpp`, `MetalManager.mm`, `OpenGLManager.cpp`, `ShellContextUtils.*`, `PrivilegeUtils.*`, `DragDropUtils.*`. |
| AppBootstrap headers | ✅ Pass | Windows: `AppBootstrap_win.h`; Linux: `AppBootstrap_linux.h`; macOS: `AppBootstrap_mac.h`. Consistent suffix per platform. |

**Verdict:** Naming is coherent. Triplet and single-OS files are consistent.

---

## 2. Preprocessor Defines

| Check | Result | Notes |
|-------|--------|-------|
| Only `_WIN32`, `__APPLE__`, `__linux__`, `__unix__` for platform | ✅ Pass | No other platform macros found in `src/`. |
| `defined(...)` in `#if` / `#elif` | ✅ Pass | All `#elif` / `#if` for `__APPLE__`, `__linux__`, `__unix__` use `defined(...)`. (Logger.h was fixed: `#elif __APPLE__` → `#elif defined(__APPLE__)`.) |
| Unix-like vs Linux-only | ✅ Pass | `FileSystemUtils.h` uses `defined(__APPLE__) \|\| defined(__unix__)` for Unix-like; `defined(__linux__)` used where Linux-only. |

**Verdict:** Defines are consistent. No further fixes needed.

---

## 3. `#endif` Comments

| Check | Result | Notes |
|-------|--------|-------|
| Platform-dedicated files | ✅ Pass | e.g. `FontUtils_win.cpp`, `FontUtils_mac.mm`, `FontUtils_linux.cpp`, `ShellContextUtils.*`, `PrivilegeUtils.*`, `MetalManager.*`, `AppBootstrap_win.cpp` / `AppBootstrap_win.h`, `AppBootstrap_mac.mm` have `#endif // _WIN32` or `#endif  // _WIN32` or `#endif // __APPLE__` / `#endif // __linux__`. |
| Shared files (e.g. Application.cpp, Logger.h, PathUtils.cpp) | ⚠️ Mixed | Many platform blocks in shared code use bare `#endif` with no comment. ~165 `#endif` lines in `src/` have no trailing comment. |
| Style (one vs two spaces before `//`) | ⚠️ Mixed | Both `#endif  // _WIN32` and `#endif // _WIN32` appear. |

**Recommendation:** Optionally standardize: (1) add matching `#endif // _WIN32` (or `__APPLE__` / `__linux__`) for every platform block in shared files; (2) pick one style (e.g. two spaces before `//`) and apply. Low priority; improves readability and grep-ability.

**Verdict:** Platform-only files are good; shared files have room for improvement.

---

## 4. Include Paths and Shared Headers

| Check | Result | Notes |
|-------|--------|-------|
| Include path style | ✅ Pass | Includes use `"platform/windows/..."`, `"platform/linux/..."`, `"platform/FontUtilsCommon.h"`, and shared `"platform/FileOperations.h"` from `src/`. |
| FileOperations.h as common interface | ✅ Pass | `FileOperations.h` is a shared cross-platform interface under `"platform/FileOperations.h"`; all platforms include it via the common `platform/` path. |
| Location of FileOperations.h | ✅ Fixed | Header moved from `platform/windows/FileOperations.h` to `platform/FileOperations.h` and all includes updated. This addresses the prior naming/organization concern (see checklist and BUILD_ISSUES_ANTICIPATION.md). |

**Verdict:** Includes are correct and documented. No mandatory change.

---

## 5. CMake and Source Selection

| Check | Result | Notes |
|-------|--------|-------|
| Platform blocks | ✅ Pass | `if(WIN32)`, `elseif(APPLE)`, `elseif(UNIX AND NOT APPLE)` used; platform-specific sources listed in the correct blocks. |
| File names and paths | ✅ Pass | `*_win.cpp`, `*_mac.mm`, `*_linux.cpp` and paths under `src/platform/` match CMakeLists.txt. |
| Unsupported platform message | ✅ Pass | Line ~781: "Unsupported platform: only WIN32, APPLE, and Linux are configured in Phase 1." |

**Verdict:** CMake is coherent with the naming convention and source layout.

---

## 6. Platform-Only Code in Shared Files

| Check | Result | Notes |
|-------|--------|-------|
| No “unified” cross-platform block | ✅ Pass | Shared files use separate `#ifdef _WIN32` / `#elif defined(__APPLE__)` / `#elif defined(__linux__)` (or similar) branches; no single block that was generalized across platforms. |
| AGENTS document rule | ✅ Pass | No evidence of modifying platform blocks to make them cross-platform; platform-specific logic lives in the correct OS branch or in platform-specific files. |

**Verdict:** Shared files respect platform boundaries.

---

## 7. Documentation and Design

| Check | Result | Notes |
|-------|--------|-------|
| docs/platform/ | ✅ Pass | `docs/platform/linux/` (5 docs), `docs/platform/windows/` (2 docs), `docs/platform/macos/` (1 public entry-point doc). |
| macOS platform docs | ✅ Fixed | `docs/platform/macos/2026-02-02_MACOS_PLATFORM_NOTES.md` added; detailed build steps remain in `docs/guides/building/MACOS_BUILD_INSTRUCTIONS.md`. |
| Build guides | ✅ Pass | BUILDING_ON_LINUX, MACOS_BUILD_INSTRUCTIONS, PGO_SETUP, etc. exist and are referenced. |

**Verdict:** Documentation is now symmetric across platforms; each OS has a `docs/platform/<os>/` entry point plus shared build guides.

---

## 8. Other Aspects (From Checklist §3)

| Aspect | Result | Notes |
|--------|--------|-------|
| API parity (FileOperations, FontUtils, AppBootstrap) | ✅ Pass | FileOperations.h declares a single cross-platform API; implementations exist for win/mac/linux. FontUtils and AppBootstrap follow the same triplet pattern. GeminiApiHttp.h documents the three platform files. |
| Build scripts | ✅ Pass | e.g. `build_tests_macos.sh` name indicates macOS; scripts under `scripts/` are identifiable by name or README. |
| Third-party code | ✅ Pass | Platform logic in `src/` uses project defines; no check of `external/` needed for this review. |

---

## 9. Summary Checklist (Quick Scan) – Results

| Item | Status |
|------|--------|
| File names: triplet suffix + single-OS unsuffixed | ✅ |
| Defines: only _WIN32/__APPLE__/__linux__/__unix__; use defined() | ✅ |
| #endif: comment matches #ifdef (in platform files) | ✅ (shared files: optional improvement) |
| Includes: shared interface documented where used | ✅ |
| CMake: platform sources in correct blocks, naming correct | ✅ |
| Shared files: platform blocks not unified | ✅ |
| Docs: platform-specific content in docs/platform or guides | ✅ |

---

## 10. Actions and Recommendations

### Done / Already Correct

- **Logger.h:** `#elif __APPLE__` was changed to `#elif defined(__APPLE__)` (done in a previous step).
- File naming, defines, includes, CMake, and platform-boundaries in shared code are coherent.

### Optional (Low Priority)

1. **#endif comments in shared files:** Add matching `#endif // _WIN32` (or `__APPLE__` / `__linux__`) for platform blocks in files such as Application.cpp, Logger.h, PathUtils.cpp, and similar. Prefer one style (e.g. two spaces before `//`) project-wide.
2. **FileOperations.h location:** Consider moving the common interface to `platform/FileOperations.h` (or `platform/common/FileOperations.h`) and updating includes when doing a larger platform refactor.
3. **docs/platform/macos/:** Add a `docs/platform/macos/` folder with a short overview or link to MACOS_BUILD_INSTRUCTIONS for parity with linux/ and windows/.

### No Action Required

- Triplet and single-OS file naming.
- Use of `defined(...)` for __APPLE__ / __linux__ / __unix__.
- FileOperations.h “Common interface header” comments from non-Windows code.
- CMake platform blocks and source lists.
- Keeping platform-only code in the correct OS blocks or files.

---

## 11. References

- Checklist: [docs/standards/2026-02-02_MULTIPLATFORM_COHERENCE_CHECKLIST.md](../standards/2026-02-02_MULTIPLATFORM_COHERENCE_CHECKLIST.md)
- Build issues: [docs/guides/building/BUILD_ISSUES_ANTICIPATION.md](../guides/building/BUILD_ISSUES_ANTICIPATION.md)
- AGENTS document (project root) – platform-specific code blocks
