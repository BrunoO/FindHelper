# 2026-02-03 Linux --crawl-folder: Show UI First, Then Crawl (Same as macOS)

## Problem

On Linux, when starting the application with `--crawl-folder=<path>`, the UI remains **blank** while crawling and indexing run. On macOS, the window is shown, the UI is visible, and the user sees progress (e.g. "Indexing..." in the status bar) during the crawl. We want the same behavior on Linux: **display the UI first, then crawl in a responsive way so the user can see progress**.

## Current Behavior (Shared Code Path)

The flow is **identical** on macOS and Linux (shared `main_common.h` and `FolderCrawlerIndexBuilder`):

1. **RunApplication**  
   - Parse args, create `FileIndex`, `IndexBuildState`.  
   - **BootstrapTraits::Initialize** (platform-specific): create window, init OpenGL/Metal, ImGui, **glfwShowWindow(window)**, return.

2. **ExecuteApplicationMain**  
   - Build `IndexBuilderConfig` from `--crawl-folder`.  
   - Create `FolderCrawlerIndexBuilder`, call **index_builder->Start(index_build_state)** (launches a **background thread**).  
   - Create **Application**(…), then **app.Run()**.

3. **Application::Run()**  
   - Main loop: `HandleMinimizedWindow` / `HandleUnfocusedWindow` / `HandleFocusedWindow` → **ProcessFrame()** (render UI, swap buffers).

So the crawl is already asynchronous (worker thread). The window is shown in bootstrap before `ExecuteApplicationMain` runs. In theory the first frame should be drawn in the first loop iteration. The observed “blank UI on Linux” during crawl suggests one or more of:

- **Event/display ordering on Linux**: Under X11/Wayland, the first frame may not be presented until the main loop has run and processed events (e.g. first `glfwPollEvents()` / swap). If anything delays the first iteration, the user sees a blank window while the crawl runs.
- **Main-thread contention**: The crawler thread might (e.g. via filesystem or memory pressure) affect scheduling so the main thread gets less CPU or is delayed.
- **Driver/OpenGL**: First present or swap might block or be delayed on some Linux setups.

No platform-specific blocking or synchronous crawl was found in the Linux bootstrap; the only difference is environment (X11/Wayland, OpenGL, drivers).

## Goal

- **Linux**: Same as macOS — window and full UI visible **before** crawl work is noticeable; crawl then runs in the background with **visible progress** (e.g. status bar “Indexing …”, entry count).
- **All platforms**: Prefer a single, robust strategy so behavior is consistent and future regressions are less likely.

## Plan

### 1. Defer index builder start until after the first frame (recommended)

**Idea:** Do **not** start the index builder in `ExecuteApplicationMain`. Instead, start it from **Application::Run()** after the **first frame** has been rendered. That guarantees:

- Window is shown (bootstrap).
- At least one full frame is drawn (window content visible).
- Then crawl starts; subsequent frames show progress.

**Steps:**

1. **main_common.h – ExecuteApplicationMain**  
   - When an `index_builder` is created for folder crawl (macOS/Linux path, and optionally Windows folder-crawl path):  
     - Still create the builder and set `index_build_state` (e.g. `source_description`, `Reset()`).  
     - **Do not** call `index_builder->Start(index_build_state)`.  
     - Pass a **“defer start”** flag into `Application` (e.g. `start_index_build_after_first_frame = true`).

2. **Application (constructor)**  
   - Add a constructor parameter, e.g. `bool start_index_build_after_first_frame`.  
   - Store it in a member (e.g. `deferred_index_build_start_`).  
   - No change to existing ownership of `index_builder_` or `index_build_state_`.

3. **Application::Run()**  
   - Before the main `while (glfwWindowShouldClose(...))` loop, **or** at the end of the first loop iteration:  
     - If `deferred_index_build_start_` is true and `index_builder_` is non-null:  
       - Call `index_builder_->Start(index_build_state_)`.  
       - Set `deferred_index_build_start_ = false`.  
   - Prefer doing this **after** the first `ProcessFrame()` so the first paint is definitely done (e.g. after first iteration, or after an explicit first frame render before the loop).

4. **Scope**  
   - Apply “defer start” for the **folder-crawler** path on **non-Windows** (macOS + Linux) at minimum.  
   - Optionally apply to **all** folder-crawler starts (including Windows when not using USN) for one consistent behavior everywhere.

**Result:** On Linux, the window appears, the first frame is drawn (UI visible), then the crawl starts and progress is visible in the status bar. Same as desired macOS behavior, without platform-specific UI logic.

### 2. Ensure progress is visible and UI stays responsive

- **Status bar:** Already shows `index_build_in_progress` and `index_build_status_text` (e.g. “Indexing Folder: …”). No change required for basic progress.
- **Optional improvement:** In `FolderCrawler` / `FolderCrawlerIndexBuilder`, ensure `entries_processed` (and related atomics) are updated frequently enough (e.g. every N entries or every N ms) so the status bar and any “N entries” display feel responsive. If the crawler currently updates only at the end, consider periodic updates.
- **Optional:** On Linux, if the main loop uses `glfwWaitEventsTimeout` when “idle”, ensure that during index building we do **not** consider the app idle (so we keep using `glfwPollEvents()` and full frame rate). This is already the case: `HandleFocusedWindow` uses “idle” only when `!has_background_work` and `IsIndexBuilding()` is part of `has_background_work`, so no change needed.

### 3. Optional: Force one frame before starting the loop

If desired for extra safety (e.g. on problematic Linux drivers):

- In **Application::Run()**, before the main loop, call **once**: `glfwPollEvents()` then `ProcessFrame()`, then start the deferred index build (if any).  
- This makes “first frame” explicit and avoids relying on the first loop iteration. Can be combined with step 1.

### 4. Testing and validation

- **Linux:** Start with `--crawl-folder=/home` (or another large directory). Confirm: window appears, UI (menu, status bar, empty state) is visible **before** “Indexing…” appears; then “Indexing …” and entry count update during crawl; no prolonged blank window.
- **macOS:** Same command; confirm behavior unchanged (window visible, then crawl, progress visible).
- **Windows:** If defer is applied to folder-crawler path, test with a folder path (no USN); confirm same UX.

## Implementation Notes

- **Backward compatibility:** No API change for callers; only the **when** of `index_builder->Start()` changes (after first frame instead of before `Application` construction).
- **Auto-crawl:** Unaffected; auto-crawl is triggered later from `Application` (e.g. `TriggerAutoCrawl()`). Defer logic only applies to the builder created in `ExecuteApplicationMain` for `--crawl-folder`.
- **Index from file / USN:** No change; when there is no folder-crawler builder, `deferred_index_build_start_` is false and nothing is deferred.

## Summary

| Step | Action |
|------|--------|
| 1 | In `ExecuteApplicationMain`, do not call `index_builder->Start()` when using folder crawler; set `index_build_state` and pass a “defer start” flag to `Application`. |
| 2 | In `Application`, add a “defer start” constructor parameter and member. |
| 3 | In `Application::Run()`, after the first frame, if defer flag set and builder non-null, call `index_builder_->Start(index_build_state_)` and clear the flag. |
| 4 | (Optional) Ensure crawler updates progress atomics frequently so status bar feels responsive. |
| 5 | Test on Linux (and macOS/Windows if scope extended) with `--crawl-folder`. |

This achieves “display the UI, then crawl in a responsive way so that the user can see the progress” on Linux, aligned with macOS, with minimal and platform-agnostic code changes.
