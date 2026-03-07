# Help Window Font Scaling Review

**Date:** 2026-03-07  
**Scope:** `src/ui/HelpWindow.cpp` — font scaling issue when resizing the Help window; no code changes, analysis and ideas only.

**Important:** The issue is **single-monitor** (not multi-monitor): font scaling is wrong or inconsistent when the user **resizes** the Help window (e.g. drags its edges) on the same display.

---

## Previous attempt (did not fix)

**Commit 2266ac47** (2026-03-05): *"Help window: add What's new entry, fix font scale on resize"*

- **Change:** At the start of Help content (inside `if (window_guard.ShowContent())`), added `ImGui::SetWindowFontScale(1.0F)` every frame.
- **Comment added:** *"Keep font scale at 1.0 regardless of viewport DPI or accidental scroll-to-zoom when the window has its own viewport (resizing can change effective scale otherwise)."*
- **Result:** Did **not** fix the issue ("to no avail"). The `SetWindowFontScale(1.0F)` call is no longer present in `HelpWindow.cpp` (reverted or removed in a later change).

**Implication:** Resetting per-window `FontWindowScale` to 1.0 each frame does not stop the font from changing when the window is resized on the same monitor. So the cause is likely **not** per-window zoom (Ctrl+scroll) or an inherited `FontWindowScale`. The scaling or visual change on resize must come from elsewhere (e.g. viewport `DpiScale` or style being updated when the window is resized, or a layout/rounding effect that looks like font scaling).

---

## Current State

- **HelpWindow.cpp** has no font-related code. It sets position/size (`SetNextWindowPos`, `SetNextWindowSize`), uses `ImGuiWindowClass` with `ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge` so the Help window gets its own OS window (and viewport), then renders content (CollapsingHeader, BulletText, etc.).
- **App-wide scaling:** The app uses `io.FontGlobalScale = settings.fontScale` (Windows: `FontUtils_win.cpp`, macOS: `FontUtils_mac.mm`, Linux: `FontUtils_linux.cpp`). Font atlas is built once (or when settings change) with `settings.fontSize`; there is no per-viewport or per-window font scale in app code.
- **ImGui:** `ImGuiConfigFlags_ViewportsEnable` is set (`AppBootstrapCommon.h`). ImGui 1.92 has `style.FontScaleMain`, `style.FontScaleDpi`, and `io.ConfigDpiScaleFonts`; this project does **not** set `io.ConfigDpiScaleFonts` and still uses the legacy `io.FontGlobalScale`.
- **Per-window scale:** ImGui has a per-window `FontWindowScale` (and `SetWindowFontScale(scale)`). Ctrl+MouseWheel zoom is gated by `io.FontAllowUserScaling` (default false), so it’s likely off unless the app enables it.

---

## Why the Settings window does not have this issue

Comparison of **Help** vs **Settings** (and **Metrics**):

| Aspect | Help window | Settings window | Metrics window |
|--------|-------------|-----------------|----------------|
| **SetNextWindowClass** | Yes: `ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge` | No | No |
| **Own viewport / OS window** | Yes — gets its own platform window (e.g. appears in ALT+TAB) | No — merges into main viewport when it fits | No — merges into main viewport when it fits |
| **Resize behavior** | User resizes a **separate OS window**; that viewport's size (and possibly `DpiScale`) changes | User resizes an ImGui window **inside** the main viewport; no separate platform window | Same as Settings |

**Conclusion:** The font scaling issue appears only on the **Help** window because it is the **only** one that uses `ImGuiViewportFlags_NoAutoMerge`, so it is the only one that gets its **own viewport** (and thus its own OS window). When that OS window is resized, the platform backend may update that viewport's `DpiScale` or the scale used for that viewport, which then affects font/layout. The **Settings** (and **Metrics**) windows do **not** set `SetNextWindowClass` / `NoAutoMerge`; they stay in the **main viewport**. Resizing them only changes the ImGui window rect inside the same viewport, so the scale (main viewport's `DpiScale` and `io.FontGlobalScale`) stays the same — hence no font scaling issue there.

**Implication for a fix:** Either (1) make the Help window use the same scale as the main viewport when it has its own viewport (e.g. force the Help viewport's scale to match the main viewport, or ignore its viewport's `DpiScale` for font), or (2) remove `NoAutoMerge` from Help so it behaves like Settings (but then Help would no longer get its own OS window / ALT+TAB entry), or (3) fix the backend so the Help viewport's scale does not change on resize on a single monitor.

