# 2026-03-01 Linux platform risks and testing focus

**Purpose:** Since Linux is the least tested platform, this document summarizes possible problem areas and what has been verified or fixed. Use it to prioritize manual testing and CI.

---

## Already addressed (this session)

- **Segfault on exit:** Double `ImGui_ImplOpenGL3_Shutdown()` in Linux cleanup → fixed (single shutdown via `CleanupImGuiAndGlfw`).
- **Crawling very slow / single-thread:** `hardware_concurrency()` returning 0 (e.g. cgroups/Docker) → fixed (fallback to 2 threads + warning log).
- **Path-dedup hot path:** Collision loop did extra allocs → optimized (zero-copy view + `PathViewEqualsNormalized`).

---

## Verified low risk

- **Static objects in AppBootstrap_linux:** `static AppSettings` and `static OpenGLManager`. Destructors run after `Cleanup()`. `OpenGLManager::Cleanup()` is idempotent when `initialized_` is false and ImGui context is already destroyed. No double-free.
- **Application destructor vs Cleanup:** `Cleanup(bootstrap)` runs after `Application::Run()` returns and before `Application` is destroyed. No use of renderer/ImGui after teardown.
- **FileOperations_linux `fork`/`execlp`:** Child only calls `execlp` then `_exit`. No mutex or non-async-signal-safe code in child. Safe.
- **SystemIdleDetector (Linux):** Uses X11 + XScreenSaver; on Wayland `XOpenDisplay(nullptr)` fails and returns -1.0 (no idle). RAII for Display and XScreenSaverInfo. No crash.
- **PathUtils `GetUserHomePath`:** Uses `getenv("HOME")`; auto-crawl uses this, so correct home on Linux. `GetDefaultUserRootPath()` returns `"/Users/"` on non-Windows (macOS-style); used as fallback for desktop/downloads when home-relative path is not used. Minor inconsistency on Linux (often `/home`), but not a crash.
- **PathUtils `GetExecutableDirectory` (Linux):** Uses `readlink("/proc/self/exe")`. If `/proc` is not mounted, fails and returns `{}`. Handled.
- **ValidatePath:** In `FileOperations.h` (shared); Linux impl uses `internal::ValidatePath` before all external commands. Path validation is in place.

---

## Worth testing / edge cases

1. **Wayland-only sessions**
   - **Idle detection:** Returns -1.0 (feature unavailable, no crash).
   - **Clipboard:** GLFW handles Wayland; no known issue.
   - **Recommendation:** Manually test: copy path, open file, open parent folder, close app.

2. **Font loading (FontUtils_linux)**
   - **popen("fc-list ...")** runs on the main thread. If `fc-list` or Fontconfig is slow/hangs, UI can block.
   - **ImGui_ImplOpenGL3_DestroyDeviceObjects()** in `ApplyFontSettings` is only used at init or when user changes font; never after shutdown. Safe.
   - **Recommendation:** Change font in Settings on Linux; confirm no hang. If system has no or broken Fontconfig, app falls back to default ImGui font (documented in BUILDING_ON_LINUX).

3. **Missing desktop utilities**
   - If `xdg-open`, `gio`, `kde-open`, `exo-open` are missing, open file / open parent folder will log errors and do nothing. Not a crash.
   - **Recommendation:** Run on a minimal install (e.g. no `xdg-utils`) and confirm graceful degradation.

4. **Headless / Xvfb**
   - BUILDING_ON_LINUX mentions `Xvfb :99` for headless. OpenGL context creation can fail without a display.
   - **Recommendation:** If you run tests under Xvfb, ensure the app is started with a valid DISPLAY and that GL context creation succeeds (or is skipped in tests).

5. **Logger during shutdown**
   - Some code (e.g. LazyAttributeLoader destructor) catches exceptions and avoids logging to prevent use of a destroyed Logger during static destruction. No change needed; just be aware that post-main logging can be unsafe.

6. **Trash (Delete to recycle)**
   - Uses `gio trash` first; fallback is manual FreeDesktop Trash in `~/.local/share/Trash`. Uses `getenv("XDG_DATA_HOME")` / `getenv("HOME")`. If both are unset, trash path is wrong and operation can fail. Edge case; not a crash.

---

## Recommendations

- **Manual smoke test on Linux:** Start app → load/crawl a folder → search → open file → open parent folder → copy path → change font in Settings → close app. Repeat on X11 and, if possible, Wayland.
- **Optional:** Add a small “Linux smoke” checklist (e.g. in BUILDING_ON_LINUX or a separate doc) listing the above scenarios.
- **CI:** If a Linux runner exists (or is added), run the same smoke steps after build; consider running under Xvfb with a dummy display if needed.

---

## References

- `src/platform/linux/AppBootstrap_linux.cpp` – cleanup order, static objects
- `src/platform/linux/OpenGLManager.cpp` – Cleanup/ShutdownImGui idempotence
- `src/platform/linux/FileOperations_linux.cpp` – fork/exec, getenv, ValidatePath
- `src/platform/linux/FontUtils_linux.cpp` – popen fc-list, DestroyDeviceObjects
- `src/utils/SystemIdleDetector.cpp` – X11/Wayland
- `docs/guides/building/BUILDING_ON_LINUX.md` – deps, troubleshooting, Wayland/OpenGL/AVX2
- `internal-docs/analysis/2026-03-01_LINUX_SEGFAULT_ON_CLOSE_INVESTIGATION.md` – segfault fix
- `internal-docs/analysis/2026-03-01_LINUX_CRAWLING_SLOW_REGESSION_INVESTIGATION.md` – crawl thread + path-dedup
