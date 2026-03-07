# External Libraries Upgrade Analysis

**Date:** 2026-03-07

This document summarizes the current versions of external dependencies and whether upgrades to their latest versions are recommended.

## Summary Table

| Library | Current Version | Latest Version | Recommendation |
|---------|-----------------|---------------|----------------|
| **Dear ImGui** | v1.92.5-docking (docking branch) | v1.92.6 (master, 2026-02-17) | Optional: update docking branch; v1.92.6 has breaking changes |
| **doctest** | v2.4.12 | v2.4.12 (2025-04-28) | ✅ **Up to date** |
| **nlohmann/json** | v3.11.2 / v3.11.3 (post-tag commits) | v3.12.0 (2025-04-11) | ⬆️ **Upgrade recommended** |
| **FreeType** | 2.14.1 (VER-2-14-1) | 2.14.x | ✅ **Up to date** |
| **GLFW** | 3.4 (Windows vendored; macOS/Linux FetchContent) | 3.4 | ✅ **Up to date** |
| **ImGui Test Engine** | v1.92.6-1 (tag) | No formal releases | ✅ Track ImGui; current is aligned |

---

## Details

### Dear ImGui (`external/imgui`)

- **Current:** Submodule on **docking** branch, describe: `v1.92.5-docking-175-g3fb22b836`
- **Latest master:** v1.92.6 (2026-02-17)
- **Latest docking:** ~39 commits ahead of current; includes merges from master up through v1.92.6 and more (docking is at 1.92.7 WIP).
- **Notes:**
  - The project uses the **docking** branch (not master). Docking is maintained separately; there is no `v1.92.6-docking` tag.
  - v1.92.6 introduces **breaking changes** (see below). Those are already in **docking** when you update to the latest docking commit.
  - **Benefits of upgrading the docking branch** (updating to latest `origin/docking`):
    - **Viewports:** Fix where the implicit "Debug" window could be targeted for mouse input while hidden (#9254).
    - **TreeNode:** `TreeNodeGetOpen()` in public API; duplicate-declaration fix; tree clipping / Property Editor demo improvements.
    - **InputText:** Shift+Enter in multi-line editor always adds a new line (#9239); Shift+Wheel horizontal scroll fix (#9249).
    - **Style:** Border sizes scaled and rounded by `ScaleAllSizes()`; 1.0f limit lifted in Style Editor.
    - **Clipper:** `UserIndex` helper; `DisplayStart`/`DisplayEnd` cleared when `Step()` returns false.
    - **Build / backends:** Warning fixes (e.g. `-Wnontrivial-memcall`), ARM64/SSE fix, SDL/WebGPU/Emscripten fixes.
    - You stay on docking and get all of the above without switching to master.
  - **Breaking changes** (from v1.92.6, already in latest docking):
    - **Fonts:** `AddFontDefault()` behavior change; prefer `AddFontDefaultBitmap()` or `AddFontDefaultVector()` if you rely on the old default. Fix for `ImFontConfig::FontDataOwnedByAtlas = false` (font data must stay valid until atlas shutdown or `RemoveFont()`).
    - **Popups:** Default parameter for `BeginPopupContextItem`, `BeginPopupContextWindow`, `BeginPopupContextVoid`, `OpenPopupOnItemClick` changed from `popup_flags = 1` to `= 0`; use `ImGuiPopupFlags_MouseButtonLeft` explicitly if you relied on left-button popups.
  - This codebase uses `FontDataOwnedByAtlas = false` and `AddFontDefault()` in several places; after upgrading, verify font loading and fallbacks (and that font data outlives the atlas as required).
- **Recommendation:** Either update the **docking** submodule to the latest docking commit (recommended for bug fixes and viewport fix) and then verify fonts/popups, or stay on current commit. Do not switch to master unless you explicitly want to leave docking.

### doctest (`external/doctest`)

- **Current:** v2.4.12 (commit 1da23a3)
- **Latest:** v2.4.12 (2025-04-28)
- **Recommendation:** No action; already on latest release.

### nlohmann/json (`external/nlohmann_json`)

- **Current:** v3.11.2 / v3.11.3 area (post-tag commits, e.g. 8c7a7d4)
- **Latest:** v3.12.0 (2025-04-11)
- **Notes:** v3.12.0 is backward-compatible and includes bug fixes (e.g. extended diagnostics assertion, iterator comparison, `get_ptr` for unsigned integers, empty tuple serialization, number parsing with `EINTR`, `nullptr` to `parse()`). Also adds conversion macro improvements and CMake 4.0 support.
- **Recommendation:** **Upgrade to v3.12.0** (e.g. point submodule to tag `v3.12.0`). Run tests after updating.

### FreeType (`external/freetype`)

- **Current:** 2.14.1 (branch VER-2-14-1; `FREETYPE_MAJOR=2`, `FREETYPE_MINOR=14`, `FREETYPE_PATCH=1`)
- **Latest:** 2.14.x series (2.14.0 announced 2025-09; 2.14.1 is patch release)
- **Recommendation:** No action; 2.14.1 is current.

### GLFW

- **Windows:** Vendored in `external/glfw` — header reports **GLFW 3.4**.
- **macOS/Linux:** Fetched via FetchContent when system GLFW 3.3+ is not found; `GIT_TAG 3.4` in `CMakeLists.txt`.
- **Latest:** 3.4 (2024-02-23)
- **Recommendation:** No action; already on latest.

### ImGui Test Engine (`external/imgui_test_engine`)

- **Current:** v1.92.6-1 (tag c043664)
- **Notes:** No formal release packages; repo uses tags. Current tag aligns with ImGui 1.92.6.
- **Recommendation:** When upgrading ImGui (docking or master), align Test Engine submodule with a compatible tag if needed.

---

## Recommended Actions

1. **nlohmann/json:** Upgrade submodule to `v3.12.0` and run the test suite.
2. **Dear ImGui:** Either update the docking submodule to latest docking commit for incremental fixes, or schedule a change to move to v1.92.6 (master) and handle breaking changes (fonts and popup defaults).
3. **Others:** No upgrade needed at this time.

---

## How Versions Were Checked

- **Submodules:** `git submodule status` and `git describe --tags` in each `external/*` directory.
- **GLFW (Windows):** `external/glfw/include/GLFW/glfw3.h` version comment.
- **FreeType:** `FREETYPE_MAJOR/MINOR/PATCH` in `external/freetype/include/freetype/freetype.h`.
- **Latest releases:** Official GitHub/GitLab release pages (ImGui, doctest, nlohmann/json, FreeType, GLFW, ImGui Test Engine).