**Implemented (2026-03-07):** Option (2) — Removed `SetNextWindowClass` / `ImGuiViewportFlags_NoAutoMerge` from `HelpWindow.cpp`. The Help window now stays in the main viewport (like Settings and Metrics). Resizing it only changes the ImGui window rect inside the main viewport, so font scale no longer changes. Trade-off: the Help window no longer gets its own OS window and no longer appears as a separate entry in ALT+TAB (it remains a floating ImGui window inside the main app window).


## What “font scaling issue when resizing” might mean

1. **Resize = drag window edges**  
   Making the Help window larger or smaller doesn’t change font size (ImGui doesn’t scale font with window size). So “scaling” might mean: text should reflow or feel proportional when the window is resized. That’s mostly layout (child with scroll, word wrap) rather than font scale.

2. **Resize = move to another monitor (different DPI)**  
   When the Help window is on a different monitor, its viewport has a different `DpiScale`. Because the app only uses `io.FontGlobalScale` (one value for all viewports), the Help window doesn’t pick up the viewport DPI. So font can look too big or too small on that monitor.

3. **Accidental Ctrl+scroll zoom**  
   If `FontAllowUserScaling` were ever enabled, Ctrl+scroll would change that window’s `FontWindowScale` and it would “stick.” Then “resizing” or moving the window wouldn’t reset it. Locking scale in Help would mean forcing `SetWindowFontScale(1.0F)` every frame inside that window.

4. **First-use size vs. later resize**  
   Size is set with `ImGuiCond_FirstUseEver`, so after the first frame the user can resize. If the initial size is wrong for a monitor’s DPI, the only way to get correct scale would be per-viewport DPI (or the user changing Settings → UI scale).

---

## Ideas (no code yet)

### 1. Lock Help window to global scale only (per-window scale = 1.0) — **already tried (2266ac47), did not fix**

- **What:** Inside `HelpWindow::Render`, after `ImGui::Begin` (i.e. inside `window_guard.ShowContent()`), call `ImGui::SetWindowFontScale(1.0F)` every frame.
- **Effect:** The Help window’s effective font scale becomes `io.FontGlobalScale * 1.0`. It won’t keep any per-window zoom (e.g. from Ctrl+scroll if that were enabled) and will always match the rest of the app’s scale.
- **When it helps:** If the issue is accidental per-window zoom or inherited scale from a parent, this normalizes the Help window. It does **not** fix different DPI on another monitor.

### 2. Enable ImGui’s DPI font scaling (`io.ConfigDpiScaleFonts`)

- **What:** At init, set `io.ConfigDpiScaleFonts = true`. Set `style.FontScaleMain` from `settings.fontScale` (user preference) and stop setting `io.FontGlobalScale`. Let ImGui set `style.FontScaleDpi = viewport->DpiScale` in `SetCurrentViewport` so each viewport (main window, Help, Metrics, etc.) uses the DPI of the monitor it’s on.
- **Effect:** When the Help window is on a different monitor, its viewport’s `DpiScale` is used and fonts scale with that monitor’s DPI. Resizing or moving the window to another monitor would then get the right scale as long as the backend sets `viewport->DpiScale` (e.g. Win32 on `WM_DPICHANGED`).
- **Caveats:** Requires verifying that the backend (Win32, GLFW, etc.) sets `viewport->DpiScale` when a window is created or moved. ImGui examples use this with a single main scale; our app has a user-facing “UI scale” that should map to `FontScaleMain`. This is a global change (all windows), not Help-only.

### 3. Map app “UI scale” to ImGui’s scale model

- **What:** Use `style.FontScaleMain` for the user’s “UI scale” (from Settings) and, if adopting idea 2, rely on `FontScaleDpi` for per-viewport DPI. Ensure font atlas build uses a base size and let `FontScaleMain * FontScaleDpi` drive the final size (ImGui 1.92 does this when `ConfigDpiScaleFonts` is true).
- **Effect:** One consistent scaling model: user scale × monitor DPI, with Help (and other secondary windows) following the viewport they’re in.

### 4. Make Help content reflow on resize (layout, not font scale)

