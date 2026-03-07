# macOS Drag-and-Drop Implementation Plan

**Date:** 2026-02-01  
**Goal:** Enable drag-and-drop from the search results table on macOS (parity with Windows).  
**Delegation:** Use the taskmaster prompt and subordinate agent via `docs/prompts/2026-02-01_MACOS_DRAG_AND_DROP_TASK.md`.

---

## Current State

- **Windows:** Drag-and-drop from filename/path columns is implemented. The UI detects a drag gesture (mouse down + movement past threshold) and calls `drag_drop::StartFileDragDrop(full_path_utf8)` implemented in `src/platform/windows/DragDropUtils.cpp` (COM/OLE, `DoDragDrop`, `CF_HDROP`).
- **macOS:** The same gesture is detected in `ResultsTable.cpp` (HandleDragAndDrop), but the call to start the drag is wrapped in `#ifdef _WIN32`, so on macOS no system drag is started.
- **Linux:** Not in scope; no drag-drop implementation.

---

## Architecture (Target)

1. **Shared API**
   - One declaration for `drag_drop::StartFileDragDrop(const std::string& full_path_utf8)` used by the UI, implemented per platform.
   - Option A: Add a small platform-agnostic header (e.g. `src/platform/FileDragDrop.h` or `platform/FileDragDrop.h`) that declares the function; Windows and macOS each implement it in their platform folder.
   - Option B: Keep declaration in `platform/windows/DragDropUtils.h` and have macOS include that header for the declaration only (same pattern as `FileOperations.h` used by both Windows and macOS). macOS then implements the same symbol in `platform/macos/DragDrop_mac.mm`.

2. **macOS implementation**
   - New file(s) under `src/platform/macos/` (e.g. `DragDrop_mac.mm`).
   - Use AppKit drag-and-drop: provide file URL(s) on the pasteboard (e.g. `NSFilenamesPboardType` / `NSPasteboardTypeFileURL`), and run a drag session (e.g. `NSDraggingSource`, or high-level APIs such as `-[NSView dragImage:…]` / pasteboard-based drag). Must run on main thread; called from ImGui/GLFW UI thread.
   - Single-file drag only (match Windows scope); directories rejected.
   - UTF-8 path in, convert to `NSURL`/`NSString` as needed.

3. **UI integration**
   - `ResultsTable.cpp`: Call `drag_drop::StartFileDragDrop(path)` when the drag gesture is confirmed and platform supports it: `#if defined(_WIN32) || defined(__APPLE__)` (no change to gesture logic; only extend the platform guard and ensure the shared declaration is included on macOS).
   - Include the shared drag-drop declaration on macOS (either from the new shared header or from the existing Windows header, depending on chosen option).

4. **Build**
   - CMake: Add the new macOS drag-drop source (e.g. `DragDrop_mac.mm`) to the macOS app target (same place as other `platform/macos/*.mm`). Do not add it to Windows or Linux targets.

5. **Docs**
   - Update `ResultsTable.h` (and any keyboard-shortcuts / help text) from “Drag-and-drop (Windows)” to “Drag-and-drop (Windows and macOS)” once implemented.
   - Optionally add a short note in `AGENTS document` or platform docs that drag-drop is implemented on Windows and macOS.

---

## Phases (for subordinate agent)

1. **API and include strategy**
   - Decide shared declaration location (new `platform/FileDragDrop.h` vs reusing `DragDropUtils.h` for declaration only).
   - Ensure `ResultsTable.cpp` includes the declaration on both Windows and macOS and calls `StartFileDragDrop` under `#if defined(_WIN32) || defined(__APPLE__)`.

2. **macOS implementation**
   - Implement `drag_drop::StartFileDragDrop(const std::string&)` in `src/platform/macos/DragDrop_mac.mm` using AppKit (pasteboard + drag session), main-thread only, single file, no directories.

3. **CMake**
   - Add `DragDrop_mac.mm` to the macOS app target.

4. **Verification**
   - Build macOS app; drag a file from the results table and drop onto Finder/Desktop or another app; confirm behavior and that Windows implementation is unchanged.

---

## References

- `src/ui/ResultsTable.cpp` – HandleDragAndDrop, drag gesture, `#ifdef _WIN32` call site.
- `src/platform/windows/DragDropUtils.h` / `DragDropUtils.cpp` – Windows API and behavior.
- `src/platform/macos/FileOperations_mac.mm` – Pattern for macOS platform impl and use of shared header.
- `docs/archive/DRAG_AND_DROP_IMPLEMENTATION_PLAN.md` – Windows-focused design (COM/OLE); useful for API semantics only.
- `docs/prompts/TaskmasterPrompt.md` – Taskmaster format.
- `AGENTS document` – Platform rules (no modifying `#ifdef` blocks to “fix” cross-platform; use abstractions and separate implementations).

---

## Delegation

Run the taskmaster-style prompt by passing the task document to the script:

    ./scripts/jules_execute_taskmaster_prompt.sh docs/prompts/2026-02-01_MACOS_DRAG_AND_DROP_TASK.md

The task document contains a ````markdown` code block with the full structured prompt for the subordinate agent (Mission Context, Core Objective, Desired Outcome, Workflow, Process, Pitfalls, Acceptance Criteria, Strict Constraints, Reference Files). The subordinate agent should implement the macOS drag-and-drop feature according to this plan and the constraints in the prompt.
