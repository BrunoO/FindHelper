# Multi-Platform Implementation Coherence Checklist

**Date:** 2026-02-02  
**Purpose:** Ensure the Windows / macOS / Linux implementation is coherent and consistent (file names, defines, includes, and related practices).  
**Scope:** Source layout, preprocessor usage, include paths, and documentation.

---

## 1. What to Check

### 1.1 File and Directory Naming

| Aspect | Convention | Current state |
|--------|------------|---------------|
| **Platform directories** | One folder per OS under `src/platform/` | ✅ `windows/`, `macos/`, `linux/` |
| **Platform-specific source suffix** | Same suffix style per platform for “triplet” files (same logical component, three implementations) | ✅ `_win`, `_mac`, `_linux` (e.g. `FontUtils_win.cpp`, `FontUtils_mac.mm`, `FontUtils_linux.cpp`) |
| **Single-platform-only files** | No suffix required when folder already implies platform | ✅ e.g. `DirectXManager.cpp` (Windows), `MetalManager.mm` (macOS), `OpenGLManager.cpp` (Linux) |
| **AppBootstrap headers** | Same suffix per platform for triplet | ✅ Windows: `AppBootstrap_win.h`; Linux: `AppBootstrap_linux.h`; macOS: `AppBootstrap_mac.h`. |

**Action:** When adding new platform-specific “triplet” implementations (e.g. `Foo_win.cpp`, `Foo_mac.mm`, `Foo_linux.cpp`), use the same suffix pattern (`_win` / `_mac` / `_linux`) and place them under `src/platform/<platform>/`.

---

### 1.2 Preprocessor Defines

| Define | Meaning | Use |
|--------|---------|-----|
| `_WIN32` | Windows (MSVC and MinGW) | ✅ Use for Windows-only code. |
| `__APPLE__` | macOS (and iOS when applicable) | ✅ Use for macOS-only code. |
| `__linux__` | Linux | ✅ Use for Linux-only code. |
| `__unix__` | Unix-like (often both macOS and Linux) | ✅ Used in `FileSystemUtils.h` for “non-Windows” paths; keep when intent is “Unix-like”, not “Linux only”. |

**Rules:**

- Prefer **one** of `_WIN32`, `__APPLE__`, or `__linux__` per branch when the code is OS-specific.
- For “Unix-like” (macOS + Linux), use `defined(__APPLE__) || defined(__unix__)` or `defined(__APPLE__) || defined(__linux__)` as appropriate; avoid mixing with `__linux__` unless the block is Linux-only.
- Always use **`defined(...)`** in `#if` / `#elif` (e.g. `#elif defined(__APPLE__)`), not bare `#elif __APPLE__`, for consistency and portability.

**Status:** Logger.h was updated to use `#elif defined(__APPLE__)`; no remaining inconsistency.

---

### 1.3 `#endif` Comments

- Use a comment after `#endif` that matches the opening `#ifdef` / `#if` (e.g. `#endif  // _WIN32`).
- Style: either `#endif  // _WIN32` (two spaces before `//`) or `#endif // _WIN32` (one space); pick one and apply consistently. Current codebase uses both; standardizing is optional but improves readability.

---

### 1.4 Include Paths and Shared Headers

| Topic | Recommendation | Current state |
|-------|----------------|---------------|
| **Include path style** | Use `platform/<platform>/...` or `platform/...` from `src/` as the project’s include root. | ✅ Includes use e.g. `"platform/windows/..."`, `"platform/FontUtilsCommon.h"`, `"platform/FileOperations.h"`. |
| **Shared interface in platform folder** | Prefer a path that does not suggest a single OS. | ✅ `FileOperations.h` is the **common** interface and now lives under `platform/FileOperations.h`; all platforms include it via the shared `platform/` path. See [BUILD_ISSUES_ANTICIPATION.md](guides/building/BUILD_ISSUES_ANTICIPATION.md) §3. |

**Status:** The shared `FileOperations.h` header has been moved to `platform/FileOperations.h` and all includes have been updated accordingly.

---

### 1.5 CMake and Source Selection

- Platform-specific sources are selected in `CMakeLists.txt` with `if(WIN32)`, `if(APPLE)`, `if(UNIX AND NOT APPLE)` (or equivalent).
- File names in CMake must match the naming convention (`*_win.cpp`, `*_mac.mm`, `*_linux.cpp`) and the actual file paths under `src/platform/`.

**Check:** When adding a new platform-specific file, add it to the correct `if(WIN32)` / `if(APPLE)` / Linux block and use the same suffix as existing triplet files.

---

### 1.6 Platform-Only Code in Shared Files

- In **shared** (non-platform) files, keep `#ifdef _WIN32` / `__APPLE__` / `__linux__` blocks **unchanged** when adding or changing behavior; do not “generalize” Windows/macOS/Linux logic inside those blocks to make one block “cross-platform.” Add or modify code in the appropriate OS block or in a new platform-specific file.
- Per AGENTS document: do not modify code inside `#ifdef _WIN32` (or other platform blocks) to make it cross-platform; extend via new abstractions or new platform files instead.

---

### 1.7 Documentation and Design

- **docs/platform/** contains OS-specific notes (e.g. `platform/linux/`, `platform/windows/`).
- When adding platform-specific behavior or build steps, update the relevant guide (e.g. BUILDING_ON_LINUX, MACOS_BUILD_INSTRUCTIONS) and, if useful, a doc under `docs/platform/<platform>/`.

---

## 2. Summary Checklist (Quick Scan)

Use this for a quick coherence pass:

- [ ] **File names:** Platform “triplet” sources use `_win` / `_mac` / `_linux`; single-OS-only files in `platform/<os>/` may be unsuffixed.
- [ ] **Defines:** Use only `_WIN32`, `__APPLE__`, `__linux__` (and `__unix__` where “Unix-like” is intended); use `defined(...)` in `#if`/`#elif`.
- [ ] **#endif:** Comment matches the opening `#ifdef`/`#if`.
- [ ] **Includes:** Shared interfaces have a clear path; if under `platform/windows/`, document “common interface” where included from other platforms.
- [ ] **CMake:** New platform sources are listed under the correct platform block and follow the same naming.
- [ ] **Shared files:** Platform blocks are not “unified” into one cross-platform block; new behavior goes in the right OS block or new platform file.
- [ ] **Docs:** Platform-specific behavior or build steps are reflected in `docs/platform/` or build guides.

---

## 3. Other Aspects to Periodically Review

- **API parity:** For abstractions used on all platforms (e.g. FileOperations, FontUtils, AppBootstrap), ensure each platform implements the same contract (same functions, same semantics where possible).
- **Tests:** Platform-specific code paths that can be tested (e.g. via mocks or compile-time exclusion) are covered where it makes sense.
- **Build scripts:** Scripts under `scripts/` that are platform-specific (e.g. macOS build/tests) are named or documented so it’s clear which OS they target.
- **Third-party code:** External code (e.g. under `external/`) may use different defines or naming; avoid leaking their conventions into project code; keep our conventions in `src/` and project headers.

---

## 4. References

- **AGENTS document** – Platform-specific code blocks, Windows rules, C++ include order.
- **docs/standards/CXX17_NAMING_CONVENTIONS.md** – General naming (PascalCase, snake_case, etc.).
- **docs/platform/** – Platform-specific notes and build details.