- **What:** Keep a single font scale. Put the whole Help content in a `BeginChild("HelpContent", ImVec2(0, 0), ImGuiChildFlags_Border)` (or similar) so that when the user resizes the Help window, the content area grows/shrinks and scrolls instead of the font changing.
- **Effect:** Resizing the window only changes how much content is visible and how much scroll is needed; font size stays constant. This addresses “window got bigger/smaller, I want layout to adapt” rather than “font should scale with window size.”

### 5. Disable Ctrl+scroll zoom for the Help window only

- **What:** If the app ever enables `io.FontAllowUserScaling`, the Help window could still call `SetWindowFontScale(1.0F)` every frame (same as idea 1) so that any zoom applied to it is effectively reset each frame. That way Help always shows at “normal” scale even if other windows allow zoom.
- **Effect:** Only relevant if user scaling is on; otherwise no change.

### 6. Ensure backend sets viewport DPI when Help gets its own viewport

(Other possibilities **not** in scope for this issue: multi-monitor DPI, accidental Ctrl+scroll zoom, or first-use size vs. later resize on another monitor.)

**For the single-monitor resize case (current issue),** because SetWindowFontScale(1.0F) did not fix it, the cause is likely: viewport `DpiScale` being updated by the backend on resize; backend reporting a different scale when the window size changes; layout/rounding using `g.CurrentDpiScale` so that when the viewport's scale changes, layout shifts in a way that looks like font scaling; or the "scaling" is perceived (content/scroll/clipping) rather than actual font size, in which case the fix is layout (idea 4).

- **What:** With `NoAutoMerge`, the Help window gets its own platform window. When that window is created or moved to another monitor, the backend should set `viewport->DpiScale` (and possibly trigger a font atlas rebuild or use `ConfigDpiScaleFonts`). Check Win32 `WM_DPICHANGED` and GLFW/macOS equivalent and whether they update the ImGui viewport’s `DpiScale`.
- **Effect:** Without this, per-viewport font scaling (idea 2) won’t work for the Help window when it’s on a different monitor.

---

## Recommended order to try

1. **Single-monitor resize (current issue):** Same DPI, window resized → font looks wrong. Focus on: instrumenting viewport `DpiScale` / backend (ideas below), or layout reflow (idea 4).
2. **SetWindowFontScale(1.0F) already tried** in 2266ac47 and did not fix; skip repeating it.
3. **Multi-monitor DPI** is out of scope for this issue; ideas 2, 3, 6 apply only if the problem expands to multi-monitor later.
4. **If the issue is “text should reflow when I resize”:** Prefer a scrollable child and fixed font (idea 4) rather than scaling font with window size.

---

## Next directions (single-monitor resize)

Because **SetWindowFontScale(1.0F)** did not fix the issue (commit 2266ac47), the cause is likely one of: viewport `DpiScale` being updated by the backend on resize; the backend reporting a different scale when the window size changes; layout/rounding using `g.CurrentDpiScale`; or the "scaling" being perceived (content/scroll/clipping) rather than actual font size change. **Suggested next steps:** (1) **Instrument:** Log `viewport->DpiScale` and `g.Style.FontScaleDpi` (or `ImGui::GetWindowDpiScale()`) when rendering the Help window to see if they change on resize on a single monitor. (2) **Backend:** Check when the platform layer updates `viewport->DpiScale` (e.g. Win32 `Platform_GetWindowDpiScale`, GLFW window content scale). (3) **Layout:** Try idea 4 (scrollable child, reflow) if the symptom is reflow/clipping rather than font size change.


## References

- **Commit 2266ac47** — Previous attempt: `SetWindowFontScale(1.0F)` in Help window; did not fix single-monitor resize issue.
- `src/ui/HelpWindow.cpp` — Help window layout and content; no font code.
- `src/platform/windows/FontUtils_win.cpp` — `ApplyFontSettings` sets `io.FontGlobalScale` and rebuilds fonts.
- `external/imgui/imgui.cpp` — `SetCurrentViewport` (≈16505): `if (g.IO.ConfigDpiScaleFonts) g.Style.FontScaleDpi = g.CurrentDpiScale`; Ctrl+scroll zoom (≈10898–10915) uses `FontWindowScale`.
- `external/imgui/imgui.h` — `io.ConfigDpiScaleFonts`, `style.FontScaleMain`, `FontScaleDpi`; `SetWindowFontScale()`.
- Internal: `internal-docs/analysis/2026-02-19_HELP_WINDOW_AS_NORMAL_WINDOW_PLAN.md` — Help as normal window with own viewport.
